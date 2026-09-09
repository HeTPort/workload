#include "avs/verify_schedule.h"
#include <cassert>

int main() {
    avs::SuccessLogRateLimit limit;
    assert(limit.Allow(0));
    assert(!limit.Allow(0));
    assert(!limit.Allow(999));
    assert(limit.Allow(1000));
    assert(!limit.Allow(1001));
    assert(!limit.Allow(500)); // A regressing timestamp cannot create a burst.
    assert(limit.Allow(5000));
    for (unsigned i = 0; i < 100000; ++i) assert(!limit.Allow(5000));
}
