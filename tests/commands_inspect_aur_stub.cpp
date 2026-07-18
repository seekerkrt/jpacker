#include "aur_rpc.hpp"

#include <cstdlib>
#include <fstream>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

// WHY: inspection handler のloop / barrierだけをcharacterizeするため、real AUR transportを
// linkから外した専用binaryへ、この決定的なtest seamを差し込む。
namespace {

const char* INSPECTION_SCENARIO_ENV = "JPACKER_TEST_INSPECTION_SCENARIO";
const char* COMMAND_LOG_ENV = "JPACKER_TEST_COMMAND_LOG";

std::string required_environment_value(const char* name) {
    const char* value = std::getenv(name);
    if(value == nullptr || value[0] == '\0') {
        throw std::runtime_error(std::string("Missing test environment variable: ") + name);
    }
    return value;
}

std::string inspection_scenario() {
    return required_environment_value(INSPECTION_SCENARIO_ENV);
}

void append_command_log(const std::string& line) {
    std::ofstream log(required_environment_value(COMMAND_LOG_ENV), std::ios::app);
    if(!log) throw std::runtime_error("Failed to open inspection command log.");

    log << line << '\n';
    if(!log) throw std::runtime_error("Failed to write inspection command log.");
}

AurPackageInfo package_info(
        const std::string& name, const std::vector<std::string>& depends = {},
        const std::vector<std::string>& provides = {}) {
    AurPackageInfo info;
    info.Name = name;
    info.PackageBase = name;
    info.Version = "2.0-1";
    info.Description = "inspection characterization fixture";
    info.Depends = depends;
    info.Provides = provides;
    info.Maintainer = "jpacker-test";
    return info;
}

bool is_graph_scenario(const std::string& scenario) {
    return scenario != "foreign-fallback" &&
           scenario != "foreign-ordinary-failure" &&
           scenario != "foreign-schema-failure" &&
           scenario != "foreign-order";
}

bool is_numbered_foreign_package(const std::string& package_name) {
    if(package_name.size() != std::string("foreign-000").size()) return false;
    if(package_name.compare(0, std::string("foreign-").size(), "foreign-") != 0) return false;

    for(size_t i = std::string("foreign-").size(); i < package_name.size(); ++i) {
        if(package_name[i] < '0' || package_name[i] > '9') return false;
    }
    return package_name >= "foreign-001" && package_name <= "foreign-101";
}

bool is_expected_numbered_batch(
        const std::vector<std::string>& package_names, size_t expected_size,
        const std::string& expected_first, const std::string& expected_last) {
    return package_names.size() == expected_size && !package_names.empty() &&
           package_names.front() == expected_first && package_names.back() == expected_last;
}

std::optional<AurPackageInfo> graph_info(const std::string& package_name) {
    if(package_name == "deps-fail") throw std::runtime_error("fixture query failure");
    if(package_name == "plan-fail") throw std::runtime_error("fixture plan failure");

    if(package_name == "deps-first" || package_name == "deps-third" ||
       package_name == "plan-first" || package_name == "plan-third" ||
       package_name == "fetch-preflight-dep-one" ||
       package_name == "fetch-preflight-dep-two" ||
       package_name == "fetch-after-root" || package_name == "fetch-entry-fail" ||
       package_name == "fetch-entry-after" || package_name == "fetch-later-root") {
        return package_info(package_name);
    }

    if(package_name == "provider-root" || package_name == "deps-provider-root") {
        return package_info(package_name, {"jpacker-inspect-203-virtual-provider"});
    }
    if(package_name == "jpacker-inspect-203-virtual-provider") {
        return std::nullopt;
    }
    if(package_name == "provider-z" || package_name == "provider-a") {
        return package_info(package_name, {}, {"jpacker-inspect-203-virtual-provider"});
    }

    if(package_name == "fetch-preflight-root") {
        return package_info(
                package_name,
                {"fetch-preflight-dep-one", "fetch-preflight-dep-two"});
    }
    if(package_name == "fetch-guard-root") {
        return package_info(package_name, {"fetch-guard-root"});
    }
    if(package_name == "fetch-exec-root") {
        return package_info(package_name, {"fetch-entry-fail", "fetch-entry-after"});
    }

    throw std::runtime_error("Unexpected inspection info call: " + package_name);
}

std::map<std::string, AurPackageInfo> foreign_info_many(
        const std::string& scenario, const std::vector<std::string>& package_names) {
    if(scenario == "foreign-fallback") {
        if(is_expected_numbered_batch(
                   package_names, 100, "foreign-001", "foreign-100")) {
            return {};
        }
        if(is_expected_numbered_batch(
                   package_names, 1, "foreign-101", "foreign-101")) {
            return {{"foreign-101", package_info("foreign-101")}};
        }
    } else if(scenario == "foreign-ordinary-failure") {
        if(is_expected_numbered_batch(
                   package_names, 100, "foreign-001", "foreign-100")) {
            throw std::runtime_error("ordinary batch failure");
        }
        if(is_expected_numbered_batch(
                   package_names, 1, "foreign-101", "foreign-101")) {
            return {{"foreign-101", package_info("foreign-101")}};
        }
    } else if(scenario == "foreign-schema-failure") {
        if(is_expected_numbered_batch(
                   package_names, 100, "foreign-001", "foreign-100")) {
            throw AurRpcResponseError("schema batch failure");
        }
    } else if(scenario == "foreign-order") {
        const std::vector<std::string> expected = {
                "foreign-order-z", "foreign-order-missing", "foreign-order-a"};
        if(package_names == expected) {
            return {
                    {"foreign-order-z", package_info("foreign-order-z")},
                    {"foreign-order-a", package_info("foreign-order-a")}};
        }
    }

    throw std::runtime_error(
            "Unexpected inspection info_many call for scenario " + scenario);
}

} // namespace

