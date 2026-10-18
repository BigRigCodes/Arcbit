-- A trigger along the guard's path: stepping into it gets a warning.
local Lookout = { Properties = { Text = "Guard: Stop! You've commited crimes against sweet rolls!" } }

function Lookout:OnTriggerEnter(other)
    if other.Tag == "Player" then Event.Publish("game.toast", { text = self.Properties.Text }) end
end

return Lookout
