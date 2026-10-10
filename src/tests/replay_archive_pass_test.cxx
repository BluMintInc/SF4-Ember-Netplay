#include "replay_slots_support.hxx"
#include "../platform/ReplayArchivePass.hxx"
#include "../platform/ReplayArchiveWorker.hxx"
#include <atomic>
#include <chrono>
#include <fstream>
#include <future>
#include <set>
#include <string>

namespace fs = std::filesystem;
namespace files = sf4e::replayfiles;
using Kind = files::SlotCopy::Kind;

// The stock game's match list: ten slots that a Versus battle saves into
// round and round (ReplaySlots.hxx: SlotToReplace).
constexpr int kFirst = 300, kLast = 309;
constexpr auto kSettle = std::chrono::minutes(2);

static void Save(const fs::path& path, const Bytes& bytes) {
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
	out.close(); CHECK(!out.fail());
}

// A game's save folder and an archive beside it, with the index and writer a
// pass is given.
struct Folders {
	fs::path root, saves, archive;
	Bytes list = EmptyList(), swan = EmptySwan();
	files::ArchiveCandidates held;
	files::SlotFileCache cache;
	explicit Folders(const char* name) {
		root = fs::temp_directory_path() / (std::string(name) + "-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		saves = root / "ssf4_savedata"; archive = root / "replays";
		CHECK(fs::create_directories(saves));
		WriteIndexes();
	}
	~Folders() { std::error_code ignored; fs::remove_all(root, ignored); }
	void WriteIndexes() { Save(saves / "LIST", list); Save(saves / "replays-swan.dat", swan); }
	files::ArchiveTarget Target() {
		return {archive, held, cache, [](const fs::path& path, const Bytes& bytes) {
			if (fs::exists(path)) return false;
			Save(path, bytes);
			return true;
		}};
	}
	// What the game writes as a match ends: the replay and its ".0", written
	// `age` before now, and, unless the index is behind, the slot's record
	// with that save time.
	fs::file_time_type SaveMatch(int slot, const Bytes& replay, std::uint32_t savedAt, bool indexed = true) {
		const fs::path file = saves / std::to_wstring(slot);
		Save(file, replay);
		Save(file.wstring() + L".0", Sidecar(replay));
		if (indexed) {
			Fill(list, swan, slot, replay, 0x44);
			WriteU32(const_cast<std::uint8_t*>(Record(list, swan, slot)) + 13, savedAt);
			Seal(swan);
			WriteIndexes();
		}
		return fs::last_write_time(file);
	}
	files::ArchivePass Pass(fs::file_time_type now, files::FileClock::duration settle = kSettle) {
		return files::CopySlots(Target(), saves, kFirst, kLast, now, settle);
	}
	std::set<std::uint32_t> Archived() const {
		std::set<std::uint32_t> crcs;
		std::size_t count = 0;
		for (const auto& entry : fs::directory_iterator(archive)) {
			files::ArchiveFile read;
			CHECK(files::ReadArchive(entry.path(), true, read) == files::ArchiveState::Valid);
			crcs.insert(read.crc); count++;
		}
		CHECK(count == crcs.size()); // no replay twice
		return crcs;
	}
};

// A replay played at `played` (seconds since 1970), which its header holds.
static Bytes Match(int match, std::uint32_t played) {
	Bytes replay = Replay(static_cast<std::uint8_t>(match + 1), 2000 + match);
	const std::uint64_t filetime = played * 10000000ull + 116444736000000000ull;
	WriteU32(replay.data() + 0x10, static_cast<std::uint32_t>(filetime));
	WriteU32(replay.data() + 0x14, static_cast<std::uint32_t>(filetime >> 32));
	return replay;
}

// Twenty-five matches through ten slots. A pass as each one is saved keeps
// every one of them; a pass only after the sitting, as the launcher's at the
// game's exit, finds the last ten.
static void TestEveryMatchOfASittingIsArchived() {
	Folders asSaved("ember-archive-each"), atExit("ember-archive-exit");
	for (int match = 0; match < 25; match++) {
		const std::uint32_t at = 1791600000u + match * 180;
		const Bytes replay = Match(match, at);
		for (Folders* folders : {&asSaved, &atExit}) {
			const int slot = SlotToReplace(folders->list, folders->swan, kFirst, kLast);
			CHECK(slot >= kFirst && slot <= kLast);
			folders->SaveMatch(slot, replay, at);
		}
		const auto pass = asSaved.Pass(files::FileClock::now());
		CHECK(pass.copied == 1 && pass.fromFile == 0 && pass.failed == 0 && !pass.readyAt);
	}
	CHECK(asSaved.Archived().size() == 25);
	CHECK(atExit.Pass(files::FileClock::now(), {}).copied == 10);
	CHECK(atExit.Archived().size() == 10);
	// Nothing new: a pass copies nothing again.
	CHECK(asSaved.Pass(files::FileClock::now()).copied == 0 && asSaved.Archived().size() == 25);
}

// A replay saved before the game's index lists it waits for the index while
// the game runs, and goes by its own file once the wait is over, or at once
// when no game is left to write the index. The index catching up later adds
// no second copy.
static void TestAFileAheadOfItsIndex() {
	Folders folders("ember-archive-ahead");
	const Bytes replay = Match(3, 1791600000u);
	const auto written = folders.SaveMatch(kFirst, replay, 0, false);
	auto pass = folders.Pass(written + std::chrono::seconds(5));
	CHECK(pass.copied == 0 && pass.readyAt && *pass.readyAt == written + kSettle);
	pass = folders.Pass(written + kSettle);
	CHECK(pass.copied == 1 && pass.fromFile == 1 && !pass.readyAt);
	folders.SaveMatch(kFirst, replay, 1791600010u);
	CHECK(folders.Pass(files::FileClock::now()).copied == 0);
	CHECK(folders.Archived().size() == 1);

	Folders gameGone("ember-archive-gone");
	const auto saved = gameGone.SaveMatch(kFirst + 4, replay, 0, false);
	pass = gameGone.Pass(saved, {});
	CHECK(pass.copied == 1 && pass.fromFile == 1);
}

// A slot caught between its replay and its ".0" is left for the next pass,
// and a damaged index does not stop any slot from going by its file.
static void TestHalfWrittenSlotAndDamagedIndex() {
	Folders folders("ember-archive-half");
	const Bytes first = Match(1, 1791600000u), second = Match(2, 1791600200u);
	folders.SaveMatch(kFirst, first, 1791600000u);
	Save(folders.saves / "301", second);
	Save(folders.saves / "301.0", Sidecar(first));
	auto pass = folders.Pass(files::FileClock::now(), {});
	CHECK(pass.copied == 1 && pass.failed == 0);
	Save(folders.saves / "301.0", Sidecar(second));
	folders.swan[100] ^= 0xFF; // the leading CRC no longer matches
	folders.WriteIndexes();
	pass = folders.Pass(files::FileClock::now(), {});
	CHECK(pass.copied == 1 && pass.fromFile == 1);
	CHECK(folders.Archived().size() == 2);
}

// A replay put back into the game (Watch) is saved under a new time in
// another slot. The archive holds it already, so it is not copied again.
static void TestAReplayPutBackIsNotCopiedAgain() {
	Folders folders("ember-archive-back");
	const Bytes replay = Match(7, 1791600000u);
	folders.SaveMatch(kFirst, replay, 1791600000u);
	CHECK(folders.Pass(files::FileClock::now()).copied == 1);
	Bytes exported, list = folders.list, swan = folders.swan, body;
	CHECK(Export(list, swan, kFirst, replay, exported));
	CHECK(Import(exported, kFirst + 5, 1791700000u, list, swan, body));
	folders.list = list; folders.swan = swan; folders.WriteIndexes();
	Save(folders.saves / std::to_wstring(kFirst + 5), body);
	Save(folders.saves / (std::to_wstring(kFirst + 5) + L".0"), Sidecar(body));
	const auto pass = folders.Pass(files::FileClock::now());
	CHECK(pass.copied == 0 && pass.failed == 0);
	CHECK(folders.Archived().size() == 1);
}

// The worker runs a pass when asked, again when a pass asks for one later,
// and once more for an ask made while a pass runs.
static void TestArchiveWorker() {
	std::atomic<int> passes{0};
	std::promise<void> inPass, release, again, third;
	auto released = release.get_future().share();
	files::ArchiveWorker worker([&]() -> std::optional<files::ArchiveWorker::Clock::duration> {
		const int pass = ++passes;
		if (pass == 1) { inPass.set_value(); released.wait(); return std::nullopt; }
		if (pass == 2) { again.set_value(); return std::chrono::milliseconds(20); }
		if (pass == 3) third.set_value();
		return std::nullopt;
	});
	worker.Want();
	CHECK(inPass.get_future().wait_for(std::chrono::seconds(2)) == std::future_status::ready);
	worker.Want(); // during the first pass
	release.set_value();
	CHECK(again.get_future().wait_for(std::chrono::seconds(2)) == std::future_status::ready);
	CHECK(third.get_future().wait_for(std::chrono::seconds(2)) == std::future_status::ready);
	worker.Stop();
	CHECK(passes == 3);
}

int main() {
	TestEveryMatchOfASittingIsArchived();
	TestAFileAheadOfItsIndex();
	TestHalfWrittenSlotAndDamagedIndex();
	TestAReplayPutBackIsNotCopiedAgain();
	TestArchiveWorker();
	return 0;
}
