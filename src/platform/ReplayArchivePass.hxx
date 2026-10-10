#pragma once

// Copying the game's match replays into Ember's archive (ReplaySlots.hxx has
// the files). It is a slot's file that is copied, not what the game's index
// says the slot holds: the game writes a replay's file and its ".0" as the
// match ends, and its indexes at a save that can come later or never.
//
// A whole replay that the archive is not known to hold goes first into the
// outbox, a folder of its own outside the archive, so neither the next match
// into the slot nor a failing archive can take it. The outbox is then drained
// into the archive, and an entry goes only once the archive holds its replay
// byte for byte. Its export carries the index's record when the index
// describes the replay, else one made from its own header (ExportSlotFile).
// The game's record names the winner of a deciding round, which a made-up one
// cannot, so when the index catches up while the slot still holds a replay
// archived that way, its record is kept beside the archive
// (ReplayProvenance.hxx: RecordPath), and readers use it (WithGameRecord). A
// replay the archive holds already is not copied again, whatever its save
// time. Sidecar runs passes as the game saves; the launcher before and after
// a game run, so an outbox left by a game that closed is drained too. The
// outbox is drained whether or not any game's saves are found
// (ReplayFiles.hxx). Portable, so ReplayArchivePassTest runs it on folders it
// builds.

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
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

// Where a pass copies to: the archive folder and the outbox; the index of the
// replays the archive holds (each copy is added to it); the cache of the
// game's slot files; and the writer that creates a file only where there is
// none.
struct ArchiveTarget {
	std::filesystem::path folder, outbox;
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

// Keeps the game's export of an archived replay whose archive file has a
// made-up record, under the first of its .records names that is free or is
// that export already. Refused: every name holds another replay's or a file
// that is not one, which is left as it is. Failed: one could not be read or
// written, which a later pass may settle.
enum class RecordKept { Kept, Refused, Failed };
inline RecordKept KeepGameRecord(const ArchiveTarget& target, const replayslots::Bytes& body, const replayslots::Bytes& exported) {
	for (int n = 0; n < kRecordNames; n++) {
		const std::filesystem::path path = RecordPath(target.folder, body.data(), body.size(), n);
		Change existing;
		if (!Snapshot(path, kMostArchiveBytes, existing)) return RecordKept::Failed;
		if (existing.existed) {
			if (RecordsReplay(existing.before, body.data(), body.size())) return RecordKept::Kept;
			continue;
		}
		std::error_code ignored;
		std::filesystem::create_directories(path.parent_path(), ignored);
		return target.createOnly(path, exported) ? RecordKept::Kept : RecordKept::Failed;
	}
	return RecordKept::Refused;
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

// An outbox entry: a replay's export, <crc>-<size>-<n>.export after its
// replay, n telling apart different replays that share a CRC and size, or the
// game's export of a replay beside one made from its header. An entry stands
// for a replay only when it is an export of that replay byte for byte, so
// writing one twice keeps one.
constexpr int kOutboxNames = 8;
inline std::filesystem::path OutboxPath(const ArchiveTarget& target, const replayslots::Bytes& body, int n) {
	char name[64] = {};
	std::snprintf(name, sizeof name, "%08x-%u-%d.export", replayslots::Crc32(body.data(), body.size()), static_cast<unsigned>(body.size()), n);
	return target.outbox / name;
}
inline bool Exports(const replayslots::Bytes& entry, const std::uint8_t* body, std::size_t size) {
	using namespace replayslots;
	return entry.size() == kExportHeaderBytes + size && !std::memcmp(entry.data(), kExportMagic, 8) &&
		std::equal(body, body + size, entry.begin() + kExportHeaderBytes) && Describes(entry.data() + 8, body, size);
}

// The outbox's entries. False when it could not be listed to the end; an
// outbox not made yet is an empty one.
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
	return true;
}

// Puts a replay's export into the outbox. One there already for the replay
// will do, unless it has a made-up record and this export the game's, which
// then goes in beside it. False when there is no outbox or the entry could
// not be written.
inline bool Enqueue(const ArchiveTarget& target, const replayslots::Bytes& body, const replayslots::Bytes& exported) {
	if (target.outbox.empty()) return false;
	const bool gameRecord = !replayslots::MadeUp(exported.data() + 8);
	for (int n = 0; n < kOutboxNames; n++) {
		const std::filesystem::path path = OutboxPath(target, body, n);
		Change existing;
		if (!Snapshot(path, kMostArchiveBytes, existing)) continue;
		if (existing.existed) {
			if (Exports(existing.before, body.data(), body.size()) && (!gameRecord || !replayslots::MadeUp(existing.before.data() + 8))) return true;
			continue;
		}
		std::error_code ignored;
		std::filesystem::create_directories(target.outbox, ignored);
		return target.createOnly(path, exported);
	}
	return false;
}

// What a pass came to. copied counts the new archive files, fromFile those of
// them with a made-up record, failed the replays that could be neither put in
// the outbox nor archived, refused the game's records with no free name.
// waiting counts the outbox entries still to be archived, and is not zero
// either when the outbox could not be listed. retry says a slot is worth
// another look though the save folder may not change again. A damaged or
// half-written index only means every slot goes by its file.
struct ArchivePass {
	int copied = 0, fromFile = 0, failed = 0, refused = 0, waiting = 0;
	bool retry = false;
};

// Settles one outbox entry: archives its replay unless the archive holds it,
// and keeps its game record beside an archive copy that has a made-up one.
// True when the entry is no longer needed.
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
	return false;
}

