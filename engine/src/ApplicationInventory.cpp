// Application: the inventory screen (docs/items.md#the-inventory-screen) -
// opening it on a bag, its stock art, and item icons for any UI.

#include <arcbit/app/Application.h>
#include <arcbit/assets/SpriteSheet.h>
#include <arcbit/core/Log.h>
#include <arcbit/ui/InventoryScreen.h>

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
    if (!_inventoryArt.Sampler.IsValid()) {
        const auto load = [this](const std::string_view name) {
            const std::string   path    = std::format("assets/engine/ui/inventory/{}.png", name);
            const TextureHandle texture = _textures->Load(path);
            if (!texture.IsValid()) LOG_WARN(Engine, "Inventory art '{}' not found", path);
            return texture;
        };
        _inventoryArt = { .Panel = load("panel"), .Row = load("row"), .Tab = load("tab"), .Slot = load("slot"), .Sampler = _nearestSampler };
    }
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

// The inventory screen's keys: Q / E (shoulder buttons) change its tabs.
void Application::RegisterInventoryActions()
{
    _input.RegisterAction(InventoryScreen::ActionPagePrev, "UI_PagePrev");
    _input.BindKey(InventoryScreen::ActionPagePrev, Key::Q);
    _input.BindGamepadButton(InventoryScreen::ActionPagePrev, GamepadButton::LeftShoulder);
    _input.RegisterAction(InventoryScreen::ActionPageNext, "UI_PageNext");
    _input.BindKey(InventoryScreen::ActionPageNext, Key::E);
    _input.BindGamepadButton(InventoryScreen::ActionPageNext, GamepadButton::RightShoulder);
    _debugConsole.Register({ .Name = "inventory", .Help = "inventory [bag] - open the inventory screen on a bag (default the party's)",
                             .Run = [this](const DebugConsole::Args& a) {
                                 const std::string bag = a.empty() ? std::string(Inventories::Party) : a[0];
                                 if (!_inventories->Find(bag)) throw std::runtime_error(std::format("no bag '{}'", bag));
                                 OpenInventory(bag);
                             } });
}
} // namespace Arcbit
