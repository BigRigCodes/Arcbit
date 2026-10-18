-- The Scripted demo's HUD (demo_hud.arcui): gold in the corner, messages
-- along the bottom that clear after a few seconds. A "game.toast" names a
-- locale key (text) and the values for its placeholders, so whoever sends
-- one needn't know the player's language.
local Hud = {}

function Hud:OnEnter()
    self:ShowGold()
    Event.Subscribe("game.gold.changed", function() self:ShowGold() end)
    Event.Subscribe("game.toast", function(payload) self:Say(Loc.Format(payload.text, payload)) end)
    Event.Subscribe("engine.locale.changed", function() self:ShowGold() end)
end

function Hud:ShowGold()
    self.Screen:Find("gold").Text = Loc.Format("demo.scripted.gold", { count = Save.Get("gold", 0) })
end

function Hud:Say(text)
    local message = self.Screen:Find("message")
    message.Text = text
    if self.Clearing then self.Clearing:Stop() end
    self.Clearing = Timer.After(4, function() message.Text = "" end)
end

return Hud
