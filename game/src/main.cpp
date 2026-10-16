// Demo dispatcher. Each demo lives in its own TU (WorldDemo.cpp,
// PhysicsDemo.cpp, TileMoveDemo.cpp, ArcDemo.cpp, ObbDemo.cpp, TriggerDemo.cpp) and exposes a single
// Run* entry point. This file picks one at compile time so we can keep
// multiple demos around without colliding on `int main`.
//
// Default = the active free-movement physics demo (PhysicsDemo). Define
// one of the alternatives below in CMake (or uncomment here) to launch a
// sister demo instead.

#include <cstdlib>

void RunWorldDemo();
void RunPhysicsDemo();
void RunTileMoveDemo();
void RunTileMoveArcDemo();
void RunArcDemo();
void RunObbDemo();
void RunTriggerDemo();


// #define ARCBIT_DEMO_WORLD
// #define ARCBIT_DEMO_TILEMOVE
// #define ARCBIT_DEMO_ARC
// #define ARCBIT_DEMO_TILEARC
#define ARCBIT_DEMO_OBB
// #define ARCBIT_DEMO_TRIGGER

int main(int /*argc*/, char* /*argv*/[])
{
#if defined(ARCBIT_DEMO_WORLD)
    RunWorldDemo();
#elif defined(ARCBIT_DEMO_TILEMOVE)
    RunTileMoveDemo();
#elif defined(ARCBIT_DEMO_ARC)
    RunArcDemo();
#elif defined(ARCBIT_DEMO_TILEARC)
    RunTileMoveArcDemo();
#elif defined(ARCBIT_DEMO_OBB)
    RunObbDemo();
#elif defined(ARCBIT_DEMO_TRIGGER)
    RunTriggerDemo();
#else
    RunPhysicsDemo();
#endif
    return EXIT_SUCCESS;
}
