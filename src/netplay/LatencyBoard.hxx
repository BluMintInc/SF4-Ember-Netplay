#pragma once
// When the room board's ping subscription is wanted (MEMBER_LATENCY.md 3.2).
// The overlay notes each frame it drew the board; the runtime asks for the
// ping only while that pulse is fresh, for the room the board showed.
#include <atomic>
#include <cstdint>

#include "SessionController.hxx"
#include "../session/RoomModel.hxx"

namespace sf4e { namespace netplay {

class LatencyBoardPulse {
public:
	// A pulse older than this is a board that is no longer drawn.
	static constexpr std::uint64_t ExpiryMs = 1500;
	// Any thread: the board of room `generation` was drawn at `nowMs`.
	void Note(std::uint64_t generation, std::uint64_t nowMs) {
		atMs_.store(0, std::memory_order_relaxed);
		generation_.store(generation, std::memory_order_release);
		atMs_.store(nowMs, std::memory_order_release);
	}
	// Whether the board of room `generation` was drawn within ExpiryMs of `nowMs`.
	bool Shown(std::uint64_t generation, std::uint64_t nowMs) const {
		const auto at = atMs_.load(std::memory_order_acquire);
		return generation && at && at <= nowMs && nowMs - at < ExpiryMs &&
			generation_.load(std::memory_order_acquire) == generation;
	}

private:
	std::atomic<std::uint64_t> generation_{0}, atMs_{0};
};

// The board is shown, the room joined, no game prepared or played, the local
// member's table not readying or in a match, and no replacement room opening.
inline bool LatencyWanted(bool boardShown, RoomState roomState, MatchState match, const room::Snapshot& room,
	bool replacing) {
	if (!boardShown || roomState != RoomState::Joined || Fighting(match) || replacing || !room.roomEpoch) return false;
	for (const auto& member : room.members) {
		if (member.id != room.localMember) continue;
		if (member.table < 0 || member.table >= static_cast<std::int8_t>(room::TableCount)) return true;
		const auto phase = room.tables[member.table].phase;
		return phase != room::TablePhase::Ready && phase != room::TablePhase::Playing && phase != room::TablePhase::Paused;
	}
	return true;
}

} }