CurlGlobal::CurlGlobal() = default;

CurlGlobal::~CurlGlobal() = default;

std::vector<AurPackageInfo> AurClient::search(const std::string& query) {
    append_command_log("aur search " + query);
    throw std::runtime_error("Unexpected inspection search call: " + query);
}

std::vector<std::string> AurClient::search_names_by_provides(
        const std::string& provided_name) {
    append_command_log("aur search-provides " + provided_name);
    if(!is_graph_scenario(inspection_scenario())) {
        throw std::runtime_error(
                "Unexpected inspection search-provides call: " + provided_name);
    }
    if(provided_name == "jpacker-inspect-203-virtual-provider") {
        // POLICY: AUR RPC order is significant to the provider presentation contract.
        return {"provider-z", "provider-a"};
    }
    throw std::runtime_error(
            "Unexpected inspection search-provides call: " + provided_name);
}

std::optional<AurPackageInfo> AurClient::info(const std::string& package_name) {
    append_command_log("aur info " + package_name);
    const std::string scenario = inspection_scenario();

    if(is_graph_scenario(scenario)) return graph_info(package_name);

    if(scenario == "foreign-fallback" && is_numbered_foreign_package(package_name) &&
       package_name != "foreign-101") {
        return package_info(package_name);
    }

    throw std::runtime_error(
            "Unexpected inspection info call for scenario " + scenario + ": " +
            package_name);
}

std::optional<AurPackageInfo> AurClient::info_strict(const std::string& package_name) {
    append_command_log("aur info-strict " + package_name);
    throw std::runtime_error("Unexpected inspection info_strict call: " + package_name);
}

std::map<std::string, AurPackageInfo> AurClient::info_many(
        const std::vector<std::string>& package_names) {
    std::string line = "aur info-many " + std::to_string(package_names.size());
    if(!package_names.empty()) {
        line += " " + package_names.front() + " " + package_names.back();
    }
    append_command_log(line);

    const std::string scenario = inspection_scenario();
    if(is_graph_scenario(scenario)) {
        throw std::runtime_error(
                "Unexpected inspection info_many call for scenario " + scenario);
    }
    return foreign_info_many(scenario, package_names);
}
