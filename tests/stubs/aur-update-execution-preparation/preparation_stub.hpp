#pragma once

#include "package_metadata.hpp"
#include "source_environment.hpp"
#include "source_preference.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace aur_update_execution_preparation_test_stub {

enum class EventKind {
    StrictPreferenceRead,
    SeparatedInstallOptionsGuard,
    ArtifactPkgdestGuard,
    PacmanDatabaseResolution,
};

struct Event {
    EventKind   kind;
    std::string subject;

    bool operator==(const Event&) const = default;
};

void reset();

void enqueue_source_preference_result(
        const std::string& package_name,
        StrictSourcePreferenceResult result);

void set_database_paths(PacmanDatabasePaths paths);
void set_database_failure(PackageMetadataFailure failure);

void fail_supported_options_guard(std::string diagnostic);
void fail_pkgdest_guard_on_call(
        std::size_t one_based_call_index,
        std::string diagnostic);

const std::vector<std::string>& strict_preference_read_history();
const std::vector<bool>& supported_options_guard_history();
const std::vector<SourceBuildEnvironment>& pkgdest_guard_history();
std::size_t database_call_count();
const std::vector<Event>& event_history();

} // namespace aur_update_execution_preparation_test_stub
