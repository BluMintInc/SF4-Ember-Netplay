#include "update/PackageInstaller.hxx"
#include "update/UpdaterArguments.hxx"
#include "update/ProgressWindow.hxx"
#include "../common/PackageInventory.hxx"
#include "../platform/Elevation.hxx"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <string>
#include <vector>

#include <windows.h>
#include <pathcch.h>
#include <shellapi.h>
#include <strsafe.h>
#include <tlhelp32.h>
#include "../common/SelectionAssetPath.hxx"

namespace {

static void AppendLog(const char* message) {
	wchar_t tempDir[MAX_PATH] = { 0 };
	if (GetTempPathW(MAX_PATH, tempDir) == 0) {
		return;
	}
	wchar_t logPath[MAX_PATH] = { 0 };
	if (FAILED(PathCchCombine(logPath, MAX_PATH, tempDir, L"sf4-netplay-update.log"))) {
		return;
	}

	SYSTEMTIME st = { 0 };
	GetLocalTime(&st);
	char line[1024] = { 0 };
	snprintf(
		line,
		sizeof(line),
		"%04u-%02u-%02u %02u:%02u:%02u %s\r\n",
		st.wYear,
		st.wMonth,
		st.wDay,
		st.wHour,
		st.wMinute,
		st.wSecond,
		message ? message : ""
	);

	HANDLE hFile = CreateFileW(
		logPath,
		FILE_APPEND_DATA,
		FILE_SHARE_READ,
		NULL,
		OPEN_ALWAYS,
		FILE_ATTRIBUTE_NORMAL,
		NULL
	);
	if (hFile == INVALID_HANDLE_VALUE) {
		return;
	}
	DWORD written = 0;
	WriteFile(hFile, line, (DWORD)strlen(line), &written, NULL);
	CloseHandle(hFile);
}

static bool WideToUtf8(const wchar_t* wide, char* out, int outLen) {
	if (!wide || !out || outLen <= 0) {
		return false;
	}
	int n = WideCharToMultiByte(CP_UTF8, 0, wide, -1, out, outLen, NULL, NULL);
	return n > 0;
}

static bool WaitForProcessExit(DWORD pid, DWORD timeoutMs) {
	if (pid == 0) {
		return true;
	}
	HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, pid);
	if (!process) {
		return true;
	}
	DWORD waitResult = WaitForSingleObject(process, timeoutMs);
	CloseHandle(process);
	return waitResult == WAIT_OBJECT_0;
}

// How long each installer step took and over how many files, in the log:
// the record of where an update's time went.
struct StepLog {
	int step = -1;
	ULONGLONG started = 0;
	std::uint64_t files = 0;
	void Heard(sf4e::launcher::UpdateStage stage, std::uint64_t total) {
		const int heard = sf4e::launcher::InstallerStepOf(stage);
		if (heard != step) { End(); step = heard; started = GetTickCount64(); }
		files = total;
	}
	void End() {
		if (step < 0) return;
		char line[160] = { 0 };
		snprintf(line, sizeof(line), "%s: %llu files in %llu ms", sf4e::launcher::InstallerSteps[step].nameKey,
			static_cast<unsigned long long>(files), static_cast<unsigned long long>(GetTickCount64() - started));
		AppendLog(line);
		step = -1;
	}
};

// What an install or a recovery waits for, shown on the window's wait line.
static sf4e::launcher::Activity ShowActivity(sf4e::launcher::ProgressWindow& window) {
    return [&window](sf4e::launcher::InstallActivity activity, const std::filesystem::path& file) {
        window.Note(sf4e::launcher::NoticeFor(activity), file.filename().wstring());
    };
}

static bool InstallFiles(const std::wstring& staging, const std::wstring& install, sf4e::launcher::ProgressWindow& window) {
    std::string error;
    StepLog steps;
    const bool installed = sf4e::launcher::InstallPackage(staging, install, error, sf4e::launcher::NeverCancelled,
        [&](sf4e::launcher::UpdateStage stage, std::uint64_t done, std::uint64_t total) {
            window.Report(stage, done, total);
            steps.Heard(stage, total);
        },
        ShowActivity(window));
    steps.End();
    if (!installed) AppendLog(error.c_str());
    return installed;
}

// Finishes an update that stopped part way; no step counts, so the window
// shows its clock and what the recovery waits for.
static bool RecoverFiles(const std::wstring& install, sf4e::launcher::ProgressWindow& window) {
    std::string error;
    const bool recovered = sf4e::launcher::RecoverPackage(install, error, false, ShowActivity(window));
    AppendLog(recovered ? "Update recovery complete" : error.c_str());
    return recovered;
}
static bool StartLauncher(const wchar_t* installDir, const wchar_t* arguments = nullptr) {
	wchar_t launcherPath[MAX_PATH] = { 0 };
	if (FAILED(PathCchCombine(launcherPath, MAX_PATH, installDir, L"Launcher.exe"))) {
		return false;
	}
	if (GetFileAttributesW(launcherPath) == INVALID_FILE_ATTRIBUTES) {
		AppendLog("Launcher.exe missing after update");
		return false;
	}

	SHELLEXECUTEINFOW sei = { 0 };
	sei.cbSize = sizeof(sei);
	sei.fMask = SEE_MASK_FLAG_NO_UI;
	sei.lpVerb = L"open";
	sei.lpFile = launcherPath;
	sei.lpDirectory = installDir;
    sei.lpParameters = arguments;
	sei.nShow = SW_SHOWNORMAL;
	if (!ShellExecuteExW(&sei)) {
		char buf[128] = { 0 };
		snprintf(buf, sizeof(buf), "failed to start Launcher.exe (Win32 %lu)", GetLastError());
		AppendLog(buf);
		return false;
	}
	return true;
}

