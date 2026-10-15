// Demo dispatcher. Each demo lives in its own TU (WorldDemo.cpp,
// PhysicsDemo.cpp, TileMoveDemo.cpp, ArcDemo.cpp, ObbDemo.cpp) and exposes a single
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


// #define ARCBIT_DEMO_WORLD
// #define ARCBIT_DEMO_TILEMOVE
// #define ARCBIT_DEMO_ARC
// #define ARCBIT_DEMO_TILEARC
#define ARCBIT_DEMO_OBB

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
#else
    RunPhysicsDemo();
#endif
    return EXIT_SUCCESS;
}
