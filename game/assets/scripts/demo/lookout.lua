-- A trigger along the guard's path: stepping into it gets a warning.
local Lookout = { Properties = { Text = "Guard: Halt! The castle is closed today." } }

function Lookout:OnTriggerEnter(other)
    if other.Tag == "Player" then Event.Publish("game.toast", { text = self.Properties.Text }) end
end

return Lookout
