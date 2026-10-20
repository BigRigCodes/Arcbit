-- The elder: everything she says is in assets/dialog/elder.arcdialog, started
-- by her Interactable - no script needed for that. The script only adds
-- touches a dialog file can't: a toast when she gives gold (the dialog's
-- "call": "Bless"), and one when the talk ends with a map bought.
local Elder = {}

function Elder:Bless()
    Event.Publish("game.toast", { text = "demo.scripted.elder_gift", count = 10 })
end

function Elder:OnDialogEnd(id, choice)
    if choice == "map" then Event.Publish("game.toast", { text = "demo.scripted.elder_map" }) end
end

return Elder
