#pragma once

#include "aur_rpc.hpp"
#include "package_metadata.hpp"

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <vector>

// actual aur_update_query.cppをlinkしたoperation testから、inventory/AUR RPC/
// vercmp transportだけを決定的に差し替えるstub境界。
namespace filtered_aur_update_operation_query_test_stub {

enum class EventKind {
    RepositoryConfigurationResolution,
    ForeignInventoryQuery,
    AurInfoMany,
    AurInfoStrict,
    VersionCompare,
};

struct Event {
    EventKind kind;
    std::vector<std::string> package_names;
    std::string detail;

    bool operator==(const Event&) const = default;
};

void reset();

void set_repository_configuration(
    PacmanRepositoryConfiguration configuration);
void set_repository_configuration_failure(PackageMetadataFailure failure);

void set_foreign_inventory(ForeignPackageInventory inventory);
void set_foreign_inventory_failure(PackageMetadataFailure failure);

void enqueue_info_many_result(
    std::map<std::string, AurPackageInfo> result);
void enqueue_info_many_failure(std::string diagnostic);

void enqueue_info_strict_result(std::optional<AurPackageInfo> result);
void enqueue_info_strict_failure(std::string diagnostic);

void enqueue_vercmp_result(std::string output);
void enqueue_vercmp_failure(std::string diagnostic);

std::size_t repository_configuration_calls();
std::size_t inventory_calls();

const std::vector<PacmanRepositoryConfiguration>&
inventory_configuration_history();
const std::vector<std::vector<std::string>>& info_many_call_history();
const std::vector<std::string>& info_strict_call_history();
const std::vector<std::string>& vercmp_call_history();
const std::vector<Event>& event_history();

void require_script_consumed();

} // namespace filtered_aur_update_operation_query_test_stub
