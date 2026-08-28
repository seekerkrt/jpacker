#include "localization.hpp"

#include "application_identity.hpp"

#include <clocale>
#include <string_view>

#include <libintl.h>

#ifndef MOGUET_LOCALE_DIRECTORY
#include "localization_config.hpp"
#endif

namespace {

constexpr std::string_view LOCALE_DIRECTORY = MOGUET_LOCALE_DIRECTORY;

// NO_TRANSLATE(Issue #308): Compile-time developer diagnostic; it cannot
// reach a user-visible CLI runtime path.
static_assert(
    !LOCALE_DIRECTORY.empty() && LOCALE_DIRECTORY.starts_with('/'),
    "MOGUET_LOCALE_DIRECTORY must be an absolute path");

} // namespace

namespace localization {

bool initialize_runtime_catalog() noexcept {
    // LC_CTYPE等を変えると既存validatorの文字分類までlocale依存になるため、
    // message selectionだけをprocess environmentから有効化する。
    static_cast<void>(std::setlocale(LC_MESSAGES, ""));

    const char* domain = application_identity::GETTEXT_DOMAIN.data();
    if(::bindtextdomain(domain, LOCALE_DIRECTORY.data()) == nullptr)
        return false;
    if(::bind_textdomain_codeset(domain, "UTF-8") == nullptr)
        return false;
    return ::textdomain(domain) != nullptr;
}

} // namespace localization
