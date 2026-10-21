#pragma once

#include <arcbit/core/Types.h>

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace Arcbit
{
class Inventories;
class SaveData;

// One of the game's currencies (ProjectDefaults::Currencies): a SaveData
// counter by default ("gold" in the save data), or - Item - a stack of the
// item with the same id in a bag, carried, dropped and lost like any item.
struct CurrencyDef
{
    std::string Id;
    bool        Item = false;
    std::string Bag  = "party"; // where an item currency is carried
};

// ---------------------------------------------------------------------------
// Currencies - reading and paying the game's currencies the same way
// whichever kind each is (docs/items.md#currencies). Dialogs, `{gold}`
// placeholders, scripts and the HUD go through this.
// ---------------------------------------------------------------------------
class Currencies
{
public:
    Currencies(SaveData& data, Inventories& bags) : _data(&data), _bags(&bags) {}

    // Told whenever an amount changes through here: the currency, the new
    // amount, and the change.
    std::function<void(const std::string& id, i64 amount, i64 delta)> OnChange;

    void                                         SetCurrencies(std::vector<CurrencyDef> currencies) { _currencies = std::move(currencies); }
    [[nodiscard]] const std::vector<CurrencyDef>& GetAll() const { return _currencies; }
    [[nodiscard]] const CurrencyDef*              Find(std::string_view id) const;

    [[nodiscard]] i64 Get(std::string_view id) const;

    // Add (or, negative, take away) as much as can be: a counter never goes
    // below 0, an item currency only fits what its bag takes. Returns the
    // change made.
    i64 Add(std::string_view id, i64 delta);

    // Pay `amount` if there's that much - all or nothing.
    bool Spend(std::string_view id, i64 amount);

private:
    SaveData*                _data;
    Inventories*             _bags;
    std::vector<CurrencyDef> _currencies;
};
} // namespace Arcbit
