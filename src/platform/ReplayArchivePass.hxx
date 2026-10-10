#pragma once

// Copying the game's match replays into Ember's archive (ReplaySlots.hxx has
// the files). It is a slot's file that is copied, not what the game's index
// says the slot holds: the game writes a replay's file and its ".0" as the
// match ends, and its indexes at a save that can come later or never.
//
// A pass reads the slots first, since they are what the next match takes. A
// whole replay the archive is not known to hold, byte for byte, goes into the
// outbox, a folder of its own outside the archive, as its export: with the
// game's record or one made from its header, by the one rule in
// ReplaySlots.hxx (ExportSlotFile). The pass then settles outbox entries, a
// bounded number of them, so a backlog never holds up the next reading of the
// slots. Settling archives the replay unless the archive holds it, and keeps
// the game's record an entry carries beside an archive copy that has a
// made-up one (ReplayProvenance.hxx: RecordPath), which readers then use
// (WithGameRecord). An entry goes only once settled, so every record and
// replay has one owner on disk until it is in the archive. Sidecar runs
// passes as the game saves; the launcher before and after a game run, so an
// outbox left by a game that closed is drained too, whether or not any game's
// saves are found (ReplayFiles.hxx). Portable, so ReplayArchivePassTest runs
// it on folders it builds.

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#include "ReplayArchiveIndex.hxx"
#include "../common/ReplayFileSafety.hxx"
#include "../common/ReplayProvenance.hxx"

