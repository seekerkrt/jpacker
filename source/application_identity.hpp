#pragma once

#include <string_view>

namespace application_identity {

inline constexpr std::string_view PROJECT_NAME = "Moguet";
inline constexpr std::string_view COMMAND_NAME = "moguet";
inline constexpr std::string_view XDG_IDENTITY = "moguet";
inline constexpr std::string_view ENVIRONMENT_PREFIX = "MOGUET_";
inline constexpr std::string_view GETTEXT_DOMAIN = "moguet";

#ifdef MOGUET_VERSION
inline constexpr std::string_view VERSION = MOGUET_VERSION;
#else
inline constexpr std::string_view VERSION = "unknown";
#endif

} // namespace application_identity
