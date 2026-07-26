#pragma once

#include "source_install.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace aur_update_execution_runner_test_stub {

enum class ScriptedExecutionOutcome {
    Installed,
    SkippedAsNeeded,
    OrdinaryFailure,
    InstalledCleanupFailure,
    SkippedAsNeededCleanupFailure,
    UnknownFailure,
};

struct ExecutionCall {
    std::size_t              call_index = 0;
    std::string              package_name;
    std::string              package_base;
    std::vector<std::string> plan_package_names;
    PacmanDatabasePaths      database_paths;
};

void reset();

void enqueue_success(ArtifactInstallExecutionOutcome outcome);
void enqueue_ordinary_failure(std::string diagnostic);
void enqueue_cleanup_failure(
        ArtifactInstallExecutionOutcome outcome,
        std::string diagnostic);
void enqueue_unknown_failure();

const std::vector<ExecutionCall>& call_history();
void require_script_consumed();

} // namespace aur_update_execution_runner_test_stub
