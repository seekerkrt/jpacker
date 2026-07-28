#include "aur_update_execution_runner.hpp"

#include "separated_source_build.hpp"

#include <exception>
#include <stdexcept>
#include <string>

namespace {

constexpr const char* UNKNOWN_EXCEPTION_DIAGNOSTIC =
        "Prepared AUR update source-build work item failed with an unknown exception.";

std::vector<std::string> required_package_names(
        const ProductionSourceBuildWorkItem& work_item) {
    std::vector<std::string> package_names;
    package_names.reserve(work_item.required_targets.size());
    for(const auto& target : work_item.required_targets) {
        package_names.push_back(target.package_name);
    }
    return package_names;
}

void require_valid_prepared_invocation(
        const PreparedAurUpdateSourceBuildInvocation& invocation,
        const PreparedProductionSourceBuildInvocation& production_invocation) {
    if(!invocation.is_valid()) {
        throw std::logic_error(
                "Prepared AUR update source-build invocation is invalid or has already been consumed.");
    }

    const std::vector<AurUpdatePreparedWorkItemAttribution>& attributions =
            invocation.work_item_attributions();
    if(production_invocation.work_items.empty() ||
       attributions.size() != production_invocation.work_items.size()) {
        throw std::logic_error(
                "Prepared AUR update source-build invocation correlation count is inconsistent.");
    }

    for(std::size_t index = 0;
        index < production_invocation.work_items.size(); ++index) {
        const ProductionSourceBuildWorkItem& work_item =
                production_invocation.work_items[index];
        const AurUpdatePreparedWorkItemAttribution& attribution =
                attributions[index];
        const RequiredPackageArtifactTarget& required_target =
                require_singular_required_package_target(work_item);
        if(attribution.invocation_work_item_index != index ||
           (index > 0 &&
            attribution.build_plan_order_index <=
                    attributions[index - 1].build_plan_order_index) ||
           attribution.package_name != required_target.package_name ||
           attribution.package_base != required_target.package_base ||
           attribution.affected_update_plan_indices.empty() ||
           attribution.affected_roots.empty()) {
            throw std::logic_error(
                    "Prepared AUR update source-build invocation work-item correlation is inconsistent.");
        }
    }
}

AurUpdateWorkItemExecutionStatus completed_status(
        ArtifactInstallExecutionOutcome outcome) {
    switch(outcome) {
    case ArtifactInstallExecutionOutcome::Installed:
        return AurUpdateWorkItemExecutionStatus::Updated;
    case ArtifactInstallExecutionOutcome::SkippedAsNeeded:
        return AurUpdateWorkItemExecutionStatus::NoChange;
    }
    throw std::logic_error("Unknown artifact install execution outcome.");
}

AurUpdateWorkItemExecutionStatus cleanup_failed_status(
        ArtifactInstallExecutionOutcome outcome) {
    switch(outcome) {
    case ArtifactInstallExecutionOutcome::Installed:
        return AurUpdateWorkItemExecutionStatus::UpdatedCleanupFailed;
    case ArtifactInstallExecutionOutcome::SkippedAsNeeded:
        return AurUpdateWorkItemExecutionStatus::NoChangeCleanupFailed;
    }
    throw std::logic_error("Unknown artifact install execution outcome.");
}

AurUpdateWorkItemExecutionResult make_not_attempted_result(
        const ProductionSourceBuildWorkItem& work_item,
        const AurUpdatePreparedWorkItemAttribution& attribution) {
    return AurUpdateWorkItemExecutionResult{
            .work_item_index = attribution.invocation_work_item_index,
            .build_plan_order_index = attribution.build_plan_order_index,
            .package_name = attribution.package_name,
            .package_base = attribution.package_base,
            .plan_package_names = required_package_names(work_item),
            .affected_update_plan_indices =
                    attribution.affected_update_plan_indices,
            .affected_roots = attribution.affected_roots,
            .status = AurUpdateWorkItemExecutionStatus::NotAttempted,
            .failure_kind =
                    AurUpdateWorkItemFailureKind::PriorWorkItemStopped,
            .diagnostic = std::nullopt,
    };
}

} // namespace

bool AurUpdateSourceBuildExecutionResult::is_success() const noexcept {
    return status == AurUpdateInvocationExecutionStatus::Completed;
}

bool AurUpdateSourceBuildExecutionResult::changed_package_state()
        const noexcept {
    for(const auto& work_item_result : work_item_results) {
        if(work_item_result.status ==
                   AurUpdateWorkItemExecutionStatus::Updated ||
           work_item_result.status ==
                   AurUpdateWorkItemExecutionStatus::UpdatedCleanupFailed) {
            return true;
        }
    }
    return false;
}

