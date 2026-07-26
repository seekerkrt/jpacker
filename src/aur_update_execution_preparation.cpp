#include "aur_update_execution_preparation.hpp"

#include "app_config.hpp"
#include "package_identifier.hpp"

#include <algorithm>
#include <exception>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

// AUR update preflightをmutation-freeなproduction invocationへ射影する。
// POLICY(#267): source preference、static work item、Pacman DB snapshotまでを所有し、
// checkout/build/installやCLI表示へ接続しない。
namespace {

const std::string AUR_BASE_URL = "https://aur.archlinux.org/";

struct ExecutableRootBinding {
    const AurUpdateExecutionTarget* target = nullptr;
    std::size_t                     root_index = 0;
    RootTargetIdentity              root;
};

struct UpdateWorkItemDraft {
    ProductionSourceBuildWorkItem work_item;
    std::vector<std::size_t>      affected_update_plan_indices;
    std::vector<RootTargetIdentity> affected_roots;
};

template<typename Value>
void add_unique(std::vector<Value>& values, const Value& value) {
    if(std::find(values.begin(), values.end(), value) == values.end()) {
        values.push_back(value);
    }
}

bool is_blocking_status(AurUpdateExecutionTargetStatus status) noexcept {
    return status == AurUpdateExecutionTargetStatus::Unsupported ||
           status == AurUpdateExecutionTargetStatus::Incomplete;
}

bool is_known_status(AurUpdateExecutionTargetStatus status) noexcept {
    return status == AurUpdateExecutionTargetStatus::Executable ||
           status == AurUpdateExecutionTargetStatus::Skipped ||
           is_blocking_status(status);
}

bool is_normal_skipped_preflight_reason(
        AurUpdateExecutionReason reason) noexcept {
    return reason == AurUpdateExecutionReason::UpToDate ||
           reason == AurUpdateExecutionReason::NonAurForeign;
}

bool has_blocking_preflight_targets(
        const AurUpdateExecutionPreflight& preflight) noexcept {
    return std::any_of(
            preflight.targets.begin(), preflight.targets.end(),
            [](const AurUpdateExecutionTarget& target) {
                return is_blocking_status(target.status);
            });
}

bool has_executable_preflight_targets(
        const AurUpdateExecutionPreflight& preflight) noexcept {
    return std::any_of(
            preflight.targets.begin(), preflight.targets.end(),
            [](const AurUpdateExecutionTarget& target) {
                return target.status ==
                        AurUpdateExecutionTargetStatus::Executable;
            });
}

bool has_role(
        const PlannedPackageTarget& target, PackageRole expected_role) {
    return std::find(
                   target.roles.begin(), target.roles.end(), expected_role) !=
           target.roles.end();
}

bool is_dependency_role(PackageRole role) noexcept {
    return role == PackageRole::RuntimeDependency ||
           role == PackageRole::BuildDependency ||
           role == PackageRole::CheckDependency;
}

bool is_known_role(PackageRole role) noexcept {
    return role == PackageRole::Root || is_dependency_role(role);
}

AurUpdatePreparationIssue make_issue(
        AurUpdatePreparationReason reason,
        std::string diagnostic) {
    AurUpdatePreparationIssue issue;
    issue.reason = reason;
    issue.diagnostic = std::move(diagnostic);
    return issue;
}

void attribute_issue_to_target(
        AurUpdatePreparationIssue& issue,
        const AurUpdateExecutionTarget& target) {
    add_unique(
            issue.affected_update_plan_indices,
            target.update_plan_index);
}

void attribute_issue_to_draft(
        AurUpdatePreparationIssue& issue,
        const UpdateWorkItemDraft& draft) {
    for(const std::size_t update_plan_index :
        draft.affected_update_plan_indices) {
        add_unique(
                issue.affected_update_plan_indices,
                update_plan_index);
    }
    for(const auto& root : draft.affected_roots) {
        add_unique(issue.affected_roots, root);
    }
}

void attribute_issue_to_all_executable_targets(
        AurUpdatePreparationIssue& issue,
        const AurUpdateSourceBuildPreparation& preparation) {
    for(const auto& target : preparation.affected_update_targets) {
        add_unique(
                issue.affected_update_plan_indices,
                target.update_plan_index);
    }
    for(const auto& root : preparation.affected_roots) {
        add_unique(issue.affected_roots, root);
    }
}

const ExecutableRootBinding* find_root_binding(
        const std::vector<ExecutableRootBinding>& bindings,
        const RootTargetIdentity& root) {
    auto found = std::find_if(
            bindings.begin(), bindings.end(),
            [&root](const ExecutableRootBinding& binding) {
                return binding.root == root;
            });
    return found == bindings.end() ? nullptr : &(*found);
}

bool collect_exact_package_target_attribution(
        const PlannedPackageTarget& package_target,
        const std::vector<ExecutableRootBinding>& bindings,
        std::vector<std::size_t>& affected_update_plan_indices,
        std::vector<RootTargetIdentity>& affected_roots) {
    if(package_target.roots.empty()) return false;

    for(const auto& root : package_target.roots) {
        if(std::find(affected_roots.begin(), affected_roots.end(), root) !=
           affected_roots.end()) {
            return false;
        }
        const ExecutableRootBinding* binding =
                find_root_binding(bindings, root);
        if(binding == nullptr) return false;

        add_unique(
                affected_update_plan_indices,
                binding->target->update_plan_index);
        add_unique(affected_roots, binding->root);
    }
    return true;
}

bool target_contains_root(
        const PlannedPackageTarget& target,
        const RootTargetIdentity& root) {
    return std::find(target.roots.begin(), target.roots.end(), root) !=
           target.roots.end();
}

void retain_preflight_blockers(
        const AurUpdateExecutionPreflight& preflight,
        AurUpdateSourceBuildPreparation& preparation) {
    for(const auto& target : preflight.targets) {
        if(is_known_status(target.status) &&
           !is_blocking_status(target.status)) {
            continue;
        }

        preparation.affected_update_targets.push_back(target);
        bool retained_typed_issue = false;
        for(const auto& preflight_issue : target.issues) {
            if(preflight_issue.reason == AurUpdateExecutionReason::None) continue;

            AurUpdatePreparationIssue issue = make_issue(
                    AurUpdatePreparationReason::BlockingPreflight,
                    preflight_issue.diagnostic);
            attribute_issue_to_target(issue, target);
            issue.preflight_issue = preflight_issue;
            issue.package_name = preflight_issue.package_name;
            issue.package_base = preflight_issue.package_base;
            preparation.issues.push_back(std::move(issue));
            retained_typed_issue = true;
        }

        if(retained_typed_issue && is_known_status(target.status)) continue;

        AurUpdatePreparationIssue issue = make_issue(
                AurUpdatePreparationReason::PreflightInconsistent,
                is_known_status(target.status)
                        ? "Blocking AUR update preflight target has no typed issue."
                        : "AUR update preflight target has an unknown status.");
        attribute_issue_to_target(issue, target);
        issue.package_name = target.update.installed_name;
        preparation.issues.push_back(std::move(issue));
    }
}

void retain_executable_preflight_inconsistencies(
        const AurUpdateExecutionPreflight& preflight,
        AurUpdateSourceBuildPreparation& preparation) {
    for(const auto& target : preflight.targets) {
        if(target.status != AurUpdateExecutionTargetStatus::Executable) continue;

        bool retained_target = false;
        for(const auto& preflight_issue : target.issues) {
            if(preflight_issue.reason == AurUpdateExecutionReason::None) continue;
            if(!retained_target) {
                preparation.affected_update_targets.push_back(target);
                retained_target = true;
            }

            AurUpdatePreparationIssue issue = make_issue(
                    AurUpdatePreparationReason::PreflightInconsistent,
                    "Executable AUR update target retains a blocking preflight issue: " +
                            preflight_issue.diagnostic);
            attribute_issue_to_target(issue, target);
            issue.preflight_issue = preflight_issue;
            issue.package_name = preflight_issue.package_name;
            issue.package_base = preflight_issue.package_base;
            preparation.issues.push_back(std::move(issue));
        }
    }
}

void retain_skipped_preflight_inconsistencies(
        const AurUpdateExecutionPreflight& preflight,
        AurUpdateSourceBuildPreparation& preparation) {
    for(const auto& target : preflight.targets) {
        if(target.status != AurUpdateExecutionTargetStatus::Skipped) continue;

        bool has_normal_skip_issue = false;
        bool retained_target = false;
        for(const auto& preflight_issue : target.issues) {
            if(preflight_issue.reason == AurUpdateExecutionReason::None) continue;
            if(is_normal_skipped_preflight_reason(preflight_issue.reason)) {
                has_normal_skip_issue = true;
                continue;
            }

            if(!retained_target) {
                preparation.affected_update_targets.push_back(target);
                retained_target = true;
            }
            AurUpdatePreparationIssue issue = make_issue(
                    AurUpdatePreparationReason::PreflightInconsistent,
                    "Skipped AUR update target retains a non-skip preflight issue: " +
                            preflight_issue.diagnostic);
            attribute_issue_to_target(issue, target);
            issue.preflight_issue = preflight_issue;
            issue.package_name = preflight_issue.package_name;
            issue.package_base = preflight_issue.package_base;
            preparation.issues.push_back(std::move(issue));
        }

        if(has_normal_skip_issue || retained_target) continue;

        // POLICY(#267): reasonのないSkipped snapshotをnormal no-opとして受理しない。
        preparation.affected_update_targets.push_back(target);
        AurUpdatePreparationIssue issue = make_issue(
                AurUpdatePreparationReason::PreflightInconsistent,
                "Skipped AUR update target has no normal skip preflight issue.");
        attribute_issue_to_target(issue, target);
        issue.package_name = target.update.installed_name;
        preparation.issues.push_back(std::move(issue));
    }
}

bool collect_executable_root_bindings(
        const AurUpdateExecutionPreflight& preflight,
        const BuildPlan& plan,
        AurUpdateSourceBuildPreparation& preparation,
        std::vector<ExecutableRootBinding>& bindings) {
    std::vector<const AurUpdateExecutionTarget*> executable_targets;
    for(std::size_t target_index = 0;
        target_index < preflight.targets.size(); ++target_index) {
        const AurUpdateExecutionTarget& target =
                preflight.targets[target_index];
        if(target.status != AurUpdateExecutionTargetStatus::Executable) continue;
        executable_targets.push_back(&target);
        preparation.affected_update_targets.push_back(target);
        if(target.update_plan_index != target_index) {
            AurUpdatePreparationIssue issue = make_issue(
                    AurUpdatePreparationReason::RootAttributionInconsistent,
                    "Executable AUR update target index differs from its position in the preflight snapshot.");
            attribute_issue_to_target(issue, target);
            issue.package_name = target.update.installed_name;
            preparation.issues.push_back(std::move(issue));
        }
    }

    if(plan.root_targets.size() != executable_targets.size()) {
        AurUpdatePreparationIssue issue = make_issue(
                AurUpdatePreparationReason::RootAttributionInconsistent,
                "BuildPlan root count does not match the executable update target count.");
        attribute_issue_to_all_executable_targets(issue, preparation);
        preparation.issues.push_back(std::move(issue));
    }

    std::set<std::size_t> seen_update_plan_indices;
    std::set<std::size_t> seen_root_indices;
    for(const auto* target : executable_targets) {
        if(!seen_update_plan_indices.insert(target->update_plan_index).second) {
            AurUpdatePreparationIssue issue = make_issue(
                    AurUpdatePreparationReason::RootAttributionInconsistent,
                    "Executable AUR update targets contain a duplicate update plan index.");
            attribute_issue_to_target(issue, *target);
            issue.package_name = target->update.installed_name;
            preparation.issues.push_back(std::move(issue));
        }

        if(!target->build_plan_root_index.has_value()) {
            AurUpdatePreparationIssue issue = make_issue(
                    AurUpdatePreparationReason::RootAttributionInconsistent,
                    "Executable AUR update target has no BuildPlan root index.");
            attribute_issue_to_target(issue, *target);
            issue.package_name = target->update.installed_name;
            preparation.issues.push_back(std::move(issue));
            continue;
        }

        const std::size_t root_index = *target->build_plan_root_index;
        if(root_index >= plan.root_targets.size()) {
            AurUpdatePreparationIssue issue = make_issue(
                    AurUpdatePreparationReason::RootAttributionInconsistent,
                    "Executable AUR update target refers to an out-of-range BuildPlan root index.");
            attribute_issue_to_target(issue, *target);
            issue.package_name = target->update.installed_name;
            preparation.issues.push_back(std::move(issue));
            continue;
        }
        if(!seen_root_indices.insert(root_index).second) {
            AurUpdatePreparationIssue issue = make_issue(
                    AurUpdatePreparationReason::RootAttributionInconsistent,
                    "Multiple executable AUR update targets refer to the same BuildPlan root index.");
            attribute_issue_to_target(issue, *target);
            issue.package_name = target->update.installed_name;
            preparation.issues.push_back(std::move(issue));
            continue;
        }

        const RootTargetIdentity& root = plan.root_targets[root_index];
        if(root.invocation_index != root_index ||
           root.requested_name != target->update.installed_name) {
            AurUpdatePreparationIssue issue = make_issue(
                    AurUpdatePreparationReason::RootAttributionInconsistent,
                    "BuildPlan root identity does not match its executable AUR update target.");
            attribute_issue_to_target(issue, *target);
            issue.affected_roots.push_back(root);
            issue.package_name = target->update.installed_name;
            preparation.issues.push_back(std::move(issue));
            continue;
        }

        if(!target->desired_install_reason.has_value()) {
            AurUpdatePreparationIssue issue = make_issue(
                    AurUpdatePreparationReason::DesiredInstallReasonMissing,
                    "Executable AUR update target has no desired install reason.");
            attribute_issue_to_target(issue, *target);
            issue.affected_roots.push_back(root);
            issue.package_name = target->update.installed_name;
            preparation.issues.push_back(std::move(issue));
        }

        bindings.push_back(ExecutableRootBinding{target, root_index, root});
    }

    if(seen_root_indices.size() != plan.root_targets.size()) {
        AurUpdatePreparationIssue issue = make_issue(
                AurUpdatePreparationReason::RootAttributionInconsistent,
                "BuildPlan contains a root that is not attributed to an executable AUR update target.");
        attribute_issue_to_all_executable_targets(issue, preparation);
        preparation.issues.push_back(std::move(issue));
    }

    // 解決できたrootは、別fieldのinconsistencyでblockしてもowned attributionに残す。
    for(const auto& root : plan.root_targets) {
        if(find_root_binding(bindings, root) != nullptr) {
            add_unique(preparation.affected_roots, root);
        }
    }
    if(!preparation.issues.empty()) return false;

    // root_targetsはcandidate順、affected_update_targetsは元update plan順の正本を保つ。
    return true;
}

bool require_root_package_attribution(
        const std::vector<ExecutableRootBinding>& bindings,
        const BuildPlan& plan,
        AurUpdateSourceBuildPreparation& preparation) {
    for(const auto& binding : bindings) {
        std::vector<const PlannedPackageTarget*> matching_targets;
        for(const auto& package_target : plan.package_targets) {
            if(package_target.package_name == binding.root.requested_name &&
               has_role(package_target, PackageRole::Root) &&
               target_contains_root(package_target, binding.root)) {
                matching_targets.push_back(&package_target);
            }
        }

        bool is_consistent = matching_targets.size() == 1 &&
                binding.target->update.aur_package.has_value();
        if(is_consistent) {
            const PlannedPackageTarget& package_target = *matching_targets.front();
            const AurUpdateRemotePackage& remote =
                    *binding.target->update.aur_package;
            is_consistent = remote.aur_name == package_target.package_name &&
                    remote.package_base == package_target.package_base;
        }
        if(is_consistent) continue;

        AurUpdatePreparationIssue issue = make_issue(
                AurUpdatePreparationReason::PackageTargetAttributionInconsistent,
                "Executable AUR update root does not have exactly one matching planned package target.");
        attribute_issue_to_target(issue, *binding.target);
        issue.affected_roots.push_back(binding.root);
        issue.package_name = binding.root.requested_name;
        preparation.issues.push_back(std::move(issue));
    }
    return preparation.issues.empty();
}

std::optional<DesiredInstallReason> desired_reason_for_package_target(
        const PlannedPackageTarget& package_target,
        const std::vector<ExecutableRootBinding>& bindings,
        AurUpdatePreparationIssue& inconsistency) {
    const ExecutableRootBinding* update_root = nullptr;
    for(const auto& binding : bindings) {
        if(binding.root.requested_name != package_target.package_name ||
           !target_contains_root(package_target, binding.root)) {
            continue;
        }
        if(update_root != nullptr) {
            inconsistency.diagnostic =
                    "Planned package target matches multiple executable update roots.";
            return std::nullopt;
        }
        update_root = &binding;
    }

    if(update_root != nullptr) {
        if(!has_role(package_target, PackageRole::Root) ||
           !update_root->target->desired_install_reason.has_value()) {
            inconsistency.diagnostic =
                    "Planned update root lost its Root role or desired install reason.";
            return std::nullopt;
        }
        // LANDMINE(#267): Root roleから再計算するとdependency-installed rootを
        // Explicitへ誤昇格する。installed reason由来のpreflight値だけを使う。
        return *update_root->target->desired_install_reason;
    }

    if(has_role(package_target, PackageRole::Root) ||
       !std::any_of(
               package_target.roles.begin(), package_target.roles.end(),
               is_dependency_role)) {
        inconsistency.diagnostic =
                "Non-root planned package target has no attributable dependency role.";
        return std::nullopt;
    }
    return DesiredInstallReason::Dependency;
}

bool collect_work_item_drafts(
        const BuildPlan& plan,
        const std::vector<ExecutableRootBinding>& bindings,
        AurUpdateSourceBuildPreparation& preparation,
        std::vector<UpdateWorkItemDraft>& drafts,
        bool needed) {
    std::vector<std::size_t> order_count_by_package_target(
            plan.package_targets.size(), 0);

    for(const auto& entry : plan.order) {
        if(!is_valid_package_name(entry.package_base) ||
           entry.package_names.size() != 1 ||
           !is_valid_package_name(entry.package_names.front())) {
            AurUpdatePreparationIssue issue = make_issue(
                    AurUpdatePreparationReason::PackageTargetAttributionInconsistent,
                    "BuildPlan execution entry does not contain exactly one valid package identity.");
            issue.package_base = entry.package_base;
            attribute_issue_to_all_executable_targets(issue, preparation);
            preparation.issues.push_back(std::move(issue));
            continue;
        }

        const std::string& package_name = entry.package_names.front();
        std::vector<std::size_t> matching_target_indices;
        for(std::size_t index = 0; index < plan.package_targets.size(); ++index) {
            const PlannedPackageTarget& target = plan.package_targets[index];
            if(target.package_name == package_name &&
               target.package_base == entry.package_base) {
                matching_target_indices.push_back(index);
            }
        }
        if(matching_target_indices.size() != 1) {
            AurUpdatePreparationIssue issue = make_issue(
                    AurUpdatePreparationReason::PackageTargetAttributionInconsistent,
                    "BuildPlan execution entry does not have exactly one planned package target.");
            issue.package_name = package_name;
            issue.package_base = entry.package_base;
            attribute_issue_to_all_executable_targets(issue, preparation);
            preparation.issues.push_back(std::move(issue));
            continue;
        }

        const std::size_t package_target_index = matching_target_indices.front();
        const PlannedPackageTarget& package_target =
                plan.package_targets[package_target_index];
        ++order_count_by_package_target[package_target_index];

        UpdateWorkItemDraft draft;
        const bool roots_are_attributed =
                collect_exact_package_target_attribution(
                        package_target, bindings,
                        draft.affected_update_plan_indices,
                        draft.affected_roots);

        const bool roles_are_known = !package_target.roles.empty() &&
                std::all_of(
                        package_target.roles.begin(),
                        package_target.roles.end(), is_known_role);
        AurUpdatePreparationIssue reason_issue = make_issue(
                AurUpdatePreparationReason::PackageTargetAttributionInconsistent,
                "Planned package target install reason is inconsistent.");
        reason_issue.package_name = package_name;
        reason_issue.package_base = entry.package_base;
        if(!roots_are_attributed) {
            // POLICY(#267): partialに解決できたrootを対象推測へ使わず、
            // exactに検証済みの全Executable rootへglobal attributionする。
            reason_issue.diagnostic =
                    "Planned package target roots cannot be attributed exactly to executable update targets.";
            attribute_issue_to_all_executable_targets(
                    reason_issue, preparation);
            preparation.issues.push_back(std::move(reason_issue));
            continue;
        }

        attribute_issue_to_draft(reason_issue, draft);
        std::optional<DesiredInstallReason> desired_reason;
        if(roles_are_known) {
            desired_reason = desired_reason_for_package_target(
                    package_target, bindings, reason_issue);
        }
        if(!roles_are_known || !desired_reason.has_value()) {
            if(!roles_are_known) {
                reason_issue.diagnostic =
                        "Planned package target contains no known package role.";
            }
            preparation.issues.push_back(std::move(reason_issue));
            continue;
        }

        draft.work_item.request.package_name = package_name;
        draft.work_item.request.checkout_name = entry.package_base;
        draft.work_item.request.git_url =
                AUR_BASE_URL + entry.package_base + ".git";
        draft.work_item.request.empty_value_policy =
                SourceEnvironmentEmptyValuePolicy::Omit;
        draft.work_item.request.needed = needed;
        draft.work_item.request.only_if_updated = false;
        draft.work_item.request.installed_snapshot = std::nullopt;
        draft.work_item.request.update_baseline = std::nullopt;
        draft.work_item.desired_reason = *desired_reason;
        draft.work_item.is_build_plan_entry = true;
        draft.work_item.uses_system_update_baseline = false;
        draft.work_item.plan_package_names = entry.package_names;
        drafts.push_back(std::move(draft));
    }

    for(std::size_t index = 0; index < order_count_by_package_target.size();
        ++index) {
        if(order_count_by_package_target[index] == 1) continue;

        const PlannedPackageTarget& package_target =
                plan.package_targets[index];
        AurUpdatePreparationIssue issue = make_issue(
                AurUpdatePreparationReason::PackageTargetAttributionInconsistent,
                "Planned package target must occur exactly once in BuildPlan execution order.");
        issue.package_name = package_target.package_name;
        issue.package_base = package_target.package_base;
        UpdateWorkItemDraft attribution;
        if(collect_exact_package_target_attribution(
                   package_target, bindings,
                   attribution.affected_update_plan_indices,
                   attribution.affected_roots)) {
            attribute_issue_to_draft(issue, attribution);
        } else {
            issue.diagnostic +=
                    " Its roots cannot be attributed exactly to executable update targets.";
            attribute_issue_to_all_executable_targets(issue, preparation);
        }
        preparation.issues.push_back(std::move(issue));
    }
    return preparation.issues.empty();
}

void retain_loaded_warnings(
        const std::string& preference_name,
        const SourcePreferenceLoaded& loaded,
        const UpdateWorkItemDraft& draft,
        AurUpdateSourceBuildPreparation& preparation) {
    for(const auto& diagnostic : loaded.warnings) {
        preparation.warnings.push_back(AurUpdatePreparationWarning{
                preference_name,
                loaded.entry_path,
                draft.affected_update_plan_indices,
                draft.affected_roots,
                diagnostic});
    }
}

std::optional<SourceBuildEnvironment> read_strict_environment(
        const std::string& preference_name,
        const UpdateWorkItemDraft& draft,
        AurUpdateSourceBuildPreparation& preparation) {
    StrictSourcePreferenceResult result;
    try {
        result = read_source_preference_strict(preference_name);
    } catch(const std::exception& error) {
        AurUpdatePreparationIssue issue = make_issue(
                AurUpdatePreparationReason::GenericPreparationInconsistent,
                "Strict source preference reader threw an unexpected exception: " +
                        std::string(error.what()));
        issue.package_name = preference_name;
        attribute_issue_to_draft(issue, draft);
        preparation.issues.push_back(std::move(issue));
        return std::nullopt;
    } catch(...) {
        AurUpdatePreparationIssue issue = make_issue(
                AurUpdatePreparationReason::GenericPreparationInconsistent,
                "Strict source preference reader threw an unknown exception.");
        issue.package_name = preference_name;
        attribute_issue_to_draft(issue, draft);
        preparation.issues.push_back(std::move(issue));
        return std::nullopt;
    }

    if(std::get_if<SourcePreferenceAbsent>(&result) != nullptr) {
        return SourceBuildEnvironment{};
    }
    if(const auto* loaded = std::get_if<SourcePreferenceLoaded>(&result)) {
        retain_loaded_warnings(
                preference_name, *loaded, draft, preparation);
        return loaded->environment;
    }

    const SourcePreferenceFailure& failure =
            std::get<SourcePreferenceFailure>(result);
    AurUpdatePreparationIssue issue = make_issue(
            AurUpdatePreparationReason::SourcePreferenceUnavailable,
            failure.diagnostic);
    issue.package_name = preference_name;
    issue.source_preference_failure = failure;
    attribute_issue_to_draft(issue, draft);
    preparation.issues.push_back(std::move(issue));
    return std::nullopt;
}

void consume_strict_source_preferences(
        std::vector<UpdateWorkItemDraft>& drafts,
        AurUpdateSourceBuildPreparation& preparation) {
    for(auto& draft : drafts) {
        const std::string& package_name =
                draft.work_item.request.package_name;
        const std::string& package_base =
                draft.work_item.request.checkout_name;

        std::optional<SourceBuildEnvironment> selected_environment =
                read_strict_environment(
                        package_name, draft, preparation);
        if(!selected_environment.has_value()) continue;

        // POLICY(#242,#267): fallback eligibilityはforward可能なnonempty assignment、
        // PKGDEST definition、requested/Base identityの3条件を既存順で判定する。
        if(!selected_environment->has_forwarded_nonempty_assignment() &&
           !selected_environment->defines("PKGDEST") &&
           package_name != package_base) {
            selected_environment = read_strict_environment(
                    package_base, draft, preparation);
            if(!selected_environment.has_value()) continue;
        }

        if(selected_environment->defines("PKGDEST")) {
            AurUpdatePreparationIssue issue = make_issue(
                    AurUpdatePreparationReason::SourcePreferencePkgdestConflict,
                    "Source preference defines invocation-owned PKGDEST.");
            issue.package_name = package_name;
            issue.package_base = package_base;
            attribute_issue_to_draft(issue, draft);
            preparation.issues.push_back(std::move(issue));
            continue;
        }
        draft.work_item.request.custom_environment =
                std::move(*selected_environment);
    }
}

void validate_static_work_items(
        const std::vector<UpdateWorkItemDraft>& drafts,
        AurUpdateSourceBuildPreparation& preparation) {
    for(const auto& draft : drafts) {
        try {
            require_static_production_source_build_work_item(
                    draft.work_item);
        } catch(const std::exception& error) {
            AurUpdatePreparationIssue issue = make_issue(
                    AurUpdatePreparationReason::StaticWorkItemInvalid,
                    error.what());
            issue.package_name = draft.work_item.request.package_name;
            issue.package_base = draft.work_item.request.checkout_name;
            attribute_issue_to_draft(issue, draft);
            preparation.issues.push_back(std::move(issue));
        } catch(...) {
            AurUpdatePreparationIssue issue = make_issue(
                    AurUpdatePreparationReason::StaticWorkItemInvalid,
                    "Static production source-build work item validation threw an unknown exception.");
            issue.package_name = draft.work_item.request.package_name;
            issue.package_base = draft.work_item.request.checkout_name;
            attribute_issue_to_draft(issue, draft);
            preparation.issues.push_back(std::move(issue));
        }
    }
}

std::vector<ProductionSourceBuildWorkItem> release_work_items(
        std::vector<UpdateWorkItemDraft> drafts) {
    std::vector<ProductionSourceBuildWorkItem> work_items;
    work_items.reserve(drafts.size());
    for(auto& draft : drafts) {
        work_items.push_back(std::move(draft.work_item));
    }
    return work_items;
}

std::vector<AurUpdatePreparedWorkItemAttribution>
snapshot_work_item_attributions(
        const std::vector<UpdateWorkItemDraft>& drafts) {
    std::vector<AurUpdatePreparedWorkItemAttribution> attributions;
    attributions.reserve(drafts.size());
    for(std::size_t index = 0; index < drafts.size(); ++index) {
        const UpdateWorkItemDraft& draft = drafts[index];
        attributions.push_back(AurUpdatePreparedWorkItemAttribution{
                index,
                draft.work_item.request.package_name,
                draft.work_item.request.checkout_name,
                draft.affected_update_plan_indices,
                draft.affected_roots});
    }
    return attributions;
}

template<typename Value>
bool has_duplicate_value(const std::vector<Value>& values) noexcept {
    for(std::size_t index = 0; index < values.size(); ++index) {
        if(std::find(values.begin(), values.begin() + index, values[index]) !=
           values.begin() + index) {
            return true;
        }
    }
    return false;
}

bool has_update_target_snapshot(
        const AurUpdateSourceBuildPreparation& preparation,
        std::size_t update_plan_index) noexcept {
    return std::any_of(
            preparation.affected_update_targets.begin(),
            preparation.affected_update_targets.end(),
            [update_plan_index](const AurUpdateExecutionTarget& target) {
                return target.update_plan_index == update_plan_index;
            });
}

bool has_root_snapshot(
        const AurUpdateSourceBuildPreparation& preparation,
        const RootTargetIdentity& root) noexcept {
    return std::find(
                   preparation.affected_roots.begin(),
                   preparation.affected_roots.end(), root) !=
           preparation.affected_roots.end();
}

bool has_exact_prepared_correlation(
        const PreparedProductionSourceBuildInvocation& production_invocation,
        const std::vector<AurUpdatePreparedWorkItemAttribution>& attributions,
        const AurUpdateSourceBuildPreparation& preparation) noexcept {
    const auto& work_items = production_invocation.work_items;
    if(work_items.empty() || attributions.size() != work_items.size() ||
       preparation.affected_update_targets.empty() ||
       preparation.affected_roots.empty()) {
        return false;
    }

    for(std::size_t index = 0; index < work_items.size(); ++index) {
        const ProductionSourceBuildWorkItem& work_item = work_items[index];
        const AurUpdatePreparedWorkItemAttribution& attribution =
                attributions[index];
        if(attribution.invocation_work_item_index != index ||
           attribution.package_name != work_item.request.package_name ||
           attribution.package_base != work_item.request.checkout_name ||
           attribution.affected_update_plan_indices.empty() ||
           attribution.affected_roots.empty() ||
           has_duplicate_value(attribution.affected_update_plan_indices) ||
           has_duplicate_value(attribution.affected_roots)) {
            return false;
        }
        if(!std::all_of(
                   attribution.affected_update_plan_indices.begin(),
                   attribution.affected_update_plan_indices.end(),
                   [&preparation](std::size_t update_plan_index) {
                       return has_update_target_snapshot(
                               preparation, update_plan_index);
                   }) ||
           !std::all_of(
                   attribution.affected_roots.begin(),
                   attribution.affected_roots.end(),
                   [&preparation](const RootTargetIdentity& root) {
                       return has_root_snapshot(preparation, root);
                   })) {
            return false;
        }
    }

    for(const auto& target : preparation.affected_update_targets) {
        const bool is_attributed = std::any_of(
                attributions.begin(), attributions.end(),
                [&target](
                        const AurUpdatePreparedWorkItemAttribution& attribution) {
                    return std::find(
                                   attribution.affected_update_plan_indices.begin(),
                                   attribution.affected_update_plan_indices.end(),
                                   target.update_plan_index) !=
                           attribution.affected_update_plan_indices.end();
                });
        if(!is_attributed) return false;
    }
    for(const auto& root : preparation.affected_roots) {
        const bool is_attributed = std::any_of(
                attributions.begin(), attributions.end(),
                [&root](
                        const AurUpdatePreparedWorkItemAttribution& attribution) {
                    return std::find(
                                   attribution.affected_roots.begin(),
                                   attribution.affected_roots.end(), root) !=
                           attribution.affected_roots.end();
                });
        if(!is_attributed) return false;
    }
    return true;
}

} // namespace