// Settles every outbox entry it can and removes those settled. An entry that
// cannot be read or settled waits for a later pass; a file there that is not
// an entry is left alone.
inline void DrainOutbox(const ArchiveTarget& target, ArchivePass& pass) {
	if (target.outbox.empty()) return;
	std::vector<std::filesystem::path> entries;
	if (!OutboxEntries(target, entries)) pass.waiting++;
	for (const auto& path : entries) {
		const auto entry = ReadFile(path);
		if (!entry) { pass.waiting++; continue; }
		if (entry->size() < replayslots::kExportHeaderBytes ||
			!Exports(*entry, entry->data() + replayslots::kExportHeaderBytes, entry->size() - replayslots::kExportHeaderBytes)) continue;
		std::error_code kept;
		if (!SettleEntry(target, *entry, pass) || !std::filesystem::remove(path, kept)) pass.waiting++;
	}
}

// What one slot came to. Nothing: no replay in it, or a whole file that is
// not one (no slot of a match, a placeholder). Retry: a replay that could not
// be read or is half written, which a later pass may find whole. Queued: in
// the outbox. Copied: kept from the outbox (it cannot be written) and archived
// straight away. Failed: neither, or the game's record of a held one could
// not be written. Refused: the game's record of a held one has no free name.
enum class SlotCopy { Nothing, Retry, Held, Queued, Copied, Failed, Refused };

inline SlotCopy CopySlot(const ArchiveTarget& target, const std::filesystem::path& saves, int slot,
	const replayslots::Bytes& list, const replayslots::Bytes& swan) {
	const std::filesystem::path file = saves / std::to_wstring(slot);
	const SlotFile current = ReadSlotFile(target.slots, file);
	std::error_code missing;
	if (current.replay.empty()) return std::filesystem::exists(file, missing) ? SlotCopy::Retry : SlotCopy::Nothing;
	replayslots::ReplayHeaderInfo header;
	if (!replayslots::ReadReplayHeader(current.replay, header)) return SlotCopy::Nothing;
	BackupEvidence evidence;
	replayslots::Bytes exported;
	if (Holds(target, current.replay, evidence) == Holding::Held) {
		// Only an Ember archive file has a record (a saver's file is the replay alone).
		if (evidence.replay.bodyOffset != replayslots::kExportHeaderBytes || !replayslots::MadeUp(evidence.replay.contents.data() + 8) ||
			!replayslots::Export(list, swan, slot, current.replay, exported)) return SlotCopy::Held;
		switch (KeepGameRecord(target, current.replay, exported)) {
		case RecordKept::Kept: return SlotCopy::Held;
		case RecordKept::Refused: return SlotCopy::Refused;
		case RecordKept::Failed: break;
		}
		return SlotCopy::Failed;
	}
	bool fromRecord = false;
	if (!replayslots::ExportSlotFile(list, swan, slot, current.replay, current.sidecar, exported, fromRecord))
		return current.sidecar == replayslots::Sidecar(current.replay) ? SlotCopy::Nothing : SlotCopy::Retry;
	if (replayslots::ArchiveName(exported).empty()) return SlotCopy::Nothing;
	if (Enqueue(target, current.replay, exported)) return SlotCopy::Queued;
	return Publish(target, current.replay, exported) ? SlotCopy::Copied : SlotCopy::Failed;
}

// An account's slots first to last, into the outbox.
inline void CopySlots(const ArchiveTarget& target, const std::filesystem::path& saves, int first, int last, ArchivePass& pass) {
	const replayslots::Bytes list = ReadFile(saves / L"LIST", kMostIndexBytes).value_or(replayslots::Bytes{});
	const replayslots::Bytes swan = ReadFile(saves / L"replays-swan.dat", kMostIndexBytes).value_or(replayslots::Bytes{});
	for (int slot = first; slot <= last; slot++) {
		switch (CopySlot(target, saves, slot, list, swan)) {
		case SlotCopy::Copied: pass.copied++; break;
		case SlotCopy::Failed: pass.failed++; pass.retry = true; break;
		case SlotCopy::Retry: pass.retry = true; break;
		case SlotCopy::Refused: pass.refused++; break;
		default: break;
		}
	}
}

// One pass: the outbox, the slots of each of the accounts' save folders (none
// at all is fine), then the outbox again for what they put in it.
inline ArchivePass RunPass(const ArchiveTarget& target, const std::vector<std::filesystem::path>& accounts, int first, int last) {
	ArchivePass pass;
	DrainOutbox(target, pass);
	pass.waiting = 0;
	for (const auto& saves : accounts) CopySlots(target, saves, first, last, pass);
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
	ArchivePass pass;
	CopySlot(target, saves, slot, list, swan);
	DrainOutbox(target, pass);
	return Holds(target, body, evidence) == Holding::Held;
}

} }
