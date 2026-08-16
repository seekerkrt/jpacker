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
                         "Do not run {} as {} or with {}.",
                         application_identity::PROJECT_NAME,
                         std::string_view("root"),
                         std::string_view("sudo"))
              << '\n';
    std::cout << "diagnostic_command="
              << localization::format_translated_message(
                         "Run {} as a normal user; {} will invoke {}/{} when needed.",
                         application_identity::COMMAND_NAME,
                         application_identity::PROJECT_NAME,
                         std::string_view("sudo"),
                         std::string_view("pacman"))
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
    std::cout << "owner_logging="
              << localization::format_translated_message(
                         "Error: {}",
                         std::string_view("{runtime-diagnostic}"))
              << '\n';
    std::cout << "owner_config="
              << localization::format_translated_message(
                         "User config error: '{}': key '{}': missing required key; expected integer {}",
                         std::string_view("/tmp/{config}"),
                         std::string_view("schema_version"), 1)
              << '\n';
    std::cout << "owner_process="
              << localization::format_translated_message(
                         "Failed to ignore {} while waiting for an explicit process: {}",
                         std::string_view("SIGINT"),
                         std::string_view("{signal-error}"))
              << '\n';
    std::cout << "owner_artifact="
              << localization::format_translated_message(
                         "Failed to inspect the descriptor for {}: {}",
                         std::string_view("/tmp/{artifact}"),
                         std::string_view("{artifact-error}"))
              << '\n';
    std::cout << "owner_metadata="
              << localization::format_translated_message(
                         "Repository package query failed: {} reported no error detail.",
                         std::string_view("libalpm"))
              << '\n';
    std::cout << "owner_inspect="
              << localization::translate_message(
                         "Recursive dependency tree:")
              << '\n';
    std::cout << "owner_sync="
              << localization::format_translated_message(
                         "Repository      : {}", std::string_view("aur"))
              << '\n';
    std::cout << "owner_aur="
              << localization::format_translated_message(
                         "Checking {} updates for {} foreign packages...",
                         std::string_view("AUR"), 7)
              << '\n';
    std::cout << "owner_upgrade="
              << localization::format_translated_message(
                         "excluded from {} update: {}",
                         std::string_view("AUR"),
                         std::string_view("{package-name}"))
              << '\n';
    std::cout << "relation_installed="
              << localization::format_translated_message(
                         "Installed conflict confirmed: declaring package {} declares conflict {} for target component {}; matched installed package {} through {}; build/install is blocked before mutation.",
                         std::string_view("declaring-a"),
                         std::string_view("legacy-a>=2"),
                         std::string_view("legacy-a"),
                         std::string_view("installed-a"),
                         std::string_view("provided-a=3"))
              << '\n';
    std::cout << "relation_planned="
              << localization::format_translated_message(
                         "Planned-target conflict confirmed: declaring package {} declares conflict {} for target component {}; matched planned package {} through {}; build/install is blocked before mutation.",
                         std::string_view("declaring-p"),
                         std::string_view("planned-api"),
                         std::string_view("planned-api"),
                         std::string_view("planned-child"),
                         std::string_view("exact-planned"))
              << '\n';
    std::cout << "relation_replacement="
              << localization::format_translated_message(
                         "Potential replacement impact: declaring package {} declares replacement {} for target component {}; matched {} package {} through {} is a replacement candidate; review is required and no automatic replacement is performed; build/install is blocked before mutation.",
                         std::string_view("declaring-r"),
                         std::string_view("legacy-r"),
                         std::string_view("legacy-r"),
                         localization::translate_message("installed"),
                         std::string_view("installed-r"),
                         std::string_view("exact-r"))
              << '\n';
    std::cout << "relation_no_match="
              << localization::format_translated_message(
                         "Confirmed no matching current or planned target: declaring package {} declares {} {} for target component {}; complete current/planned observation found no matching package or provided component; this relation does not block build/install.",
                         std::string_view("declaring-n"),
                         localization::translate_message("conflict"),
                         std::string_view("absent-n"),
                         std::string_view("absent-n"))
              << '\n';
    std::cout << "relation_unknown="
              << localization::format_translated_message(
                         "Relation judgment unavailable: declaring package {} declares {} {} for target component {}; current/planned observation is {}; {}; this is not a confirmed absence, so build/install is blocked.",
                         std::string_view("declaring-u"),
                         localization::translate_message("conflict"),
                         std::string_view("unknown-u>=2"),
                         std::string_view("unknown-u"),
                         localization::translate_message("unavailable"),
                         std::string_view("inventory-u"))
              << '\n';
    std::cout << "relation_invalid="
              << localization::format_translated_message(
                         "Invalid relation metadata or observation: declaring package {} declares {} {} for target component {}; {}; invalid input is fail-closed, so build/install is blocked.",
                         std::string_view("declaring-i"),
                         localization::translate_message("replacement"),
                         std::string_view("invalid-i"),
                         std::string_view("invalid-i"),
                         std::string_view("invalid-old-self"))
              << '\n';
    std::cout << "relation_declared="
              << localization::format_translated_message(
                         "Declared relation awaiting assessment: declaring package {} declares {} {} for target component {}; current/planned assessment is incomplete, so build/install remains blocked under the fail-closed policy.",
                         std::string_view("declaring-d"),
                         localization::translate_message("conflict"),
                         std::string_view("declared-d"),
                         std::string_view("declared-d"))
              << '\n';
    std::cout << "relation_check="
              << localization::format_translated_message(
                         "Relation Check  : {}",
                         localization::translate_message(
                                 "deferred to planning and build preflight"))
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
