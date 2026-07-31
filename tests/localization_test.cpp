#include "application_identity.hpp"
#include "localization.hpp"

#include <clocale>
#include <iostream>
#include <string>
#include <string_view>

#include <libintl.h>

namespace {

const char* printable_value(const char* value) {
    return value == nullptr ? "<null>" : value;
}

} // namespace

int main(int argc, char* argv[]) {
    if(argc != 2 ||
       (std::string_view(argv[1]) != "one" &&
        std::string_view(argv[1]) != "two")) {
        std::cerr << "usage: localization-test one | two\n";
        return 2;
    }

    if(!localization::initialize_runtime_catalog()) {
        std::cerr << "localization initialization failed\n";
        return 1;
    }

    const unsigned long count = std::string_view(argv[1]) == "one" ? 1 : 2;
    const char* domain = ::textdomain(nullptr);

    std::cout << "domain=" << printable_value(domain) << '\n';
    std::cout << "locale_directory="
              << printable_value(::bindtextdomain(domain, nullptr)) << '\n';
    std::cout << "codeset="
              << printable_value(
                         ::bind_textdomain_codeset(domain, nullptr))
              << '\n';
    std::cout << "ctype_locale="
              << printable_value(std::setlocale(LC_CTYPE, nullptr)) << '\n';
    std::cout << "message_locale="
              << printable_value(std::setlocale(LC_MESSAGES, nullptr)) << '\n';
    std::cout << "help="
              << localization::translate_message(
                         "Show this help message and exit")
              << '\n';
    std::cout << "diagnostic_project="
              << localization::format_translated_message(
                         "Do not run {} as root or with sudo.",
                         application_identity::PROJECT_NAME)
              << '\n';
    std::cout << "diagnostic_command="
              << localization::format_translated_message(
                         "Run {} as a normal user; {} will invoke sudo/pacman when needed.",
                         application_identity::COMMAND_NAME,
                         application_identity::PROJECT_NAME)
              << '\n';
    std::cout << "prompt="
              << localization::translate_message("Rebuild package?") << '\n';
    std::cout << "missing="
              << localization::translate_message("Missing catalog entry.")
              << '\n';
    std::cout << "braces="
              << localization::translate_message("Use {name} as data.")
              << '\n';
    std::cout << "data="
              << localization::format_translated_message(
                         "Selected package: {}", std::string_view("{danger}"))
              << '\n';
    std::cout << "plural="
              << localization::format_translated_plural_message(
                         "Processed {} package.",
                         "Processed {} packages.", count, count)
              << '\n';
    std::cout << "command=" << application_identity::COMMAND_NAME << '\n';
    std::cout << "option=--help\n";
    std::cout << "external=pacman output\n";
    return 0;
}
