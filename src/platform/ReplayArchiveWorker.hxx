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
// a match started. MatchEnded asks for one and expects a new replay: the game
// saves it after the battle closes, so passes go on until one copies or
// stages something. A pass that asks for a retry (a half-written slot, a
// failed copy) or still expects a replay is followed by another after a delay
// that doubles from `first` up to `most`, at most `attempts` times in a row,
// whether or not the folder changes again; then the worker waits to be asked.
// A staged replay brings the next pass forward to when it is due. Asks made
// during a pass are kept for one more. Passes never overlap, and Stop waits
// for one in progress.
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
 // After a pass: when the next one runs, none until asked.
 std::optional<Clock::duration> Next(const ArchivePass& pass, bool& expecting, int& attempt) const {
  if (pass.copied || pass.staged) expecting = false;
  std::optional<Clock::duration> next;
  if (!pass.retry && !expecting) attempt = 0;
  else if (attempt < retries_.attempts) next = (std::min)(retries_.first * (1 << (std::min)(attempt++, 16)), retries_.most);
  else { attempt = 0; expecting = false; }
  if (pass.readyAt) {
   const auto left = std::chrono::duration_cast<Clock::duration>((std::max)(*pass.readyAt - FileClock::now(), FileClock::duration::zero()));
   next = next ? (std::min)(*next, left) : left;
  }
  return next;
 }
 void Run() noexcept {
  std::unique_lock<std::mutex> lock(mutex_);
  std::optional<Clock::time_point> due;
  bool expecting = false;
  int attempt = 0;
  for (;;) {
   const auto asked = [&] { return stop_ || wanted_; };
   if (due) wake_.wait_until(lock, *due, asked);
   else wake_.wait(lock, asked);
   if (stop_) return;
   expecting = expecting || matchEnded_;
   wanted_ = matchEnded_ = false;
   lock.unlock();
   ArchivePass pass;
   try { pass = pass_(); } catch (...) { pass.retry = true; }
   const auto next = Next(pass, expecting, attempt);
   lock.lock();
   due.reset();
   if (next) due = Clock::now() + *next;
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
