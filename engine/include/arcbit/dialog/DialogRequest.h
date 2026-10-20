#pragma once

#include <arcbit/ecs/Entity.h>

#include <string>

namespace Arcbit
{
class Scene;

// A conversation to start (Application::StartDialog): which dialog, from
// which node, and who is in it. With - the entity talked to - is held while
// it runs, its script's methods are what "call" reaches, and it hears
// OnDialogEnd; By (the player) is held too. Both live in Scene.
struct DialogRequest
{
    std::string Id;
    std::string Node; // empty: the dialog's start
    Scene*      Scene = nullptr;
    Entity      With;
    Entity      By;
};
} // namespace Arcbit
