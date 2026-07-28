#include "execution_stub.hpp"

#include "separated_source_build.hpp"

#include <deque>
#include <optional>
#include <stdexcept>
#include <utility>

namespace {

namespace stub = aur_update_execution_runner_test_stub;

struct ScriptedExecution {
    stub::ScriptedExecutionOutcome outcome =
            stub::ScriptedExecutionOutcome::Installed;
    std::string diagnostic;
};

struct ExecutionStubState {
    std::deque<ScriptedExecution> outcomes;
    std::vector<stub::ExecutionCall> calls;
    std::vector<stub::Event> events;
    const PacmanDatabasePaths* first_database_paths_address = nullptr;
    std::optional<std::string> expectation_failure;
};

struct UnknownExecutionFailure {};

ExecutionStubState g_state;

void enqueue(
        stub::ScriptedExecutionOutcome outcome,
        std::string diagnostic = {}) {
    g_state.outcomes.push_back(
            ScriptedExecution{outcome, std::move(diagnostic)});
}

[[noreturn]] void fail_unexpected_execution() {
    constexpr const char* DIAGNOSTIC =
            "Unexpected source-build work-item execution with no pending outcome.";
    g_state.expectation_failure = DIAGNOSTIC;
    throw std::logic_error(DIAGNOSTIC);
}

void record_event(std::size_t call_index, stub::EventKind kind) {
    stub::ExecutionCall& call = g_state.calls.at(call_index);
    call.events.push_back(kind);
    g_state.events.push_back(stub::Event{
            call_index,
            kind,
            call.package_name,
            call.package_base});
}

std::vector<std::string> required_package_names(
        const ProductionSourceBuildWorkItem& work_item) {
    std::vector<std::string> package_names;
    package_names.reserve(work_item.required_targets.size());
    for(const auto& target : work_item.required_targets) {
        package_names.push_back(target.package_name);
    }
    return package_names;
}

} // namespace

namespace aur_update_execution_runner_test_stub {

void reset() {
    g_state = ExecutionStubState{};
}

void enqueue_success(ArtifactInstallExecutionOutcome outcome) {
    switch(outcome) {
        case ArtifactInstallExecutionOutcome::Installed:
            enqueue(ScriptedExecutionOutcome::Installed);
            return;
        case ArtifactInstallExecutionOutcome::SkippedAsNeeded:
            enqueue(ScriptedExecutionOutcome::SkippedAsNeeded);
            return;
    }

    throw std::logic_error(
            "AUR update execution stub received an unknown install outcome.");
}

void enqueue_ordinary_failure(std::string diagnostic) {
    enqueue(
            ScriptedExecutionOutcome::OrdinaryFailure,
            std::move(diagnostic));
}

void enqueue_cleanup_failure(
        ArtifactInstallExecutionOutcome outcome,
        std::string diagnostic) {
    switch(outcome) {
        case ArtifactInstallExecutionOutcome::Installed:
            enqueue(
                    ScriptedExecutionOutcome::InstalledCleanupFailure,
                    std::move(diagnostic));
            return;
        case ArtifactInstallExecutionOutcome::SkippedAsNeeded:
            enqueue(
                    ScriptedExecutionOutcome::SkippedAsNeededCleanupFailure,
                    std::move(diagnostic));
            return;
    }

    throw std::logic_error(
            "AUR update execution stub received an unknown cleanup outcome.");
}

void enqueue_unknown_failure() {
    enqueue(ScriptedExecutionOutcome::UnknownFailure);
}

const std::vector<ExecutionCall>& call_history() {
    return g_state.calls;
}

const std::vector<Event>& event_history() {
    return g_state.events;
}

void require_script_consumed() {
    if(g_state.expectation_failure.has_value()) {
        throw std::logic_error(*g_state.expectation_failure);
    }
    if(!g_state.outcomes.empty()) {
        throw std::logic_error(
                "AUR update execution stub has unconsumed outcomes.");
    }
}

} // namespace aur_update_execution_runner_test_stub

SeparatedSourceBuildCleanupError::SeparatedSourceBuildCleanupError(
        ArtifactInstallExecutionOutcome install_outcome,
        const std::string& diagnostic)
    : std::runtime_error(diagnostic),
      install_outcome_(install_outcome) {
}

std::optional<ArtifactInstallExecutionOutcome>
execute_prepared_source_build_work_item(
        const ProductionSourceBuildWorkItem& work_item,
        const PacmanDatabasePaths& database_paths,
        const AppConfig& config) {
    const RequiredPackageArtifactTarget& required_target =
            require_singular_required_package_target(work_item);

    // POLICY(#267): by-value capabilityのmove元addressとは比較できないため、
    // runnerが全work itemへ渡したsnapshot参照同士の同一性をstub内で検証する。
    if(g_state.first_database_paths_address == nullptr) {
        g_state.first_database_paths_address = &database_paths;
    } else if(g_state.first_database_paths_address != &database_paths) {
        g_state.expectation_failure =
                "AUR update runner did not reuse one Pacman database snapshot.";
    }

    const std::size_t call_index = g_state.calls.size();
    g_state.calls.push_back(stub::ExecutionCall{
            call_index,
            required_target.package_name,
            required_target.package_base,
            required_package_names(work_item),
            database_paths,
            config,
            {}});

    if(g_state.outcomes.empty()) fail_unexpected_execution();

    ScriptedExecution scripted = std::move(g_state.outcomes.front());
    g_state.outcomes.pop_front();

    // POLICY(#281): ordinary/unknown failureはbuild中の停止として固定する。
    // cleanup failureだけはtransaction完了後のcleanup試行まで順序へ残す。
    record_event(call_index, stub::EventKind::Checkout);
    record_event(call_index, stub::EventKind::Build);
    switch(scripted.outcome) {
        case stub::ScriptedExecutionOutcome::Installed:
            record_event(call_index, stub::EventKind::Install);
            record_event(call_index, stub::EventKind::Cleanup);
            return ArtifactInstallExecutionOutcome::Installed;
        case stub::ScriptedExecutionOutcome::SkippedAsNeeded:
            record_event(call_index, stub::EventKind::Install);
            record_event(call_index, stub::EventKind::Cleanup);
            return ArtifactInstallExecutionOutcome::SkippedAsNeeded;
        case stub::ScriptedExecutionOutcome::OrdinaryFailure:
            throw std::runtime_error(scripted.diagnostic);
        case stub::ScriptedExecutionOutcome::InstalledCleanupFailure:
            record_event(call_index, stub::EventKind::Install);
            record_event(call_index, stub::EventKind::Cleanup);
            throw SeparatedSourceBuildCleanupError(
                    ArtifactInstallExecutionOutcome::Installed,
                    scripted.diagnostic);
        case stub::ScriptedExecutionOutcome::SkippedAsNeededCleanupFailure:
            record_event(call_index, stub::EventKind::Install);
            record_event(call_index, stub::EventKind::Cleanup);
            throw SeparatedSourceBuildCleanupError(
                    ArtifactInstallExecutionOutcome::SkippedAsNeeded,
                    scripted.diagnostic);
        case stub::ScriptedExecutionOutcome::UnknownFailure:
            throw UnknownExecutionFailure{};
    }

    throw std::logic_error("AUR update execution stub has an unknown outcome.");
}
