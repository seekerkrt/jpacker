#include "phase_stub.hpp"

#include "artifact_install_executor.hpp"
#include "separated_source_build.hpp"

#include <deque>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <utility>

namespace {

namespace stub = system_source_upgrade_test_stub;

enum class ScriptedSourceExecutionKind {
    Success,
    Failure,
    CacheFailure,
    CleanupFailure,
    UnknownFailure,
};

struct ScriptedSourceExecution {
    ScriptedSourceExecutionKind kind =
            ScriptedSourceExecutionKind::Success;
    SourceBuildExecutionResult result;
    ArtifactInstallExecutionOutcome cleanup_outcome =
            ArtifactInstallExecutionOutcome::Installed;
    std::string diagnostic;
};

struct PhaseStubState {
    SourcePreferenceDirectorySnapshot preference_directory;
    std::optional<std::string> preference_directory_failure;
    std::map<std::string, std::deque<StrictSourcePreferenceResult>>
            preference_results;
    std::map<std::string, ResolvedSourceBuildIdentity> identities;
    std::map<std::string, std::string> identity_failures;
    std::map<std::string, std::string> work_item_failures;
    std::optional<std::string> invocation_failure;
    std::optional<std::string> supported_options_failure;
    bool cache_seed_failure = false;
    bool cache_activation_failure = false;
    std::deque<stub::MetadataSessionScript> metadata_sessions;
    int system_exit_status = 0;
    std::optional<std::string> system_failure;
    std::deque<ScriptedSourceExecution> source_executions;

    std::vector<stub::Event> events;
    std::vector<stub::ConfigSnapshot> supported_option_configs;
    std::vector<stub::ConfigSnapshot> invocation_configs;
    std::vector<stub::SourceExecutionCall> source_calls;
    std::vector<std::string> system_commands;
    std::optional<std::string> expectation_failure;
};

struct UnknownSourceExecutionFailure {};

PhaseStubState g_state;

stub::ConfigSnapshot snapshot_config(const AppConfig& config) {
    return stub::ConfigSnapshot{
            config.user_config.review.pkgbuild == ReviewPolicy::Skip,
            config.user_config.review.diff == ReviewPolicy::Skip,
            config.no_confirm,
            config.user_config.build.mode == BuildMode::Rebuild,
            config.user_config.build.mode == BuildMode::Clean,
            config.rm_deps,
            config.editor};
}

[[noreturn]] void fail_unexpected(const std::string& diagnostic) {
    g_state.expectation_failure = diagnostic;
    throw std::logic_error(diagnostic);
}

ResolvedSourceBuildIdentity default_identity(
        const std::string& package_name) {
    return ResolvedSourceBuildIdentity{
            package_name,
            package_name,
            "repository:" + package_name,
            "https://packages.example/" + package_name + ".git",
            SourceBuildSourceKind::Repository,
            false};
}

} // namespace

void activate_production_source_build_cache(
        PreparedProductionSourceBuildInvocation&) {
    // Phase tests isolate cache/filesystem mutation behind this seam.
    if(g_state.cache_activation_failure) {
        throw TrustedCacheError(TrustedCacheFailure{
                TrustedCacheStage::RootRevalidation,
                TrustedCacheErrorCode::ConcurrentReplacement,
                std::nullopt});
    }
}

void seed_production_source_build_cache(
        PreparedProductionSourceBuildInvocation& invocation,
        const ValidatedCacheRoot& cache_root) {
    if(g_state.cache_seed_failure) {
        throw TrustedCacheError(TrustedCacheFailure{
                TrustedCacheStage::RootRevalidation,
                TrustedCacheErrorCode::ConcurrentReplacement,
                std::nullopt});
    }
    cache_root.require_unchanged_identity();
    invocation.cache_root = cache_root;
    for(auto& work_item : invocation.work_items) {
        work_item.cache_root = cache_root;
    }
}

