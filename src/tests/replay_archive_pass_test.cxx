#include "replay_slots_support.hxx"
#include "../platform/ReplayArchivePass.hxx"
#include "../platform/ReplayArchiveWorker.hxx"
#include <atomic>
#include <chrono>
#include <fstream>
#include <set>
#include <string>
#include <thread>

namespace fs = std::filesystem;
namespace files = sf4e::replayfiles;

// The stock game's match list: ten slots that a Versus battle saves into
// round and round.
constexpr int kFirst = 300, kLast = 309;
constexpr auto kSettle = std::chrono::minutes(2);

static void Save(const fs::path& path, const Bytes& bytes) {
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
	out.close(); CHECK(!out.fail());
}

static std::size_t Count(const fs::path& folder) {
	std::error_code missing;
	std::size_t count = 0;
	for (fs::directory_iterator at(folder, missing), end; !missing && at != end; at.increment(missing)) count += at->is_regular_file();
	return count;
}

// A game's save folder and an archive beside it, with the index and writer a
// pass is given. failWrites makes that many writes fail first.
struct Folders {
	fs::path root, saves, archive;
	Bytes list = EmptyList(), swan = EmptySwan();
	files::ArchiveCandidates held;
	files::SlotFileCache cache;
	int failWrites = 0;
	explicit Folders(const char* name) {
		root = fs::temp_directory_path() / (std::string(name) + "-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		saves = root / "ssf4_savedata"; archive = root / "replays";
		CHECK(fs::create_directories(saves));
		WriteIndexes();
	}
	~Folders() { std::error_code ignored; fs::remove_all(root, ignored); }
	void WriteIndexes() { Save(saves / "LIST", list); Save(saves / "replays-swan.dat", swan); }
	files::ArchiveTarget Target() {
		return {archive, held, cache, [this](const fs::path& path, const Bytes& bytes) {
			if (failWrites > 0) { failWrites--; return false; }
			if (fs::exists(path)) return false;
			Save(path, bytes);
			return true;
		}};
	}
	// What the game writes as a match ends: the replay and its ".0", and,
	// unless its index is behind, the slot's record with that save time.
	fs::file_time_type SaveMatch(int slot, const Bytes& replay, std::uint32_t savedAt, bool indexed) {
		const fs::path file = saves / std::to_wstring(slot);
		Save(file, replay);
		Save(file.wstring() + L".0", Sidecar(replay));
		if (indexed) Index(slot, replay, savedAt);
		return fs::last_write_time(file);
	}
	void Index(int slot, const Bytes& replay, std::uint32_t savedAt) {
		Fill(list, swan, slot, replay, 0x44);
		WriteU32(const_cast<std::uint8_t*>(Record(list, swan, slot)) + 13, savedAt);
		Seal(swan);
		WriteIndexes();
	}
	files::ArchivePass Pass(fs::file_time_type now, files::FileClock::duration settle = kSettle) {
		return files::CopySlots(Target(), saves, kFirst, kLast, now, settle);
	}
	// The archive's files, each a different replay.
	std::size_t Archived() const {
		std::size_t count = 0;
		std::set<std::uint32_t> crcs;
		std::error_code missing;
		for (fs::directory_iterator at(archive, missing), end; !missing && at != end; at.increment(missing)) {
			if (!at->is_regular_file()) continue;
			files::ArchiveFile read;
			CHECK(files::ReadArchive(at->path(), true, read) == files::ArchiveState::Valid);
			crcs.insert(read.crc); count++;
		}
		CHECK(count == crcs.size());
		return count;
	}
	std::size_t Staged() const { return Count(archive / ".pending"); }
};

// A replay played at `played` (seconds since 1970), which its header holds.
static Bytes Match(int match, std::uint32_t played) {
	Bytes replay = Replay(static_cast<std::uint8_t>(match + 1), 2000 + match);
	const std::uint64_t filetime = played * 10000000ull + 116444736000000000ull;
	WriteU32(replay.data() + 0x10, static_cast<std::uint32_t>(filetime));
	WriteU32(replay.data() + 0x14, static_cast<std::uint32_t>(filetime >> 32));
	return replay;
}

// Twenty-five matches through ten slots while the game's index never lists
// one. A pass as each is saved keeps every one of them before its slot comes
// round again; they reach the archive when the wait for the index is over.
// One pass after the sitting finds only the last ten.
static void TestASittingWithTheIndexBehind() {
	Folders asSaved("ember-archive-each"), atExit("ember-archive-exit");
	for (int match = 0; match < 25; match++) {
		const Bytes replay = Match(match, 1791600000u + match * 180);
		const auto saved = asSaved.SaveMatch(kFirst + match % 10, replay, 0, false);
		atExit.SaveMatch(kFirst + match % 10, replay, 0, false);
		const auto pass = asSaved.Pass(saved);
		CHECK(pass.staged == 1 && pass.copied == 0 && !pass.retry && pass.readyAt);
	}
	CHECK(asSaved.Staged() == 25 && asSaved.Archived() == 0);
	const auto pass = asSaved.Pass(files::FileClock::now() + kSettle);
	CHECK(pass.copied == 25 && pass.fromFile == 25 && !pass.retry && !pass.readyAt);
	CHECK(asSaved.Archived() == 25 && asSaved.Staged() == 0);
	CHECK(atExit.Pass(files::FileClock::now(), {}).copied == 10 && atExit.Archived() == 10);
}

// A replay its index describes goes straight in with the index's record.
static void TestAnIndexedReplayGoesStraightIn() {
	Folders folders("ember-archive-indexed");
	for (int match = 0; match < 12; match++) {
		const std::uint32_t at = 1791600000u + match * 180;
		const int slot = SlotToReplace(folders.list, folders.swan, kFirst, kLast);
		const auto pass = folders.Pass(folders.SaveMatch(slot, Match(match, at), at, true));
		CHECK(pass.copied == 1 && pass.fromFile == 0 && pass.staged == 0);
	}
	CHECK(folders.Archived() == 12 && folders.Staged() == 0);
}

// A staged replay whose slot is taken by the next match before its record
// comes is archived from what was staged. One whose record comes while the
// slot still holds it goes in with that record, once.
static void TestAReplayOverwrittenBeforeItsRecord() {
	Folders folders("ember-archive-overwritten");
	const Bytes first = Match(1, 1791600000u), second = Match(2, 1791600200u), third = Match(3, 1791600400u);
	const auto saved = folders.SaveMatch(kFirst, first, 0, false);
	CHECK(folders.Pass(saved).staged == 1);
	folders.SaveMatch(kFirst, second, 1791600200u, true);
	auto pass = folders.Pass(saved + std::chrono::seconds(10));
	CHECK(pass.copied == 1 && pass.fromFile == 0 && pass.readyAt && *pass.readyAt > saved + std::chrono::seconds(10));
	pass = folders.Pass(saved + kSettle + std::chrono::seconds(1));
	CHECK(pass.copied == 1 && pass.fromFile == 1);
	const auto later = folders.SaveMatch(kFirst + 1, third, 0, false);
	CHECK(folders.Pass(later).staged == 1);
	folders.Index(kFirst + 1, third, 1791600400u);
	pass = folders.Pass(later + std::chrono::seconds(5));
	CHECK(pass.copied == 1 && pass.fromFile == 0 && !pass.readyAt);
	CHECK(folders.Archived() == 3 && folders.Staged() == 0);
}

// A slot caught between its replay and its ".0", and a copy that cannot be
// written, ask for another pass, which takes them. A damaged index does not
// stop a slot from going by its file.
static void TestRetriesAndADamagedIndex() {
	Folders folders("ember-archive-retry");
	const Bytes first = Match(1, 1791600000u), second = Match(2, 1791600200u);
	Save(folders.saves / "300", first);
	Save(folders.saves / "300.0", Sidecar(second));
	auto pass = folders.Pass(files::FileClock::now(), {});
	CHECK(pass.copied == 0 && pass.retry);
	Save(folders.saves / "300.0", Sidecar(first));
	folders.failWrites = 1;
	pass = folders.Pass(files::FileClock::now(), {});
	CHECK(pass.copied == 0 && pass.failed == 1 && pass.retry);
	pass = folders.Pass(files::FileClock::now(), {});
	CHECK(pass.copied == 1 && !pass.retry);
	folders.SaveMatch(kFirst + 1, second, 0, false);
	folders.swan[100] ^= 0xFF; // the leading CRC no longer matches
	folders.WriteIndexes();
	pass = folders.Pass(files::FileClock::now(), {});
	CHECK(pass.copied == 1 && pass.fromFile == 1 && !pass.retry);
	CHECK(folders.Archived() == 2);
}

// A replay staged by a game that has gone is archived by the launcher's pass,
// which waits for no index.
static void TestTheLauncherTakesWhatWasStaged() {
	Folders folders("ember-archive-left");
	const auto saved = folders.SaveMatch(kFirst, Match(4, 1791600000u), 0, false);
	CHECK(folders.Pass(saved).staged == 1);
	Save(folders.saves / "300", Bytes(51200, 0)); // the slot holds something else now
	const auto pass = folders.Pass(files::FileClock::now(), {});
	CHECK(pass.copied == 1 && pass.fromFile == 1 && folders.Archived() == 1 && folders.Staged() == 0);
}

// A replay put back into the game (Watch) is saved under a new time in
// another slot. The archive holds it already, so it is not copied again, and
// an import's backup of that slot is the copy there.
static void TestAReplayPutBackAndItsBackup() {
	Folders folders("ember-archive-back");
	const Bytes replay = Match(7, 1791600000u);
	folders.SaveMatch(kFirst, replay, 1791600000u, true);
	CHECK(folders.Pass(files::FileClock::now()).copied == 1);
	Bytes exported, list = folders.list, swan = folders.swan, body;
	CHECK(Export(list, swan, kFirst, replay, exported));
	CHECK(Import(exported, kFirst + 5, 1791700000u, list, swan, body));
	folders.list = list; folders.swan = swan; folders.WriteIndexes();
	Save(folders.saves / std::to_wstring(kFirst + 5), body);
	Save(folders.saves / (std::to_wstring(kFirst + 5) + L".0"), Sidecar(body));
	const auto pass = folders.Pass(files::FileClock::now());
	CHECK(pass.copied == 0 && pass.failed == 0 && !pass.retry);
	CHECK(folders.Archived() == 1);
	// A pass ran before the import's backup: the backup is that copy.
	files::BackupEvidence evidence;
	CHECK(files::EnsureBackup(folders.Target(), folders.saves, kFirst + 5, folders.list, folders.swan, body, evidence) && !evidence.path.empty());
	// No pass ran: the backup makes the copy, even of a replay the index is behind.
	const Bytes other = Match(8, 1791600300u);
	folders.SaveMatch(kFirst + 2, other, 0, false);
	CHECK(files::EnsureBackup(folders.Target(), folders.saves, kFirst + 2, folders.list, folders.swan, other, evidence));
	CHECK(folders.Archived() == 2);
}

// Waits up to two seconds for the count to reach `least`, then a little
// longer to see that it stays there.
static int Settled(const std::atomic<int>& passes, int least) {
	for (int waited = 0; passes < least && waited < 2000; waited += 5) std::this_thread::sleep_for(std::chrono::milliseconds(5));
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	return passes;
}

// The worker's schedule: retries without being asked again, at most so many
// in a row; after a match ends, passes until one finds its replay; a staged
// replay's due time brings a pass forward.
static void TestArchiveWorkerSchedule() {
	const files::ArchiveWorker::Retries retries{std::chrono::milliseconds(1), std::chrono::milliseconds(4), 3};
	{
		std::atomic<int> passes{0};
		files::ArchiveWorker worker([&] { ++passes; files::ArchivePass pass; pass.retry = true; return pass; }, retries);
		worker.Want();
		CHECK(Settled(passes, 4) == 4);
		worker.Want(); // asked again: one more round of retries
		CHECK(Settled(passes, 8) == 8);
	}
	{
		std::atomic<int> passes{0};
		files::ArchiveWorker worker([&] { files::ArchivePass pass; pass.staged = ++passes == 3; return pass; }, retries);
		worker.MatchEnded();
		CHECK(Settled(passes, 3) == 3);
	}
	{
		std::atomic<int> passes{0};
		files::ArchiveWorker worker([&] {
			files::ArchivePass pass;
			if (++passes == 1) pass.readyAt = files::FileClock::now() + std::chrono::milliseconds(30);
			return pass;
		}, retries);
		worker.Want();
		CHECK(Settled(passes, 2) == 2);
	}
}

int main() {
	TestASittingWithTheIndexBehind();
	TestAnIndexedReplayGoesStraightIn();
	TestAReplayOverwrittenBeforeItsRecord();
	TestRetriesAndADamagedIndex();
	TestTheLauncherTakesWhatWasStaged();
	TestAReplayPutBackAndItsBackup();
	TestArchiveWorkerSchedule();
	return 0;
}
