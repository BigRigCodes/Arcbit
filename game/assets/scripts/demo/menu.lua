-- A menu built in Lua (demo_menu.arcui, L in the Scripted demo): spend gold
-- on lanterns, with the button off until you can afford one.
local Menu = {}

local LanternCost = 5

function Menu:OnEnter()
    self:Refresh()
    Event.Subscribe("engine.locale.changed", function() self:Refresh() end)
end

function Menu:Refresh()
    local gold = Save.Get("gold", 0)
    self.Screen:Find("gold").Text     = Loc.Format("demo.shop.have", { count = gold })
    self.Screen:Find("lanterns").Text = Loc.Format("demo.shop.lanterns", { count = Save.Get("lanterns", 0) })
    self.Screen:Find("buy").Text      = Loc.Format("demo.shop.buy", { cost = LanternCost })
    self.Screen:Find("buy").Enabled   = gold >= LanternCost
end

function Menu:OnBuy()
    Save.Set("gold", Save.Get("gold", 0) - LanternCost)
    Save.Set("lanterns", Save.Get("lanterns", 0) + 1)
    Event.Publish("game.gold.changed", {})
    self:Refresh()
end

function Menu:OnClose() self.Screen:Close() end

return Menu
