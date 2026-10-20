-- Walks back and forth a tile at a time, pausing at each end: one task
-- (self:Run) steering its tile movement with entity:Move, instead of a state
-- machine. Talked to, it's held: it finishes the step it's on, so it stands
-- on a tile facing whoever talks to it, and carries on once let go.
local Guard = { Properties = { Tiles = 7, Pause = 1 } }

-- Toward `target` until the last step is under way, then let it land there.
local function WalkTo(entity, target, tile)
    while true do
        local dx = target.X - entity.Position.X
        if math.abs(dx) <= tile / 2 then break end
        entity:Move(Vec2(dx > 0 and 1 or -1, 0))
        Wait()
    end
    entity:Move(Vec2(0, 0))
    while (target - entity.Position):Length() > 0.5 do Wait() end
end

function Guard:OnStart()
    local tile = (Tilemap.TileToWorld(1, 0) - Tilemap.TileToWorld(0, 0)):Length()
    local home = self.Entity.Position
    local away = home + Vec2(self.Properties.Tiles * tile, 0)
    self:Run(function()
        while true do
            WalkTo(self.Entity, away, tile)
            Wait(self.Properties.Pause)
            WalkTo(self.Entity, home, tile)
            Wait(self.Properties.Pause)
        end
    end)
end

return Guard