PreparedAurUpdateSourceBuildInvocation::
        PreparedAurUpdateSourceBuildInvocation(
                PreparedProductionSourceBuildInvocation&& production_invocation,
                std::vector<AurUpdatePreparedWorkItemAttribution>&&
                        work_item_attributions) noexcept
    : production_invocation_(std::move(production_invocation)),
      work_item_attributions_(std::move(work_item_attributions)) {
}

PreparedAurUpdateSourceBuildInvocation::
        PreparedAurUpdateSourceBuildInvocation(
                PreparedAurUpdateSourceBuildInvocation&& other) noexcept
    : production_invocation_(std::move(other.production_invocation_)),
      work_item_attributions_(std::move(other.work_item_attributions_)),
      valid_(std::exchange(other.valid_, false)) {
}

bool AurUpdateSourceBuildPreparation::is_prepared() const noexcept {
    return invocation.has_value() && invocation->is_valid() && issues.empty() &&
           has_exact_prepared_correlation(
                   invocation->production_invocation_,
                   invocation->work_item_attributions(), *this);
}

bool AurUpdateSourceBuildPreparation::is_noop() const noexcept {
    return !invocation.has_value() && issues.empty();
}

bool AurUpdateSourceBuildPreparation::is_blocked() const noexcept {
    return !invocation.has_value() && !issues.empty();
}

