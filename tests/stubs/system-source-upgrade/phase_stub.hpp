#pragma once

#include "app_config.hpp"
#include "package_metadata.hpp"
#include "source_build.hpp"
#include "source_install.hpp"
#include "source_preference.hpp"

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace system_source_upgrade_test_stub {

enum class EventKind {
    PreferenceDirectorySnapshot,
    SupportedOptionsGuard,
    StrictPreferenceRead,
    SourceIdentityResolution,
    SourceWorkItemPreparation,
    SourceInvocationPreparation,
    MetadataSessionOpen,
    InstalledPackageQuery,
    LocalPackageSnapshot,
    Progress,
    SystemCommand,
    SourceExecution,
};

struct Event {
    EventKind   kind;
    std::string subject;

    bool operator==(const Event&) const = default;
};

struct ConfigSnapshot {
    bool        no_edit = false;
    bool        no_diff = false;
    bool        no_confirm = false;
    bool        rebuild = false;
    bool        clean_build = false;
    bool        rm_deps = false;
    std::string editor;

    bool operator==(const ConfigSnapshot&) const = default;
};

struct MetadataSessionScript {
    std::optional<PackageMetadataFailure> open_failure;
    LocalPackageVersionSnapshotResult local_package_snapshot =
            LocalPackageVersionSnapshot{};
    std::map<std::string, InstalledPackageQueryResult>
            installed_package_results;
};

struct SourceExecutionCall {
    std::string package_name;
    std::string package_base;
    SourceBuildRequest request;
    ConfigSnapshot config;
};

void reset();

void set_preference_directory(SourcePreferenceDirectorySnapshot snapshot);
void fail_preference_directory(std::string diagnostic);
void enqueue_preference_result(
        const std::string& package_name,
        StrictSourcePreferenceResult result);

void set_source_identity(
        const std::string& package_name,
        ResolvedSourceBuildIdentity identity);
void fail_source_identity(
        const std::string& package_name,
        std::string diagnostic);
void fail_source_work_item(
        const std::string& package_name,
        std::string diagnostic);
void fail_source_invocation(std::string diagnostic);
void fail_supported_options(std::string diagnostic);
void fail_cache_seed();
void fail_cache_activation();

void enqueue_metadata_session(MetadataSessionScript script);

void set_system_command_exit_status(int exit_status);
void fail_system_command(std::string diagnostic);
void enqueue_source_success(SourceBuildExecutionResult result);
void enqueue_source_failure(std::string diagnostic);
void enqueue_source_cache_failure();
void enqueue_source_cleanup_failure(
        ArtifactInstallExecutionOutcome outcome,
        std::string diagnostic);
void enqueue_source_unknown_failure();

void record_progress(const std::string& subject);

const std::vector<Event>& events();
const std::vector<ConfigSnapshot>& supported_option_configs();
const std::vector<ConfigSnapshot>& invocation_configs();
const std::vector<SourceExecutionCall>& source_execution_calls();
const std::vector<std::string>& system_commands();

void require_script_consumed();

} // namespace system_source_upgrade_test_stub
