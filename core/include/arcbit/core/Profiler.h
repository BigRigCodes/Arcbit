#pragma once

#include <arcbit/core/Log.h>

#include <chrono>
#include <string>

// ---------------------------------------------------------------------------
// ScopedTimer
//
// Measures wall-clock time for the scope it is declared in and logs the
// result on destruction.
//
// Future: when Tracy is integrated, ARCBIT_PROFILE_SCOPE will expand to a
// Tracy zone instead, giving you a full frame timeline in the Tracy GUI with
// no changes to call sites. See: https://github.com/wolfpld/tracy
// ---------------------------------------------------------------------------
namespace Arcbit {

class ScopedTimer
{
public:
    ScopedTimer(Channel channel, std::string name)
        : m_Channel(channel)
        , m_Name(std::move(name))
        , m_Start(std::chrono::high_resolution_clock::now())
    {}

    ~ScopedTimer()
    {
        auto end     = std::chrono::high_resolution_clock::now();
        auto elapsed = std::chrono::duration<double, std::milli>(end - m_Start).count();
        Log::Debug(m_Channel, "{} - {:.3f} ms", m_Name, elapsed);
    }

    // Non-copyable, non-movable — lifetime tied to the scope.
    ScopedTimer(const ScopedTimer&)            = delete;
    ScopedTimer& operator=(const ScopedTimer&) = delete;
    ScopedTimer(ScopedTimer&&)                 = delete;
    ScopedTimer& operator=(ScopedTimer&&)      = delete;

private:
    Channel     m_Channel;
    std::string m_Name;
    std::chrono::high_resolution_clock::time_point m_Start;
};

} // namespace Arcbit

// ---------------------------------------------------------------------------
// ARCBIT_PROFILE_SCOPE(channel, name)
//
// Times the remainder of the current scope.
//
// Usage:
//   void LoadMap(const std::string& path)
//   {
//       ARCBIT_PROFILE_SCOPE(Engine, "LoadMap");
//       ...
//   }
//
// Output (debug log):
//   [Engine   ] LoadMap — 4.217 ms
// ---------------------------------------------------------------------------
#define ARCBIT_PROFILE_SCOPE(channel, name) \
    ::Arcbit::ScopedTimer _scopedTimer##__LINE__(::Arcbit::Channel::channel, name)