namespace system_source_upgrade_test_stub {

void reset() {
    g_state = PhaseStubState{};
}

void set_preference_directory(SourcePreferenceDirectorySnapshot snapshot) {
    g_state.preference_directory = std::move(snapshot);
    g_state.preference_directory_failure.reset();
}

void fail_preference_directory(std::string diagnostic) {
    g_state.preference_directory_failure = std::move(diagnostic);
}

void enqueue_preference_result(
        const std::string& package_name,
        StrictSourcePreferenceResult result) {
    g_state.preference_results[package_name].push_back(std::move(result));
}

void set_source_identity(
        const std::string& package_name,
        ResolvedSourceBuildIdentity identity) {
    g_state.identities[package_name] = std::move(identity);
    g_state.identity_failures.erase(package_name);
}

void fail_source_identity(
        const std::string& package_name,
        std::string diagnostic) {
    g_state.identity_failures[package_name] = std::move(diagnostic);
}

void fail_source_work_item(
        const std::string& package_name,
        std::string diagnostic) {
    g_state.work_item_failures[package_name] = std::move(diagnostic);
}

void fail_source_invocation(std::string diagnostic) {
    g_state.invocation_failure = std::move(diagnostic);
}

void fail_supported_options(std::string diagnostic) {
    g_state.supported_options_failure = std::move(diagnostic);
}

void fail_cache_seed() {
    g_state.cache_seed_failure = true;
}

void fail_cache_activation() {
    g_state.cache_activation_failure = true;
}

void enqueue_metadata_session(MetadataSessionScript script) {
    g_state.metadata_sessions.push_back(std::move(script));
}

void set_system_command_exit_status(int exit_status) {
    g_state.system_exit_status = exit_status;
    g_state.system_failure.reset();
}

void fail_system_command(std::string diagnostic) {
    g_state.system_failure = std::move(diagnostic);
}

void enqueue_source_success(SourceBuildExecutionResult result) {
    ScriptedSourceExecution execution;
    execution.kind = ScriptedSourceExecutionKind::Success;
    execution.result = std::move(result);
    g_state.source_executions.push_back(std::move(execution));
}

void enqueue_source_failure(std::string diagnostic) {
    ScriptedSourceExecution execution;
    execution.kind = ScriptedSourceExecutionKind::Failure;
    execution.diagnostic = std::move(diagnostic);
    g_state.source_executions.push_back(std::move(execution));
}

void enqueue_source_cache_failure() {
    ScriptedSourceExecution execution;
    execution.kind = ScriptedSourceExecutionKind::CacheFailure;
    g_state.source_executions.push_back(std::move(execution));
}

void enqueue_source_cleanup_failure(
        ArtifactInstallExecutionOutcome outcome,
        std::string diagnostic) {
    ScriptedSourceExecution execution;
    execution.kind = ScriptedSourceExecutionKind::CleanupFailure;
    execution.cleanup_outcome = outcome;
    execution.diagnostic = std::move(diagnostic);
    g_state.source_executions.push_back(std::move(execution));
}

void enqueue_source_unknown_failure() {
    ScriptedSourceExecution execution;
    execution.kind = ScriptedSourceExecutionKind::UnknownFailure;
    g_state.source_executions.push_back(std::move(execution));
}

void record_progress(const std::string& subject) {
    g_state.events.push_back(Event{EventKind::Progress, subject});
}

const std::vector<Event>& events() {
    return g_state.events;
}

const std::vector<ConfigSnapshot>& supported_option_configs() {
    return g_state.supported_option_configs;
}

const std::vector<ConfigSnapshot>& invocation_configs() {
    return g_state.invocation_configs;
}

const std::vector<SourceExecutionCall>& source_execution_calls() {
    return g_state.source_calls;
}

const std::vector<std::string>& system_commands() {
    return g_state.system_commands;
}

void require_script_consumed() {
    if(g_state.expectation_failure.has_value()) {
        throw std::logic_error(*g_state.expectation_failure);
    }
    for(const auto& [package_name, results] : g_state.preference_results) {
        if(!results.empty()) {
            throw std::logic_error(
                    "Unconsumed source preference result for " + package_name + ".");
        }
    }
    if(!g_state.metadata_sessions.empty()) {
        throw std::logic_error("Unconsumed metadata session script.");
    }
    if(!g_state.source_executions.empty()) {
        throw std::logic_error("Unconsumed source execution script.");
    }
}

} // namespace system_source_upgrade_test_stub

SourcePreferenceDirectorySnapshot snapshot_source_preference_directory() {
    g_state.events.push_back(stub::Event{
            stub::EventKind::PreferenceDirectorySnapshot,
            "package.build"});
    if(g_state.preference_directory_failure.has_value()) {
        throw std::runtime_error(*g_state.preference_directory_failure);
    }
    return g_state.preference_directory;
}

