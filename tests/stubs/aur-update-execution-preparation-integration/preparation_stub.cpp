#include "preparation_stub.hpp"

#include "source_environment.hpp"
#include "source_install.hpp"

#include <cstdlib>
#include <optional>
#include <stdexcept>
#include <utility>

namespace {

struct PreparationStubState {
    PacmanDatabasePaths database_paths{"/", "/var/lib/pacman"};
    std::optional<PackageMetadataFailure> database_failure;
    std::size_t database_resolution_calls = 0;
    std::size_t separated_option_check_calls = 0;
    std::size_t artifact_pkgdest_check_calls = 0;
};

PreparationStubState g_state;

} // namespace

namespace aur_update_execution_preparation_integration_stub {

void reset() {
    g_state = PreparationStubState{};
}

void set_database_paths(PacmanDatabasePaths database_paths) {
    g_state.database_paths = std::move(database_paths);
    g_state.database_failure.reset();
}

void set_database_failure(PackageMetadataFailure failure) {
    g_state.database_failure = std::move(failure);
}

std::size_t database_resolution_call_count() {
    return g_state.database_resolution_calls;
}

std::size_t separated_option_check_call_count() {
    return g_state.separated_option_check_calls;
}

std::size_t artifact_pkgdest_check_call_count() {
    return g_state.artifact_pkgdest_check_calls;
}

} // namespace aur_update_execution_preparation_integration_stub

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

PackageMetadataError::PackageMetadataError(PackageMetadataFailure failure)
    : std::runtime_error(failure.diagnostic), failure_(std::move(failure)) {
}

const PackageMetadataFailure& PackageMetadataError::failure() const noexcept {
    return failure_;
}

PacmanDatabasePaths resolve_pacman_database_paths() {
    ++g_state.database_resolution_calls;
    if(g_state.database_failure.has_value()) {
        throw PackageMetadataError(*g_state.database_failure);
    }
    return g_state.database_paths;
}

void require_supported_separated_install_options(bool rm_deps) {
    ++g_state.separated_option_check_calls;
    if(rm_deps) {
        throw std::runtime_error(
            "Separated build/install does not support --rmdeps.");
    }
}

void require_unclaimed_artifact_pkgdest(
    const SourceBuildEnvironment& environment) {
    ++g_state.artifact_pkgdest_check_calls;
    if(environment.defines("PKGDEST")) {
        throw std::runtime_error(
            "Source environment PKGDEST conflicts with invocation-owned "
            "artifact workspace.");
    }
    // getenv()はdefined-emptyでもnon-nullを返すproduction契約を再現する。
    if(std::getenv("PKGDEST") != nullptr) {
        throw std::runtime_error(
            "Inherited PKGDEST conflicts with invocation-owned artifact workspace.");
    }
}

std::shared_ptr<ReviewedSourceFatalStatePreflightSlot>
preflight_reviewed_source_fatal_state_for_production(
    const SourceBuildRequest&) {
    return nullptr;
}

void seed_production_source_build_cache(
    PreparedProductionSourceBuildInvocation& invocation,
    const ValidatedCacheRoot& cache_root) {
    invocation.cache_root = cache_root;
    for(auto& work_item : invocation.work_items) {
        work_item.cache_root = cache_root;
    }
}
