#include "package_base_artifact_install_plan.hpp"

#include "package_identifier.hpp"

#include <cstddef>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

// NO_TRANSLATE(Issue #308): logic_error text in this translation unit reports
// internal policy/reducer invariant violations, not user-facing policy failures.
#ifdef MOGUET_ENABLE_PACKAGE_BASE_ARTIFACT_INSTALL_PLAN_TEST_HOOKS
PackageBaseArtifactInstallReasonPlanObserverForTest
    g_reason_plan_observer = nullptr;

void notify_reason_plan_observer_for_test() {
    if(g_reason_plan_observer != nullptr) g_reason_plan_observer();
}
#else
void notify_reason_plan_observer_for_test() {
}
#endif

enum class SafeFinalInstallReason {
    Explicit,
    Dependency
};

struct ResolvedReasonPolicyItem {
    SelectedPackageBaseArtifactInstallReasonPolicyInput input;
    InstallReasonDirective directive;
    SafeFinalInstallReason final_reason;
};

SafeFinalInstallReason resolve_safe_final_install_reason(
    const SelectedPackageBaseArtifactInstallReasonPolicyInput& input,
    InstallReasonDirective directive) {
    switch(directive) {
        case InstallReasonDirective::AsExplicit:
            return SafeFinalInstallReason::Explicit;
        case InstallReasonDirective::AsDependency:
            return SafeFinalInstallReason::Dependency;
        case InstallReasonDirective::Default:
            break;
        default:
            throw std::logic_error("Unknown install reason directive.");
    }

    switch(input.installed_version_state) {
        case InstalledVersionState::NotInstalled:
            // pacman -Uの新規targetは、reason optionなしではexplicitになる。
            return SafeFinalInstallReason::Explicit;
        case InstalledVersionState::SameVersion:
        case InstalledVersionState::DifferentVersion:
            break;
        default:
            // 通常はこの前のsingular reducerで拒否されるが、ここでも閉じる。
            throw std::logic_error("Unknown installed version state.");
    }

    if(!input.existing_reason.has_value()) {
        throw std::logic_error(
            "Installed package must have an existing install reason.");
    }
    switch(input.existing_reason.value()) {
        case ExistingInstallReason::Explicit:
            return SafeFinalInstallReason::Explicit;
        case ExistingInstallReason::Dependency:
            return SafeFinalInstallReason::Dependency;
        default:
            throw std::logic_error("Unknown existing install reason.");
    }
}

void require_valid_policy_identity(
    const ArtifactPackageIdentity& identity) {
    if(!is_valid_package_name(identity.package_name)) {
        throw std::logic_error(
            "PackageBase install policy has an invalid package name.");
    }
    if(identity.full_version.empty()) {
        throw std::logic_error(
            "PackageBase install policy has an empty package version.");
    }
}

std::optional<InstallReasonDirective> reduce_transaction_directive(
    const std::vector<ResolvedReasonPolicyItem>& items) {
    bool all_default = true;
    bool all_explicit = true;
    bool all_dependency = true;
    for(const ResolvedReasonPolicyItem& item : items) {
        if(item.directive != InstallReasonDirective::Default) {
            all_default = false;
        }
        if(item.final_reason != SafeFinalInstallReason::Explicit) {
            all_explicit = false;
        }
        if(item.final_reason != SafeFinalInstallReason::Dependency) {
            all_dependency = false;
        }
    }

    if(all_default) return InstallReasonDirective::Default;
    if(all_explicit) return InstallReasonDirective::AsExplicit;
    if(all_dependency) return InstallReasonDirective::AsDependency;
    return std::nullopt;
}

bool transaction_directive_preserves_final_reason(
    InstallReasonDirective transaction_directive,
    SafeFinalInstallReason final_reason) {
    switch(transaction_directive) {
        case InstallReasonDirective::Default:
            return true;
        case InstallReasonDirective::AsExplicit:
            return final_reason == SafeFinalInstallReason::Explicit;
        case InstallReasonDirective::AsDependency:
            return final_reason == SafeFinalInstallReason::Dependency;
        default:
            throw std::logic_error("Unknown transaction install reason directive.");
    }
}

PackageBaseArtifactInstallExpectedOutcome resolve_expected_outcome(
    const ResolvedReasonPolicyItem& item,
    InstallReasonDirective transaction_directive,
    bool needed) {
    if(!needed ||
       item.input.installed_version_state !=
           InstalledVersionState::SameVersion ||
       item.directive != InstallReasonDirective::Default) {
        return PackageBaseArtifactInstallExpectedOutcome::Installed;
    }

    // POLICY(#268): global reason optionがDefault itemにも作用してよいのは、
    // skipされても既存reasonがtransaction後のsafe final reasonと一致するときだけ。
    if(!transaction_directive_preserves_final_reason(
           transaction_directive, item.final_reason)) {
        throw std::logic_error(
            "Transaction install reason would change a same-version skipped package.");
    }
    return PackageBaseArtifactInstallExpectedOutcome::SkippedAsNeeded;
}

} // namespace