StrictSourcePreferenceResult read_source_preference_strict(
        const std::string& package_name) {
    g_state.events.push_back(stub::Event{
            stub::EventKind::StrictPreferenceRead,
            package_name});
    auto found = g_state.preference_results.find(package_name);
    if(found == g_state.preference_results.end() || found->second.empty()) {
        fail_unexpected(
                "Unexpected strict source preference read for " +
                package_name + ".");
    }
    StrictSourcePreferenceResult result =
            std::move(found->second.front());
    found->second.pop_front();
    return result;
}

void require_supported_production_source_build_options(
        const AppConfig& config) {
    g_state.supported_option_configs.push_back(snapshot_config(config));
    g_state.events.push_back(stub::Event{
            stub::EventKind::SupportedOptionsGuard,
            "source-options"});
    if(g_state.supported_options_failure.has_value()) {
        throw std::runtime_error(*g_state.supported_options_failure);
    }
}

ResolvedSourceBuildIdentity resolve_source_build_identity(
        const std::string& package_name) {
    const char* cache_home = std::getenv("XDG_CACHE_HOME");
    if(cache_home == nullptr ||
       !std::filesystem::is_directory(
               std::filesystem::path(cache_home) / "moguet")) {
        fail_unexpected(
                "Source identity resolution started before cache authority preparation.");
    }
    g_state.events.push_back(stub::Event{
            stub::EventKind::SourceIdentityResolution,
            package_name});
    auto failure = g_state.identity_failures.find(package_name);
    if(failure != g_state.identity_failures.end()) {
        throw std::runtime_error(failure->second);
    }
    auto identity = g_state.identities.find(package_name);
    return identity == g_state.identities.end()
            ? default_identity(package_name)
            : identity->second;
}

ProductionSourceBuildWorkItem prepare_resolved_source_build_work_item(
        const ResolvedSourceBuildIdentity& identity,
        SourceBuildEnvironment environment,
        bool only_if_updated,
        bool needed) {
    g_state.events.push_back(stub::Event{
            stub::EventKind::SourceWorkItemPreparation,
            identity.requested_name});
    auto failure = g_state.work_item_failures.find(identity.requested_name);
    if(failure != g_state.work_item_failures.end()) {
        throw std::runtime_error(failure->second);
    }

    ProductionSourceBuildWorkItem work_item;
    work_item.request.package_name = identity.requested_name;
    work_item.request.checkout_name = identity.package_base;
    work_item.request.git_url = identity.git_url;
    work_item.request.custom_environment = std::move(environment);
    work_item.request.only_if_updated = only_if_updated;
    work_item.request.needed = needed;
    work_item.required_targets.push_back(RequiredPackageArtifactTarget{
            identity.package_base,
            identity.requested_name,
            DesiredInstallReason::Explicit});
    work_item.uses_system_update_baseline =
            identity.source_kind == SourceBuildSourceKind::Repository;
    return work_item;
}

PreparedProductionSourceBuildInvocation
prepare_production_source_build_invocation(
        std::vector<ProductionSourceBuildWorkItem> work_items,
        const AppConfig& config) {
    g_state.invocation_configs.push_back(snapshot_config(config));
    g_state.events.push_back(stub::Event{
            stub::EventKind::SourceInvocationPreparation,
            "source-invocation"});
    if(g_state.invocation_failure.has_value()) {
        throw std::runtime_error(*g_state.invocation_failure);
    }
    return PreparedProductionSourceBuildInvocation{
            std::move(work_items),
            PacmanDatabasePaths{"/stub/root", "/stub/database"},
            std::nullopt};
}

PackageMetadataError::PackageMetadataError(PackageMetadataFailure failure)
    : std::runtime_error(failure.diagnostic),
      failure_(std::move(failure)) {
}

const PackageMetadataFailure& PackageMetadataError::failure() const noexcept {
    return failure_;
}

struct PackageMetadataSession::Impl {
    explicit Impl(stub::MetadataSessionScript scripted)
        : script(std::move(scripted)) {
    }

    stub::MetadataSessionScript script;
};

PackageMetadataSession::PackageMetadataSession(
        std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl)) {
}

PackageMetadataSession::PackageMetadataSession(
        PackageMetadataSession&&) noexcept = default;

PackageMetadataSession& PackageMetadataSession::operator=(
        PackageMetadataSession&&) noexcept = default;

PackageMetadataSession::~PackageMetadataSession() noexcept = default;

