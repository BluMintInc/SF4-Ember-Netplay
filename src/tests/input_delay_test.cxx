#include "../common/InputDelay.hxx"
#include <cstdlib>
#include <initializer_list>
#include <iostream>
#include "test_support.hxx"
int main() {
    using namespace sf4e;
    // Auto holds the recommendation between its bounds and never readies at 0.
    CHECK(AutoInputDelay(0) == 1 && AutoInputDelay(1) == 1 && AutoInputDelay(2) == 2 && AutoInputDelay(3) == 3);
    CHECK(AutoInputDelay(4) == 4 && AutoInputDelay(5) == 4 && AutoInputDelay(MaximumInputDelay) == 4);
    for (int recommended = -1; recommended <= MaximumInputDelay + 1; ++recommended)
        CHECK(AutoInputDelay(recommended) >= AutoInputDelayMinimum && AutoInputDelay(recommended) <= AutoInputDelayMaximum &&
            AutoInputDelay(recommended) > 0);
    // No recommendation, or one outside the delay range, is a new profile's two frames.
    CHECK(AutoInputDelay(-1) == DefaultInputDelay && AutoInputDelay(MaximumInputDelay + 1) == DefaultInputDelay && DefaultInputDelay == 2);
    // The delay row: Auto, then 0 to MaximumInputDelay, stopping at either end.
    CHECK(MinimumInputDelay == 0);
    CHECK(StepInputDelay(false, 2, -1) == 1 && StepInputDelay(false, 2, 1) == 3);
    CHECK(StepInputDelay(false, 1, -1) == 0 && StepInputDelay(false, 0, 1) == 1);
    CHECK(StepInputDelay(false, 0, -1) == AutoInputDelayChoice && StepInputDelay(true, 3, -1) == AutoInputDelayChoice);
    CHECK(StepInputDelay(true, 3, 1) == 0);
    CHECK(StepInputDelay(false, MaximumInputDelay, 1) == MaximumInputDelay);
    // Every step lands on Auto or a delay in range, and walking Right from
    // Auto visits Auto, 0, 1, ... MaximumInputDelay in order.
    for (int delay = MinimumInputDelay; delay <= MaximumInputDelay; ++delay)
        for (int delta : {-1, 1}) for (bool automatic : {false, true}) {
            const int next = StepInputDelay(automatic, delay, delta);
            CHECK(next == AutoInputDelayChoice || (next >= MinimumInputDelay && next <= MaximumInputDelay));
        }
    int walked = StepInputDelay(true, 5, 1);
    for (int expected = 0; expected <= MaximumInputDelay; ++expected) {
        CHECK(walked == expected);
        walked = StepInputDelay(false, walked, 1);
    }
    // The Auto choice is never a delay the room or a saved profile could hold.
    CHECK(AutoInputDelayChoice < MinimumInputDelay);
    static_assert(AutoInputDelay(-1) == 2 && StepInputDelay(true, 0, 1) == 0, "constant expressions");
    std::cout << "Auto delay bounds and delay row steps passed\n";
}
