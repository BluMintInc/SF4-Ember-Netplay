// The per-frame trace a desync report logs (common/FrameTrace.hxx), the frame
// identity its rows are sampled under, and the order mismatches reach it in
// (common/ConfirmedCheckpoint.hxx).

#include <stdio.h>

#include <map>
#include <vector>

#include "../common/ConfirmedCheckpoint.hxx"
#include "../common/FrameTrace.hxx"

using sf4e::statehash::FrameTrace;

static int g_failures = 0;

#define CHECK(cond)                                                          \
	do {                                                                     \
		if (!(cond)) {                                                       \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);           \
			g_failures++;                                                    \
		}                                                                    \
	} while (0)

struct Row {
	int value = 0;
};

using Trace = FrameTrace<Row, 8>;

// A rollback re-simulates frames already traced: each pass replaces the row
// and is counted, so a confirmed frame keeps the corrected timeline's values.
static void ResimulationReplacesTheRowAndCountsThePass() {
	Trace trace;
	trace.Begin(5).row.value = 1;
	CHECK(trace.Find(5)->passes == 1);
	Trace::Entry& again = trace.Begin(5);
	CHECK(again.row.value == 0);
	again.row.value = 2;
	CHECK(trace.Find(5)->passes == 2);
	CHECK(trace.Find(5)->row.value == 2);
}

// A slot taken by a later frame no longer answers for the earlier one, and
// that later frame starts with a single pass.
static void ALaterFrameTakesTheSlotOver() {
	Trace trace;
	trace.Begin(3).row.value = 3;
	trace.Begin(3);
	trace.Begin(11).row.value = 11;
	CHECK(trace.Find(3) == nullptr);
	CHECK(trace.Find(11)->passes == 1);
	CHECK(trace.Find(11)->row.value == 11);
}

static void UntracedFramesAreNotFound() {
	Trace trace;
	CHECK(trace.Find(0) == nullptr);
	CHECK(trace.Find(-1) == nullptr);
	trace.Begin(4);
	CHECK(trace.Find(12) == nullptr);
	trace.Clear();
	CHECK(trace.Find(4) == nullptr);
	CHECK(trace.Begin(4).passes == 1);
}

// GGPO as the battle drives it (sf4e__Game__Battle__System.cxx PlayGgpoFrame,
// Ggpo.cxx callbacks): a frame is sampled right after it is simulated, under
// the state frame after the held one; a save or a rollback's load sets the
// held frame. Values stand for the engine state: the inputs played so far.
struct ScriptedBattle {
	int held = 0; // GGPO saves frame 0 before the first input
	int state = 0;
	int saved[16] = {};
	FrameTrace<Row, 16> trace;

	void Simulate(int input) {
		state += input;
		Row& row = trace.Begin(sf4e::statehash::StateFrameAfterInput(held)).row;
		row.value = input * 1000 + state;
	}
	void Save() { saved[++held] = state; }
	void Load(int frame) { state = saved[frame]; held = frame; }
};

// The advance that saves a frame may poll and roll back over it. The frames
// it re-simulates with the real input replace the predicted samples, so the
// frame keeps the corrected input with the state it made, and two passes.
static void ARollbackInsideTheAdvanceKeepsTheCorrectedSample() {
	ScriptedBattle battle;
	for (int frame = 0; frame < 4; frame++) { battle.Simulate(1); battle.Save(); }
	// Frame 5 is simulated with the remote input predicted as 1...
	battle.Simulate(1);
	battle.Save();
	// ...and that save's poll learns it was 2 from frame 5 on: roll back.
	battle.Load(4);
	battle.Simulate(2);
	battle.Save();
	CHECK(battle.held == 5);
	const auto* entry = battle.trace.Find(5);
	CHECK(entry != nullptr);
	CHECK(entry->passes == 2);
	CHECK(entry->row.value == 2 * 1000 + 6);
	CHECK(battle.trace.Find(4)->passes == 1);
	CHECK(battle.trace.Find(4)->row.value == 1 * 1000 + 4);
}

struct Checkpoint {
	int frame;
	int ggpoStateFrame;
};

// Frames 1890 and 1920 sit in the ring's last and first slots. Waiting
// together, the earlier is compared first, so the one trace a battle logs
// ends at 1890 (frames 1860 to 1890) on both PCs; later mismatches log none
// until the battle starts again.
static void TheEarliestPendingMismatchTakesTheTrace() {
	using namespace sf4e::statehash;
	CHECK(CheckpointRingIndex(1890) == CheckpointRingSize - 1);
	CHECK(CheckpointRingIndex(1920) == 0);
	const Checkpoint local[] = { { 1920, 1920 }, { 1890, 1890 } }; // ring order
	const auto find = [&](int frame) -> const Checkpoint* {
		for (const Checkpoint& checkpoint : local) if (checkpoint.frame == frame) return &checkpoint;
		return nullptr;
	};
	Trace trace;
	std::vector<int> reported, dumped;
	const auto report = [&](const Checkpoint& checkpoint, int) {
		reported.push_back(checkpoint.frame);
		if (trace.TakeDump()) dumped.push_back(checkpoint.frame);
	};
	std::map<int, int> pending = { { 1920, 0 }, { 1890, 0 } };
	ReconcileRemoteHashes(pending, 1920, find, report);
	CHECK(reported == std::vector<int>({ 1890, 1920 }));
	CHECK(dumped == std::vector<int>({ 1890 }));
	CHECK(pending.empty());
	CHECK(!trace.TakeDump());
	// A new battle logs its own first mismatch.
	trace.Clear();
	CHECK(trace.TakeDump());

	// A checkpoint not yet confirmed here keeps its remote hash waiting.
	pending = { { 1920, 0 }, { 1890, 0 } };
	reported.clear();
	ReconcileRemoteHashes(pending, 1900, find, report);
	CHECK(reported == std::vector<int>({ 1890 }));
	CHECK(pending.size() == 1 && pending.count(1920) == 1);
}

int main() {
	ResimulationReplacesTheRowAndCountsThePass();
	ALaterFrameTakesTheSlotOver();
	UntracedFramesAreNotFound();
	ARollbackInsideTheAdvanceKeepsTheCorrectedSample();
	TheEarliestPendingMismatchTakesTheTrace();
	if (g_failures) {
		printf("%d failure(s)\n", g_failures);
		return 1;
	}
	printf("FrameTrace: all passed\n");
	return 0;
}
