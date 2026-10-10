#pragma once
#include "../common/ReplayFileSafety.hxx"
#include "../common/ReplaySlots.hxx"
#include <algorithm>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <system_error>
#include <vector>

namespace sf4e { namespace replayfiles {
// Lightweight immutable CRC buckets. Consumers copy only the selected bucket,
// outside the publication lock. No replay bytes ever enter this index.
class ArchiveCandidates {
public:
 using Paths = std::vector<std::filesystem::path>;
 using Bucket = std::shared_ptr<const Paths>;
 using Map = std::map<std::uint32_t, Bucket>;
 Paths Candidates(std::uint32_t crc) const {
  Bucket bucket;
  { std::lock_guard<std::mutex> lock(mutex_); const auto at = files_.find(crc); if (at != files_.end()) bucket = at->second; }
  return bucket ? *bucket : Paths{};
 }
 void Add(std::uint32_t crc, const std::filesystem::path& path) {
  // Build a bucket outside the consumer lock; retry if another publisher won.
  for (;;) {
   Bucket before;
   { std::lock_guard<std::mutex> lock(mutex_); before = files_[crc]; }
   Paths paths = before ? *before : Paths{};
   if (std::find(paths.begin(), paths.end(), path) == paths.end()) paths.push_back(path);
   auto after = std::make_shared<const Paths>(std::move(paths));
   std::lock_guard<std::mutex> lock(mutex_);
   if (files_[crc] != before) continue;
   if (refreshing_) added_[crc] = after;
   files_[crc] = std::move(after);
   return;
  }
 }
 template<class Scan> void Refresh(const Scan& scan) {
  std::lock_guard<std::mutex> worker(workerMutex_);
  { std::lock_guard<std::mutex> lock(mutex_); refreshing_ = true; }
  Map seen;
  try {
   seen = scan();
   for (;;) {
    Map added;
    {
     std::lock_guard<std::mutex> lock(mutex_);
     if (added_.empty()) { files_.swap(seen); refreshing_ = false; break; }
     added.swap(added_);
    }
    for (const auto& item : added) {
     Paths paths = seen[item.first] ? *seen[item.first] : Paths{};
     for (const auto& path : *item.second) if (std::find(paths.begin(), paths.end(), path) == paths.end()) paths.push_back(path);
     seen[item.first] = std::make_shared<const Paths>(std::move(paths));
    }
   }
  } catch (...) {
   Map discarded;
   { std::lock_guard<std::mutex> lock(mutex_); refreshing_ = false; discarded.swap(added_); }
   throw;
  }
  // Destruction of the previous archive-sized map is also outside the lock.
 }
private:
 mutable std::mutex mutex_;
 std::mutex workerMutex_;
 bool refreshing_ = false;
 Map files_, added_;
};

// Every archive file a replay may be in, by CRC, for
// ArchiveCandidates::Refresh: each file that reads whole, which `read` gets
// too, and each of Ember's files whose name gives a CRC (ReplaySlots.hxx:
// ParseArchiveName) but that cannot be read now, which may hold a replay with
// that CRC. complete says nothing was missed: false when a folder could not
// be listed or entered, or a replay file that could not be read gives no CRC
// to keep it under. No archive yet is an empty one, and complete.
template<class Read> ArchiveCandidates::Map ScanArchive(const std::filesystem::path& archive, const Read& read, bool& complete) {
 std::map<std::uint32_t, ArchiveCandidates::Paths> paths;
 complete = true;
 std::error_code error;
 std::filesystem::recursive_directory_iterator at(archive, error);
 if (error) {
  std::error_code probe;
  complete = !std::filesystem::exists(archive, probe) && !probe;
  return {};
 }
 for (const std::filesystem::recursive_directory_iterator end; at != end;) {
  const std::filesystem::path path = at->path();
  if (path.extension() == L".emberreplay" || path.extension() == L".usf4replay") {
   ArchiveFile file;
   const bool inRoot = path.parent_path() == archive;
   const ArchiveState state = ReadArchive(path, inRoot, file);
   std::uint64_t time = 0;
   std::uint32_t crc = 0;
   if (state == ArchiveState::Valid) { read(path, file); paths[file.crc].push_back(path); }
   else if (state == ArchiveState::Unreadable) {
    if (inRoot && replayslots::ParseArchiveName(path.filename().wstring(), time, crc)) paths[crc].push_back(path);
    else complete = false;
   }
  }
  at.increment(error);
  if (error) { complete = false; break; }
 }
 ArchiveCandidates::Map seen;
 for (auto& item : paths) seen.emplace(item.first, std::make_shared<const ArchiveCandidates::Paths>(std::move(item.second)));
 return seen;
}

// A game slot's replay kept by its path, size and write time, so an unchanged
// slot's 50 KB are read and summed once. Only a successful read is kept: a
// slot the game holds open is read again on the next scan. The lock is held
// only to look and to store, never while a file is read.
class SlotFileCache {
public:
 template<class Reader> std::optional<replayslots::Bytes> Read(const std::filesystem::path& file, const Reader& read) {
  std::error_code failed;
  const std::uintmax_t size = std::filesystem::file_size(file, failed);
  if (failed) return std::nullopt;
  const std::filesystem::file_time_type written = std::filesystem::last_write_time(file, failed);
  if (failed) return std::nullopt;
  {
   std::lock_guard<std::mutex> lock(mutex_);
   const auto at = kept_.find(file);
   if (at != kept_.end() && at->second.size == size && at->second.written == written) return at->second.replay;
  }
  std::optional<replayslots::Bytes> replay = read(file);
  if (replay) { std::lock_guard<std::mutex> lock(mutex_); kept_[file] = Kept{size, written, *replay}; }
  return replay;
 }
private:
 struct Kept { std::uintmax_t size; std::filesystem::file_time_type written; replayslots::Bytes replay; };
 std::mutex mutex_;
 std::map<std::filesystem::path, Kept> kept_;
};
} }
