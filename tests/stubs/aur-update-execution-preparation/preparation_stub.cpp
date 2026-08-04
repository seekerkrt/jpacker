#include "preparation_stub.hpp"

#include "artifact_install_plan.hpp"
#include "artifact_workspace.hpp"
#include "source_install.hpp"

#include <deque>
#include <map>
#include <optional>
#include <stdexcept>
#include <utility>

namespace {

namespace stub = aur_update_execution_preparation_test_stub;

struct PreparationStubState {
    std::map<std::string, std::deque<StrictSourcePreferenceResult>>
            preference_results;
    PacmanDatabasePaths database_paths{
            "/stub/root",
            "/stub/database"};
    std::optional<PackageMetadataFailure> database_failure;
    std::optional<std::string>            supported_options_failure;
    std::optional<std::size_t>            pkgdest_failure_call;
    std::string                           pkgdest_failure_diagnostic;
    std::vector<std::string>              preference_reads;
    std::vector<bool>                     supported_options_guards;
    std::vector<SourceBuildEnvironment>   pkgdest_guards;
    std::size_t                           database_calls = 0;
    std::vector<stub::Event>              events;
};

PreparationStubState g_state;

std::string environment_subject(const SourceBuildEnvironment& environment) {
    if(environment.ordered_assignments.empty()) return "<empty>";

    std::string subject;
    for(std::size_t index = 0;
        index < environment.ordered_assignments.size(); ++index) {
        if(index > 0) subject += ",";
        const SourceEnvironmentAssignment& assignment =
                environment.ordered_assignments[index];
        subject += assignment.key + "=" + assignment.value;
    }
    return subject;
}

} // namespace

namespace aur_update_execution_preparation_test_stub {

void reset() {
    g_state = PreparationStubState{};
}

void enqueue_source_preference_result(
        const std::string& package_name,
        StrictSourcePreferenceResult result) {
    g_state.preference_results[package_name].push_back(std::move(result));
}

void set_database_paths(PacmanDatabasePaths paths) {
    g_state.database_paths = std::move(paths);
    g_state.database_failure.reset();
}

void set_database_failure(PackageMetadataFailure failure) {
    g_state.database_failure = std::move(failure);
}

void fail_supported_options_guard(std::string diagnostic) {
    g_state.supported_options_failure = std::move(diagnostic);
}

void fail_pkgdest_guard_on_call(
        std::size_t one_based_call_index,
        std::string diagnostic) {
    g_state.pkgdest_failure_call = one_based_call_index;
    g_state.pkgdest_failure_diagnostic = std::move(diagnostic);
}

const std::vector<std::string>& strict_preference_read_history() {
    return g_state.preference_reads;
}

const std::vector<bool>& supported_options_guard_history() {
    return g_state.supported_options_guards;
}

const std::vector<SourceBuildEnvironment>& pkgdest_guard_history() {
    return g_state.pkgdest_guards;
}

std::size_t database_call_count() {
    return g_state.database_calls;
}

const std::vector<Event>& event_history() {
    return g_state.events;
}

} // namespace aur_update_execution_preparation_test_stub

void ValidatedCacheRoot::require_unchanged_identity() const {
}

std::uintmax_t ValidatedCacheRoot::device() const noexcept {
    return 0;
}

std::uintmax_t ValidatedCacheRoot::inode() const noexcept {
    return 0;
}

std::uintmax_t ValidatedCacheRoot::owner() const noexcept {
    return 0;
}

StrictSourcePreferenceResult read_source_preference_strict(
        const std::string& package_name) {
    g_state.preference_reads.push_back(package_name);
    g_state.events.push_back(
            stub::Event{stub::EventKind::StrictPreferenceRead, package_name});

    auto scripted = g_state.preference_results.find(package_name);
    if(scripted == g_state.preference_results.end() ||
       scripted->second.empty()) {
        return SourcePreferenceAbsent{};
    }

    StrictSourcePreferenceResult result =
            std::move(scripted->second.front());
    scripted->second.pop_front();
    return result;
}

PacmanDatabasePaths resolve_pacman_database_paths() {
    ++g_state.database_calls;
    g_state.events.push_back(stub::Event{
            stub::EventKind::PacmanDatabaseResolution,
            "pacman-database"});
    if(g_state.database_failure.has_value()) {
        throw PackageMetadataError(*g_state.database_failure);
    }
    return g_state.database_paths;
}

PackageMetadataError::PackageMetadataError(PackageMetadataFailure failure)
    : std::runtime_error(failure.diagnostic),
      failure_(std::move(failure)) {}

const PackageMetadataFailure& PackageMetadataError::failure() const noexcept {
    return failure_;
}

void require_supported_separated_install_options(bool rm_deps) {
    g_state.supported_options_guards.push_back(rm_deps);
    g_state.events.push_back(stub::Event{
            stub::EventKind::SeparatedInstallOptionsGuard,
            rm_deps ? "rm_deps=true" : "rm_deps=false"});
    if(g_state.supported_options_failure.has_value()) {
        throw std::runtime_error(*g_state.supported_options_failure);
    }
}

void require_unclaimed_artifact_pkgdest(
        const SourceBuildEnvironment& environment) {
    g_state.pkgdest_guards.push_back(environment);
    g_state.events.push_back(stub::Event{
            stub::EventKind::ArtifactPkgdestGuard,
            environment_subject(environment)});

    if(g_state.pkgdest_failure_call.has_value() &&
       g_state.pkgdest_guards.size() == *g_state.pkgdest_failure_call) {
        throw std::runtime_error(g_state.pkgdest_failure_diagnostic);
    }
}

void seed_production_source_build_cache(
        PreparedProductionSourceBuildInvocation& invocation,
        const ValidatedCacheRoot& cache_root) {
    invocation.cache_root = cache_root;
    for(auto& work_item : invocation.work_items) {
        work_item.cache_root = cache_root;
    }
}