PackageMetadataSession PackageMetadataSession::open(
        const PacmanDatabasePaths& paths) {
    g_state.events.push_back(stub::Event{
            stub::EventKind::MetadataSessionOpen,
            paths.db_path.string()});
    if(g_state.metadata_sessions.empty()) {
        fail_unexpected("Unexpected package metadata session open.");
    }
    stub::MetadataSessionScript script =
            std::move(g_state.metadata_sessions.front());
    g_state.metadata_sessions.pop_front();
    if(script.open_failure.has_value()) {
        throw PackageMetadataError(*script.open_failure);
    }
    return PackageMetadataSession(std::make_unique<Impl>(std::move(script)));
}

InstalledPackageQueryResult PackageMetadataSession::query_installed_package(
        const std::string& package_name) const {
    g_state.events.push_back(stub::Event{
            stub::EventKind::InstalledPackageQuery,
            package_name});
    if(impl_ == nullptr) {
        return PackageMetadataFailure{
                PackageMetadataErrorCode::QueryFailed,
                "Stub package metadata session is closed."};
    }
    auto found = impl_->script.installed_package_results.find(package_name);
    return found == impl_->script.installed_package_results.end()
            ? InstalledPackageQueryResult{PackageNotFound{}}
            : found->second;
}

LocalPackageVersionSnapshotResult
PackageMetadataSession::snapshot_local_package_versions() const {
    g_state.events.push_back(stub::Event{
            stub::EventKind::LocalPackageSnapshot,
            "local-packages"});
    if(impl_ == nullptr) {
        return PackageMetadataFailure{
                PackageMetadataErrorCode::QueryFailed,
                "Stub package metadata session is closed."};
    }
    return impl_->script.local_package_snapshot;
}

int run_command(const std::string& command) {
    g_state.system_commands.push_back(command);
    g_state.events.push_back(stub::Event{
            stub::EventKind::SystemCommand,
            command});
    if(g_state.system_failure.has_value()) {
        throw std::runtime_error(*g_state.system_failure);
    }
    return g_state.system_exit_status;
}

SeparatedSourceBuildCleanupError::SeparatedSourceBuildCleanupError(
        ArtifactInstallExecutionOutcome install_outcome,
        const std::string& diagnostic)
    : std::runtime_error(diagnostic),
      install_outcome_(install_outcome) {
}

SourceBuildExecutionResult execute_prepared_source_build_work_item_typed(
        const ProductionSourceBuildWorkItem& work_item,
        const PacmanDatabasePaths& database_paths,
        const AppConfig& config) {
    static_cast<void>(database_paths);
    if(work_item.required_targets.size() != 1 ||
       work_item.request.package_name.empty() ||
       work_item.request.package_name !=
               work_item.required_targets.front().package_name ||
       work_item.request.checkout_name !=
               work_item.required_targets.front().package_base) {
        fail_unexpected(
                "System source upgrade singular execution received an inconsistent required target.");
    }
    g_state.source_calls.push_back(stub::SourceExecutionCall{
            work_item.request.package_name,
            work_item.request.checkout_name,
            work_item.request,
            snapshot_config(config)});
    g_state.events.push_back(stub::Event{
            stub::EventKind::SourceExecution,
            work_item.request.package_name});
    if(g_state.source_executions.empty()) {
        fail_unexpected(
                "Unexpected source execution for " +
                work_item.request.package_name + ".");
    }

    ScriptedSourceExecution execution =
            std::move(g_state.source_executions.front());
    g_state.source_executions.pop_front();
    switch(execution.kind) {
        case ScriptedSourceExecutionKind::Success:
            return execution.result;
        case ScriptedSourceExecutionKind::Failure:
            throw std::runtime_error(execution.diagnostic);
        case ScriptedSourceExecutionKind::CacheFailure:
            throw TrustedCacheError(TrustedCacheFailure{
                    TrustedCacheStage::RootRevalidation,
                    TrustedCacheErrorCode::ConcurrentReplacement,
                    std::nullopt});
        case ScriptedSourceExecutionKind::CleanupFailure:
            throw SeparatedSourceBuildCleanupError(
                    execution.cleanup_outcome,
                    execution.diagnostic);
        case ScriptedSourceExecutionKind::UnknownFailure:
            throw UnknownSourceExecutionFailure{};
    }
    throw std::logic_error("Unknown scripted source execution kind.");
}
