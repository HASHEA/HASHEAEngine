#include "htime.h"
#if defined(_MSC_VER)
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#else
#include <time.h>
#endif
namespace HASHEAENGINE
{
#if defined(_MSC_VER)
    // Cached frequency.
    // From Microsoft Docs: (https://docs.microsoft.com/en-us/windows/win32/api/profileapi/nf-profileapi-queryperformancefrequency)
    // "The frequency of the performance counter is fixed at system boot and is consistent across all processors. 
    // Therefore, the frequency need only be queried upon application initialization, and the result can be cached."
    static LARGE_INTEGER s_frequency;
#endif

    //
    //
    void time_service_init() {
#if defined(_MSC_VER)
        // Cache this value - by Microsoft Docs it will not change during process lifetime.
        QueryPerformanceFrequency(&s_frequency);
#endif
    }

    //
    //
    void time_service_shutdown() {
        // Nothing to do.
    }

    // Taken from the Rust code base: https://github.com/rust-lang/rust/blob/3809bbf47c8557bd149b3e52ceb47434ca8378d5/src/libstd/sys_common/mod.rs#L124
    // Computes (value*numer)/denom without overflow, as long as both
    // (numer*denom) and the overall result fit into i64 (which is the case
    // for our time conversions).
    static int64_t int64_mul_div(int64_t value, int64_t numer, int64_t denom) {
        const int64_t q = value / denom;
        const int64_t r = value % denom;
        // Decompose value as (value/denom*denom + value%denom),
        // substitute into (value*numer)/denom and simplify.
        // r < denom, so (denom*numer) is the upper bound of (r*numer)
        return q * numer + r * numer / denom;
    }

    //
    //
    int64_t time_now() {
#if defined(_MSC_VER)
        // Get current time
        LARGE_INTEGER time;
        QueryPerformanceCounter(&time);

        // Convert to microseconds
        // const i64 microseconds_per_second = 1000000LL;
        const int64_t microseconds = int64_mul_div(time.QuadPart, 1000000LL, s_frequency.QuadPart);
#else
        timespec tp;
        clock_gettime(CLOCK_MONOTONIC, &tp);

        const uint64_t now = tp.tv_sec * 1000000000 + tp.tv_nsec;
        const int64_t microseconds = now / 1000;
#endif

        return microseconds;
    }

    //
    //
    int64_t time_from(int64_t starting_time) {
        return time_now() - starting_time;
    }

    //
    //
    double time_from_microseconds(int64_t starting_time) {
        return time_microseconds(time_from(starting_time));
    }

    //
    //
    double time_from_milliseconds(int64_t starting_time) {
        return time_milliseconds(time_from(starting_time));
    }

    //
    //
    double time_from_seconds(int64_t starting_time) {
        return time_seconds(time_from(starting_time));
    }

    double time_delta_seconds(int64_t starting_time, int64_t ending_time) {
        return time_seconds(ending_time - starting_time);
    }

    double time_delta_milliseconds(int64_t starting_time, int64_t ending_time) {
        return time_milliseconds(ending_time - starting_time);
    }

    //
    //
    double time_microseconds(int64_t time) {
        return (double)time;
    }

    //
    //
    double time_milliseconds(int64_t time) {
        return (double)time / 1000.0;
    }

    //
    //
    double time_seconds(int64_t time) {
        return (double)time / 1000000.0;
    }

};