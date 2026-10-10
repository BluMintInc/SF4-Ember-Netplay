#pragma once
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>

namespace sf4e { namespace replayfiles {
// Runs archive passes (ReplayArchivePass.hxx) on a thread of its own, so a
// match's replay reaches the archive as the game saves it. Want asks for a
// pass: a match started or ended, or the game's save folder changed. A pass
// may ask for the next one after a delay at the latest (a slot waiting for the
// game's index); asks made during a pass are kept for one more. Passes never
// overlap, and Stop waits for one in progress.
class ArchiveWorker {
public:
 using Clock = std::chrono::steady_clock;
 using Pass = std::function<std::optional<Clock::duration>()>;
 explicit ArchiveWorker(Pass pass) : pass_(std::move(pass)), thread_([this] { Run(); }) {}
 ~ArchiveWorker() { Stop(); }
 ArchiveWorker(const ArchiveWorker&) = delete;
 ArchiveWorker& operator=(const ArchiveWorker&) = delete;
 void Want() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!stop_) { wanted_ = true; wake_.notify_one(); }
 }
 void Stop() {
  { std::lock_guard<std::mutex> lock(mutex_); stop_ = true; wake_.notify_all(); }
  if (thread_.joinable()) thread_.join();
 }
private:
 void Run() noexcept {
  std::unique_lock<std::mutex> lock(mutex_);
  std::optional<Clock::time_point> due;
  for (;;) {
   const auto asked = [&] { return stop_ || wanted_; };
   if (due) wake_.wait_until(lock, *due, asked);
   else wake_.wait(lock, asked);
   if (stop_) return;
   wanted_ = false;
   lock.unlock();
   std::optional<Clock::duration> again;
   try { again = pass_(); } catch (...) {}
   lock.lock();
   due.reset();
   if (again) due = Clock::now() + *again;
  }
 }
 Pass pass_;
 std::mutex mutex_;
 std::condition_variable wake_;
 bool wanted_ = false, stop_ = false;
 std::thread thread_;
};
} }
