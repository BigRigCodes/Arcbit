-- Walks back and forth, pausing at each end: one task (self:Run) with Wait,
-- instead of a state machine.
local Guard = { Properties = { Distance = 256, Seconds = 2, Pause = 1 } }

local Tick = 1 / 60 -- a task resumes once per fixed tick

-- Held (someone's talking to it), it stands where it is until let go.
local function Walk(entity, to, seconds)
    local from, t = entity.Position, 0
    while t < seconds do
        Wait()
        if not entity:IsHeld() then
            t = t + Tick
            entity.Position = from + (to - from) * math.min(t / seconds, 1)
        end
    end
end

function Guard:OnStart()
    local home = self.Entity.Position
    local away = home + Vec2(self.Properties.Distance, 0)
    self:Run(function()
        while true do
            Walk(self.Entity, away, self.Properties.Seconds)
            Wait(self.Properties.Pause)
            Walk(self.Entity, home, self.Properties.Seconds)
            Wait(self.Properties.Pause)
        end
    end)
end

return Guard
