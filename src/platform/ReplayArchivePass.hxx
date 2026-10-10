#pragma once

// Copying the game's match replays into Ember's archive (ReplaySlots.hxx has
// the files). It is a slot's file that is copied, not what the game's index
// says the slot holds: the game writes a replay's file and its ".0" as the
// match ends, and its indexes at a save that can come later or never. A whole
// replay goes into the archive the moment a pass sees it, so the next match
// into the slot cannot take it: with the index's record when the index
// describes it, else with one made from its own header (ExportSlotFile). The
// game's record names the winner of a deciding round, which a made-up one
// cannot, so when the index catches up while the slot still holds a replay
// archived that way, its record is kept beside the archive
// (ReplayProvenance.hxx: RecordPath), and readers use it (WithGameRecord). A
// replay the archive holds already is not copied again, whatever its save
// time. Sidecar runs passes as the game saves; the launcher before and after a
// game run (ReplayFiles.hxx). Portable, so ReplayArchivePassTest runs it on
// folders it builds.

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <system_error>

#include "ReplayArchiveIndex.hxx"
#include "../common/ReplayFileSafety.hxx"
#include "../common/ReplayProvenance.hxx"

namespace sf4e { namespace replayfiles {

// The largest index read: the game writes LIST at 76 KB and replays-swan.dat
// at 44 KB.
constexpr std::size_t kMostIndexBytes = 1 << 20;

// Where a pass copies to: the archive folder, the index of the replays it
// holds (each copy is added to it), the cache of the game's slot files and the
// writer that creates a file only where there is none.
struct ArchiveTarget {
	std::filesystem::path folder;
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

// Keeps the game's record of an archived replay whose archive file has a
// made-up one. False when it could not be written.
inline bool KeepGameRecord(const ArchiveTarget& target, const replayslots::Bytes& body, const replayslots::Bytes& exported) {
	const std::filesystem::path path = RecordPath(target.folder, body.data(), body.size());
	const replayslots::Bytes record(exported.begin() + 8, exported.begin() + 8 + replayslots::kRecordBytes);
	Change existing;
	if (!Snapshot(path, replayslots::kRecordBytes, existing)) return false;
	if (existing.existed) return true; // written before, or not ours to replace
	std::error_code ignored;
	std::filesystem::create_directories(path.parent_path(), ignored);
	return target.createOnly(path, record);
}

// What one slot came to. Nothing: no replay in it, or a whole file that is
// not one (no slot of a match, a placeholder). Retry: a replay that could not
// be read, is half written, or whose copy in the archive could not be read,
// which a later pass may settle. Copied: into the archive with the index's
// record; CopiedFromFile: with one made from its header. Failed: the copy, or
// the game's record of a held one, could not be written.
enum class SlotCopy { Nothing, Retry, Held, Copied, CopiedFromFile, Failed };

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
	switch (Holds(target, current.replay, evidence)) {
	case Holding::Unknown: return SlotCopy::Retry;
	case Holding::Held:
		// Only an Ember archive file has a record (a saver's file is the replay alone).
		if (evidence.replay.bodyOffset != replayslots::kExportHeaderBytes || !replayslots::MadeUp(evidence.replay.contents.data() + 8) ||
			!replayslots::Export(list, swan, slot, current.replay, exported)) return SlotCopy::Held;
		return KeepGameRecord(target, current.replay, exported) ? SlotCopy::Held : SlotCopy::Failed;
	case Holding::Absent: break;
	}
	bool fromRecord = false;
	if (!replayslots::ExportSlotFile(list, swan, slot, current.replay, current.sidecar, exported, fromRecord))
		return current.sidecar == replayslots::Sidecar(current.replay) ? SlotCopy::Nothing : SlotCopy::Retry;
	const std::wstring name = replayslots::ArchiveName(exported);
	if (name.empty()) return SlotCopy::Nothing;
	std::error_code ignored;
	std::filesystem::create_directories(target.folder, ignored);
	const std::filesystem::path path = target.folder / name;
	ArchiveFile copied;
	if (!PublishArchive(current.replay, exported,
			[&](ArchiveFile& existing) { return ReadArchive(path, true, existing); },
			[&](const replayslots::Bytes& bytes) { return target.createOnly(path, bytes); }) ||
		ReadArchive(path, true, copied) != ArchiveState::Valid) return SlotCopy::Failed;
	target.held.Add(copied.crc, path);
	return fromRecord ? SlotCopy::Copied : SlotCopy::CopiedFromFile;
}

// One pass over an account's slots first to last. copied counts the new
// archive files, fromFile those of them with a made-up record, failed what
// could not be written; retry says a slot is worth another look though the
// save folder may not change again. A damaged or half-written index only
// means every slot goes by its file.
struct ArchivePass {
	int copied = 0, fromFile = 0, failed = 0;
	bool retry = false;
};
inline ArchivePass CopySlots(const ArchiveTarget& target, const std::filesystem::path& saves, int first, int last) {
	const replayslots::Bytes list = ReadFile(saves / L"LIST", kMostIndexBytes).value_or(replayslots::Bytes{});
	const replayslots::Bytes swan = ReadFile(saves / L"replays-swan.dat", kMostIndexBytes).value_or(replayslots::Bytes{});
	ArchivePass pass;
	for (int slot = first; slot <= last; slot++) {
		switch (CopySlot(target, saves, slot, list, swan)) {
		case SlotCopy::Copied: pass.copied++; break;
		case SlotCopy::CopiedFromFile: pass.copied++; pass.fromFile++; break;
		case SlotCopy::Failed: pass.failed++; pass.retry = true; break;
		case SlotCopy::Retry: pass.retry = true; break;
		default: break;
		}
	}
	return pass;
}

// The replay an import is about to replace has a verified copy in the archive,
// whoever made it: held already, or copied now from its slot. evidence is that
// copy. The caller holds the one copy lock (ReplayFiles.cxx), so no pass
// decides on the slot meanwhile.
inline bool EnsureBackup(const ArchiveTarget& target, const std::filesystem::path& saves, int slot,
	const replayslots::Bytes& list, const replayslots::Bytes& swan, const replayslots::Bytes& body, BackupEvidence& evidence) {
	if (Holds(target, body, evidence) == Holding::Held) return true;
	CopySlot(target, saves, slot, list, swan);
	return Holds(target, body, evidence) == Holding::Held;
}

} }
