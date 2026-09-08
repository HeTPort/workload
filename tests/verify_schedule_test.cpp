#include "avs/verify_schedule.h"

#include <cassert>

int main() {
    assert(avs::ShouldVerify(1, 1));
    assert(!avs::ShouldVerify(1, 2));
    assert(avs::ShouldVerify(2, 2));
    assert(!avs::ShouldVerify(100, 0));

    assert(avs::ShouldLogSuccessfulVerify(1, 60));
    assert(!avs::ShouldLogSuccessfulVerify(2, 60));
    assert(avs::ShouldLogSuccessfulVerify(61, 60));
    assert(!avs::ShouldLogSuccessfulVerify(1, 0));
    return 0;
}
