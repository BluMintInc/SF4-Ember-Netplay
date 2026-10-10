#pragma once

// Copying the game's match replays into Ember's archive (ReplaySlots.hxx has
// the files). It is a slot's file that is copied, not what the game's index
// says the slot holds: the game writes a replay's file and its ".0" as the
// match ends, and its indexes at a save that can come later or never. A whole
// replay is preserved the moment a pass sees it, so the next match into the
// slot cannot take it. A replay the index describes goes straight into the
// archive with the index's record. One the index does not describe yet is
// staged as it is (.pending in the archive), and becomes an archive file with
// the index's record if the index catches up while the slot still holds it,
// else with a record made from its own header once `settle` is over
// (ExportSlotFile). The record matters for one thing: it names the winner of
// a deciding round (ReplayInputs.hxx). A replay the archive holds already is
// not copied again, whatever its save time. Sidecar runs passes as the game
// saves, with a settle; the launcher before and after a game run, with none
// (ReplayFiles.hxx). Portable, so ReplayArchivePassTest runs it on folders it
// builds.

#include <algorithm>
#include <chrono>
#include <cstdio>
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

// A replay waiting for its record: .pending/<crc>-<size>.replay, the slot's
// file byte for byte.
inline std::filesystem::path StagedPath(const ArchiveTarget& target, const replayslots::Bytes& body) {
	char name[40] = {};
	std::snprintf(name, sizeof name, "%08x-%u.replay", replayslots::Crc32(body.data(), body.size()), static_cast<unsigned>(body.size()));
	return target.folder / ".pending" / name;
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

// Puts a replay into the archive under the record of `exported`, and drops
// its staged copy, which the archive file now stands for. False when it could
// not be published or read back.
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
	std::filesystem::remove(StagedPath(target, body), ignored);
	return true;
}

// What one slot came to. Nothing: no replay in it, or a whole file that is
// not one (no slot of a match, a placeholder). Retry: a replay that could not
// be read or is half written, which a later pass may find whole. Held: in the
// archive or staged by an earlier pass. Staged: kept
// as it is until its record or the end of settle. Copied: in the archive with
// the index's record; CopiedFromFile: with one made from its header. Failed:
// it could not be published or staged.
enum class SlotCopy { Nothing, Retry, Held, Staged, Copied, CopiedFromFile, Failed };

inline SlotCopy CopySlot(const ArchiveTarget& target, const std::filesystem::path& saves, int slot,
	const replayslots::Bytes& list, const replayslots::Bytes& swan, FileClock::duration settle) {
	const std::filesystem::path file = saves / std::to_wstring(slot);
	const SlotFile current = ReadSlotFile(target.slots, file);
	std::error_code missing;
	if (current.replay.empty()) return std::filesystem::exists(file, missing) ? SlotCopy::Retry : SlotCopy::Nothing;
	replayslots::ReplayHeaderInfo header;
	if (!replayslots::ReadReplayHeader(current.replay, header)) return SlotCopy::Nothing;
	BackupEvidence evidence;
	if (Holds(target, current.replay, evidence)) {
		std::error_code ignored;
		std::filesystem::remove(StagedPath(target, current.replay), ignored);
		return SlotCopy::Held;
	}
	replayslots::Bytes exported;
	bool fromRecord = false;
	if (!replayslots::ExportSlotFile(list, swan, slot, current.replay, current.sidecar, exported, fromRecord))
		return current.sidecar == replayslots::Sidecar(current.replay) ? SlotCopy::Nothing : SlotCopy::Retry;
	if (fromRecord || settle == FileClock::duration::zero()) {
		if (!Publish(target, current.replay, exported)) return SlotCopy::Failed;
		return fromRecord ? SlotCopy::Copied : SlotCopy::CopiedFromFile;
	}
	const std::filesystem::path staged = StagedPath(target, current.replay);
	std::error_code ignored;
	if (ReadFile(staged) == current.replay) return SlotCopy::Held;
	std::filesystem::create_directories(staged.parent_path(), ignored);
	return target.createOnly(staged, current.replay) ? SlotCopy::Staged : SlotCopy::Failed;
}

// One pass: an account's slots first to last, then every staged replay whose
// settle is over, which goes in with a record made from its header. copied
// counts the new archive files, fromFile those of them with a made-up record,
// staged the replays staged by this pass, failed the copies that could not be
// made. retry says a slot or a copy is worth another look though the folder
// may not change again; readyAt is the earliest a staged replay is due.
struct ArchivePass {
	int copied = 0, fromFile = 0, staged = 0, failed = 0;
	bool retry = false;
	std::optional<FileClock::time_point> readyAt;
};

inline void PublishStaged(const ArchiveTarget& target, FileClock::time_point now, FileClock::duration settle, ArchivePass& pass) {
	std::error_code error;
	for (std::filesystem::directory_iterator at(target.folder / ".pending", error), end; !error && at != end; at.increment(error)) {
		const std::filesystem::path path = at->path();
		const auto body = ReadFile(path);
		replayslots::Bytes exported;
		BackupEvidence evidence;
		// Only a file this pass would have staged under this name.
		if (!body || StagedPath(target, *body) != path || !replayslots::ExportFromReplayAlone(*body, exported)) continue;
		std::error_code failed;
		if (Holds(target, *body, evidence)) { std::filesystem::remove(path, failed); continue; }
		const auto due = std::filesystem::last_write_time(path, failed) + settle;
		if (!failed && now < due) {
			if (!pass.readyAt || due < *pass.readyAt) pass.readyAt = due;
		} else if (Publish(target, *body, exported)) { pass.copied++; pass.fromFile++; }
		else { pass.failed++; pass.retry = true; }
	}
}

inline ArchivePass CopySlots(const ArchiveTarget& target, const std::filesystem::path& saves, int first, int last,
	FileClock::time_point now, FileClock::duration settle) {
	const replayslots::Bytes list = ReadFile(saves / L"LIST", kMostIndexBytes).value_or(replayslots::Bytes{});
	const replayslots::Bytes swan = ReadFile(saves / L"replays-swan.dat", kMostIndexBytes).value_or(replayslots::Bytes{});
	ArchivePass pass;
	for (int slot = first; slot <= last; slot++) {
		switch (CopySlot(target, saves, slot, list, swan, settle)) {
		case SlotCopy::Copied: pass.copied++; break;
		case SlotCopy::CopiedFromFile: pass.copied++; pass.fromFile++; break;
		case SlotCopy::Staged: pass.staged++; break;
		case SlotCopy::Failed: pass.failed++; pass.retry = true; break;
		case SlotCopy::Retry: pass.retry = true; break;
		default: break;
		}
	}
	PublishStaged(target, now, settle, pass);
	return pass;
}

// The replay an import is about to replace has a verified copy in the archive,
// whoever made it: held already, or copied now from its slot. evidence is that
// copy. The caller holds the one copy lock (ReplayFiles.cxx), so no pass
// decides on the slot meanwhile.
inline bool EnsureBackup(const ArchiveTarget& target, const std::filesystem::path& saves, int slot,
	const replayslots::Bytes& list, const replayslots::Bytes& swan, const replayslots::Bytes& body, BackupEvidence& evidence) {
	if (Holds(target, body, evidence)) return true;
	CopySlot(target, saves, slot, list, swan, FileClock::duration::zero());
	return Holds(target, body, evidence);
}

} }