PackageBaseArtifactInstallReasonPlanResult::
    PackageBaseArtifactInstallReasonPlanResult(
        PackageBaseArtifactInstallReasonPlan plan)
    : outcome_(std::in_place_type<PackageBaseArtifactInstallReasonPlan>,
               std::move(plan)) {
}

PackageBaseArtifactInstallReasonPlanResult::
    PackageBaseArtifactInstallReasonPlanResult(
        MixedPackageBaseInstallReasonUnsupported failure)
    : outcome_(
          std::in_place_type<MixedPackageBaseInstallReasonUnsupported>,
          std::move(failure)) {
}

bool PackageBaseArtifactInstallReasonPlanResult::is_success() const noexcept {
    return std::holds_alternative<PackageBaseArtifactInstallReasonPlan>(
        outcome_);
}

const PackageBaseArtifactInstallReasonPlan*
PackageBaseArtifactInstallReasonPlanResult::success() const noexcept {
    return std::get_if<PackageBaseArtifactInstallReasonPlan>(&outcome_);
}

const MixedPackageBaseInstallReasonUnsupported*
PackageBaseArtifactInstallReasonPlanResult::failure() const noexcept {
    return std::get_if<MixedPackageBaseInstallReasonUnsupported>(&outcome_);
}

PackageBaseArtifactInstallReasonPlanResult
resolve_package_base_artifact_install_reason_plan(
    const PackageBaseArtifactInstallReasonPolicyInput& input) {
    notify_reason_plan_observer_for_test();
    if(!is_valid_package_name(input.package_base)) {
        throw std::logic_error(
            "PackageBase install policy has an invalid PackageBase.");
    }
    if(input.selected_artifacts.empty()) {
        throw std::logic_error(
            "PackageBase install policy requires at least one selected artifact.");
    }

    std::vector<ResolvedReasonPolicyItem> resolved_items;
    resolved_items.reserve(input.selected_artifacts.size());
    for(const SelectedPackageBaseArtifactInstallReasonPolicyInput& item :
        input.selected_artifacts) {
        require_valid_policy_identity(item.identity);
        InstallReasonDirective directive = resolve_install_reason_directive(
            item.desired_reason, item.installed_version_state,
            item.existing_reason, input.needed);
        SafeFinalInstallReason final_reason =
            resolve_safe_final_install_reason(item, directive);
        resolved_items.push_back(
            ResolvedReasonPolicyItem{item, directive, final_reason});
    }

    std::optional<InstallReasonDirective> transaction_directive =
        reduce_transaction_directive(resolved_items);
    if(!transaction_directive.has_value()) {
        MixedPackageBaseInstallReasonUnsupported failure;
        failure.package_base = input.package_base;
        failure.selected_artifacts.reserve(resolved_items.size());
        for(const ResolvedReasonPolicyItem& item : resolved_items) {
            failure.selected_artifacts.push_back(
                MixedPackageBaseInstallReasonArtifact{
                    item.input.identity,
                    item.input.desired_reason,
                    item.input.installed_version_state,
                    item.input.existing_reason,
                    item.directive});
        }
        return PackageBaseArtifactInstallReasonPlanResult(
            std::move(failure));
    }

    PackageBaseArtifactInstallReasonPlan plan;
    plan.package_base = input.package_base;
    plan.transaction_directive = transaction_directive.value();
    plan.needed = input.needed;
    plan.selected_artifacts.reserve(resolved_items.size());
    for(const ResolvedReasonPolicyItem& item : resolved_items) {
        plan.selected_artifacts.push_back(
            PlannedPackageBaseArtifactInstallReason{
                item.input.identity,
                item.input.desired_reason,
                item.input.installed_version_state,
                item.input.existing_reason,
                item.directive,
                resolve_expected_outcome(
                    item, transaction_directive.value(),
                    input.needed)});
    }

    return PackageBaseArtifactInstallReasonPlanResult(std::move(plan));
}

#ifdef MOGUET_ENABLE_PACKAGE_BASE_ARTIFACT_INSTALL_PLAN_TEST_HOOKS
void set_package_base_artifact_install_reason_plan_observer_for_test(
    PackageBaseArtifactInstallReasonPlanObserverForTest observer) {
    g_reason_plan_observer = observer;
}
#endif