// The installer's uninstaller runs this from a copy outside the folder:
// Windows will not delete a running image, so a copy inside the folder would
// leave itself behind.
static int Uninstall(const wchar_t* installDir) {
	std::string error;
	if (!sf4e::launcher::UninstallPackage(installDir, error)) {
		AppendLog(("ERROR: uninstall failed: " + error).c_str());
		return 1;
	}
	AppendLog("Uninstall complete");
	return 0;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
	// Players double-click Updater.exe to update. Its own work needs the
	// launcher's arguments, so a plain start opens the launcher's Updates
	// window, which checks for, downloads and installs the update.
	if (argc == 1) {
		wchar_t ownDir[MAX_PATH] = { 0 };
		const DWORD length = GetModuleFileNameW(nullptr, ownDir, MAX_PATH);
		if (length == 0 || length >= MAX_PATH || FAILED(PathCchRemoveFileSpec(ownDir, MAX_PATH))) return 1;
		AppendLog("Updater started without arguments; opening the Updates window");
		return StartLauncher(ownDir, L"--updates") ? 0 : 1;
	}

	// The Launcher starts this only as the normal user (UpdaterMayRun); a copy
	// started any other way does not install or recover either.
	if (sf4e::platform::ProcessElevation() != sf4e::platform::Elevation::Normal) {
		AppendLog("ERROR: Updater does not run as administrator; start Ember normally");
		return 1;
	}

	sf4e::launcher::UpdaterArguments arguments;
	if (!sf4e::launcher::ParseUpdaterArguments(argc, argv, arguments)) {
		AppendLog("ERROR: missing -InstallDir or -StagingDir");
		return 1;
	}
	const wchar_t* installDir = arguments.installDir.c_str();
	const DWORD waitPid = arguments.waitPid;
	if (arguments.uninstall) return Uninstall(installDir);

	// An install, or with -RecoverOnly the finishing of one that stopped part
	// way, which the Launcher asks for at startup. Both wait for the launcher
	// to close under the same window and start it again afterwards
	// (UpdaterReopensLauncher).
	char startLine[1024] = { 0 };
	char installUtf8[MAX_PATH * 2] = { 0 };
	char stagingUtf8[MAX_PATH * 2] = { 0 };
	WideToUtf8(installDir, installUtf8, sizeof(installUtf8));
	WideToUtf8(arguments.stagingDir.c_str(), stagingUtf8, sizeof(stagingUtf8));
	snprintf(
		startLine,
		sizeof(startLine),
		"Updater start InstallDir=%s StagingDir=%s WaitPid=%lu RecoverOnly=%d",
		installUtf8,
		stagingUtf8,
		waitPid,
		arguments.recoverOnly ? 1 : 0
	);
	AppendLog(startLine);

	if (GetFileAttributesW(installDir) == INVALID_FILE_ATTRIBUTES) {
		AppendLog("ERROR: install directory not found");
		return 1;
	}
	if (!arguments.recoverOnly && GetFileAttributesW(arguments.stagingDir.c_str()) == INVALID_FILE_ATTRIBUTES) {
		AppendLog("ERROR: staging directory not found");
		return 1;
	}

	// The launcher passes the window's texts in the player's language; one
	// from before them does not, and the window shows their English.
	sf4e::launcher::ProgressWindow::Options windowOptions;
	windowOptions.notices = sf4e::launcher::NoticeTexts(arguments.notices);
	windowOptions.headline = sf4e::launcher::UpdaterHeadline(arguments, windowOptions.notices);
	windowOptions.stages = arguments.stages;
	sf4e::launcher::ProgressWindow progress(L"SF4 Ember Netplay", std::move(windowOptions));
	progress.Note(sf4e::launcher::UpdaterNotice::WaitingForLauncher);
	if (!WaitForProcessExit(waitPid, 30000)) {
		AppendLog("ERROR: launcher is still running; nothing was changed");
		progress.Close();
		// An install's launcher shows the failure; a recovery's is the one
		// still running.
		if (!arguments.recoverOnly) StartLauncher(installDir, L"--updates --update-error");
		return 1;
	}
	progress.Note(std::nullopt);

	const bool done = arguments.recoverOnly ? RecoverFiles(arguments.installDir, progress) :
		InstallFiles(arguments.stagingDir, arguments.installDir, progress);
	progress.Close();
	if (!sf4e::launcher::UpdaterReopensLauncher(arguments)) return done ? 0 : 1;
	AppendLog(done ? "starting Launcher.exe" : "starting Launcher.exe to show the failure");
	const bool started = StartLauncher(installDir, done ? nullptr : L"--updates --update-error");
	if (done && started) AppendLog("Updater complete");
	return done && started ? 0 : 1;
}
