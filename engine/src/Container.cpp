#include <arcbit/items/Container.h>

namespace Arcbit
{
std::string_view ContainerOpeningName(const ContainerOpening opening)
{
    return opening == ContainerOpening::TakeAll ? "take_all" : "transfer";
}

std::optional<ContainerOpening> ParseContainerOpening(const std::string_view name)
{
    if (name == "transfer") return ContainerOpening::Transfer;
    if (name == "take_all") return ContainerOpening::TakeAll;
    return std::nullopt;
}
} // namespace Arcbit
