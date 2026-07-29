#include "aur_rpc.hpp"

#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

AurPackageInfo package_info(
        const std::string& name, const std::vector<std::string>& depends = {},
        const std::vector<std::string>& make_depends = {},
        const std::vector<std::string>& check_depends = {},
        const std::vector<std::string>& provides = {},
        const std::string& package_base = {}) {
    AurPackageInfo info;
    info.Name = name;
    info.PackageBase = package_base.empty() ? name : package_base;
    info.Version = "1.0-1";
    info.Description = "dependency plan model fixture";
    info.Depends = depends;
    info.MakeDepends = make_depends;
    info.CheckDepends = check_depends;
    info.Provides = provides;
    info.Maintainer = "moguet-test";
    return info;
}

bool is_leaf_package(const std::string& package_name) {
    const std::vector<std::string> leaves = {
            "case2-runtime-dep",
            "case2-build-dep",
            "case2-check-dep",
            "case3-shared",
            "case4-lib",
            "case5-common",
            "case7-provider-pkg",
            "case10-common",
            "case13-common",
            "case15-shared",
            "case16-a-only",
            "case16-b-only",
            "case16-shared",
            "case17-leaf",
            "case19-early-dep",
            "case19-late-dep",
    };
    for(const auto& leaf : leaves) {
        if(package_name == leaf) return true;
    }
    return false;
}

} // namespace

std::vector<AurPackageInfo> AurClient::search(const std::string& query) {
    throw AurRpcResponseError("Unexpected dependency-plan AUR search: " + query);
}

std::vector<std::string> AurClient::search_names_by_provides(
        const std::string& provided_name) {
    if(provided_name == "case7-virtual-api") {
        return {"case7-provider-pkg", "case7-provider-pkg"};
    }
    if(provided_name == "case11-virtual") return {"case11-provider"};
    if(provided_name == "preflight-provider-candidate-virtual") {
        return {
                "preflight-provider-broken",
                "preflight-provider-good",
        };
    }
    if(provided_name == "preflight-provider-search-virtual") {
        throw std::runtime_error("legacy provider search failure");
    }
    if(provided_name == "preflight-provider-response-virtual") {
        throw AurRpcResponseError("provider search response failure");
    }
    if(provided_name == "preflight-provider-candidate-response-virtual") {
        return {"preflight-provider-candidate-response-broken"};
    }
    if(provided_name == "case9-missing" || provided_name == "case11-missing") return {};
    if(provided_name == "preflight-dependency-failure-child" ||
       provided_name == "preflight-shared-failure") {
        return {};
    }
    throw AurRpcResponseError(
            "Unexpected dependency-plan AUR provider search: " + provided_name);
}

std::vector<std::string> AurClient::search_names_by_provides_strict(
        const std::string& provided_name) {
    if(provided_name == "preflight-provider-search-virtual") {
        throw std::runtime_error("strict provider search failure");
    }
    if(provided_name == "preflight-provider-response-virtual") {
        throw AurRpcResponseError("provider search response failure");
    }
    return search_names_by_provides(provided_name);
}

