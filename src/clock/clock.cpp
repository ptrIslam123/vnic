#include "clock/clock.h"

#include <ctime>

namespace vnic::clock::monotonic {

std::uint64_t Now() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);  // VDSO → ~20-30 нс
    return ts.tv_sec * 1'000'000'000ULL + ts.tv_nsec;
}

} // namespace vnic::clock::monotonic
