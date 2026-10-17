#pragma once

#include <arcbit/core/Log.h>

// ---------------------------------------------------------------------------
// Platform debug break
// ---------------------------------------------------------------------------
#ifdef _MSC_VER
    #define ARCBIT_DEBUG_BREAK() __debugbreak()
#elif defined(__GNUC__) || defined(__clang__)
    #define ARCBIT_DEBUG_BREAK() __builtin_trap()
#else
    #define ARCBIT_DEBUG_BREAK() ((void)0)
#endif

// ---------------------------------------------------------------------------
// Strip full file path down to filename only for cleaner assert output.
// Evaluated at compile time.
// ---------------------------------------------------------------------------
namespace Arcbit::Detail {

constexpr const char* StripPath(const char* path)
{
    const char* file = path;
    for (const char* p = path; *p; ++p)
    {
        if (*p == '/' || *p == '\\')
            file = p + 1;
    }
    return file;
}

} // namespace Arcbit::Detail

#define ARCBIT_FILENAME (::Arcbit::Detail::StripPath(__FILE__))

// ---------------------------------------------------------------------------
// ARCBIT_ASSERT(condition, message)
//
// Debug builds only. If `condition` is false:
//   - Logs a critical message with file + line
//   - Breaks into the debugger
//
// Stripped entirely in Release builds — condition is NOT evaluated.
// Use for invariants that should never be false if the code is correct.
// ---------------------------------------------------------------------------
#ifdef ARCBIT_DEBUG
    #define ARCBIT_ASSERT(condition, msg)                                          \
        do {                                                                       \
            if (!(condition)) {                                                    \
                LOG_CRITICAL(Engine, "Assert failed: ({}) - {} [{}:{}]",           \
                    #condition, msg, ARCBIT_FILENAME, __LINE__);                   \
                ARCBIT_DEBUG_BREAK();                                              \
            }                                                                     \
        } while (false)
#else
    #define ARCBIT_ASSERT(condition, msg) ((void)0)
#endif

// ---------------------------------------------------------------------------
// ARCBIT_VERIFY(condition, message)
//
// Both Debug and Release builds. `condition` is always evaluated.
// If false:
//   - Debug:   logs critical + breaks into debugger
//   - Release: logs error + continues (no crash)
//
// Use for things that can fail at runtime (resource loads, API calls)
// where you want to catch the failure in debug but survive in release.
// ---------------------------------------------------------------------------
#ifdef ARCBIT_DEBUG
    #define ARCBIT_VERIFY(condition, msg)                                          \
        do {                                                                       \
            if (!(condition)) {                                                    \
                LOG_CRITICAL(Engine, "Verify failed: ({}) - {} [{}:{}]",           \
                    #condition, msg, ARCBIT_FILENAME, __LINE__);                   \
                ARCBIT_DEBUG_BREAK();                                              \
            }                                                                     \
        } while (false)
#else
    #define ARCBIT_VERIFY(condition, msg)                                          \
        do {                                                                       \
            if (!(condition)) {                                                    \
                LOG_ERROR(Engine, "Verify failed: ({}) - {} [{}:{}]",              \
                    #condition, msg, ARCBIT_FILENAME, __LINE__);                   \
            }                                                                     \
        } while (false)
#endif
