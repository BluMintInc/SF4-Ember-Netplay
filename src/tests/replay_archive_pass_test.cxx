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

static void Save(const fs::path& path, const Bytes& bytes) {
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
	out.close(); CHECK(!out.fail());
}

// A game's save folder and an archive beside it, with the index and writer a
// pass is given. failWrites makes that many writes fail first.
struct Folders {
	fs::path root, saves, archive;
	Bytes list = EmptyList(), swan = EmptySwan();
	files::ArchiveCandidates held;
	files::SlotFileCache cache;
	std::atomic<int> failWrites{0};
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
	// unless its index is behind, the slot's record.
	void SaveMatch(int slot, const Bytes& replay, std::uint32_t savedAt, bool indexed) {
		const fs::path file = saves / std::to_wstring(slot);
		Save(file, replay);
		Save(file.wstring() + L".0", Sidecar(replay));
		if (indexed) Index(slot, replay, savedAt);
	}
	// The slot's record as the game writes it, naming player 2 the winner.
	void Index(int slot, const Bytes& replay, std::uint32_t savedAt) {
		Fill(list, swan, slot, replay, 0x44);
		std::uint8_t* record = const_cast<std::uint8_t*>(Record(list, swan, slot));
		WriteU32(record + 13, savedAt);
		record[49] = 1;
		Seal(swan);
		WriteIndexes();
	}
	files::ArchivePass Pass() { return files::CopySlots(Target(), saves, kFirst, kLast); }
	// The archive's replay files, each a different replay.
	std::vector<fs::path> Archived() const {
		std::vector<fs::path> paths;
		std::set<std::uint32_t> crcs;
		std::error_code missing;
		for (fs::directory_iterator at(archive, missing), end; !missing && at != end; at.increment(missing)) {
			if (at->path().extension() != L".emberreplay") continue;
			files::ArchiveFile read;
			if (files::ReadArchive(at->path(), true, read) != files::ArchiveState::Valid) continue;
			crcs.insert(read.crc); paths.push_back(at->path());
		}
		CHECK(paths.size() == crcs.size());
		return paths;
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

// Twenty-five matches through ten slots while the game's index never lists
// one. A pass as each is saved puts every one in the archive at once; one
// pass after the sitting finds only the last ten.
static void TestASittingWithTheIndexBehind() {
	Folders asSaved("ember-archive-each"), atExit("ember-archive-exit");
	for (int match = 0; match < 25; match++) {
		const Bytes replay = Match(match, 1791600000u + match * 180);
		asSaved.SaveMatch(kFirst + match % 10, replay, 0, false);
		atExit.SaveMatch(kFirst + match % 10, replay, 0, false);
		const auto pass = asSaved.Pass();
		CHECK(pass.copied == 1 && pass.fromFile == 1 && !pass.retry);
		CHECK(asSaved.Archived().size() == static_cast<std::size_t>(match + 1));
	}
	CHECK(atExit.Pass().copied == 10 && atExit.Archived().size() == 10);
}

// A replay its index describes goes in with the index's record.
static void TestAnIndexedReplayGoesInWithItsRecord() {
	Folders folders("ember-archive-indexed");
	for (int match = 0; match < 12; match++) {
		const std::uint32_t at = 1791600000u + match * 180;
		folders.SaveMatch(SlotToReplace(folders.list, folders.swan, kFirst, kLast), Match(match, at), at, true);
		const auto pass = folders.Pass();
		CHECK(pass.copied == 1 && pass.fromFile == 0);
	}
	CHECK(folders.Archived().size() == 12);
}

// A replay archived before its index listed it keeps that archive file. When
// the index catches up while the slot holds it, the game's record is kept
// beside it, once, and readers see its winner; the slot being taken by the
// next match changes nothing for it.
static void TestTheGameRecordComesLater() {
	Folders folders("ember-archive-record");
	const Bytes replay = Match(1, 1791600000u);
	folders.SaveMatch(kFirst, replay, 0, false);
	CHECK(folders.Pass().copied == 1);
	const auto archived = folders.Archived();
	CHECK(archived.size() == 1);
	const Bytes contents = *files::ReadFile(archived[0]);
	CHECK(MadeUp(contents.data() + 8) && RecordWinner(files::WithGameRecord(folders.archive, contents).data() + 8) == -1);
	folders.Index(kFirst, replay, 1791600010u);
	auto pass = folders.Pass();
	CHECK(pass.copied == 0 && pass.failed == 0 && !pass.retry);
	const Bytes read = files::WithGameRecord(folders.archive, contents);
	CHECK(!MadeUp(read.data() + 8) && RecordWinner(read.data() + 8) == 1);
	CHECK(files::WithGameRecord(folders.archive, *files::ReadFile(archived[0])) == read);
	CHECK(folders.Pass().copied == 0);
	folders.SaveMatch(kFirst, Match(2, 1791600200u), 0, false);
	CHECK(folders.Pass().copied == 1 && folders.Archived().size() == 2);
	CHECK(files::ReadFile(archived[0]) == contents);
}

// A slot caught between its replay and its ".0", and a copy that cannot be
// written, ask for another pass, which takes them. A damaged index does not
// stop a slot from going by its file.
static void TestRetriesAndADamagedIndex() {
	Folders folders("ember-archive-retry");
	const Bytes first = Match(1, 1791600000u), second = Match(2, 1791600200u);
	Save(folders.saves / "300", first);
	Save(folders.saves / "300.0", Sidecar(second));
	auto pass = folders.Pass();
	CHECK(pass.copied == 0 && pass.retry);
	Save(folders.saves / "300.0", Sidecar(first));
	folders.failWrites = 1;
	pass = folders.Pass();
	CHECK(pass.copied == 0 && pass.failed == 1 && pass.retry);
	pass = folders.Pass();
	CHECK(pass.copied == 1 && !pass.retry);
	folders.SaveMatch(kFirst + 1, second, 0, false);
	folders.swan[100] ^= 0xFF; // the leading CRC no longer matches
	folders.WriteIndexes();
	pass = folders.Pass();
	CHECK(pass.copied == 1 && pass.fromFile == 1 && !pass.retry);
	CHECK(folders.Archived().size() == 2);
}

// An archive file that may hold the slot's replay but cannot be read is not
// taken as a copy, and the replay is not copied again beside it: the pass
// asks to look again.
static void TestAnUnreadableCopy() {
	Folders folders("ember-archive-unreadable");
	folders.SaveMatch(kFirst, Match(3, 1791600000u), 0, false);
	CHECK(folders.Pass().copied == 1);
	const fs::path copy = folders.Archived().at(0);
	CHECK(fs::remove(copy) && fs::create_directory(copy)); // there, but no file to read
	const auto pass = folders.Pass();
	CHECK(pass.copied == 0 && pass.retry && folders.Archived().empty());
	CHECK(fs::remove(copy));
	CHECK(folders.Pass().copied == 1 && folders.Archived().size() == 1);
}

// A replay put back into the game (Watch) is saved under a new time in
// another slot. The archive holds it already, so it is not copied again, and
// an import's backup of that slot is the copy there.
static void TestAReplayPutBackAndItsBackup() {
	Folders folders("ember-archive-back");
	const Bytes replay = Match(7, 1791600000u);
	folders.SaveMatch(kFirst, replay, 1791600000u, true);
	CHECK(folders.Pass().copied == 1);
	Bytes exported, list = folders.list, swan = folders.swan, body;
	CHECK(Export(list, swan, kFirst, replay, exported));
	CHECK(Import(exported, kFirst + 5, 1791700000u, list, swan, body));
	folders.list = list; folders.swan = swan; folders.WriteIndexes();
	Save(folders.saves / std::to_wstring(kFirst + 5), body);
	Save(folders.saves / (std::to_wstring(kFirst + 5) + L".0"), Sidecar(body));
	const auto pass = folders.Pass();
	CHECK(pass.copied == 0 && pass.failed == 0 && !pass.retry);
	CHECK(folders.Archived().size() == 1);
	// A pass ran before the import's backup: the backup is that copy.
	files::BackupEvidence evidence;
	CHECK(files::EnsureBackup(folders.Target(), folders.saves, kFirst + 5, folders.list, folders.swan, body, evidence) && !evidence.path.empty());
	// No pass ran: the backup makes the copy, even of a replay the index is behind.
	const Bytes other = Match(8, 1791600300u);
	folders.SaveMatch(kFirst + 2, other, 0, false);
	CHECK(files::EnsureBackup(folders.Target(), folders.saves, kFirst + 2, folders.list, folders.swan, other, evidence));
	CHECK(folders.Archived().size() == 2);
}

// Waits up to two seconds for `done`.
template<class Done> static bool Eventually(const Done& done) {
	for (int waited = 0; !done() && waited < 2000; waited += 2) std::this_thread::sleep_for(std::chrono::milliseconds(2));
	return done();
}
// Waits for the count to reach `least`, then a little longer to see that it
// stays there.
static int Settled(const std::atomic<int>& passes, int least) {
	Eventually([&] { return passes >= least; });
	std::this_thread::sleep_for(std::chrono::milliseconds(150));
	return passes;
}

// The worker's schedule. A retry is followed by a run of passes without
// being asked again, and a run ends; asked again, another run follows. After
// a match ends a run follows whatever the passes find.
static void TestArchiveWorkerSchedule() {
	const files::ArchiveWorker::Retries retries{std::chrono::milliseconds(1), std::chrono::milliseconds(4), 3};
	{
		std::atomic<int> passes{0};
		files::ArchiveWorker worker([&] { ++passes; files::ArchivePass pass; pass.retry = true; return pass; }, retries);
		worker.Want();
		CHECK(Settled(passes, 4) == 4);
		worker.Want();
		CHECK(Settled(passes, 8) == 8);
	}
	{
		std::atomic<int> passes{0};
		files::ArchiveWorker worker([&] { ++passes; files::ArchivePass pass; pass.copied = 1; return pass; }, retries);
		worker.Want();
		CHECK(Settled(passes, 1) == 1);
		worker.MatchEnded();
		CHECK(Settled(passes, 5) == 5);
	}
}

// The worker driving real passes. A match ends; the first pass copies an
// older replay, and the match's own replay is saved after it, with no further
// ask: the run after the match end archives it. A copy that cannot be read is
// looked at again until it can, without a change to the save folder.
static void TestArchiveWorkerWithPasses() {
	const files::ArchiveWorker::Retries retries{std::chrono::milliseconds(5), std::chrono::milliseconds(40), 8};
	Folders folders("ember-archive-worker");
	folders.SaveMatch(kFirst, Match(1, 1791600000u), 1791600000u, true);
	std::atomic<int> passes{0};
	files::ArchiveWorker worker([&] { const auto pass = folders.Pass(); ++passes; return pass; }, retries);
	worker.MatchEnded();
	CHECK(Eventually([&] { return passes >= 1; }));
	folders.SaveMatch(kFirst + 1, Match(2, 1791600200u), 0, false);
	CHECK(Eventually([&] { return folders.Archived().size() == 2; }));
	worker.Stop();

	Folders unreadable("ember-archive-worker-unreadable");
	unreadable.SaveMatch(kFirst, Match(3, 1791600000u), 0, false);
	CHECK(unreadable.Pass().copied == 1);
	const fs::path copy = unreadable.Archived().at(0);
	CHECK(fs::remove(copy) && fs::create_directory(copy));
	std::atomic<int> looks{0};
	files::ArchiveWorker again([&] { const auto pass = unreadable.Pass(); ++looks; return pass; }, retries);
	again.Want();
	CHECK(Eventually([&] { return looks >= 2; }));
	CHECK(fs::remove(copy));
	CHECK(Eventually([&] { return unreadable.Archived().size() == 1; }));
}

int main() {
	TestASittingWithTheIndexBehind();
	TestAnIndexedReplayGoesInWithItsRecord();
	TestTheGameRecordComesLater();
	TestRetriesAndADamagedIndex();
	TestAnUnreadableCopy();
	TestAReplayPutBackAndItsBackup();
	TestArchiveWorkerSchedule();
	TestArchiveWorkerWithPasses();
	return 0;
}
