-- The Scripted demo's scene script: a HUD with the gold count and messages,
-- the L menu, and a console command - all Lua, no C++ for this demo.
local DemoScene = {}

function DemoScene:OnLoad()
    self.Hud = UI.Open("assets/ui/demo_hud.arcui")
    Console.Register("gold", function(amount)
        Save.Set("gold", math.tointeger(tonumber(amount or "") or 0) or 0)
        Event.Publish("game.gold.changed", {})
    end, "gold <n> - set the Scripted demo's gold", true)
    Event.Publish("game.toast", { text = "This scene runs on Lua. E uses things, L opens the menu." })
end

function DemoScene:OnUpdate()
    local menuOpen = self.Menu and self.Menu:IsOpen()
    if Input.IsPressed("Demo_LuaMenu") and not menuOpen then
        self.Menu = UI.Open("assets/ui/demo_menu.arcui")
    end
end

function DemoScene:OnUnload()
    if self.Hud then self.Hud:Close() end
    if self.Menu then self.Menu:Close() end
end

return DemoScene