bool AurUpdateSourceBuildExecutionResult::has_not_attempted_items()
        const noexcept {
    for(const auto& work_item_result : work_item_results) {
        if(work_item_result.status ==
           AurUpdateWorkItemExecutionStatus::NotAttempted) {
            return true;
        }
    }
    return false;
}

bool AurUpdateSourceBuildExecutionResult::has_cleanup_failure()
        const noexcept {
    for(const auto& work_item_result : work_item_results) {
        if(work_item_result.status ==
                   AurUpdateWorkItemExecutionStatus::UpdatedCleanupFailed ||
           work_item_result.status ==
                   AurUpdateWorkItemExecutionStatus::NoChangeCleanupFailed) {
            return true;
        }
    }
    return false;
}

std::optional<std::size_t>
AurUpdateSourceBuildExecutionResult::stopped_work_item_index()
        const noexcept {
    for(const auto& work_item_result : work_item_results) {
        if(work_item_result.status == AurUpdateWorkItemExecutionStatus::Failed ||
           work_item_result.status ==
                   AurUpdateWorkItemExecutionStatus::UpdatedCleanupFailed ||
           work_item_result.status ==
                   AurUpdateWorkItemExecutionStatus::NoChangeCleanupFailed) {
            return work_item_result.work_item_index;
        }
    }
    return std::nullopt;
}

AurUpdateSourceBuildExecutionResult
execute_prepared_aur_update_source_build_invocation(
        PreparedAurUpdateSourceBuildInvocation invocation,
        const AppConfig& config) {
    // POLICY(#267): capability validity/correlationは最初のexecutor callより前に
    // 全件検証し、moved-from/replayed snapshotをempty successへ潰さない。
    const PreparedProductionSourceBuildInvocation& production_invocation =
            invocation.production_invocation_;
    require_valid_prepared_invocation(invocation, production_invocation);
    const std::vector<AurUpdatePreparedWorkItemAttribution>& attributions =
            invocation.work_item_attributions();

    AurUpdateSourceBuildExecutionResult result;
    result.work_item_results.reserve(production_invocation.work_items.size());
    for(std::size_t index = 0;
        index < production_invocation.work_items.size(); ++index) {
        result.work_item_results.push_back(make_not_attempted_result(
                production_invocation.work_items[index],
                attributions[index]));
    }

    // POLICY(#267): system mutationを開始する前に全result entryをowned snapshot化する。
    // 成功時だけ次のPackageBaseへ進み、最初のfailure後はexecutorを呼ばない。
    for(std::size_t index = 0;
        index < production_invocation.work_items.size(); ++index) {
        AurUpdateWorkItemExecutionResult& work_item_result =
                result.work_item_results[index];
        try {
            const std::optional<ArtifactInstallExecutionOutcome>
                    install_outcome =
                    execute_prepared_source_build_work_item(
                            production_invocation.work_items[index],
                            production_invocation.database_paths,
                            config);
            if(!install_outcome.has_value()) {
                throw std::logic_error(
                        "Prepared AUR update source-build work item completed without an artifact install outcome.");
            }
            work_item_result.status = completed_status(*install_outcome);
            work_item_result.failure_kind =
                    AurUpdateWorkItemFailureKind::None;
        } catch(const SeparatedSourceBuildCleanupError& error) {
            // POLICY(#267): package change outcomeとcleanup failureをflattenせず、
            // install/--needed skipのどちらでも後続だけを停止する。
            work_item_result.status = cleanup_failed_status(
                    error.install_outcome());
            work_item_result.failure_kind =
                    AurUpdateWorkItemFailureKind::
                            CleanupFailedAfterPackageTransaction;
            work_item_result.diagnostic = error.what();
            result.status = AurUpdateInvocationExecutionStatus::
                    StoppedAfterPackageCleanupFailure;
            return result;
        } catch(const std::exception& error) {
            work_item_result.status = AurUpdateWorkItemExecutionStatus::Failed;
            work_item_result.failure_kind =
                    AurUpdateWorkItemFailureKind::BuildOrInstallFailed;
            work_item_result.diagnostic = error.what();
            result.status = AurUpdateInvocationExecutionStatus::
                    StoppedOnWorkItemFailure;
            return result;
        } catch(...) {
            work_item_result.status = AurUpdateWorkItemExecutionStatus::Failed;
            work_item_result.failure_kind =
                    AurUpdateWorkItemFailureKind::UnknownException;
            work_item_result.diagnostic = UNKNOWN_EXCEPTION_DIAGNOSTIC;
            result.status = AurUpdateInvocationExecutionStatus::
                    StoppedOnWorkItemFailure;
            return result;
        }
    }

    return result;
}
