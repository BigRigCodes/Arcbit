// Test runner entry point. A custom main (rather than Catch2WithMain) so the
// engine's named loggers exist before any test runs — engine code logs through
// them and would dereference a null logger otherwise.

#include <arcbit/core/Log.h>

#include <catch2/catch_session.hpp>

int main(int argc, char* argv[])
{
    Arcbit::Log::Init();
    const int result = Catch::Session().run(argc, argv);
    Arcbit::Log::Shutdown();
    return result;
}
