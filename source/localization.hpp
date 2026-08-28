#pragma once

#include <cstddef>
#include <format>
#include <string>
#include <string_view>

#include <libintl.h>

namespace localization {

// Process startupで1回だけ呼び、以降のmessage lookupが使うdomainを固定する。
// Unsupportedなprocess localeやcatalog欠落はgettextのEnglish fallbackへ委ねる。
bool initialize_runtime_catalog() noexcept;

namespace detail {

inline std::string lookup_message(std::string_view message_id) {
    const std::string owned_message_id(message_id);
    return ::gettext(owned_message_id.c_str());
}

inline std::string lookup_plural_message(
    std::string_view singular_message_id,
    std::string_view plural_message_id, unsigned long count) {
    const std::string owned_singular_message_id(singular_message_id);
    const std::string owned_plural_message_id(plural_message_id);
    return ::ngettext(
        owned_singular_message_id.c_str(),
        owned_plural_message_id.c_str(), count);
}

} // namespace detail

// std::string等のruntime textをmsgidにせず、xgettextが抽出できるsource literalを
// call siteへ置く。brace自体を表示するmessageはformat APIを経由しなくてよい。
template <std::size_t Size>
std::string translate_message(const char (&message_id)[Size]) {
    static_assert(Size > 0);
    return detail::lookup_message(
        std::string_view(message_id, Size - 1));
}

template <std::size_t SingularSize, std::size_t PluralSize>
std::string translate_plural_message(
    const char (&singular_message_id)[SingularSize],
    const char (&plural_message_id)[PluralSize], unsigned long count) {
    static_assert(SingularSize > 0 && PluralSize > 0);
    return detail::lookup_plural_message(
        std::string_view(singular_message_id, SingularSize - 1),
        std::string_view(plural_message_id, PluralSize - 1), count);
}

template <typename... Args>
std::string format_translated_message(
    std::format_string<Args...> message_id, Args&&... args) {
    auto format_arguments = std::make_format_args(args...);
    try {
        return std::vformat(
            detail::lookup_message(message_id.get()), format_arguments);
    } catch(const std::format_error&) {
        // Catalog validationはrelease gateだが、壊れた外部catalogでも
        // safety diagnosticを失わないようEnglish msgidへ戻す。
        return std::vformat(message_id.get(), format_arguments);
    }
}

template <typename... Args>
std::string format_translated_plural_message(
    std::format_string<Args...> singular_message_id,
    std::format_string<Args...> plural_message_id, unsigned long count,
    Args&&... args) {
    auto format_arguments = std::make_format_args(args...);
    try {
        return std::vformat(
            detail::lookup_plural_message(
                singular_message_id.get(), plural_message_id.get(),
                count),
            format_arguments);
    } catch(const std::format_error&) {
        const std::string_view fallback_message_id =
            count == 1 ? singular_message_id.get()
                       : plural_message_id.get();
        return std::vformat(fallback_message_id, format_arguments);
    }
}

} // namespace localization
