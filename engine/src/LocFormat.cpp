// Numbers and dates in the player's language: separators, grouping, date
// order and month names, from each language's "loc.*" keys (falling back to
// English like any string).

#include <arcbit/core/Loc.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <ctime>
#include <format>

namespace Arcbit
{
namespace
{
// A format setting: the language's string for key, else fallback (no
// locale loaded, or a key it doesn't set).
std::string Setting(const std::string_view key, const std::string_view fallback)
{
    return Loc::Has(key) ? Loc::Get(key) : std::string(fallback);
}

// "1234567" -> "1,234,567" with the language's separator - only from
// group_from digits (4 in English: 1,234; 5 in Spanish: 1234, 12 345).
std::string Group(const std::string_view digits)
{
    u32 from = 4;
    const std::string setting = Setting("loc.group_from", "4");
    std::from_chars(setting.data(), setting.data() + setting.size(), from);
    if (digits.size() < from) return std::string(digits);

    const std::string separator = Setting("loc.group", ",");
    std::string       out;
    for (usize i = 0; i < digits.size(); ++i) {
        if (i > 0 && (digits.size() - i) % 3 == 0) out += separator;
        out += digits[i];
    }
    return out;
}

std::tm LocalTime(const i64 unixSeconds)
{
    const std::time_t time = unixSeconds;
    std::tm           local{};
#ifdef _WIN32
    localtime_s(&local, &time);
#else
    localtime_r(&time, &local);
#endif
    return local;
}

// The fields a date or time pattern can use.
std::vector<LocArg> DateFields(const std::tm& t)
{
    const i32 month = t.tm_mon + 1, hour12 = t.tm_hour % 12 == 0 ? 12 : t.tm_hour % 12;
    return { { "year", std::format("{}", t.tm_year + 1900) }, // text, so a year never gets grouped
             { "month", std::format("{}", month) },
             { "month2", std::format("{:02}", month) },
             { "month_name", Setting(std::format("loc.month.{}", month), std::format("{}", month)) },
             { "month_short", Setting(std::format("loc.month_short.{}", month), std::format("{}", month)) },
             { "day", std::format("{}", t.tm_mday) },
             { "day2", std::format("{:02}", t.tm_mday) },
             { "hour", std::format("{}", t.tm_hour) },
             { "hour2", std::format("{:02}", t.tm_hour) },
             { "hour12", std::format("{}", hour12) },
             { "minute", std::format("{:02}", t.tm_min) },
             { "second", std::format("{:02}", t.tm_sec) },
             { "ampm", Setting(t.tm_hour < 12 ? "loc.am" : "loc.pm", t.tm_hour < 12 ? "AM" : "PM") } };
}

// "-1234.5" (as std::format writes it) -> "-1,234.5" / "-1 234,5".
std::string LocalizeDigits(std::string text)
{
    if (text.empty() || text.find_first_of("eEin") != std::string::npos) return text; // exponents, inf, nan as they are
    std::string sign;
    if (text.front() == '-') sign = "-", text.erase(0, 1);
    const usize       dot      = text.find('.');
    const std::string integral = Group(text.substr(0, dot));
    if (dot == std::string::npos) return sign + integral;
    return sign + integral + Setting("loc.decimal", ".") + text.substr(dot + 1);
}
} // namespace

std::string Loc::FormatNumber(const f64 value, const i32 decimals)
{
    if (decimals >= 0) return LocalizeDigits(std::format("{:.{}f}", value, decimals));
    const bool whole = value == std::floor(value) && std::abs(value) < 1e15;
    return LocalizeDigits(whole ? std::format("{:.0f}", value) : std::format("{}", value));
}

std::string Loc::FormatDate(const i64 unixSeconds, const std::string_view style)
{
    const std::vector<LocArg> fields = DateFields(LocalTime(unixSeconds));
    const auto pattern = [&](const std::string_view which) {
        const std::string_view fallback = which == "time" ? "{hour2}:{minute}"
                                        : which == "datetime" ? "{date} {time}"
                                                              : "{year}-{month2}-{day2}";
        return Setting(std::format("loc.{}", which), fallback);
    };
    if (style != "datetime") return Interpolate(pattern(style == "time" ? "time" : "date"), fields);

    std::vector<LocArg> both = fields;
    both.emplace_back("date", Interpolate(pattern("date"), fields));
    both.emplace_back("time", Interpolate(pattern("time"), fields));
    return Interpolate(pattern("datetime"), both);
}

// {name} or {name:spec}: numbers take the language's separators unless the
// spec says otherwise - n2 (two decimals), date / time / datetime (a Unix
// time), raw (as passed).
std::string Loc::FormatArg(const LocArg& arg, const std::string_view spec)
{
    if (!arg.Number || spec == "raw") return arg.Text;
    if (spec == "date" || spec == "time" || spec == "datetime")
        return FormatDate(static_cast<i64>(*arg.Number), spec);
    if (spec.size() == 2 && spec[0] == 'n' && spec[1] >= '0' && spec[1] <= '9') return FormatNumber(*arg.Number, spec[1] - '0');
    return LocalizeDigits(arg.Text); // as the number was written: a float's shortest form, not its double's
}
} // namespace Arcbit