namespace sf4e { namespace replayfiles {

// The largest index read: the game writes LIST at 76 KB and replays-swan.dat
// at 44 KB.
constexpr std::size_t kMostIndexBytes = 1 << 20;

// Where a pass copies to: the archive folder and the outbox; how many outbox
// entries a pass settles at most, and where the next pass starts on them, so
// an entry that cannot be settled does not keep others waiting; the index of
// the replays the archive holds (each copy is added to it); the cache of the
// game's slot files; and the writer that creates a file only where there is
// none.
struct ArchiveTarget {
	std::filesystem::path folder, outbox;
	std::size_t settleBudget;
	std::size_t& settleFrom;
	ArchiveCandidates& held;
	SlotFileCache& slots;
	std::function<bool(const std::filesystem::path&, const replayslots::Bytes&)> createOnly;
};

// Whether the archive holds this replay. Unknown: a file that may hold it
// could not be read, so it is neither copied again nor taken as held.
// evidence is the file that holds it.
enum class Holding { Held, Absent, Unknown };
inline Holding Holds(const ArchiveTarget& target, const replayslots::Bytes& body, BackupEvidence& evidence) {
	Holding holding = Holding::Absent;
	for (const auto& path : target.held.Candidates(replayslots::Crc32(body.data(), body.size()))) {
		ArchiveFile read;
		const ArchiveState state = ReadArchive(path, path.parent_path() == target.folder, read);
		if (state == ArchiveState::Valid && read.SameBody(body)) { evidence = {path, std::move(read)}; return Holding::Held; }
		if (state == ArchiveState::Unreadable) holding = Holding::Unknown;
	}
	return holding;
}

// A slot's replay and ".0" as they are now, each empty when it could not be
// read. The replay is read through the cache; the four bytes of the ".0",
// which the game writes beside the replay, every time.
struct SlotFile { replayslots::Bytes replay, sidecar; };
inline SlotFile ReadSlotFile(SlotFileCache& cache, const std::filesystem::path& file) {
	SlotFile slot;
	if (auto replay = cache.Read(file, [](const std::filesystem::path& path) { return ReadFile(path); })) slot.replay = std::move(*replay);
	if (auto sidecar = ReadFile(file.wstring() + L".0", 4)) slot.sidecar = std::move(*sidecar);
	return slot;
}

// Whether the game's record of exactly this replay is kept beside the archive.
inline bool GameRecordKept(const ArchiveTarget& target, const replayslots::Bytes& body) {
	const auto kept = ReadFile(RecordPath(target.folder, body.data(), body.size()));
	return kept && RecordsReplay(*kept, body.data(), body.size());
}

// Keeps the game's export of an archived replay. Refused: a file that is not
// it is where it goes, and is left as it is. Failed: it could not be read or
// written, which a later pass may settle.
enum class RecordKept { Kept, Refused, Failed };
inline RecordKept KeepGameRecord(const ArchiveTarget& target, const replayslots::Bytes& body, const replayslots::Bytes& exported) {
	const std::filesystem::path path = RecordPath(target.folder, body.data(), body.size());
	Change existing;
	if (!Snapshot(path, kMostArchiveBytes, existing)) return RecordKept::Failed;
	if (existing.existed) return RecordsReplay(existing.before, body.data(), body.size()) ? RecordKept::Kept : RecordKept::Refused;
	std::error_code ignored;
	std::filesystem::create_directories(path.parent_path(), ignored);
	return target.createOnly(path, exported) ? RecordKept::Kept : RecordKept::Failed;
}

// Writes a replay's archive file and adds it to the index. False when it
// could not be written or read back.
inline bool Publish(const ArchiveTarget& target, const replayslots::Bytes& body, const replayslots::Bytes& exported) {
	const std::wstring name = replayslots::ArchiveName(exported);
	if (name.empty()) return false;
	std::error_code ignored;
	std::filesystem::create_directories(target.folder, ignored);
	const std::filesystem::path path = target.folder / name;
	ArchiveFile copied;
	if (!PublishArchive(body, exported,
			[&](ArchiveFile& existing) { return ReadArchive(path, true, existing); },
			[&](const replayslots::Bytes& bytes) { return target.createOnly(path, bytes); }) ||
		ReadArchive(path, true, copied) != ArchiveState::Valid) return false;
	target.held.Add(copied.crc, path);
	return true;
}

// An outbox entry: a replay's export, named after the replay (ReplayKey), with
// .game before .export when it carries the game's record. It stands for that
// replay only when its body is the replay byte for byte, so writing it twice
// keeps one.
inline std::filesystem::path OutboxPath(const ArchiveTarget& target, const replayslots::Bytes& body, const replayslots::Bytes& exported) {
	return target.outbox / (ReplayKey(body.data(), body.size()) + (replayslots::MadeUp(exported.data() + 8) ? ".export" : ".game.export"));
}
inline bool Exports(const replayslots::Bytes& entry, const std::uint8_t* body, std::size_t size) {
	using namespace replayslots;
	return entry.size() == kExportHeaderBytes + size && !std::memcmp(entry.data(), kExportMagic, 8) &&
		std::equal(body, body + size, entry.begin() + kExportHeaderBytes) && Describes(entry.data() + 8, body, size);
}

// Puts a replay's export into the outbox. The entry's path, or none when there
// is no outbox or the entry could not be written.
inline std::optional<std::filesystem::path> Enqueue(const ArchiveTarget& target, const replayslots::Bytes& body, const replayslots::Bytes& exported) {
	if (target.outbox.empty()) return std::nullopt;
	const std::filesystem::path path = OutboxPath(target, body, exported);
	Change existing;
	if (!Snapshot(path, kMostArchiveBytes, existing)) return std::nullopt;
	if (existing.existed) return Exports(existing.before, body.data(), body.size()) ? std::optional<std::filesystem::path>(path) : std::nullopt;
	std::error_code ignored;
	std::filesystem::create_directories(target.outbox, ignored);
	return target.createOnly(path, exported) ? std::optional<std::filesystem::path>(path) : std::nullopt;
}

// The outbox's entries in name order. False when it could not be listed to
// the end; an outbox not made yet is an empty one.
inline bool OutboxEntries(const ArchiveTarget& target, std::vector<std::filesystem::path>& entries) {
	entries.clear();
	std::error_code error;
	std::filesystem::directory_iterator at(target.outbox, error);
	if (error) {
		std::error_code probe;
		return !std::filesystem::exists(target.outbox, probe) && !probe;
	}
	for (const std::filesystem::directory_iterator end; at != end;) {
		if (at->path().extension() == L".export") entries.push_back(at->path());
		at.increment(error);
		if (error) return false;
	}
	std::sort(entries.begin(), entries.end());
	return true;
}

// What a pass came to. copied counts the new archive files, fromFile those of
// them with a made-up record, failed the replays and records that could not
// be written, refused the game's records whose place another file holds.
// waiting counts the outbox entries still to be settled, and is not zero
// either when the outbox could not be listed; more says some of them were
// left for the next pass. retry says a slot is worth another look though the
// save folder may not change again. A damaged or half-written index only
// means every slot goes by its file.
struct ArchivePass {
	int copied = 0, fromFile = 0, failed = 0, refused = 0, waiting = 0;
	bool retry = false, more = false;
};

// Settles one outbox entry: archives its replay unless the archive holds it,
// and keeps the game's record it carries beside an archive copy that has a
// made-up one. True when the entry is no longer needed.
inline bool SettleEntry(const ArchiveTarget& target, const replayslots::Bytes& entry, ArchivePass& pass) {
	const replayslots::Bytes body(entry.begin() + replayslots::kExportHeaderBytes, entry.end());
	BackupEvidence evidence;
	switch (Holds(target, body, evidence)) {
	case Holding::Unknown: return false;
	case Holding::Absent:
		if (!Publish(target, body, entry) || Holds(target, body, evidence) != Holding::Held) { pass.failed++; return false; }
		pass.copied++; pass.fromFile += replayslots::MadeUp(entry.data() + 8);
		return true;
	case Holding::Held: break;
	}
	if (replayslots::MadeUp(entry.data() + 8) || evidence.replay.bodyOffset != replayslots::kExportHeaderBytes ||
		!replayslots::MadeUp(evidence.replay.contents.data() + 8)) return true;
	switch (KeepGameRecord(target, body, entry)) {
	case RecordKept::Kept: return true;
	case RecordKept::Refused: pass.refused++; return true;
	case RecordKept::Failed: break;
	}
	pass.failed++;
	return false;
}

// Settles the outbox entry at path and removes it once settled. False while
// it waits. A file there that is not an entry is left alone.
inline bool Settle(const ArchiveTarget& target, const std::filesystem::path& path, ArchivePass& pass) {
	const auto entry = ReadFile(path);
	if (!entry) return false;
	if (entry->size() < replayslots::kExportHeaderBytes ||
		!Exports(*entry, entry->data() + replayslots::kExportHeaderBytes, entry->size() - replayslots::kExportHeaderBytes)) return true;
	std::error_code kept;
	return SettleEntry(target, *entry, pass) && std::filesystem::remove(path, kept);
}

// Settles up to the budget of outbox entries, starting where the last pass
// stopped and going round.
inline void DrainOutbox(const ArchiveTarget& target, ArchivePass& pass) {
	if (target.outbox.empty()) return;
	std::vector<std::filesystem::path> entries;
	if (!OutboxEntries(target, entries)) pass.waiting++;
	const std::size_t count = entries.size(), visits = (std::min)(count, target.settleBudget);
	for (std::size_t visit = 0; visit < visits; visit++)
		if (!Settle(target, entries[(target.settleFrom + visit) % count], pass)) pass.waiting++;
	pass.waiting += static_cast<int>(count - visits);
	pass.more = visits < count;
	target.settleFrom += visits;
}

// What one slot came to. Nothing: no replay in it, or a whole file that is
// not one (no slot of a match, a placeholder). Retry: a replay that could not
// be read or is half written, which a later pass may find whole. Held: in the
// archive, with any game record of it kept. Queued: in the outbox, a replay
// or a game record of one the archive holds. Copied: kept from the outbox (it
// cannot be written) and archived straight away. Failed: neither.
enum class SlotCopy { Nothing, Retry, Held, Queued, Copied, Failed };

inline SlotCopy CopySlot(const ArchiveTarget& target, const std::filesystem::path& saves, int slot,
	const replayslots::Bytes& list, const replayslots::Bytes& swan) {
	const std::filesystem::path file = saves / std::to_wstring(slot);
	const SlotFile current = ReadSlotFile(target.slots, file);
	std::error_code missing;
	if (current.replay.empty()) return std::filesystem::exists(file, missing) ? SlotCopy::Retry : SlotCopy::Nothing;
	replayslots::ReplayHeaderInfo header;
	if (!replayslots::ReadReplayHeader(current.replay, header)) return SlotCopy::Nothing;
	replayslots::Bytes exported;
	bool fromRecord = false;
	if (!replayslots::ExportSlotFile(list, swan, slot, current.replay, current.sidecar, exported, fromRecord))
		return current.sidecar == replayslots::Sidecar(current.replay) ? SlotCopy::Nothing : SlotCopy::Retry;
	if (replayslots::ArchiveName(exported).empty()) return SlotCopy::Nothing;
	BackupEvidence evidence;
	const bool held = Holds(target, current.replay, evidence) == Holding::Held;
	// Held: only the game's record may be wanted still, beside a copy with a
	// made-up one, and it goes through the outbox like a replay.
	if (held && (!fromRecord || evidence.replay.bodyOffset != replayslots::kExportHeaderBytes ||
		!replayslots::MadeUp(evidence.replay.contents.data() + 8) || GameRecordKept(target, current.replay))) return SlotCopy::Held;
	if (Enqueue(target, current.replay, exported)) return SlotCopy::Queued;
	return !held && Publish(target, current.replay, exported) ? SlotCopy::Copied : SlotCopy::Failed;
}

// One pass: the slots of each of the accounts' save folders (none at all is
// fine), then the outbox.
inline ArchivePass RunPass(const ArchiveTarget& target, const std::vector<std::filesystem::path>& accounts, int first, int last) {
	ArchivePass pass;
	for (const auto& saves : accounts) {
		const replayslots::Bytes list = ReadFile(saves / L"LIST", kMostIndexBytes).value_or(replayslots::Bytes{});
		const replayslots::Bytes swan = ReadFile(saves / L"replays-swan.dat", kMostIndexBytes).value_or(replayslots::Bytes{});
		for (int slot = first; slot <= last; slot++) {
			switch (CopySlot(target, saves, slot, list, swan)) {
			case SlotCopy::Copied: pass.copied++; break;
			case SlotCopy::Failed: pass.failed++; pass.retry = true; break;
			case SlotCopy::Retry: pass.retry = true; break;
			default: break;
			}
		}
	}
	DrainOutbox(target, pass);
	return pass;
}

// The replay an import is about to replace has a verified copy in the archive,
// whoever made it: held already, or copied now from its slot through the
// outbox. evidence is that copy. The caller holds the one copy lock
// (ReplayFiles.cxx), so no pass decides on the slot meanwhile.
inline bool EnsureBackup(const ArchiveTarget& target, const std::filesystem::path& saves, int slot,
	const replayslots::Bytes& list, const replayslots::Bytes& swan, const replayslots::Bytes& body, BackupEvidence& evidence) {
	if (Holds(target, body, evidence) == Holding::Held) return true;
	const SlotFile current = ReadSlotFile(target.slots, saves / std::to_wstring(slot));
	replayslots::Bytes exported;
	bool fromRecord = false;
	if (current.replay == body && replayslots::ExportSlotFile(list, swan, slot, body, current.sidecar, exported, fromRecord)) {
		ArchivePass pass;
		if (const auto queued = Enqueue(target, body, exported)) Settle(target, *queued, pass);
		else Publish(target, body, exported);
	}
	return Holds(target, body, evidence) == Holding::Held;
}

} }
