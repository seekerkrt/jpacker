#ifndef MOGUET_VERSION
#error "application identity test requires build-supplied MOGUET_VERSION"
#endif

#include "application_identity.hpp"

#include <iostream>
#include <string_view>

static_assert(application_identity::PROJECT_NAME == "Moguet");
static_assert(application_identity::COMMAND_NAME == "moguet");
static_assert(application_identity::XDG_IDENTITY == "moguet");
static_assert(application_identity::ENVIRONMENT_PREFIX == "MOGUET_");
static_assert(application_identity::GETTEXT_DOMAIN == "moguet");
static_assert(application_identity::VERSION == std::string_view{MOGUET_VERSION});

int main(int argc, char* argv[]) {
    if(argc != 2) {
        std::cerr << "usage: application-identity-test <expected-version>\n";
        return 2;
    }

    if(application_identity::VERSION != std::string_view{argv[1]}) {
        std::cerr << "Application identity version does not match build VERSION.\n";
        return 1;
    }

    std::cout << "application identity tests: all checks passed\n";
    return 0;
}
