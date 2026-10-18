-- A chest that gives its gold once. Whether it's been opened lives in
-- self.Saved, and the chest is Persistent, so it stays open across scene
-- changes and saves.
local Chest = { Properties = { Gold = 10 } }

local OpenTint = { 0.35, 0.25, 0.15, 1 }

function Chest:OnStart()
    if self.Saved.Opened then self.Entity:Set("SpriteRenderer", { tint = OpenTint }) end
end

function Chest:OnInteract(by)
    if self.Saved.Opened then
        Event.Publish("game.toast", { text = "The chest is empty." })
        return
    end
    self.Saved.Opened = true
    self.Entity:Set("SpriteRenderer", { tint = OpenTint })
    Save.Set("gold", Save.Get("gold", 0) + self.Properties.Gold)
    Event.Publish("game.gold.changed", {})
    Event.Publish("game.toast", { text = "Found " .. self.Properties.Gold .. " gold!" })
end

return Chest
