#pragma once
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>

#include "ReplayArchivePass.hxx"

namespace sf4e { namespace replayfiles {
// Runs archive passes (ReplayArchivePass.hxx) on a thread of its own and
// decides when the next one runs, so a match's replay reaches the archive as
// the game saves it. Want asks for a pass: the game's save folder changed, or
// a match started. MatchEnded asks for one and opens a window of passes: the
// game saves the replay after the battle closes, and its index at a save that
// may come later, which a watcher may not report. The window is a run of
// passes `first` apart, then twice that, up to `most`, `attempts` of them,
// whatever the passes find; a later MatchEnded starts it again. A pass that
// asks for a retry (a slot or a copy that a later pass may settle) is
// followed by the same run, unless it is one of a run already: retries stop
// at the end of a run until the worker is asked again. Asks made during a
// pass are kept for one more. Passes never overlap, and Stop waits for one in
// progress.
class ArchiveWorker {
public:
 using Clock = std::chrono::steady_clock;
 using Pass = std::function<ArchivePass()>;
 struct Retries { Clock::duration first, most; int attempts; };
 ArchiveWorker(Pass pass, Retries retries) : pass_(std::move(pass)), retries_(retries), thread_([this] { Run(); }) {}
 ~ArchiveWorker() { Stop(); }
 ArchiveWorker(const ArchiveWorker&) = delete;
 ArchiveWorker& operator=(const ArchiveWorker&) = delete;
 void Want() { Ask(false); }
 void MatchEnded() { Ask(true); }
 void Stop() {
  { std::lock_guard<std::mutex> lock(mutex_); stop_ = true; wake_.notify_all(); }
  if (thread_.joinable()) thread_.join();
 }
private:
 void Ask(bool matchEnded) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (stop_) return;
  wanted_ = true; matchEnded_ = matchEnded_ || matchEnded;
  wake_.notify_one();
 }
 void Run() noexcept {
  std::unique_lock<std::mutex> lock(mutex_);
  std::optional<Clock::time_point> due;
  // Passes left in the window, and how many of it have run.
  int left = 0, ran = 0;
  for (;;) {
   const auto woken = [&] { return stop_ || wanted_; };
   if (due) wake_.wait_until(lock, *due, woken);
   else wake_.wait(lock, woken);
   if (stop_) return;
   // Asked for, rather than the next pass of a run.
   const bool asked = wanted_;
   if (matchEnded_) { left = retries_.attempts; ran = 0; }
   wanted_ = matchEnded_ = false;
   lock.unlock();
   ArchivePass pass;
   try { pass = pass_(); } catch (...) { pass.retry = true; }
   if (pass.retry && asked && left == 0) { left = retries_.attempts; ran = 0; }
   lock.lock();
   due.reset();
   if (left > 0) {
    left--;
    due = Clock::now() + (std::min)(retries_.first * (1 << (std::min)(ran++, 16)), retries_.most);
   }
  }
 }
 Pass pass_;
 const Retries retries_;
 std::mutex mutex_;
 std::condition_variable wake_;
 bool wanted_ = false, matchEnded_ = false, stop_ = false;
 std::thread thread_;
};
} }