AurUpdateSourceBuildPreparation prepare_aur_update_source_build_invocation(
        const AurUpdateExecutionPreflight& preflight,
        bool needed,
        const AppConfig& config) {
    AurUpdateSourceBuildPreparation preparation;

    // Blocking preflightとnormal no-opはstrict readerやDB resolverへ進めない。
    retain_skipped_preflight_inconsistencies(preflight, preparation);
    if(has_blocking_preflight_targets(preflight) ||
       std::any_of(
               preflight.targets.begin(), preflight.targets.end(),
               [](const AurUpdateExecutionTarget& target) {
                   return !is_known_status(target.status);
               })) {
        retain_preflight_blockers(preflight, preparation);
        return preparation;
    }
    if(!preparation.issues.empty()) return preparation;
    if(!has_executable_preflight_targets(preflight)) return preparation;

    retain_executable_preflight_inconsistencies(
            preflight, preparation);
    if(!preparation.issues.empty()) return preparation;

    for(const auto& target : preflight.targets) {
        if(target.status == AurUpdateExecutionTargetStatus::Executable) continue;
        if(target.status == AurUpdateExecutionTargetStatus::Skipped) continue;

        AurUpdatePreparationIssue issue = make_issue(
                AurUpdatePreparationReason::PreflightInconsistent,
                "AUR update preflight contains an unexpected target status.");
        attribute_issue_to_target(issue, target);
        preparation.issues.push_back(std::move(issue));
    }
    if(!preparation.issues.empty()) return preparation;

    if(!preflight.build_plan.has_value()) {
        for(const auto& target : preflight.targets) {
            if(target.status == AurUpdateExecutionTargetStatus::Executable) {
                preparation.affected_update_targets.push_back(target);
            }
        }
        AurUpdatePreparationIssue issue = make_issue(
                AurUpdatePreparationReason::BuildPlanMissing,
                "Executable AUR update preflight has no combined BuildPlan.");
        attribute_issue_to_all_executable_targets(issue, preparation);
        preparation.issues.push_back(std::move(issue));
        return preparation;
    }

    const BuildPlan& plan = *preflight.build_plan;
    if(plan.order.empty()) {
        for(const auto& target : preflight.targets) {
            if(target.status == AurUpdateExecutionTargetStatus::Executable) {
                preparation.affected_update_targets.push_back(target);
            }
        }
        AurUpdatePreparationIssue issue = make_issue(
                AurUpdatePreparationReason::BuildPlanOrderEmpty,
                "Executable AUR update BuildPlan has an empty execution order.");
        attribute_issue_to_all_executable_targets(issue, preparation);
        preparation.issues.push_back(std::move(issue));
        return preparation;
    }

    std::vector<ExecutableRootBinding> bindings;
    if(!collect_executable_root_bindings(
               preflight, plan, preparation, bindings)) {
        return preparation;
    }
    if(!require_root_package_attribution(
               bindings, plan, preparation)) {
        return preparation;
    }

    std::vector<UpdateWorkItemDraft> drafts;
    drafts.reserve(plan.order.size());
    if(!collect_work_item_drafts(
               plan, bindings, preparation, drafts, needed)) {
        return preparation;
    }

    consume_strict_source_preferences(drafts, preparation);
    if(!preparation.issues.empty()) return preparation;

    validate_static_work_items(drafts, preparation);
    if(!preparation.issues.empty()) return preparation;

    try {
        // POLICY(#267): generic preparationをinvocation全体で一度だけ呼び、
        // PacmanDatabasePathsを全work itemで共有するowned snapshotにする。
        std::vector<AurUpdatePreparedWorkItemAttribution> attributions =
                snapshot_work_item_attributions(drafts);
        PreparedProductionSourceBuildInvocation production_invocation =
                prepare_production_source_build_invocation(
                release_work_items(std::move(drafts)), config);
        if(!has_exact_prepared_correlation(
                   production_invocation, attributions, preparation)) {
            throw std::logic_error(
                    "Prepared AUR update source-build invocation correlation is inconsistent.");
        }

        // POLICY(#267): generic invocationとattributionの両方が完成してから、
        // 相関済みaggregateを一度だけpublishする。
        PreparedAurUpdateSourceBuildInvocation prepared_invocation(
                std::move(production_invocation), std::move(attributions));
        return AurUpdateSourceBuildPreparation{
                std::move(preparation.issues),
                std::move(preparation.warnings),
                std::move(preparation.affected_update_targets),
                std::move(preparation.affected_roots),
                std::optional<PreparedAurUpdateSourceBuildInvocation>{
                        std::move(prepared_invocation)}};
    } catch(const PackageMetadataError& error) {
        AurUpdatePreparationIssue issue = make_issue(
                AurUpdatePreparationReason::PacmanDatabaseUnavailable,
                error.failure().diagnostic);
        issue.package_metadata_failure = error.failure();
        attribute_issue_to_all_executable_targets(issue, preparation);
        preparation.issues.push_back(std::move(issue));
    } catch(const std::exception& error) {
        AurUpdatePreparationIssue issue = make_issue(
                AurUpdatePreparationReason::GenericPreparationInconsistent,
                "Generic production source-build preparation failed unexpectedly: " +
                        std::string(error.what()));
        attribute_issue_to_all_executable_targets(issue, preparation);
        preparation.issues.push_back(std::move(issue));
    } catch(...) {
        AurUpdatePreparationIssue issue = make_issue(
                AurUpdatePreparationReason::GenericPreparationInconsistent,
                "Generic production source-build preparation failed with an unknown exception.");
        attribute_issue_to_all_executable_targets(issue, preparation);
        preparation.issues.push_back(std::move(issue));
    }
    return preparation;
}
