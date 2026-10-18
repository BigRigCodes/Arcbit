-- A sign: shows its text (a property, set in the scene file) when read.
local Sign = { Properties = { Text = "..." } }

function Sign:OnInteract(by)
    Event.Publish("game.toast", { text = self.Properties.Text })
end

return Sign
