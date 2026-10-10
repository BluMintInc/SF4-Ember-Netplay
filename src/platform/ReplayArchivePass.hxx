#pragma once

// Copying the game's match replays into Ember's archive (ReplaySlots.hxx has
// the files). It is a slot's file that is copied, not what the game's index
// says the slot holds: the game writes a replay's file and its ".0" as the
// match ends, and its indexes at a save that can come later or never. A file
// the index describes is copied with the index's record. One it does not
// describe yet, and whose ".0" says it is whole, is given `settle` for the
// index to catch up and is then copied with a record made from its own header
// (ExportSlotFile). A replay the archive holds already is not copied again,
// whatever its save time. Sidecar runs a pass each time the game saves, with a
// settle while the game can still write its index; the launcher runs one
// before and after a game run, with none (ReplayFiles.hxx). Portable, so
// ReplayArchivePassTest runs it on folders it builds.

#include <chrono>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <system_error>

#include "ReplayArchiveIndex.hxx"
#include "../common/ReplayFileSafety.hxx"

namespace sf4e { namespace replayfiles {

using FileClock = std::filesystem::file_time_type::clock;

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

// Whether the archive holds this replay; evidence is the file that does.
inline bool Holds(const ArchiveTarget& target, const replayslots::Bytes& body, BackupEvidence& evidence) {
	return VerifyBackup(target.held.Candidates(replayslots::Crc32(body.data(), body.size())), target.folder, body, evidence);
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

// What one slot came to. Nothing: no whole replay in its file. Waiting: a
// whole file the index does not describe, saved less than settle ago; it is
// copied from the file alone at readyAt. Failed: the copy could not be
// published or read back.
struct SlotCopy {
	enum class Kind { Nothing, Held, Waiting, Copied, Failed };
	Kind kind = Kind::Nothing;
	bool fromRecord = false;
	FileClock::time_point readyAt{};
};

inline SlotCopy CopySlot(const ArchiveTarget& target, const std::filesystem::path& saves, int slot,
	const replayslots::Bytes& list, const replayslots::Bytes& swan, FileClock::time_point now, FileClock::duration settle) {
	using Kind = SlotCopy::Kind;
	const std::filesystem::path file = saves / std::to_wstring(slot);
	const SlotFile current = ReadSlotFile(target.slots, file);
	replayslots::ReplayHeaderInfo header;
	BackupEvidence evidence;
	if (!replayslots::ReadReplayHeader(current.replay, header)) return {};
	if (Holds(target, current.replay, evidence)) return {Kind::Held};
	replayslots::Bytes exported;
	bool fromRecord = false;
	if (!replayslots::ExportSlotFile(list, swan, slot, current.replay, current.sidecar, exported, fromRecord)) return {};
	const std::wstring name = replayslots::ArchiveName(exported);
	if (name.empty()) return {};
	if (!fromRecord) {
		std::error_code failed;
		const auto written = std::filesystem::last_write_time(file, failed);
		if (!failed && now - written < settle) return {Kind::Waiting, false, written + settle};
	}
	std::error_code ignored;
	std::filesystem::create_directories(target.folder, ignored);
	const std::filesystem::path path = target.folder / name;
	ArchiveFile copied;
	if (!PublishArchive(current.replay, exported,
			[&](ArchiveFile& existing) { return ReadArchive(path, true, existing); },
			[&](const replayslots::Bytes& bytes) { return target.createOnly(path, bytes); }) ||
		ReadArchive(path, true, copied) != ArchiveState::Valid) return {Kind::Failed, fromRecord};
	target.held.Add(copied.crc, path);
	return {Kind::Copied, fromRecord};
}

// One pass over an account's slots first to last. copied counts the new
// archive files, fromFile those of them with a made-up record, failed the
// slots that could not be copied; readyAt is the earliest a waiting slot is
// copied, none when no slot waits. A damaged or half-written index only means
// every slot goes by its file.
struct ArchivePass {
	int copied = 0, fromFile = 0, failed = 0;
	std::optional<FileClock::time_point> readyAt;
};
inline ArchivePass CopySlots(const ArchiveTarget& target, const std::filesystem::path& saves, int first, int last,
	FileClock::time_point now, FileClock::duration settle) {
	const replayslots::Bytes list = ReadFile(saves / L"LIST", kMostIndexBytes).value_or(replayslots::Bytes{});
	const replayslots::Bytes swan = ReadFile(saves / L"replays-swan.dat", kMostIndexBytes).value_or(replayslots::Bytes{});
	ArchivePass pass;
	for (int slot = first; slot <= last; slot++) {
		const SlotCopy copy = CopySlot(target, saves, slot, list, swan, now, settle);
		if (copy.kind == SlotCopy::Kind::Copied) { pass.copied++; pass.fromFile += !copy.fromRecord; }
		else if (copy.kind == SlotCopy::Kind::Failed) pass.failed++;
		else if (copy.kind == SlotCopy::Kind::Waiting && (!pass.readyAt || copy.readyAt < *pass.readyAt)) pass.readyAt = copy.readyAt;
	}
	return pass;
}

} }
