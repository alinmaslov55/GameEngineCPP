#ifndef CORE_THREAD_AFFINITY_H
#define CORE_THREAD_AFFINITY_H

#include "Engine/Utils/Logger.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <thread>

namespace Engine::Detail {

/// @brief Asserts in debug builds that the caller executes on the subsystem's owner thread.
/// Compiles to a zero-overhead no-op in Release (#ifdef NDEBUG) builds.
inline void AssertThreadAffinity([[maybe_unused]] std::thread::id ownerThreadId,
                                 [[maybe_unused]] const char* operation) noexcept {
#ifndef NDEBUG
    const std::thread::id callerId = std::this_thread::get_id();
    if (callerId != ownerThreadId) [[unlikely]] {
        std::ostringstream ownerStream;
        ownerStream << ownerThreadId;
        std::ostringstream callerStream;
        callerStream << callerId;
        const char* opName = operation ? operation : "<unknown>";

        ENGINE_CORE_CRITICAL(
            "Main-thread affinity violation in '{}': caller thread {} != owner thread {}.",
            opName, callerStream.str(), ownerStream.str());
        if (auto& logger = ::Engine::Logger::GetCoreLogger()) {
            logger->flush();
        }
        std::fprintf(stderr,
                     "[FATAL] Main-thread affinity violation in '%s': caller=%s, owner=%s.\n",
                     opName, callerStream.str().c_str(), ownerStream.str().c_str());
        std::fflush(stderr);
        assert(false && "Main-thread affinity violation: Scene / Box2D mutation attempted off owner thread!");
        std::abort();
    }
#endif
}

} // namespace Engine::Detail

#endif // CORE_THREAD_AFFINITY_H
