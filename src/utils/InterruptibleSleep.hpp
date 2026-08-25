#pragma once

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <stop_token>

namespace utils
{
    // !\brief Sleep for `duration`, returning early when the worker is asked to
    // stop.
    //
    // Every worker in this tree needs this and each one used to spell it out as
    // a condition-variable wait with a predicate that is never true:
    //
    //     std::unique_lock lock{ m };
    //     cv.wait_for(lock, token, 100ms, []{ return false; });
    //
    // The predicate appeared as `return false`, `return 1 == 0` and
    // `return 0 == 1` in different files, which made a plain sleep read like a
    // condition being waited on. `cv` is what the stop request wakes, so it has
    // to be the one the worker's stop_callback notifies.
    template<class Rep, class Period>
    void InterruptibleSleep(std::condition_variable_any& cv, std::mutex& mutex,
        std::stop_token token, std::chrono::duration<Rep, Period> duration)
    {
        std::unique_lock lock(mutex);
        cv.wait_for(lock, token, duration, [] { return false; });
    }
}
