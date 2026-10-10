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

// A game's save folder, an archive and an outbox beside it, with the index
// and writer a pass is given. While archiveBroken is set no file can be
// written into the archive.
struct Folders {
	fs::path root, saves, archive, outbox;
	Bytes list = EmptyList(), swan = EmptySwan();
	files::ArchiveCandidates held;
	files::SlotFileCache cache;
	std::atomic<bool> archiveBroken{false};
	explicit Folders(const char* name) {
		root = fs::temp_directory_path() / (std::string(name) + "-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		saves = root / "ssf4_savedata"; archive = root / "replays"; outbox = root / "replay-outbox";
		CHECK(fs::create_directories(saves));
		WriteIndexes();
	}
	~Folders() { std::error_code ignored; fs::remove_all(root, ignored); }
	void WriteIndexes() { Save(saves / "LIST", list); Save(saves / "replays-swan.dat", swan); }
	// The pass's view; a process that starts afresh has an index and a slot
	// cache of its own.
	files::ArchiveTarget Target(files::ArchiveCandidates& index, files::SlotFileCache& slots) {
		return {archive, outbox, index, slots, [this](const fs::path& path, const Bytes& bytes) {
			if (archiveBroken && *path.lexically_relative(archive).begin() != "..") return false;
			if (fs::exists(path)) return false;
			Save(path, bytes);
			return true;
		}};
	}
	files::ArchiveTarget Target() { return Target(held, cache); }
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
	files::ArchivePass Pass() { return files::RunPass(Target(), {saves}, kFirst, kLast); }
	// The archive's replay files, each a different replay, and their replays.
	std::vector<fs::path> Archived() const {
		std::vector<fs::path> paths;
		const std::size_t bodies = Bodies(&paths).size();
		CHECK(bodies == paths.size());
		return paths;
	}
	std::set<Bytes> Bodies(std::vector<fs::path>* paths = nullptr) const {
		std::set<Bytes> bodies;
		std::error_code missing;
		for (fs::directory_iterator at(archive, missing), end; !missing && at != end; at.increment(missing)) {
			if (at->path().extension() != L".emberreplay") continue;
			files::ArchiveFile read;
			if (files::ReadArchive(at->path(), true, read) != files::ArchiveState::Valid) continue;
			bodies.insert(read.Body());
			if (paths) paths->push_back(at->path());
		}
		return bodies;
	}
	std::size_t Outboxed() const {
		std::size_t entries = 0;
		std::error_code missing;
		for (fs::directory_iterator at(outbox, missing), end; !missing && at != end; at.increment(missing)) entries++;
		return entries;
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

// A slot caught between its replay and its ".0" asks for another pass, which
// takes it; a replay whose archive file cannot be written waits in the
// outbox. A damaged index does not stop a slot from going by its file.
static void TestRetriesAndADamagedIndex() {
	Folders folders("ember-archive-retry");
	const Bytes first = Match(1, 1791600000u), second = Match(2, 1791600200u);
	Save(folders.saves / "300", first);
	Save(folders.saves / "300.0", Sidecar(second));
	auto pass = folders.Pass();
	CHECK(pass.copied == 0 && pass.retry);
	Save(folders.saves / "300.0", Sidecar(first));
	folders.archiveBroken = true;
	pass = folders.Pass();
	CHECK(pass.copied == 0 && pass.waiting == 1 && !pass.retry && folders.Outboxed() == 1);
	folders.archiveBroken = false;
	pass = folders.Pass();
	CHECK(pass.copied == 1 && pass.waiting == 0 && !pass.retry && folders.Outboxed() == 0);
	folders.SaveMatch(kFirst + 1, second, 0, false);
	folders.swan[100] ^= 0xFF; // the leading CRC no longer matches
	folders.WriteIndexes();
	pass = folders.Pass();
	CHECK(pass.copied == 1 && pass.fromFile == 1 && !pass.retry);
	CHECK(folders.Archived().size() == 2);
}

// A replay whose archive file cannot be written waits in the outbox, so the
// next match taking its slot does not lose it: both reach the archive, byte
// for byte, once it can be written.
static void TestTheOutboxOutlivesTheSlot() {
	Folders folders("ember-archive-held");
	const Bytes first = Match(1, 1791600000u), second = Match(2, 1791600200u);
	folders.archiveBroken = true;
	folders.SaveMatch(kFirst, first, 0, false);
	auto pass = folders.Pass();
	CHECK(pass.copied == 0 && pass.failed == 1 && pass.waiting == 1);
	folders.SaveMatch(kFirst, second, 0, false);
	CHECK(folders.Pass().waiting == 2);
	folders.archiveBroken = false;
	pass = folders.Pass();
	CHECK(pass.copied == 2 && pass.waiting == 0 && folders.Outboxed() == 0);
	CHECK(folders.Bodies() == std::set<Bytes>({first, second}));
}

// An archive file with the same CRC as a new replay cannot be read, so it is
// not known whether the archive holds the replay. The replay goes into the
// outbox all the same, and its slot being taken loses nothing.
static void TestAnUnreadableCandidateKeepsTheReplay() {
	Folders folders("ember-archive-candidate");
	const Bytes earlier = Match(1, 1791600000u);
	Bytes later = Match(1, 1791600600u); // the same size, played later
	ForgeCrc(later, Crc32(earlier.data(), earlier.size()));
	folders.SaveMatch(kFirst, earlier, 0, false);
	CHECK(folders.Pass().copied == 1);
	const fs::path copy = folders.Archived().at(0);
	const Bytes kept = *files::ReadFile(copy);
	CHECK(fs::remove(copy) && fs::create_directory(copy)); // there, but no file to read
	folders.SaveMatch(kFirst + 1, later, 0, false);
	// Neither replay can be told from the unreadable file: both are kept.
	auto pass = folders.Pass();
	CHECK(pass.copied == 0 && pass.waiting == 2 && folders.Outboxed() == 2);
	const Bytes next = Match(3, 1791600900u);
	folders.SaveMatch(kFirst + 1, next, 0, false);
	CHECK(folders.Pass().copied == 1);
	CHECK(fs::remove(copy));
	Save(copy, kept);
	pass = folders.Pass();
	CHECK(pass.copied == 1 && pass.waiting == 0 && folders.Outboxed() == 0);
	CHECK(folders.Bodies() == std::set<Bytes>({earlier, later, next}));
}

// The outbox has no limit: forty matches through ten slots while the archive
// cannot be written all wait there, and all reach the archive once it can.
static void TestManyReplaysAllWait() {
	Folders folders("ember-archive-many");
	folders.archiveBroken = true;
	std::set<Bytes> all;
	for (int match = 0; match < 40; match++) {
		const Bytes replay = Match(match, 1791600000u + match * 180);
		all.insert(replay);
		folders.SaveMatch(kFirst + match % 10, replay, 0, false);
		CHECK(folders.Pass().waiting == match + 1);
	}
	folders.archiveBroken = false;
	const auto pass = folders.Pass();
	CHECK(pass.copied == 40 && pass.waiting == 0 && folders.Outboxed() == 0 && folders.Bodies() == all);
}

// A game that closes with replays in the outbox leaves them there. The next
// process archives them with an index and a slot cache of its own, and needs
// no game's saves to find them.
static void TestTheOutboxOutlivesTheProcess() {
	Folders folders("ember-archive-closed");
	const Bytes first = Match(1, 1791600000u), second = Match(2, 1791600200u);
	folders.archiveBroken = true;
	folders.SaveMatch(kFirst, first, 0, false);
	CHECK(folders.Pass().waiting == 1);
	folders.SaveMatch(kFirst, second, 0, false);
	folders.archiveBroken = false;
	files::ArchiveCandidates index;
	files::SlotFileCache slots;
	auto pass = files::RunPass(folders.Target(index, slots), {}, kFirst, kLast);
	CHECK(pass.copied == 1 && pass.waiting == 0 && folders.Outboxed() == 0);
	pass = files::RunPass(folders.Target(index, slots), {folders.saves}, kFirst, kLast);
	CHECK(pass.copied == 1 && folders.Bodies() == std::set<Bytes>({first, second}));
}

// An outbox that cannot be listed counts as waiting, not as empty.
static void TestAnOutboxThatCannotBeListed() {
	Folders folders("ember-archive-unlisted");
	folders.archiveBroken = true;
	folders.SaveMatch(kFirst, Match(1, 1791600000u), 0, false);
	CHECK(folders.Pass().waiting == 1);
	fs::rename(folders.outbox, folders.root / "away");
	Save(folders.outbox, Bytes(8, 1)); // a file where the outbox was
	folders.archiveBroken = false;
	files::ArchiveCandidates index;
	files::SlotFileCache slots;
	CHECK(files::RunPass(folders.Target(index, slots), {}, kFirst, kLast).waiting > 0);
}

// The game's record of a replay seen while its archive copy cannot be read is
// kept with it in the outbox, and kept beside the archive copy when the
// outbox is drained, after the slot has been taken.
static void TestTheGameRecordSurvivesTheOutbox() {
	Folders folders("ember-archive-outbox-record");
	const Bytes replay = Match(1, 1791600000u);
	folders.SaveMatch(kFirst, replay, 0, false);
	CHECK(folders.Pass().copied == 1);
	const fs::path copy = folders.Archived().at(0);
	const Bytes contents = *files::ReadFile(copy);
	CHECK(fs::remove(copy) && fs::create_directory(copy));
	folders.Index(kFirst, replay, 1791600010u);
	CHECK(folders.Pass().waiting == 1);
	folders.SaveMatch(kFirst, Match(2, 1791600200u), 0, false);
	CHECK(fs::remove(copy));
	Save(copy, contents);
	const auto pass = folders.Pass();
	CHECK(pass.waiting == 0 && folders.Outboxed() == 0);
	CHECK(RecordWinner(files::WithGameRecord(folders.archive, contents).data() + 8) == 1);

	// A replay in the outbox with a made-up record gets its game record beside
	// it when the index catches up, though the slot is taken before the
	// archive can be written.
	Folders later("ember-archive-outbox-richer");
	const Bytes other = Match(3, 1791600300u);
	later.archiveBroken = true;
	later.SaveMatch(kFirst, other, 0, false);
	CHECK(later.Pass().waiting == 1);
	later.Index(kFirst, other, 1791600310u);
	CHECK(later.Pass().waiting == 2);
	later.SaveMatch(kFirst, Match(4, 1791600400u), 0, false);
	later.archiveBroken = false;
	CHECK(later.Pass().waiting == 0 && later.Outboxed() == 0);
	bool found = false;
	for (const auto& path : later.Archived()) {
		const Bytes read = files::WithGameRecord(later.archive, *files::ReadFile(path));
		if (Bytes(read.begin() + kExportHeaderBytes, read.end()) != other) continue;
		found = true;
		CHECK(RecordWinner(read.data() + 8) == 1);
	}
	CHECK(found);
}

// The game's record kept for a replay is that replay's own: another replay
// with the same CRC, size and fighters has its record under another name, a
// file there that is not a record is left alone, and a replay with every name
// taken has none kept, which the pass reports without retrying.
static void TestGameRecordsBelongToTheirOwnReplay() {
	Folders folders("ember-archive-records");
	const Bytes replay = Match(5, 1791600000u), twin = SameCrcDifferentBody(replay);
	folders.SaveMatch(kFirst, replay, 0, false);
	CHECK(folders.Pass().copied == 1);
	const fs::path archived = folders.Archived().at(0);
	// The twin's record, naming player 1, takes the first name.
	Bytes list = EmptyList(), swan = EmptySwan(), twinExport;
	Fill(list, swan, kFirst, twin, 0x44);
	const_cast<std::uint8_t*>(Record(list, swan, kFirst))[49] = 0;
	Seal(swan);
	CHECK(Export(list, swan, kFirst, twin, twinExport));
	CHECK(files::KeepGameRecord(folders.Target(), twin, twinExport) == files::RecordKept::Kept);
	// The second name holds something that is not a record.
	Save(files::RecordPath(folders.archive, replay.data(), replay.size(), 1), Bytes(64, 7));
	const Bytes contents = *files::ReadFile(archived);
	CHECK(RecordWinner(files::WithGameRecord(folders.archive, contents).data() + 8) == -1);
	folders.Index(kFirst, replay, 1791600010u);
	const auto pass = folders.Pass();
	CHECK(pass.copied == 0 && pass.refused == 0 && !pass.retry);
	CHECK(RecordWinner(files::WithGameRecord(folders.archive, contents).data() + 8) == 1);
	CHECK(files::ReadFile(files::RecordPath(folders.archive, replay.data(), replay.size(), 1)) == Bytes(64, 7));

	Folders full("ember-archive-records-full");
	full.SaveMatch(kFirst, replay, 0, false);
	CHECK(full.Pass().copied == 1);
	CHECK(fs::create_directories(full.archive / ".records"));
	for (int n = 0; n < files::kRecordNames; n++) Save(files::RecordPath(full.archive, replay.data(), replay.size(), n), Bytes(64, 7));
	full.Index(kFirst, replay, 1791600010u);
	const auto refused = full.Pass();
	CHECK(refused.refused == 1 && refused.failed == 0 && !refused.retry);
	CHECK(RecordWinner(files::WithGameRecord(full.archive, *files::ReadFile(full.Archived().at(0))).data() + 8) == -1);
}

// An archive file that may hold the slot's replay but cannot be read is not
// taken as a copy, and the replay is not copied again beside it: it waits in
// the outbox until the file can be read.
static void TestAnUnreadableCopy() {
	Folders folders("ember-archive-unreadable");
	folders.SaveMatch(kFirst, Match(3, 1791600000u), 0, false);
	CHECK(folders.Pass().copied == 1);
	const fs::path copy = folders.Archived().at(0);
	CHECK(fs::remove(copy) && fs::create_directory(copy)); // there, but no file to read
	const auto pass = folders.Pass();
	CHECK(pass.copied == 0 && pass.waiting == 1 && folders.Outboxed() == 1 && folders.Archived().empty());
	CHECK(fs::remove(copy));
	CHECK(folders.Pass().copied == 1 && folders.Archived().size() == 1 && folders.Outboxed() == 0);
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
// a match ends passes run until its window closes, whatever they find, and
// then stop.
static void TestArchiveWorkerSchedule() {
	{
		std::atomic<int> passes{0};
		files::ArchiveWorker worker([&] { ++passes; files::ArchivePass pass; pass.retry = true; return pass; },
			{std::chrono::milliseconds(1), std::chrono::milliseconds(4), 3, {}, {}});
		worker.Want();
		CHECK(Settled(passes, 4) == 4);
		worker.Want();
		CHECK(Settled(passes, 8) == 8);
	}
	{
		std::atomic<int> passes{0};
		files::ArchiveWorker worker([&] { ++passes; files::ArchivePass pass; pass.copied = 1; return pass; },
			{std::chrono::milliseconds(1), std::chrono::milliseconds(4), 3, std::chrono::milliseconds(60), {}});
		worker.Want();
		CHECK(Settled(passes, 1) == 1);
		worker.MatchEnded();
		const int afterWindow = Settled(passes, 6);
		CHECK(afterWindow >= 6 && Settled(passes, afterWindow) == afterWindow);
	}
}

// The worker driving real passes. A match ends; the first pass copies an
// older replay, the save folder changes twenty times, each change asking for
// a pass that finds nothing new, and then the match's own replay is saved
// with no further ask: the window after the match end archives it. A copy
// that cannot be read is looked at again until it can, without a change to
// the save folder.
static void TestArchiveWorkerWithPasses() {
	const files::ArchiveWorker::Schedule schedule{std::chrono::milliseconds(10), std::chrono::milliseconds(40), 3, std::chrono::seconds(1), std::chrono::seconds(1)};
	Folders folders("ember-archive-worker");
	folders.SaveMatch(kFirst, Match(1, 1791600000u), 1791600000u, true);
	std::atomic<int> passes{0};
	files::ArchiveWorker worker([&] { const auto pass = folders.Pass(); ++passes; return pass; }, schedule);
	worker.MatchEnded();
	CHECK(Eventually([&] { return passes >= 1; }));
	for (int change = 0; change < 20; change++) {
		const int before = passes;
		worker.Want();
		CHECK(Eventually([&] { return passes > before; }));
	}
	folders.SaveMatch(kFirst + 1, Match(2, 1791600200u), 0, false);
	CHECK(Eventually([&] { return folders.Archived().size() == 2; }));
	worker.Stop();

	Folders unreadable("ember-archive-worker-unreadable");
	unreadable.SaveMatch(kFirst, Match(3, 1791600000u), 0, false);
	CHECK(unreadable.Pass().copied == 1);
	const fs::path copy = unreadable.Archived().at(0);
	CHECK(fs::remove(copy) && fs::create_directory(copy));
	std::atomic<int> looks{0};
	files::ArchiveWorker again([&] { const auto pass = unreadable.Pass(); ++looks; return pass; },
		{std::chrono::milliseconds(5), std::chrono::milliseconds(40), 8, {}, std::chrono::milliseconds(20)});
	again.Want();
	CHECK(Eventually([&] { return looks >= 2; }));
	CHECK(fs::remove(copy));
	CHECK(Eventually([&] { return unreadable.Archived().size() == 1; }));
	again.Stop();

	// The archive cannot be written past every retry and match window: the
	// outbox keeps the worker coming back until it can, unasked, though no
	// account is signed in any more.
	Folders broken("ember-archive-worker-broken");
	broken.archiveBroken = true;
	broken.SaveMatch(kFirst, Match(4, 1791600000u), 0, false);
	CHECK(broken.Pass().waiting == 1);
	std::atomic<int> tries{0};
	files::ArchiveWorker steady([&] { const auto pass = files::RunPass(broken.Target(), {}, kFirst, kLast); ++tries; return pass; },
		{std::chrono::milliseconds(1), std::chrono::milliseconds(2), 2, std::chrono::milliseconds(5), std::chrono::milliseconds(20)});
	steady.MatchEnded();
	CHECK(Eventually([&] { return tries >= 12; }));
	broken.archiveBroken = false;
	CHECK(Eventually([&] { return broken.Archived().size() == 1 && broken.Outboxed() == 0; }));
	steady.Stop();

	// An outbox that cannot be listed keeps the worker coming back too, until
	// it can be.
	Folders unlisted("ember-archive-worker-unlisted");
	unlisted.archiveBroken = true;
	unlisted.SaveMatch(kFirst, Match(5, 1791600000u), 0, false);
	CHECK(unlisted.Pass().waiting == 1);
	fs::rename(unlisted.outbox, unlisted.root / "away");
	Save(unlisted.outbox, Bytes(8, 1));
	unlisted.archiveBroken = false;
	std::atomic<int> lists{0};
	files::ArchiveWorker listing([&] { const auto pass = files::RunPass(unlisted.Target(), {}, kFirst, kLast); ++lists; return pass; },
		{std::chrono::milliseconds(1), std::chrono::milliseconds(2), 2, {}, std::chrono::milliseconds(20)});
	listing.Want();
	CHECK(Eventually([&] { return lists >= 4; }));
	fs::remove(unlisted.outbox);
	fs::rename(unlisted.root / "away", unlisted.outbox);
	CHECK(Eventually([&] { return unlisted.Archived().size() == 1 && unlisted.Outboxed() == 0; }));
}

int main() {
	TestASittingWithTheIndexBehind();
	TestAnIndexedReplayGoesInWithItsRecord();
	TestTheGameRecordComesLater();
	TestRetriesAndADamagedIndex();
	TestTheOutboxOutlivesTheSlot();
	TestAnUnreadableCandidateKeepsTheReplay();
	TestManyReplaysAllWait();
	TestTheOutboxOutlivesTheProcess();
	TestAnOutboxThatCannotBeListed();
	TestTheGameRecordSurvivesTheOutbox();
	TestGameRecordsBelongToTheirOwnReplay();
	TestAnUnreadableCopy();
	TestAReplayPutBackAndItsBackup();
	TestArchiveWorkerSchedule();
	TestArchiveWorkerWithPasses();
	return 0;
}