std::optional<AurPackageInfo> AurClient::info(const std::string& package_name) {
    if(package_name == "case1-app") return package_info(package_name);

    if(package_name == "case2-app") {
        AurPackageInfo info = package_info(
                package_name, {"case2-runtime-dep>=2"}, {"case2-build-dep"},
                {"case2-check-dep"});
        info.OptDepends = {"case2-optional-dep"};
        return info;
    }
    if(is_leaf_package(package_name)) {
        if(package_name == "case7-provider-pkg") {
            return package_info(package_name, {}, {}, {}, {"case7-virtual-api"});
        }
        return package_info(package_name);
    }

    if(package_name == "case3-app") {
        return package_info(package_name, {"case3-shared"}, {"case3-shared"});
    }
    if(package_name == "case4-app") {
        return package_info(package_name, {"case4-lib"});
    }
    if(package_name == "case5-app" || package_name == "case5-tool") {
        return package_info(package_name, {"case5-common"});
    }
    if(package_name == "case6-app") {
        return package_info(package_name, {"case6-repo-lib"});
    }
    if(package_name == "case7-app") {
        return package_info(package_name, {"case7-virtual-api"});
    }
    if(package_name == "case8-app") {
        return package_info(package_name, {"case8-virtual"});
    }
    if(package_name == "case9-app") {
        return package_info(package_name, {"case9-missing"});
    }
    if(package_name == "case10-app") {
        return package_info(package_name, {"case10-lib"});
    }
    if(package_name == "case10-lib") {
        return package_info(package_name, {"case10-common"});
    }

    if(package_name == "case11-root") {
        AurPackageInfo info = package_info(
                package_name,
                {"case11-direct", "case11-virtual", "case11-ambiguous",
                 "case11-missing", "case11-cycle-a"});
        info.Conflicts = {"case11-old"};
        return info;
    }
    if(package_name == "case11-direct") {
        AurPackageInfo info = package_info(
                package_name, {}, {}, {}, {}, "case11-direct-base");
        info.Replaces = {"case11-replaced"};
        return info;
    }
    if(package_name == "case11-provider") {
        return package_info(package_name, {}, {}, {}, {"case11-virtual"});
    }
    if(package_name == "case11-cycle-a") {
        return package_info(package_name, {"case11-root"});
    }

    if(package_name == "case13-app") {
        return package_info(package_name, {"case13-lib"});
    }
    if(package_name == "case13-lib") {
        return package_info(package_name, {"case13-common"});
    }
    if(package_name == "case14-app") {
        return package_info(package_name, {"case14-virtual"});
    }
    if(package_name == "case15-app") {
        return package_info(
                package_name, {"case15-shared"}, {"case15-shared"},
                {"case15-shared"});
    }

    if(package_name == "case16-child-a") {
        return package_info(
                package_name, {"case16-a-only", "case16-shared"}, {}, {}, {},
                "case16-suite");
    }
    if(package_name == "case16-child-b") {
        return package_info(
                package_name, {"case16-b-only", "case16-shared"}, {}, {}, {},
                "case16-suite");
    }

    if(package_name == "case17-parent") {
        return package_info(
                package_name, {"case17-sibling"}, {}, {}, {},
                "case17-suite");
    }
    if(package_name == "case17-sibling") {
        return package_info(
                package_name, {"case17-leaf"}, {}, {}, {},
                "case17-suite");
    }

    if(package_name == "case18-cycle-a") {
        return package_info(package_name, {"case18-cycle-b"});
    }
    if(package_name == "case18-cycle-b") {
        return package_info(package_name, {"case18-cycle-a"});
    }

    if(package_name == "case20-cycle-a") {
        return package_info(
                package_name, {"case20-cycle-b"}, {}, {}, {},
                "case20-suite");
    }
    if(package_name == "case20-cycle-b") {
        return package_info(
                package_name, {"case20-cycle-a"}, {}, {}, {},
                "case20-suite");
    }

    if(package_name == "case19-consumer") {
        return package_info(package_name, {"case19-suite-a"});
    }
    if(package_name == "case19-suite-a") {
        return package_info(
                package_name, {"case19-early-dep"}, {}, {}, {},
                "case19-suite");
    }
    if(package_name == "case19-suite-b") {
        return package_info(
                package_name, {"case19-late-dep"}, {}, {}, {},
                "case19-suite");
    }

    if(package_name == "preflight-root-metadata-failure") {
        throw std::runtime_error("legacy root metadata failure");
    }
    if(package_name == "preflight-root-not-found") return std::nullopt;
    if(package_name == "preflight-later-root") return package_info(package_name);
    if(package_name == "preflight-dependency-failure-root") {
        return package_info(package_name, {"preflight-dependency-failure-child"});
    }
    if(package_name == "preflight-dependency-failure-child") {
        throw std::runtime_error("legacy dependency metadata failure");
    }
    if(package_name == "preflight-provider-search-root") {
        return package_info(package_name, {"preflight-provider-search-virtual"});
    }
    if(package_name == "preflight-provider-candidate-root") {
        return package_info(package_name, {"preflight-provider-candidate-virtual"});
    }
    if(package_name == "preflight-provider-good") {
        return package_info(
                package_name, {}, {}, {},
                {"preflight-provider-candidate-virtual"});
    }
    if(package_name == "preflight-provider-broken") {
        throw std::runtime_error("legacy provider candidate failure");
    }
    if(package_name == "preflight-shared-root-a" ||
       package_name == "preflight-shared-root-b") {
        return package_info(package_name, {"preflight-shared-parent"});
    }
    if(package_name == "preflight-shared-parent") {
        return package_info(package_name, {"preflight-shared-failure"});
    }
    if(package_name == "preflight-shared-failure") {
        throw std::runtime_error("legacy shared dependency metadata failure");
    }
    if(package_name == "preflight-response-error-root") {
        throw AurRpcResponseError("root response failure");
    }
    if(package_name == "preflight-provider-response-root") {
        return package_info(package_name, {"preflight-provider-response-virtual"});
    }
    if(package_name == "preflight-dependency-response-root") {
        return package_info(package_name, {"preflight-dependency-response-child"});
    }
    if(package_name == "preflight-provider-candidate-response-root") {
        return package_info(
                package_name,
                {"preflight-provider-candidate-response-virtual"});
    }
    if(package_name == "preflight-repository-exact-failure-root") {
        return package_info(
                package_name,
                {"preflight-repository-exact-failure-child"});
    }
    if(package_name == "preflight-repository-provider-failure-root") {
        return package_info(
                package_name,
                {"preflight-repository-provider-failure-virtual"});
    }
    if(package_name == "preflight-response-must-stop-before-later-root") {
        throw std::runtime_error("response error did not stop later root resolution");
    }

    if(package_name == "case7-virtual-api" || package_name == "case8-virtual" ||
       package_name == "case9-missing" || package_name == "case11-virtual" ||
       package_name == "case11-ambiguous" || package_name == "case11-missing" ||
       package_name == "case14-virtual" ||
       package_name == "preflight-provider-search-virtual" ||
       package_name == "preflight-provider-candidate-virtual" ||
       package_name == "preflight-provider-response-virtual" ||
       package_name == "preflight-provider-candidate-response-virtual" ||
       package_name == "preflight-repository-provider-failure-virtual") {
        return std::nullopt;
    }

    throw AurRpcResponseError("Unexpected dependency-plan AUR info call: " + package_name);
}

std::optional<AurPackageInfo> AurClient::info_strict(const std::string& package_name) {
    if(package_name == "preflight-root-metadata-failure") {
        throw std::runtime_error("strict root metadata failure");
    }
    if(package_name == "preflight-dependency-failure-child") {
        throw std::runtime_error("strict dependency metadata failure");
    }
    if(package_name == "preflight-provider-broken") {
        throw std::runtime_error("strict provider candidate failure");
    }
    if(package_name == "preflight-shared-failure") {
        throw std::runtime_error("strict shared dependency metadata failure");
    }
    if(package_name == "preflight-dependency-response-child") {
        throw AurRpcResponseError("dependency info response failure");
    }
    if(package_name == "preflight-provider-candidate-response-broken") {
        throw AurRpcResponseError("provider candidate info response failure");
    }
    return info(package_name);
}

std::map<std::string, AurPackageInfo> AurClient::info_many(
        const std::vector<std::string>& package_names) {
    std::map<std::string, AurPackageInfo> package_by_name;
    for(const auto& package_name : package_names) {
        std::optional<AurPackageInfo> package = info(package_name);
        if(package.has_value()) package_by_name.emplace(package_name, package.value());
    }
    return package_by_name;
}
