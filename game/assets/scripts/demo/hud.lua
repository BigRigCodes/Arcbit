-- The Scripted demo's HUD (demo_hud.arcui): gold in the corner, messages
-- along the bottom that clear after a few seconds.
local Hud = {}

function Hud:OnEnter()
    self:ShowGold()
    Event.Subscribe("game.gold.changed", function() self:ShowGold() end)
    Event.Subscribe("game.toast", function(payload) self:Say(payload.text) end)
end

function Hud:ShowGold()
    self.Screen:Find("gold").Text = "Gold: " .. Save.Get("gold", 0)
end

function Hud:Say(text)
    local message = self.Screen:Find("message")
    message.Text = text
    if self.Clearing then self.Clearing:Stop() end
    self.Clearing = Timer.After(4, function() message.Text = "" end)
end

return Hud
