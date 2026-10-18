-- A door to another scene (its Scene property, and an optional Entry).
local Door = { Properties = { Scene = "World", Entry = "" } }

function Door:OnInteract(by)
    Scene.Switch(self.Properties.Scene, self.Properties.Entry)
end

return Door
