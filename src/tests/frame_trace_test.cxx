// The per-frame trace a desync report logs (common/FrameTrace.hxx).

#include <stdio.h>

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

int main() {
	ResimulationReplacesTheRowAndCountsThePass();
	ALaterFrameTakesTheSlotOver();
	UntracedFramesAreNotFound();
	if (g_failures) {
		printf("%d failure(s)\n", g_failures);
		return 1;
	}
	printf("FrameTrace: all passed\n");
	return 0;
}
