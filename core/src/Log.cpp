#include <arcbit/core/Log.h>

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace Arcbit
{
// Static member definition
std::array<std::shared_ptr<spdlog::logger>, Log::ChannelCount> Log::s_Loggers;

// ---------------------------------------------------------------------------
// Channel metadata
// ---------------------------------------------------------------------------
namespace
{
    struct ChannelInfo
    {
        const char* Name;
    };

    constexpr std::array<ChannelInfo, static_cast<size_t>(Channel::Count)> ChannelInfoTable = {
        {
            {"Engine"},    // Channel::Engine
            {"Render"},    // Channel::Render
            {"ECS"},       // Channel::ECS
            {"Platform"},  // Channel::Platform
            {"Audio"},     // Channel::Audio
            {"Combat"},    // Channel::Combat
            {"Scripting"}, // Channel::Scripting
            {"EventBus"},  // Channel::EventBus
            {"Game"},      // Channel::Game
            {"UI"},        // Channel::UI
        }
    };
} // anonymous namespace

// ---------------------------------------------------------------------------
// Log::Init
//
// Format:
//   [HH:MM:SS.mmm] [Channel  ] [ThreadID] [level   ] message
//
// Each channel gets its own named logger sharing the same sink(s), so you
// can see the channel in every line and still filter or redirect per-sink.
// ---------------------------------------------------------------------------
void Log::Init(bool fileOutput, const std::string& filePath)
{
    #ifdef _WIN32
    // Log text is UTF-8. A Windows console otherwise decodes it with the OEM
    // code page, so any non-ASCII character prints as garbage (an em dash
    // shows up as three junk characters).
    SetConsoleOutputCP(CP_UTF8);
    #endif

    // Build the shared sink list
    std::vector<spdlog::sink_ptr> sinks;

    auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    consoleSink->set_pattern("[%T.%e] [%-9n] [%6t] [%^%-8l%$] %v");
    sinks.push_back(consoleSink);

    if (fileOutput) {
        auto fileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(filePath, true);
        // No color codes in the file
        fileSink->set_pattern("[%T.%e] [%-9n] [%6t] [%-8l] %v");
        sinks.push_back(fileSink);
    }

    // Create one named logger per channel, all sharing the same sinks
    for (size_t i = 0; i < ChannelCount; ++i) {
        auto logger = std::make_shared<spdlog::logger>(ChannelInfoTable[i].Name, sinks.begin(), sinks.end());

        #ifdef ARCBIT_DEBUG
        logger->set_level(spdlog::level::trace);
        logger->flush_on(spdlog::level::warn);
        #else
        logger->set_level(spdlog::level::info);
        logger->flush_on(spdlog::level::err);
        #endif

        spdlog::register_logger(logger);
        s_Loggers[i] = std::move(logger);
    }
}

void Log::Shutdown() { spdlog::shutdown(); }

void Log::SetLevel(Channel channel, spdlog::level::level_enum level) { GetLogger(channel).set_level(level); }

void Log::SetAllLevels(spdlog::level::level_enum level)
{
    for (auto& logger : s_Loggers)
        logger->set_level(level);
}

spdlog::logger& Log::GetLogger(Channel channel) { return *s_Loggers[static_cast<size_t>(channel)]; }
} // namespace Arcbit
