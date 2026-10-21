#include <arcbit/items/Currencies.h>

#include <arcbit/core/Log.h>
#include <arcbit/items/Inventories.h>
#include <arcbit/save/SaveData.h>

#include <algorithm>
#include <limits>

namespace Arcbit
{
const CurrencyDef* Currencies::Find(const std::string_view id) const
{
    const auto it = std::ranges::find(_currencies, id, &CurrencyDef::Id);
    return it != _currencies.end() ? &*it : nullptr;
}

i64 Currencies::Get(const std::string_view id) const
{
    const CurrencyDef* currency = Find(id);
    if (!currency) return 0;
    if (!currency->Item) return _data->Get<i64>(std::string(id), 0);
    const Inventory* bag = _bags->Find(currency->Bag);
    return bag ? bag->Count(id) : 0;
}

i64 Currencies::Add(const std::string_view id, const i64 delta)
{
    const CurrencyDef* currency = Find(id);
    if (!currency) {
        LOG_WARN(Engine, "Currency: there's no currency '{}'", id);
        return 0;
    }
    i64 changed = 0;
    if (!currency->Item) {
        const i64 now = _data->Get<i64>(std::string(id), 0);
        changed       = std::max(delta, -now);
        _data->Set(std::string(id), now + changed);
    }
    else if (Inventory* bag = _bags->Find(currency->Bag)) {
        constexpr i64 Most = std::numeric_limits<u32>::max();
        const u32     n    = static_cast<u32>(std::min(delta < 0 ? -delta : delta, Most));
        changed            = delta < 0 ? -static_cast<i64>(bag->Remove(id, n)) : bag->Add(id, n);
    }
    if (changed != 0 && OnChange) OnChange(std::string(id), Get(id), changed);
    return changed;
}

bool Currencies::Spend(const std::string_view id, const i64 amount)
{
    if (amount < 0 || Get(id) < amount) return false;
    return Add(id, -amount) == -amount;
}
} // namespace Arcbit
