#include "aur_update_cli_presentation.hpp"

#include "aur_update_operation_result.hpp"

#include <algorithm>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

namespace {

bool is_known_install_reason(DesiredInstallReason reason) noexcept {
    return reason == DesiredInstallReason::Explicit ||
           reason == DesiredInstallReason::Dependency;
}

std::string_view install_reason_label(DesiredInstallReason reason) {
    switch(reason) {
    case DesiredInstallReason::Explicit:
        return "explicit";
    case DesiredInstallReason::Dependency:
        return "dependency";
    }
    throw std::logic_error("Unknown AUR child desired install reason.");
}

std::string_view metadata_error_label(PackageMetadataErrorCode code) {
    switch(code) {
    case PackageMetadataErrorCode::ConfigurationUnavailable:
        return "configuration unavailable";
    case PackageMetadataErrorCode::ConfigurationMalformed:
        return "configuration malformed";
    case PackageMetadataErrorCode::InitializationFailed:
        return "database initialization failed";
    case PackageMetadataErrorCode::LocalDatabaseUnavailable:
        return "local database unavailable";
    case PackageMetadataErrorCode::InvalidPackageName:
        return "invalid package name";
    case PackageMetadataErrorCode::QueryFailed:
        return "package query failed";
    case PackageMetadataErrorCode::MalformedMetadata:
        return "malformed package metadata";
    case PackageMetadataErrorCode::SyncDatabaseUnavailable:
        return "sync database unavailable";
    case PackageMetadataErrorCode::RepositoryNotConfigured:
        return "repository not configured";
    }
    throw std::logic_error("Unknown package metadata failure code.");
}

std::string_view source_build_failure_label(
        AurUpdateSourceBuildFailureCategory category) {
    switch(category) {
    case AurUpdateSourceBuildFailureCategory::Build:
        return "source build failure";
    case AurUpdateSourceBuildFailureCategory::ArtifactValidation:
        return "artifact validation failure";
    case AurUpdateSourceBuildFailureCategory::ArtifactIdentity:
        return "artifact identity failure";
    case AurUpdateSourceBuildFailureCategory::Other:
        return "build or install failure";
    }
    throw std::logic_error("Unknown AUR source-build failure category.");
}

std::string_view correlation_failure_label(
        AurUpdateExecutionCorrelationFailureReason reason) {
    switch(reason) {
    case AurUpdateExecutionCorrelationFailureReason::PackageBaseMismatch:
        return "PackageBase mismatch";
    case AurUpdateExecutionCorrelationFailureReason::DesiredInstallReasonMismatch:
        return "install reason mismatch";
    case AurUpdateExecutionCorrelationFailureReason::
            SelectedArtifactIdentityMismatch:
        return "selected artifact identity mismatch";
    case AurUpdateExecutionCorrelationFailureReason::EmptySelectedArtifactVersion:
        return "selected artifact version missing";
    case AurUpdateExecutionCorrelationFailureReason::UnknownChildOutcome:
        return "unknown child outcome";
    case AurUpdateExecutionCorrelationFailureReason::DuplicateSelectedChild:
        return "duplicate selected child";
    case AurUpdateExecutionCorrelationFailureReason::MissingSelectedChild:
        return "missing selected child";
    case AurUpdateExecutionCorrelationFailureReason::ExtraSelectedChild:
        return "extra selected child";
    case AurUpdateExecutionCorrelationFailureReason::
            InvalidUnselectedArtifactIdentity:
        return "invalid unselected artifact identity";
    case AurUpdateExecutionCorrelationFailureReason::
            SelectedAndUnselectedIdentityOverlap:
        return "selected/unselected identity overlap";
    case AurUpdateExecutionCorrelationFailureReason::
            DuplicateUnselectedArtifactIdentity:
        return "duplicate unselected artifact identity";
    }
    throw std::logic_error("Unknown AUR execution correlation failure reason.");
}

std::string transaction_failure_summary(
        const AurUpdatePackageTransactionFailureSnapshot& failure) {
    switch(failure.category) {
    case AurUpdatePackageTransactionFailureCategory::CommandFailed: {
        std::string summary = "package transaction failed";
        if(failure.exit_code.has_value()) {
            summary += " (exit code " + std::to_string(*failure.exit_code) + ")";
        }
        return summary;
    }
    case AurUpdatePackageTransactionFailureCategory::CommandExecutionFailed:
        if(failure.exit_code.has_value()) {
            throw std::logic_error(
                    "AUR transaction process failure unexpectedly has an exit code.");
        }
        return "package transaction process exception";
    case AurUpdatePackageTransactionFailureCategory::Other:
        if(failure.exit_code.has_value()) {
            throw std::logic_error(
                    "AUR unknown transaction failure unexpectedly has an exit code.");
        }
        return "package transaction unknown exception";
    }
    throw std::logic_error("Unknown AUR package transaction failure category.");
}

std::string failure_detail_summary(
        AurUpdateWorkItemFailureKind kind,
        const AurUpdateWorkItemFailureDetail* detail) {
    const bool has_typed_detail = detail != nullptr &&
            !std::holds_alternative<std::monostate>(*detail);
    switch(kind) {
    case AurUpdateWorkItemFailureKind::None:
        if(has_typed_detail) {
            throw std::logic_error(
                    "Successful AUR work item unexpectedly has failure detail.");
        }
        return "none";
    case AurUpdateWorkItemFailureKind::CleanupFailedAfterPackageTransaction:
        if(has_typed_detail) {
            throw std::logic_error(
                    "AUR cleanup failure has unexpected typed failure detail.");
        }
        return "cleanup failure after successful package transaction";
    case AurUpdateWorkItemFailureKind::UnknownException:
        if(has_typed_detail) {
            throw std::logic_error(
                    "Unknown AUR failure has unexpected typed failure detail.");
        }
        return "unknown exception";
    case AurUpdateWorkItemFailureKind::PriorWorkItemStopped:
        if(has_typed_detail) {
            throw std::logic_error(
                    "NotAttempted AUR work item has unexpected failure detail.");
        }
        return "prior work item stopped";
    case AurUpdateWorkItemFailureKind::BuildOrInstallFailed:
        break;
    default:
        throw std::logic_error("Unknown AUR work-item failure kind.");
    }

    if(detail == nullptr) return "build or install failure";
    return std::visit(
            [](const auto& failure) -> std::string {
                using Failure = std::decay_t<decltype(failure)>;
                if constexpr(std::is_same_v<Failure, std::monostate>) {
                    return "build or install failure";
                } else if constexpr(std::is_same_v<
                                            Failure,
                                            PackageBaseArtifactIdentitySelectionFailure>) {
                    return "artifact selection failure";
                } else if constexpr(std::is_same_v<
                                            Failure,
                                            MixedPackageBaseInstallReasonUnsupported>) {
                    return "mixed install reason unsupported";
                } else if constexpr(std::is_same_v<Failure, PackageMetadataFailure>) {
                    return "package metadata failure (" +
                            std::string(metadata_error_label(failure.code)) + ")";
                } else if constexpr(std::is_same_v<
                                            Failure,
                                            AurUpdateSourceBuildFailureSnapshot>) {
                    return std::string(
                            source_build_failure_label(failure.category));
                } else if constexpr(std::is_same_v<
                                            Failure,
                                            AurUpdatePackageTransactionFailureSnapshot>) {
                    return transaction_failure_summary(failure);
                } else if constexpr(std::is_same_v<
                                            Failure,
                                            AurUpdateExecutionCorrelationFailure>) {
                    return "result correlation failure (" +
                            std::string(
                                    correlation_failure_label(failure.reason)) +
                            ")";
                }
            },
            *detail);
}

bool is_known_work_item_status(
        AurUpdateWorkItemExecutionStatus status) noexcept {
    switch(status) {
    case AurUpdateWorkItemExecutionStatus::Updated:
    case AurUpdateWorkItemExecutionStatus::NoChange:
    case AurUpdateWorkItemExecutionStatus::Failed:
    case AurUpdateWorkItemExecutionStatus::UpdatedCleanupFailed:
    case AurUpdateWorkItemExecutionStatus::NoChangeCleanupFailed:
    case AurUpdateWorkItemExecutionStatus::NotAttempted:
        return true;
    }
    return false;
}

bool is_known_child_status(AurUpdateChildExecutionStatus status) noexcept {
    switch(status) {
    case AurUpdateChildExecutionStatus::Installed:
    case AurUpdateChildExecutionStatus::SkippedAsNeeded:
    case AurUpdateChildExecutionStatus::InstalledCleanupFailed:
    case AurUpdateChildExecutionStatus::SkippedAsNeededCleanupFailed:
    case AurUpdateChildExecutionStatus::NotAttempted:
        return true;
    }
    return false;
}

bool is_selected_child_status(AurUpdateChildExecutionStatus status) noexcept {
    return status == AurUpdateChildExecutionStatus::Installed ||
           status == AurUpdateChildExecutionStatus::SkippedAsNeeded ||
           status == AurUpdateChildExecutionStatus::InstalledCleanupFailed ||
           status ==
                   AurUpdateChildExecutionStatus::SkippedAsNeededCleanupFailed;
}

bool child_status_matches_work_item(
        AurUpdateWorkItemExecutionStatus work_item_status,
        AurUpdateChildExecutionStatus child_status) noexcept {
    switch(work_item_status) {
    case AurUpdateWorkItemExecutionStatus::Updated:
        return child_status == AurUpdateChildExecutionStatus::Installed ||
               child_status == AurUpdateChildExecutionStatus::SkippedAsNeeded;
    case AurUpdateWorkItemExecutionStatus::NoChange:
        return child_status == AurUpdateChildExecutionStatus::SkippedAsNeeded;
    case AurUpdateWorkItemExecutionStatus::Failed:
    case AurUpdateWorkItemExecutionStatus::NotAttempted:
        return child_status == AurUpdateChildExecutionStatus::NotAttempted;
    case AurUpdateWorkItemExecutionStatus::UpdatedCleanupFailed:
        return child_status ==
                       AurUpdateChildExecutionStatus::InstalledCleanupFailed ||
               child_status == AurUpdateChildExecutionStatus::
                                       SkippedAsNeededCleanupFailed;
    case AurUpdateWorkItemExecutionStatus::NoChangeCleanupFailed:
        return child_status == AurUpdateChildExecutionStatus::
                                       SkippedAsNeededCleanupFailed;
    }
    return false;
}

bool failure_kind_matches_work_item(
        AurUpdateWorkItemExecutionStatus status,
        AurUpdateWorkItemFailureKind kind) noexcept {
    switch(status) {
    case AurUpdateWorkItemExecutionStatus::Updated:
    case AurUpdateWorkItemExecutionStatus::NoChange:
        return kind == AurUpdateWorkItemFailureKind::None;
    case AurUpdateWorkItemExecutionStatus::Failed:
        return kind == AurUpdateWorkItemFailureKind::BuildOrInstallFailed ||
               kind == AurUpdateWorkItemFailureKind::UnknownException;
    case AurUpdateWorkItemExecutionStatus::UpdatedCleanupFailed:
    case AurUpdateWorkItemExecutionStatus::NoChangeCleanupFailed:
        return kind == AurUpdateWorkItemFailureKind::
                               CleanupFailedAfterPackageTransaction;
    case AurUpdateWorkItemExecutionStatus::NotAttempted:
        return kind == AurUpdateWorkItemFailureKind::PriorWorkItemStopped;
    }
    return false;
}

void require_valid_identity(
        const ArtifactPackageIdentity& identity,
        std::string_view context) {
    if(identity.package_name.empty() || identity.full_version.empty()) {
        throw std::logic_error(
                std::string(context) + " has an incomplete package identity.");
    }
}

bool transaction_snapshots_match(
        const AurUpdatePackageTransactionFailureSnapshot& left,
        const AurUpdatePackageTransactionFailureSnapshot& right) noexcept {
    if(left.category != right.category || left.exit_code != right.exit_code ||
       left.attempted_artifacts.size() != right.attempted_artifacts.size()) {
        return false;
    }
    for(std::size_t index = 0; index < left.attempted_artifacts.size();
        ++index) {
        const AurUpdatePackageTransactionAttempt& left_attempt =
                left.attempted_artifacts[index];
        const AurUpdatePackageTransactionAttempt& right_attempt =
                right.attempted_artifacts[index];
        if(left_attempt.identity.package_name !=
                   right_attempt.identity.package_name ||
           left_attempt.identity.full_version !=
                   right_attempt.identity.full_version ||
           left_attempt.desired_reason != right_attempt.desired_reason) {
            return false;
        }
    }
    return true;
}

void require_coherent_work_item(
        const AurUpdateWorkItemExecutionResult& work_item) {
    if(!is_known_work_item_status(work_item.status)) {
        throw std::logic_error("Unknown AUR work-item execution status.");
    }
    if(work_item.package_base.empty() || work_item.child_results.empty() ||
       work_item.child_results.size() != work_item.plan_package_names.size() ||
       !failure_kind_matches_work_item(work_item.status, work_item.failure_kind)) {
        throw std::logic_error("AUR work-item presentation snapshot is incoherent.");
    }
    if(work_item.child_results.size() == 1) {
        if(work_item.package_name !=
           work_item.child_results.front().required_package_name) {
            throw std::logic_error(
                    "AUR singular work-item package identity is incoherent.");
        }
    } else if(!work_item.package_name.empty()) {
        throw std::logic_error(
                "AUR multiple-child work item retained a singular package name.");
    }

    std::set<std::string> selected_names;
    bool has_installed_child = false;
    for(std::size_t index = 0; index < work_item.child_results.size(); ++index) {
        const AurUpdateChildExecutionResult& child =
                work_item.child_results[index];
        if(!is_known_child_status(child.status)) {
            throw std::logic_error("Unknown AUR child execution status.");
        }
        if(child.work_item_index != work_item.work_item_index ||
           child.build_plan_order_index != work_item.build_plan_order_index ||
           child.required_child_index != index ||
           child.package_base != work_item.package_base ||
           child.required_package_name.empty() ||
           child.required_package_name != work_item.plan_package_names[index] ||
           !is_known_install_reason(child.desired_install_reason) ||
           !child_status_matches_work_item(work_item.status, child.status)) {
            throw std::logic_error(
                    "AUR child presentation snapshot is incoherent.");
        }

        if(is_selected_child_status(child.status)) {
            if(!child.selected_artifact.has_value()) {
                throw std::logic_error(
                        "Completed AUR child has no selected artifact identity.");
            }
            require_valid_identity(
                    *child.selected_artifact, "Selected AUR artifact");
            if(child.selected_artifact->package_name !=
                       child.required_package_name ||
               !selected_names.insert(
                                      child.selected_artifact->package_name)
                        .second) {
                throw std::logic_error(
                        "Selected AUR child artifact identity is incoherent.");
            }
        } else if(child.selected_artifact.has_value()) {
            throw std::logic_error(
                    "Uncompleted AUR child unexpectedly has a selected artifact.");
        }
        has_installed_child = has_installed_child ||
                child.status == AurUpdateChildExecutionStatus::Installed ||
                child.status ==
                        AurUpdateChildExecutionStatus::InstalledCleanupFailed;
    }

    if((work_item.status == AurUpdateWorkItemExecutionStatus::Updated ||
        work_item.status ==
                AurUpdateWorkItemExecutionStatus::UpdatedCleanupFailed) &&
       !has_installed_child) {
        throw std::logic_error(
                "Updated AUR work item has no installed child outcome.");
    }

    std::set<std::string> unselected_names;
    for(const ArtifactPackageIdentity& identity :
        work_item.unselected_artifacts) {
        require_valid_identity(identity, "Unselected AUR artifact");
        if(selected_names.contains(identity.package_name) ||
           !unselected_names.insert(identity.package_name).second) {
            throw std::logic_error(
                    "Unselected AUR artifact identity is incoherent.");
        }
    }
    if((work_item.status == AurUpdateWorkItemExecutionStatus::Failed ||
        work_item.status == AurUpdateWorkItemExecutionStatus::NotAttempted) &&
       !work_item.unselected_artifacts.empty()) {
        throw std::logic_error(
                "Uncompleted AUR work item retained unselected artifacts.");
    }

    const auto* transaction_detail = std::get_if<
            AurUpdatePackageTransactionFailureSnapshot>(
            &work_item.failure_detail);
    if(work_item.transaction_failure.has_value()) {
        if(work_item.status != AurUpdateWorkItemExecutionStatus::Failed) {
            throw std::logic_error(
                    "Non-failed AUR work item retained transaction failure evidence.");
        }
        static_cast<void>(transaction_failure_summary(
                *work_item.transaction_failure));
        for(const AurUpdatePackageTransactionAttempt& attempt :
            work_item.transaction_failure->attempted_artifacts) {
            require_valid_identity(attempt.identity, "AUR transaction attempt");
            static_cast<void>(install_reason_label(attempt.desired_reason));
        }
        if(transaction_detail != nullptr) {
            if(!transaction_snapshots_match(
                       *transaction_detail, *work_item.transaction_failure)) {
                throw std::logic_error(
                        "AUR transaction failure snapshots are inconsistent.");
            }
        } else if(!std::holds_alternative<
                          AurUpdateExecutionCorrelationFailure>(
                          work_item.failure_detail)) {
            throw std::logic_error(
                    "AUR transaction evidence has no typed transaction or correlation failure.");
        }
    } else if(transaction_detail != nullptr) {
        throw std::logic_error(
                "AUR transaction failure detail has no attempt evidence.");
    }

    static_cast<void>(failure_detail_summary(
            work_item.failure_kind, &work_item.failure_detail));
}

bool is_ordinary_singular_success(
        const AurUpdateWorkItemExecutionResult& work_item) noexcept {
    if(work_item.child_results.size() != 1 ||
       !work_item.unselected_artifacts.empty() ||
       work_item.failure_kind != AurUpdateWorkItemFailureKind::None ||
       (work_item.status != AurUpdateWorkItemExecutionStatus::Updated &&
        work_item.status != AurUpdateWorkItemExecutionStatus::NoChange)) {
        return false;
    }
    const AurUpdateChildExecutionResult& child =
            work_item.child_results.front();
    return work_item.package_base == child.required_package_name &&
           (child.status == AurUpdateChildExecutionStatus::Installed ||
            child.status == AurUpdateChildExecutionStatus::SkippedAsNeeded);
}

std::string_view child_outcome_label(
        const AurUpdateWorkItemExecutionResult& work_item,
        AurUpdateChildExecutionStatus status) {
    switch(status) {
    case AurUpdateChildExecutionStatus::Installed:
        return "installed / updated";
    case AurUpdateChildExecutionStatus::SkippedAsNeeded:
        return "skipped as needed / no change";
    case AurUpdateChildExecutionStatus::InstalledCleanupFailed:
        return "installed / updated, but cleanup failed";
    case AurUpdateChildExecutionStatus::SkippedAsNeededCleanupFailed:
        return "skipped as needed / no change, but cleanup failed";
    case AurUpdateChildExecutionStatus::NotAttempted:
        return work_item.status == AurUpdateWorkItemExecutionStatus::NotAttempted
                ? "not attempted: prior work item stopped"
                : "no successful outcome";
    }
    throw std::logic_error("Unknown AUR child execution status.");
}

std::string child_summary_line(
        const AurUpdateWorkItemExecutionResult& work_item,
        const AurUpdateChildExecutionResult& child) {
    std::string line = "  required child: " + child.required_package_name;
    if(child.selected_artifact.has_value()) {
        line += " -> " + child.selected_artifact->package_name + " " +
                child.selected_artifact->full_version;
    }
    line += " (" +
            std::string(install_reason_label(child.desired_install_reason)) +
            "): " + std::string(child_outcome_label(work_item, child.status));
    return line;
}

bool should_print_failure(AurUpdateWorkItemFailureKind kind) {
    switch(kind) {
    case AurUpdateWorkItemFailureKind::None:
    case AurUpdateWorkItemFailureKind::PriorWorkItemStopped:
        return false;
    case AurUpdateWorkItemFailureKind::BuildOrInstallFailed:
    case AurUpdateWorkItemFailureKind::CleanupFailedAfterPackageTransaction:
    case AurUpdateWorkItemFailureKind::UnknownException:
        return true;
    }
    throw std::logic_error("Unknown AUR work-item failure kind.");
}

void append_work_item_presentation(
        AurUpdateCliPresentation& presentation,
        const AurUpdateWorkItemExecutionResult& work_item) {
    require_coherent_work_item(work_item);
    if(!is_ordinary_singular_success(work_item)) {
        presentation.summary_lines.push_back(
                "PackageBase result: " + work_item.package_base);
        for(const AurUpdateChildExecutionResult& child :
            work_item.child_results) {
            presentation.summary_lines.push_back(
                    child_summary_line(work_item, child));
        }
        for(const ArtifactPackageIdentity& identity :
            work_item.unselected_artifacts) {
            presentation.summary_lines.push_back(
                    "  produced artifact: " + identity.package_name + " " +
                    identity.full_version +
                    " (not selected; not installed)");
        }
    }

    if(!should_print_failure(work_item.failure_kind)) return;
    presentation.error_lines.push_back(
            "  execution failure for PackageBase " + work_item.package_base +
            ": " + failure_detail_summary(
                              work_item.failure_kind,
                              &work_item.failure_detail));
    if(!work_item.transaction_failure.has_value()) return;

    const bool detail_is_transaction = std::holds_alternative<
            AurUpdatePackageTransactionFailureSnapshot>(
            work_item.failure_detail);
    if(!detail_is_transaction) {
        presentation.error_lines.push_back(
                "    package transaction evidence: " +
                transaction_failure_summary(*work_item.transaction_failure));
    }
    for(const AurUpdatePackageTransactionAttempt& attempt :
        work_item.transaction_failure->attempted_artifacts) {
        presentation.error_lines.push_back(
                "    transaction attempt: " + attempt.identity.package_name +
                " " + attempt.identity.full_version + " (" +
                std::string(install_reason_label(attempt.desired_reason)) +
                ")");
    }
}

} // namespace

