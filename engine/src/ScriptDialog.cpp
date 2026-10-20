// Dialog for scripts: start a conversation, ask whether one is running, end
// it (docs/dialog.md). Application runs them (ScriptServices).

#include "ScriptState.h"

#include <arcbit/core/Log.h>

namespace Arcbit
{
// Dialog.Start("elder") / Dialog.Start("elder", { Node = "greet", With = npc, By = player }).
// From an entity's hook, With defaults to that entity - an NPC's OnInteract
// talks as itself. False if the dialog doesn't exist; one started while
// another runs waits its turn.
void ScriptEngine::State::BindDialog()
{
    sol::table dialog = Lua.create_table();
    dialog.set_function("Start", [this](const std::string& id, const sol::optional<sol::table> options) {
        if (!Services.StartDialog) {
            LOG_WARN(Scripting, "Dialog.Start: no dialogs here");
            return false;
        }
        DialogRequest request{ .Id = id, .Scene = ContextScene() };
        std::optional<ScriptEntity> with, by;
        if (options) {
            request.Node = (*options)["Node"].get_or(std::string{});
            with         = (*options)["With"].get<std::optional<ScriptEntity>>();
            by           = (*options)["By"].get<std::optional<ScriptEntity>>();
        }
        if (!with && Current.Self.valid()) with = Current.Self["Entity"].get<std::optional<ScriptEntity>>();
        if (with && Resolve(*with)) request.Scene = Resolve(*with), request.With = with->Id;
        if (by && Resolve(*by) == request.Scene) request.By = by->Id;
        return Services.StartDialog(request);
    });
    dialog.set_function("IsActive", [this] { return Services.DialogActive && Services.DialogActive(); });
    dialog.set_function("Stop", [this] {
        if (Services.StopDialog) Services.StopDialog();
    });
    AddGlobal("Dialog", dialog);
}
} // namespace Arcbit
