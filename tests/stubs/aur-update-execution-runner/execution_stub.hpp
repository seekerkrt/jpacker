#pragma once

#include "app_config.hpp"
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

// execute_prepared_source_build_work_item()が隠すlifecycleを、operation testで
// mutation有無と順序だけ観測するためのconceptual event。production primitiveを
// 分解・再実装する契約ではない。
enum class EventKind {
    Checkout,
    Build,
    Install,
    Cleanup,
};

struct Event {
    std::size_t call_index = 0;
    EventKind   kind = EventKind::Checkout;
    std::string package_name;
    std::string package_base;

    bool operator==(const Event&) const = default;
};

struct ExecutionCall {
    std::size_t              call_index = 0;
    std::string              package_name;
    std::string              package_base;
    std::vector<std::string> plan_package_names;
    PacmanDatabasePaths      database_paths;
    AppConfig                config;
    std::vector<EventKind>   events;
};

void reset();

void enqueue_success(ArtifactInstallExecutionOutcome outcome);
void enqueue_ordinary_failure(std::string diagnostic);
void enqueue_cleanup_failure(
        ArtifactInstallExecutionOutcome outcome,
        std::string diagnostic);
void enqueue_unknown_failure();

const std::vector<ExecutionCall>& call_history();
const std::vector<Event>& event_history();
void require_script_consumed();

} // namespace aur_update_execution_runner_test_stub