std::string aur_update_cli_target_failure_summary(
        const AurUpdateOperationTargetResult& target) {
    if(!target.execution_failure_kind.has_value() ||
       *target.execution_failure_kind == AurUpdateWorkItemFailureKind::None) {
        return "failure category unavailable";
    }
    const AurUpdateWorkItemFailureDetail* detail =
            target.execution_failure_detail.has_value()
            ? &*target.execution_failure_detail
            : nullptr;
    return failure_detail_summary(*target.execution_failure_kind, detail);
}

AurUpdateCliPresentation format_aur_update_cli_presentation(
        const AurUpdateOperationResult& result) {
    AurUpdateCliPresentation presentation;
    std::set<std::size_t> presented_failure_work_items;
    for(const AurUpdateWorkItemExecutionResult& work_item :
        result.execution_work_items) {
        append_work_item_presentation(presentation, work_item);
        if(should_print_failure(work_item.failure_kind)) {
            presented_failure_work_items.insert(work_item.work_item_index);
        }
    }

    // Correlationが壊れwork-item snapshotを失ったdefensive resultでも、targetの
    // typed decisive failureだけはraw diagnosticなしで一度表示する。
    for(const AurUpdateOperationTargetResult& target : result.targets) {
        if(!target.execution_failure_kind.has_value() ||
           !should_print_failure(*target.execution_failure_kind)) {
            continue;
        }
        if(target.execution_work_item_index.has_value() &&
           presented_failure_work_items.contains(
                   *target.execution_work_item_index)) {
            continue;
        }
        if(target.execution_work_item_index.has_value()) {
            presented_failure_work_items.insert(
                    *target.execution_work_item_index);
        }
        presentation.error_lines.push_back(
                "  execution failure: " +
                aur_update_cli_target_failure_summary(target));
    }
    return presentation;
}
