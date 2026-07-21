#include "repository_query.hpp"

#include <stdexcept>
#include <string>
#include <vector>

bool is_repo_package(const std::string& package_name) {
    return package_name == "case6-repo-lib";
}

std::vector<ProvidedDependency> find_repo_providers(
        const std::string& dependency_name) {
    if(dependency_name == "case8-virtual") {
        return {
                ProvidedDependency{"extra", "case8-provider-a"},
                ProvidedDependency{"community", "case8-provider-b"},
        };
    }
    if(dependency_name == "case11-ambiguous") {
        return {
                ProvidedDependency{"extra", "case11-provider-a"},
                ProvidedDependency{"community", "case11-provider-b"},
        };
    }
    if(dependency_name == "case14-virtual") {
        return {ProvidedDependency{"extra", "case14-provider"}};
    }
    if(dependency_name == "case7-virtual-api" || dependency_name == "case9-missing" ||
       dependency_name == "case11-virtual" || dependency_name == "case11-missing") {
        return {};
    }
    throw std::runtime_error(
            "Unexpected dependency-plan repository provider query: " + dependency_name);
}
