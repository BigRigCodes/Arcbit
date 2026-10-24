// Application: the inventory screen (docs/items.md#the-inventory-screen) -
// opening it on a bag, its stock art, and item icons for any UI.

#include <arcbit/app/Application.h>
#include <arcbit/assets/SpriteSheet.h>
#include <arcbit/core/Log.h>
#include <arcbit/events/EngineEvents.h>
#include <arcbit/ui/DiscardScreen.h>
#include <arcbit/ui/InventoryScreen.h>

#include <algorithm>
#include <array>
#include <format>
#include <stdexcept>

namespace Arcbit
{
void Application::OpenInventory(const std::string_view bag)
{
    if (!_inventories->Find(bag)) {
        LOG_WARN(Engine, "OpenInventory: there's no bag '{}'", bag);
        return;
    }
    if (!_inventoryArt.Sampler.IsValid()) LoadInventoryArt();
    auto screen     = std::make_unique<InventoryScreen>();
    UIScreen* shown = screen.get();
    screen->Art     = _inventoryArt;
    screen->Bag     = std::string(bag);
    screen->OnClose = [this, shown] { _ui.Close(shown); };
    screen->Host    = { .Items = &_items, .Bags = _inventories.get(), .Members = _party.get(), .Stats = &_statSet,
                        .Money = _currencies.get(),
                        .Use   = [this](const std::string_view item, const std::string_view member, const std::string_view from) {
                            return UseItem(item, member, from);
                        },
                        .Drop = [this](const std::string_view item, const u32 count, const std::string_view from) {
                            return DropItem(item, count, from).IsValid();
                        },
                        .LoadIcon = [this](const std::string_view icon) { return LoadItemIcon(icon); } };
    _ui.OpenMenu(std::move(screen));
}

bool Application::ChangeBag(const std::string_view bag, const InventoryRules& to, std::function<void(bool)> done)
{
    Inventory* from = _inventories->Find(bag);
    if (!from) return false;
    const auto changed = [this, name = std::string(bag), kind = to.Kind] {
        _events.PublishEngine(EngineEvents::BagChanged, { { "bag", name }, { "kind", kind } });
    };
    if (from->Convert(to)) {
        changed();
        if (done) done(true);
        return true;
    }
    if (!_inventoryArt.Sampler.IsValid()) LoadInventoryArt();
    auto      screen = std::make_unique<DiscardScreen>();
    UIScreen* shown  = screen.get();
    screen->Art      = _inventoryArt;
    screen->Bag      = from;
    screen->To       = to;
    screen->Items    = &_items;
    screen->LoadIcon = [this](const std::string_view icon) { return LoadItemIcon(icon); };
    screen->OnDone   = [this, shown, changed, done = std::move(done)](const bool ok) {
        _ui.Close(shown);
        if (ok) changed();
        if (done) done(ok);
    };
    _ui.Push(std::move(screen)); // over the inventory screen, if that's where it came from
    return false;
}

// The stock inventory art, shared by the inventory and transfer screens.
void Application::LoadInventoryArt()
{
    const auto load = [this](const std::string_view name) {
        const std::string   path    = std::format("assets/engine/ui/inventory/{}.png", name);
        const TextureHandle texture = _textures->Load(path);
        if (!texture.IsValid()) LOG_WARN(Engine, "Inventory art '{}' not found", path);
        return texture;
    };
    _inventoryArt = { .Panel = load("panel"), .Row = load("row"), .Tab = load("tab"), .Slot = load("slot"), .Sampler = _nearestSampler };
}

// An icon: a texture, or a sprite sheet's frame ("sheet.json#frame"). Each
// is read once, a missing one included (logged once).
std::optional<ItemIcon> Application::LoadItemIcon(const std::string_view icon)
{
    if (icon.empty()) return std::nullopt;
    const std::string key(icon);
    if (const auto known = _itemIcons.find(key); known != _itemIcons.end()) return known->second;

    std::optional<ItemIcon> found;
    if (const size_t hash = key.find('#'); hash != std::string::npos) {
        const std::string path  = key.substr(0, hash);
        auto&             sheet = _iconSheets[path];
        if (!sheet) sheet = std::make_unique<SpriteSheet>(SpriteSheet::Load(path, *_textures));
        if (const auto frame = sheet->IsValid() ? sheet->GetFrame(key.substr(hash + 1)) : std::nullopt)
            found = ItemIcon{ .Texture = sheet->GetTexture(), .Sampler = _nearestSampler, .UV = frame->UV };
    } else if (const TextureHandle texture = _textures->Load(key); texture.IsValid())
        found = ItemIcon{ .Texture = texture, .Sampler = _nearestSampler };
    if (!found) LOG_WARN(Engine, "Item icon '{}' not found", key);
    _itemIcons.emplace(key, found);
    return found;
}

// The hotbar: wanted when the project's party bag has quick slots or a
// hotbar section, or the project asks for one.
bool Application::HotbarWanted() const
{
    const InventoryRules& bag = _config.Defaults.PartyBag;
    return _config.Defaults.Hotbar || bag.QuickSlots > 0 ||
           std::ranges::any_of(bag.Sections, [](const InventorySection& s) { return s.Name == HotbarScreen::Section; });
}

// Its keys (gameplay actions, rebindable: 1-9, the shoulder buttons, F /
// D-pad down to use) and the HUD, shown while a scene plays and no menu is up.
void Application::StartHotbar()
{
    if (!HotbarWanted()) return;
    constexpr std::array Digits = { Key::Num1, Key::Num2, Key::Num3, Key::Num4, Key::Num5, Key::Num6, Key::Num7, Key::Num8, Key::Num9 };
    for (u32 i = 0; i < HotbarScreen::ActionSlots.size(); ++i) {
        const std::string name = std::format("Hotbar_{}", i + 1);
        _input.RegisterAction(HotbarScreen::ActionSlots[i], name, std::format("ui.input.hotbar_{}", i + 1), "ui.input.hotbar");
        _input.BindKey(HotbarScreen::ActionSlots[i], Digits[i]);
    }
    _input.RegisterAction(HotbarScreen::ActionNext, "Hotbar_Next", "ui.input.hotbar_next", "ui.input.hotbar");
    _input.BindGamepadButton(HotbarScreen::ActionNext, GamepadButton::RightShoulder);
    _input.RegisterAction(HotbarScreen::ActionPrev, "Hotbar_Prev", "ui.input.hotbar_prev", "ui.input.hotbar");
    _input.BindGamepadButton(HotbarScreen::ActionPrev, GamepadButton::LeftShoulder);
    _input.RegisterAction(HotbarScreen::ActionUse, "Hotbar_Use", "ui.input.hotbar_use", "ui.input.hotbar");
    _input.BindKey(HotbarScreen::ActionUse, Key::F);
    _input.BindGamepadButton(HotbarScreen::ActionUse, GamepadButton::DPadDown);

    auto hotbar  = std::make_unique<HotbarScreen>();
    _hotbar      = hotbar.get();
    hotbar->Art  = _inventoryArt;
    hotbar->Host = { .Bag      = [this] { return _inventories->Find(Inventories::Party); },
                     .Items    = &_items,
                     .LoadIcon = [this](const std::string_view icon) { return LoadItemIcon(icon); },
                     .Shown    = [this] { return _scenes.GetActive() && !_ui.HasOpenMenu() && !_ui.HasGameBlockingScreen(); } };
    _ui.Push(std::move(hotbar));
}

// Each tick, unless a screen has the input: a number picks a slot (and uses
// it, if the project says), the shoulders and the wheel step, Use uses.
void Application::UpdateHotbar()
{
    if (!_hotbar || _ui.HasBlockingScreen()) return;
    for (u32 i = 0; i < HotbarScreen::ActionSlots.size(); ++i)
        if (_input.JustPressed(HotbarScreen::ActionSlots[i])) {
            _hotbar->Select(i);
            if (_config.Defaults.HotbarNumberUses) (void)UseHotbar();
        }
    if (_input.JustPressed(HotbarScreen::ActionNext)) _hotbar->Step(1);
    if (_input.JustPressed(HotbarScreen::ActionPrev)) _hotbar->Step(-1);
    if (const f32 wheel = _input.GetScrollDelta(); wheel != 0.0f) _hotbar->Step(wheel > 0.0f ? -1 : 1);
    if (_input.JustPressed(HotbarScreen::ActionUse)) (void)UseHotbar();
}

bool Application::UseHotbar()
{
    const std::string item = _hotbar ? _hotbar->GetSelectedItem() : std::string{};
    const ItemDef*    def  = item.empty() ? nullptr : _items.Find(item);
    if (!def) return false;
    if (!def->Use.empty()) return UseItem(item) == UseResult::Used;
    const PartyMember* leader = _party->GetLeader();
    return !def->Slot.empty() && leader && _party->Equip(leader->Id, item);
}

// bag party grid 6 4: the console's way to try the kinds.
void Application::ChangeBagCommand(const DebugConsole::Args& a)
{
    if (a.size() < 2) throw std::runtime_error("bag <bag> <pockets | weight W | grid W H | survival W H W | list SLOTS>");
    if (!_inventories->Find(a[0])) throw std::runtime_error(std::format("no bag '{}'", a[0]));
    const auto number = [&](const size_t at, const char* what) {
        if (a.size() <= at) throw std::runtime_error(std::format("{} {} what?", a[1], what));
        return DebugConsole::Decimal(a[at], what);
    };
    const auto whole = [&](const size_t at, const char* what) {
        if (a.size() <= at) throw std::runtime_error(std::format("{} {} what?", a[1], what));
        return static_cast<u32>(DebugConsole::Integer(a[at], what, 1, 64));
    };
    InventoryRules to;
    if (a[1] == "pockets") to = InventoryRules::Pockets();
    else if (a[1] == "weight") to = InventoryRules::Weight(number(2, "weight"));
    else if (a[1] == "grid") to = InventoryRules::Grid(whole(2, "width"), whole(3, "height"));
    else if (a[1] == "survival") to = InventoryRules::Survival(whole(2, "width"), whole(3, "height"), number(4, "weight"));
    else if (a[1] == "list") to.Slots = whole(2, "slots");
    else throw std::runtime_error(std::format("no kind '{}'", a[1]));
    to.QuickSlots = static_cast<u32>(_inventories->Find(a[0])->GetQuickSlots().size()); // the hotbar stays
    _debugConsole.Print(ChangeBag(a[0], to) ? std::format("{} is {} now", a[0], to.Kind) : "it won't all fit - choose what to discard");
}

// The inventory screen's keys: Q / E (shoulder buttons) change its tabs.
void Application::RegisterInventoryActions()
{
    _input.RegisterAction(InventoryScreen::ActionPagePrev, "UI_PagePrev");
    _input.BindKey(InventoryScreen::ActionPagePrev, Key::Q);
    _input.BindGamepadButton(InventoryScreen::ActionPagePrev, GamepadButton::LeftShoulder);
    _input.RegisterAction(InventoryScreen::ActionPageNext, "UI_PageNext");
    _input.BindKey(InventoryScreen::ActionPageNext, Key::E);
    _input.BindGamepadButton(InventoryScreen::ActionPageNext, GamepadButton::RightShoulder);
    _debugConsole.Register({ .Name = "bag", .Help = "bag <bag> <pockets | weight W | grid W H | survival W H W | list SLOTS> - change a bag's kind",
                             .Cheat = true, .Run = [this](const DebugConsole::Args& a) { ChangeBagCommand(a); } });
    _debugConsole.Register({ .Name = "inventory", .Help = "inventory [bag] - open the inventory screen on a bag (default the party's)",
                             .Run = [this](const DebugConsole::Args& a) {
                                 const std::string bag = a.empty() ? std::string(Inventories::Party) : a[0];
                                 if (!_inventories->Find(bag)) throw std::runtime_error(std::format("no bag '{}'", bag));
                                 OpenInventory(bag);
                             } });
}
} // namespace Arcbit
