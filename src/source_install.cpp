#include "source_install.hpp"

#include "app_config.hpp"
#include "aur_rpc.hpp"
#include "build_plan_artifact_target_projection.hpp"
#include "cache_authority.hpp"
#include "dependency_plan.hpp"
#include "localization.hpp"
#include "logging.hpp"
#include "package_identifier.hpp"
#include "process.hpp"
#include "repository_query.hpp"
#include "separated_source_build.hpp"
#include "source_build.hpp"
#include "source_install_internal.hpp"
#include "source_preference.hpp"
#include "shell_words.hpp"

#include <algorithm>
#include <filesystem>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace fs = std::filesystem;

namespace {

// NO_TRANSLATE: These are protocol endpoint identities, not user-facing prose.
const std::string AUR_BASE_URL = "https://aur.archlinux.org/";
const std::string ARCH_GIT_BASE = "https://gitlab.archlinux.org/archlinux/packaging/packages/";

SourceBuildEnvironment load_source_preference_environment(
        const std::string& package_name) {
    return get_package_env(
            package_name,
            [](const fs::path& entry_path) {
                // TRANSLATORS: The placeholder is a source preference file path.
                Logger::info(localization::format_translated_message(
                        "Loading custom build flags from {}.",
                        entry_path.string()));
            },
            [](const std::string& warning) {
                Logger::warn(warning);
            });
}

bool has_distinct_package_base(const AurPackageInfo& info) {
    return info.PackageBase != info.Name;
}

std::string canonical_source_key(
        SourceBuildSourceKind source_kind,
        const std::string& package_base) {
    switch(source_kind) {
        case SourceBuildSourceKind::Repository:
            // NO_TRANSLATE: Stable internal source identity key.
            return "repository:" + package_base;
        case SourceBuildSourceKind::Aur:
            // NO_TRANSLATE: Stable internal source identity key.
            return "aur:" + package_base;
    }
    throw std::logic_error(localization::translate_message(
            "Unknown source-build source kind."));
}

void require_supported_registered_source_install_target(
        const ResolvedSourceBuildIdentity& source) {
    // POLICY(#98,#268): registered source upgradeのlegacy singular lifecycleは
    // requested split childを個別選択できないため安全側で停止する。
    if(source.source_kind == SourceBuildSourceKind::Aur &&
       source.has_distinct_package_base) {
        // TRANSLATORS: The placeholders are the AUR identity, a requested package name, the PackageBase field identity, and its value.
        throw std::runtime_error(localization::format_translated_message(
                "Registered source upgrade does not support split {} preference {} from {} {}; this route requires a singular package identity.",
                "AUR",
                source.requested_name,
                "PackageBase",
                source.package_base));
    }
}

void add_selected_repository_provider(
        std::vector<ProvidedDependency>& providers,
        const ProvidedDependency& provider);

DesiredInstallReason resolve_source_target_reason(
        const ResolvedSourceBuildIdentity& source,
        bool use_package_base_lifecycle,
        const ProviderSelectionCallback& select_provider,
        std::vector<ProvidedDependency>& selected_repository_providers,
        std::optional<std::vector<std::string>>&
                configured_repository_order) {
    if(source.source_kind != SourceBuildSourceKind::Aur) {
        return DesiredInstallReason::Explicit;
    }

    // POLICY(#174,#268): dependency graph全体のRPC schemaを解決してから
    // route固有のexecutable guardへ進む。registered source upgradeのlegacy
    // singular ownerだけはsplit selection guardを維持する。
    BuildPlan plan = resolve_build_plan(
            source.requested_name, select_provider);
    configured_repository_order = plan.configured_repository_order;
    if(use_package_base_lifecycle) {
        require_executable_build_plan(source.requested_name, plan);
    } else {
        require_supported_registered_source_install_target(source);
        require_executable_install_plan(source.requested_name, plan);
    }
    for(const BuildPlanProvidedDependency& dependency : plan.provided) {
        if(dependency.resolution !=
           ProviderResolutionKind::UserSelected) {
            continue;
        }
        if(std::holds_alternative<AurProviderOrigin>(
                   dependency.provider.origin) &&
           !use_package_base_lifecycle) {
            throw std::runtime_error(
                    localization::format_translated_message(
                            "Registered source upgrade cannot enforce a selected {} dependency provider.",
                            "AUR"));
        }
        if(std::holds_alternative<RepositoryProviderOrigin>(
                   dependency.provider.origin)) {
            add_selected_repository_provider(
                    selected_repository_providers,
                    dependency.provider);
        }
    }
    BuildPlanArtifactTargetProjectionResult projection =
            project_build_plan_required_artifact_targets(plan);
    if(!projection.is_success()) {
        // TRANSLATORS: The placeholders are the literal BuildPlan identity and a requested package name.
        throw std::logic_error(localization::format_translated_message(
                "{} required artifact target projection failed for {}.",
                "BuildPlan", source.requested_name));
    }
    for(const auto& unit : projection.success()->build_units) {
        if(unit.package_base != source.package_base) continue;
        for(const auto& target : unit.required_targets) {
            if(target.package_name == source.requested_name) {
                return target.desired_reason;
            }
        }
    }
    // TRANSLATORS: The placeholders are the literal BuildPlan identity and a requested package name.
    throw std::logic_error(localization::format_translated_message(
            "{} required artifact target projection omitted {}.",
            "BuildPlan", source.requested_name));
}

std::string join_required_package_names(
        const std::vector<RequiredPackageArtifactTarget>& targets) {
    std::stringstream ss;
    for(size_t i = 0; i < targets.size(); ++i) {
        if(i > 0) ss << ", ";
        ss << targets[i].package_name;
    }
    return ss.str();
}

std::string aur_git_url_for_package_base(const std::string& package_base) {
    require_valid_package_name(package_base);
    return AUR_BASE_URL + package_base + ".git";
}

void add_selected_repository_provider(
        std::vector<ProvidedDependency>& providers,
        const ProvidedDependency& provider) {
    auto same = [&provider](const ProvidedDependency& existing) {
        return same_provider_identity(existing, provider);
    };
    if(std::find_if(providers.begin(), providers.end(), same) !=
       providers.end()) {
        return;
    }
    providers.push_back(provider);
}

void attach_selected_repository_providers(
        ProductionSourceBuildWorkItem& work_item,
        const BuildPlan& plan) {
    for(const BuildPlanDependencyEdge& edge : plan.dependency_edges) {
        if(edge.parent_package_base != work_item.request.checkout_name ||
           edge.kind != DependencyKind::Provided ||
           edge.provider_resolution != ProviderResolutionKind::UserSelected ||
           !edge.resolved_provider.has_value() ||
           !std::holds_alternative<RepositoryProviderOrigin>(
                   edge.resolved_provider->origin)) {
            continue;
        }
        add_selected_repository_provider(
                work_item.selected_repository_providers,
                edge.resolved_provider.value());
    }
}

ProductionSourceBuildWorkItem make_aur_source_build_work_item(
        const ProjectedBuildPlanArtifactTargets& unit,
        const BuildPlan& plan,
        bool use_source_build_preferences,
        bool needed) {
    const bool is_singular = unit.required_targets.size() == 1;
    const std::string preference_name = is_singular
            ? unit.required_targets.front().package_name
            : unit.package_base;
    SourceBuildEnvironment environment;
    if(use_source_build_preferences) {
        SourceBuildEnvironment requested_environment =
                load_source_preference_environment(preference_name);
        // POLICY(#242): empty definitionを保持したまま、fallback判定だけは従来の
        // forward可能なnonempty assignment基準にする。PKGDEST definitionは
        // fallbackで捨てず、all-target preflightまで保持する。
        if(!requested_environment.has_forwarded_nonempty_assignment() &&
           !requested_environment.defines("PKGDEST") && is_singular &&
           preference_name != unit.package_base) {
            environment =
                    load_source_preference_environment(unit.package_base);
        } else {
            environment = requested_environment;
        }
    }

    ProductionSourceBuildWorkItem work_item;
    if(is_singular) {
        work_item.request.package_name =
                unit.required_targets.front().package_name;
    }
    work_item.request.checkout_name = unit.package_base;
    work_item.request.git_url = aur_git_url_for_package_base(unit.package_base);
    work_item.request.custom_environment = std::move(environment);
    work_item.request.needed = needed;
    work_item.required_targets = unit.required_targets;
    work_item.is_build_plan_entry = true;
    work_item.configured_repository_order =
            plan.configured_repository_order;
    attach_selected_repository_providers(work_item, plan);
    require_static_production_source_build_work_item(work_item);
    return work_item;
}

enum class RepositoryProviderInstallDirective {
    Default,
    AsDependency,
};

[[noreturn]] void throw_malformed_repository_provider_metadata(
        const std::string& diagnostic) {
    throw PackageMetadataError(PackageMetadataFailure{
            PackageMetadataErrorCode::MalformedMetadata, diagnostic});
}

RepositoryProviderInstallDirective
resolve_repository_provider_install_directive(
        const std::vector<ProvidedDependency>& providers,
        const PacmanDatabasePaths& database_paths) {
    std::optional<RepositoryProviderInstallDirective> transaction_directive;
    PackageMetadataSession session =
            PackageMetadataSession::open(database_paths);
    for(const ProvidedDependency& provider : providers) {
        InstalledPackageQueryResult query_result =
                session.query_installed_package(provider.package_name);
        RepositoryProviderInstallDirective provider_directive =
                RepositoryProviderInstallDirective::AsDependency;
        if(const auto* failure =
                   std::get_if<PackageMetadataFailure>(&query_result)) {
            // POLICY: metadata failureと未導入を区別し、reasonを推測してmutationしない。
            throw PackageMetadataError(*failure);
        }
        if(const auto* metadata =
                   std::get_if<InstalledPackageMetadata>(&query_result)) {
            if(metadata->name != provider.package_name) {
                throw_malformed_repository_provider_metadata(
                        localization::translate_message(
                                "Installed package metadata name does not match the selected repository provider identity."));
            }
            if(metadata->version.empty()) {
                throw_malformed_repository_provider_metadata(
                        localization::translate_message(
                                "Installed package metadata contains an empty version."));
            }
            switch(metadata->reason) {
            case InstalledPackageReason::Explicit:
                provider_directive =
                        RepositoryProviderInstallDirective::Default;
                break;
            case InstalledPackageReason::Dependency:
                break;
            case InstalledPackageReason::Unknown:
                throw_malformed_repository_provider_metadata(
                        localization::translate_message(
                                "Installed package metadata contains an unknown install reason."));
            default:
                throw_malformed_repository_provider_metadata(
                        localization::translate_message(
                                "Installed package metadata contains an invalid install reason."));
            }
        } else if(!std::holds_alternative<PackageNotFound>(query_result)) {
            throw std::logic_error(localization::translate_message(
                    "Unknown installed package query result."));
        }

        if(transaction_directive.has_value() &&
           transaction_directive.value() != provider_directive) {
            throw std::runtime_error(localization::translate_message(
                    "Selected repository provider install reasons cannot be represented by one package transaction."));
        }
        transaction_directive = provider_directive;
    }
    return transaction_directive.value_or(
            RepositoryProviderInstallDirective::AsDependency);
}

int install_selected_repository_providers(
        const std::vector<ProvidedDependency>& providers,
        RepositoryProviderInstallDirective directive,
        const AppConfig& config) {
    std::vector<std::string> targets;
    for(const ProvidedDependency& provider : providers) {
        const auto& repository =
                std::get<RepositoryProviderOrigin>(provider.origin);
        targets.push_back(
                repository.repository_name + "/" + provider.package_name);
    }
    std::vector<std::string> command{"sudo", "pacman", "-S"};
    switch(directive) {
    case RepositoryProviderInstallDirective::Default:
        break;
    case RepositoryProviderInstallDirective::AsDependency:
        command.push_back("--asdeps");
        break;
    }
    command.push_back("--needed");
    if(config.no_confirm) command.push_back("--noconfirm");
    command.push_back("--");
    command.insert(command.end(), targets.begin(), targets.end());
    Logger::info(localization::format_translated_message(
            "Installing selected repository providers: {}",
            shell_words::join(targets)));
    return run_command(shell_words::join(command));
}

ProductionSourceBuildWorkItem make_direct_source_build_work_item(
        const ResolvedSourceBuildIdentity& source,
        SourceBuildEnvironment environment,
        SourceEnvironmentEmptyValuePolicy empty_value_policy,
        bool only_if_updated,
        bool needed,
        bool use_package_base_lifecycle,
        const ProviderSelectionCallback& select_provider) {
    ProductionSourceBuildWorkItem work_item;
    work_item.request.package_name = source.requested_name;
    work_item.request.checkout_name = source.package_base;
    work_item.request.git_url = source.git_url;
    work_item.request.custom_environment = std::move(environment);
    work_item.request.empty_value_policy = empty_value_policy;
    work_item.request.only_if_updated = only_if_updated;
    work_item.request.needed = needed;
    DesiredInstallReason reason = resolve_source_target_reason(
            source, use_package_base_lifecycle, select_provider,
            work_item.selected_repository_providers,
            work_item.configured_repository_order);
    work_item.required_targets.push_back(RequiredPackageArtifactTarget{
            source.package_base, source.requested_name, reason});
    work_item.is_build_plan_entry = use_package_base_lifecycle;
    work_item.uses_system_update_baseline =
            source.source_kind == SourceBuildSourceKind::Repository;
    require_static_production_source_build_work_item(work_item);
    return work_item;
}

std::optional<ArtifactInstallExecutionOutcome> flatten_source_build_result(
        const SourceBuildExecutionResult& result) {
    switch(result.status) {
        case SourceBuildExecutionStatus::Installed:
            return ArtifactInstallExecutionOutcome::Installed;
        case SourceBuildExecutionStatus::SkippedAsNeeded:
            return ArtifactInstallExecutionOutcome::SkippedAsNeeded;
        case SourceBuildExecutionStatus::UpToDate:
        case SourceBuildExecutionStatus::UpdateStatusUnknownSkipped:
            return std::nullopt;
    }
    throw std::logic_error(localization::translate_message(
            "Unknown source-build execution status."));
}

bool should_present_package_base_result(
        const ProductionSourceBuildWorkItem& work_item,
        const PackageBaseSourceBuildExecutionResult& result) noexcept {
    return work_item.required_targets.size() != 1 ||
           work_item.required_targets.front().package_name !=
                   work_item.request.checkout_name ||
           !result.unselected_artifacts().empty();
}

void present_package_base_result(
        const ProductionSourceBuildWorkItem& work_item,
        const PackageBaseSourceBuildExecutionResult& result) {
    if(!should_present_package_base_result(work_item, result)) return;
    if(result.package_base() != work_item.request.checkout_name ||
       result.selected_children().size() !=
               work_item.required_targets.size()) {
        throw std::logic_error(
                localization::format_translated_message(
                        "{} source-build result is incoherent for presentation.",
                        "PackageBase"));
    }

    // TRANSLATORS: The placeholders are the PackageBase field identity and an AUR PackageBase name.
    Logger::info(localization::format_translated_message(
            "{} result: {}", "PackageBase", result.package_base()));
    for(std::size_t index = 0;
        index < result.selected_children().size(); ++index) {
        const RequiredPackageArtifactTarget& required =
                work_item.required_targets[index];
        const PackageBaseSourceBuildSelectedResult& child =
                result.selected_children()[index];
        if(child.identity.package_name != required.package_name ||
           child.identity.full_version.empty() ||
           child.desired_reason != required.desired_reason) {
            throw std::logic_error(
                    localization::format_translated_message(
                            "{} source-build child result is incoherent for presentation.",
                            "PackageBase"));
        }
        if(child.desired_reason == DesiredInstallReason::Explicit &&
           child.outcome == ArtifactInstallExecutionOutcome::Installed) {
            // TRANSLATORS: The placeholders are the requested package, produced package, and full version.
            Logger::info(localization::format_translated_message(
                    "  required child: {} -> {} {} (explicit): installed",
                    required.package_name,
                    child.identity.package_name,
                    child.identity.full_version));
        } else if(child.desired_reason == DesiredInstallReason::Explicit &&
                  child.outcome ==
                          ArtifactInstallExecutionOutcome::SkippedAsNeeded) {
            // TRANSLATORS: The placeholders are the requested package, produced package, full version, and literal --needed option.
            Logger::info(localization::format_translated_message(
                    "  required child: {} -> {} {} (explicit): skipped as needed ({})",
                    required.package_name,
                    child.identity.package_name,
                    child.identity.full_version,
                    "--needed"));
        } else if(child.desired_reason == DesiredInstallReason::Dependency &&
                  child.outcome == ArtifactInstallExecutionOutcome::Installed) {
            // TRANSLATORS: The placeholders are the requested package, produced package, and full version.
            Logger::info(localization::format_translated_message(
                    "  required child: {} -> {} {} (dependency): installed",
                    required.package_name,
                    child.identity.package_name,
                    child.identity.full_version));
        } else if(child.desired_reason == DesiredInstallReason::Dependency &&
                  child.outcome ==
                          ArtifactInstallExecutionOutcome::SkippedAsNeeded) {
            // TRANSLATORS: The placeholders are the requested package, produced package, full version, and literal --needed option.
            Logger::info(localization::format_translated_message(
                    "  required child: {} -> {} {} (dependency): skipped as needed ({})",
                    required.package_name,
                    child.identity.package_name,
                    child.identity.full_version,
                    "--needed"));
        } else {
            throw std::logic_error(localization::format_translated_message(
                    "{} source-build child result has an unknown install reason or outcome.",
                    "PackageBase"));
        }
    }
    for(const ArtifactPackageIdentity& unselected :
        result.unselected_artifacts()) {
        if(unselected.package_name.empty() ||
           unselected.full_version.empty()) {
            throw std::logic_error(
                    localization::format_translated_message(
                            "{} unselected artifact identity is incoherent for presentation.",
                            "PackageBase"));
        }
        // TRANSLATORS: The placeholders are a produced package name and full version.
        Logger::info(localization::format_translated_message(
                "  produced artifact: {} {} (not selected; not installed)",
                unselected.package_name,
                unselected.full_version));
    }
}

} // namespace

LocalSourceBuildDependencyPreparation::
        LocalSourceBuildDependencyPreparation(
                std::vector<ProductionSourceBuildWorkItem>
                        remote_work_items,
                std::vector<ProvidedDependency>
                        selected_repository_providers) noexcept
    : remote_work_items_(std::move(remote_work_items)),
      selected_repository_providers_(
              std::move(selected_repository_providers)) {}

const std::vector<ProductionSourceBuildWorkItem>&
LocalSourceBuildDependencyPreparation::remote_work_items() const noexcept {
    return remote_work_items_;
}

const std::vector<ProvidedDependency>&
LocalSourceBuildDependencyPreparation::selected_repository_providers()
        const noexcept {
    return selected_repository_providers_;
}

#ifdef MOGUET_ENABLE_TEST_OVERRIDES
LocalSourceBuildDependencyPreparation
LocalSourceBuildDependencyPreparation::
        make_for_production_source_build_test(
                std::vector<ProductionSourceBuildWorkItem>
                        remote_work_items,
                std::vector<ProvidedDependency>
                        selected_repository_providers) {
    return LocalSourceBuildDependencyPreparation(
            std::move(remote_work_items),
            std::move(selected_repository_providers));
}
#endif

ProductionSourceBuildWorkItem prepare_aur_source_build_work_item_internal(
        const ProjectedBuildPlanArtifactTargets& unit,
        const BuildPlan& plan,
        bool use_source_build_preferences,
        bool needed) {
    return make_aur_source_build_work_item(
            unit, plan, use_source_build_preferences, needed);
}

void seed_production_source_build_cache(
        PreparedProductionSourceBuildInvocation& invocation,
        const ValidatedCacheRoot& cache_root) {
    if(invocation.work_items.empty()) {
        throw std::logic_error(
                localization::translate_message(
                        "Cannot seed cache for an empty source-build invocation."));
    }

    cache_root.require_unchanged_identity();
    std::optional<ValidatedCacheRoot> existing_root = invocation.cache_root;
    for(const auto& work_item : invocation.work_items) {
        if(!work_item.cache_root.has_value()) continue;
        work_item.cache_root->require_unchanged_identity();
        if(!existing_root.has_value()) {
            existing_root = work_item.cache_root.value();
            continue;
        }
        existing_root->require_unchanged_identity();
        if(existing_root->device() != work_item.cache_root->device() ||
           existing_root->inode() != work_item.cache_root->inode() ||
           existing_root->owner() != work_item.cache_root->owner()) {
            throw std::logic_error(
                    localization::translate_message(
                            "Production source-build work items use different cache authorities."));
        }
    }

    if(existing_root.has_value() &&
       (existing_root->device() != cache_root.device() ||
        existing_root->inode() != cache_root.inode() ||
        existing_root->owner() != cache_root.owner())) {
        throw std::logic_error(
                localization::translate_message(
                        "Production source-build invocation cache authority changed."));
    }

    invocation.cache_root = cache_root;
    for(auto& work_item : invocation.work_items) {
        work_item.cache_root = cache_root;
    }
}

void activate_production_source_build_cache(
        PreparedProductionSourceBuildInvocation& invocation) {
    if(invocation.work_items.empty()) {
        if(!invocation.local_source_authority.has_value()) {
            throw std::logic_error(
                    localization::translate_message(
                            "Cannot activate cache for an empty source-build invocation."));
        }
        if(!invocation.cache_root.has_value()) {
            throw std::logic_error(localization::translate_message(
                    "Local source-build dependency invocation has no prepared cache authority."));
        }
        invocation.cache_root->require_unchanged_identity();
        return;
    }
    std::optional<ValidatedCacheRoot> shared_root = invocation.cache_root;
    for(const auto& work_item : invocation.work_items) {
        if(!work_item.cache_root.has_value()) continue;
        if(!shared_root.has_value()) shared_root = work_item.cache_root;
    }
    if(!shared_root.has_value()) shared_root = prepare_process_cache_root();
    seed_production_source_build_cache(invocation, shared_root.value());
}

SelectedRepositoryProviderTransactionResult
execute_selected_repository_provider_transaction(
        const PreparedProductionSourceBuildInvocation& invocation,
        const AppConfig& config) {
    SelectedRepositoryProviderTransactionResult result;
    result.selected_providers = invocation.selected_repository_providers;
    if(result.selected_providers.empty()) return result;

    if(!invocation.cache_root.has_value()) {
        throw std::logic_error(
                localization::translate_message(
                        "Production source-build invocation has no prepared cache authority."));
    }
    // POLICY(#272): retained cache authorityをexact pacman transaction直前に
    // 再検証する。system phase中のroot replacementを古いsnapshotで通さない。
    invocation.cache_root->require_unchanged_identity();

    RepositoryProviderInstallDirective directive;
    try {
        directive = resolve_repository_provider_install_directive(
                result.selected_providers, invocation.database_paths);
    } catch(const std::exception& error) {
        result.status = SelectedRepositoryProviderTransactionStatus::
                BlockedBeforeExecution;
        result.diagnostic = error.what();
        return result;
    } catch(...) {
        result.status = SelectedRepositoryProviderTransactionStatus::
                BlockedBeforeExecution;
        result.diagnostic = localization::translate_message(
                "Failed to inspect selected repository provider install reasons with an unknown exception.");
        return result;
    }

    // Metadata sessionを閉じた後、actual pacman直前にもcache authorityを再証明する。
    invocation.cache_root->require_unchanged_identity();

    // --needed成功だけではactual changeを断言できず、nonzero/exec failureも
    // partial transactionの可能性を持つため、snapshotなしではUnknownを保つ。
    result.package_state_change = PackageStateChange::Unknown;
    try {
        const int exit_status = install_selected_repository_providers(
                result.selected_providers, directive, config);
        result.command_exit_status = exit_status;
        if(exit_status == 0) {
            result.status =
                    SelectedRepositoryProviderTransactionStatus::Succeeded;
            return result;
        }
        result.status = SelectedRepositoryProviderTransactionStatus::Failed;
        result.diagnostic = localization::translate_message(
                "Failed to install selected repository providers.");
        return result;
    } catch(const std::exception& error) {
        result.status = SelectedRepositoryProviderTransactionStatus::Failed;
        result.diagnostic = error.what();
        return result;
    } catch(...) {
        result.status = SelectedRepositoryProviderTransactionStatus::Failed;
        result.diagnostic = localization::translate_message(
                "Failed to install selected repository providers with an unknown exception.");
        return result;
    }
}

namespace {

const ValidatedCacheRoot& require_prepared_cache_root(
        const ProductionSourceBuildWorkItem& work_item) {
    if(!work_item.cache_root.has_value()) {
        throw std::logic_error(
                localization::translate_message(
                        "Production source-build work item has no prepared cache authority."));
    }
    work_item.cache_root->require_unchanged_identity();
    return work_item.cache_root.value();
}

} // namespace

ResolvedSourceBuildIdentity resolve_source_build_identity(
        const std::string& package_name) {
    require_valid_package_name(package_name);

    if(is_repo_package(package_name)) {
        const SourceBuildSourceKind source_kind =
                SourceBuildSourceKind::Repository;
        return ResolvedSourceBuildIdentity{
                package_name,
                package_name,
                canonical_source_key(source_kind, package_name),
                ARCH_GIT_BASE + package_name + ".git",
                source_kind,
                false};
    }

    std::optional<AurPackageInfo> info;
    try {
        info = AurClient::info(package_name);
    } catch(const AurRpcResponseError&) {
        throw;
    } catch(const std::exception& error) {
        // TRANSLATORS: The placeholders are the AUR identity, a package name, and an AUR diagnostic.
        throw std::runtime_error(localization::format_translated_message(
                "Failed to fetch {} info for {}: {}",
                "AUR",
                package_name,
                error.what()));
    }

    if(!info.has_value()) {
        // TRANSLATORS: The placeholders are the AUR identity and a package name.
        throw std::runtime_error(localization::format_translated_message(
                "Package not found in repos or {}: {}",
                "AUR",
                package_name));
    }
    if(info->PackageBase.empty()) {
        // TRANSLATORS: The placeholders are the AUR identity, package name, and PackageBase field identity.
        throw std::runtime_error(localization::format_translated_message(
                "{} info for {} does not include {}.",
                "AUR", package_name, "PackageBase"));
    }
    require_valid_package_name(info->PackageBase);

    const SourceBuildSourceKind source_kind = SourceBuildSourceKind::Aur;
    return ResolvedSourceBuildIdentity{
            package_name,
            info->PackageBase,
            canonical_source_key(source_kind, info->PackageBase),
            AUR_BASE_URL + info->PackageBase + ".git",
            source_kind,
            has_distinct_package_base(info.value())};
}

ResolvedSourceBuildIdentity make_repository_source_build_identity(
        const RepositoryPackagePresent& package) {
    require_valid_package_name(package.package_name);
    if(package.repository_name.empty()) {
        throw std::invalid_argument(localization::translate_message(
                "Repository source-build identity has no repository name."));
    }
    const SourceBuildSourceKind source_kind =
            SourceBuildSourceKind::Repository;
    return ResolvedSourceBuildIdentity{
            package.package_name,
            package.package_name,
            canonical_source_key(source_kind, package.package_name),
            ARCH_GIT_BASE + package.package_name + ".git",
            source_kind,
            false};
}

void build_source_target(
        const std::string& package_name,
        const SourceBuildEnvironment& custom_environment,
        const AppConfig& config) {
    RemoteSourceBuildPreparation preparation = prepare_remote_source_build(
            package_name, custom_environment, config);
    if(const auto* blocked =
               std::get_if<RemoteSourceBuildPlanFailure>(&preparation);
       blocked != nullptr) {
        require_executable_build_plan(package_name, blocked->plan);
        throw std::logic_error(localization::translate_message(
                "Remote source-build plan was rejected without a blocking detail."));
    }
    PreparedRemoteSourceBuild prepared = std::move(
            std::get<PreparedRemoteSourceBuild>(preparation));
    execute_prepared_source_build_invocation(
            std::move(prepared.invocation), config);
}

RemoteSourceBuildPreparation prepare_remote_source_build(
        const std::string& package_name,
        const SourceBuildEnvironment& custom_environment,
        const AppConfig& config) {
    // --rmdepsはAUR/repository probeより前に、invocation optionとして拒否する。
    require_supported_production_source_build_options(config);
    require_valid_package_name(package_name);
    ResolvedSourceBuildIdentity source =
            resolve_source_build_identity(package_name);
    std::vector<ProductionSourceBuildWorkItem> work_items;
    std::optional<BuildPlan> aur_build_plan;
    ProviderSelectionCallback select_provider =
            provider_selection_callback(config);
    if(source.source_kind == SourceBuildSourceKind::Aur) {
        BuildPlan plan = resolve_build_plan(package_name, select_provider);
        try {
            require_executable_build_plan(package_name, plan);
        } catch(const std::exception&) {
            return RemoteSourceBuildPlanFailure{
                    std::move(source), std::move(plan)};
        }
        work_items = prepare_aur_source_build_work_items(
                plan, false, false);
        auto root_work_item = std::find_if(
                work_items.begin(), work_items.end(),
                [&source](const ProductionSourceBuildWorkItem& candidate) {
                    return candidate.request.checkout_name ==
                           source.package_base;
                });
        if(root_work_item == work_items.end()) {
            throw std::logic_error(localization::format_translated_message(
                    "{} required artifact target projection omitted {}.",
                    "BuildPlan", source.requested_name));
        }
        root_work_item->request.custom_environment = custom_environment;
        root_work_item->request.empty_value_policy =
                SourceEnvironmentEmptyValuePolicy::Forward;
        aur_build_plan.emplace(std::move(plan));
    } else {
        work_items.push_back(make_direct_source_build_work_item(
                source, custom_environment,
                SourceEnvironmentEmptyValuePolicy::Forward, false, false,
                false, select_provider));
    }
    PreparedProductionSourceBuildInvocation invocation =
            prepare_production_source_build_invocation(
                    std::move(work_items), config);
    return PreparedRemoteSourceBuild{
            std::move(source), std::move(aur_build_plan),
            std::move(invocation)};
}

ProductionSourceBuildWorkItem prepare_resolved_source_build_work_item(
        const ResolvedSourceBuildIdentity& identity,
        SourceBuildEnvironment environment,
        bool only_if_updated,
        bool needed) {
    return prepare_resolved_source_build_work_item(
            identity, std::move(environment), only_if_updated, needed,
            ProviderSelectionCallback{});
}

ProductionSourceBuildWorkItem prepare_resolved_source_build_work_item(
        const ResolvedSourceBuildIdentity& identity,
        SourceBuildEnvironment environment,
        bool only_if_updated,
        bool needed,
        const ProviderSelectionCallback& select_provider) {
    return make_direct_source_build_work_item(
            identity, std::move(environment),
            SourceEnvironmentEmptyValuePolicy::Omit, only_if_updated, needed,
            false, select_provider);
}

std::vector<ProductionSourceBuildWorkItem> prepare_aur_source_build_work_items(
        const BuildPlan& plan,
        bool use_source_build_preferences,
        bool needed) {
    require_compatible_selected_provider_package_identities(plan);
    BuildPlanArtifactTargetProjectionResult projection =
            project_build_plan_required_artifact_targets(plan);
    if(!projection.is_success()) {
        // TRANSLATORS: The placeholder is the literal BuildPlan identity.
        throw std::logic_error(localization::format_translated_message(
                "{} required artifact target projection failed before source-build work-item preparation.",
                "BuildPlan"));
    }

    std::vector<ProductionSourceBuildWorkItem> work_items;
    work_items.reserve(projection.success()->build_units.size());
    for(const auto& unit : projection.success()->build_units) {
        work_items.push_back(make_aur_source_build_work_item(
                unit, plan, use_source_build_preferences, needed));
    }
    return work_items;
}

ProductionSourceBuildWorkItem prepare_smart_source_build_work_item(
        const std::string& package_name,
        bool only_if_updated,
        bool needed) {
    return prepare_smart_source_build_work_item(
            package_name, only_if_updated, needed,
            ProviderSelectionCallback{});
}

ProductionSourceBuildWorkItem prepare_smart_source_build_work_item(
        const std::string& package_name,
        bool only_if_updated,
        bool needed,
        const ProviderSelectionCallback& select_provider) {
    SourceBuildEnvironment environment =
            load_source_preference_environment(package_name);
    ResolvedSourceBuildIdentity identity =
            resolve_source_build_identity(package_name);
    return prepare_resolved_source_build_work_item(
            identity, std::move(environment), only_if_updated, needed,
            select_provider);
}

PackageBaseSourceBuildExecutionResult
execute_prepared_package_base_source_build_work_item_typed(
        const ProductionSourceBuildWorkItem& work_item,
        const PacmanDatabasePaths& database_paths,
        const AppConfig& config) {
    // set ownerはAUR BuildPlanから必要childを確定したwork itemに限定する。
    require_static_production_source_build_work_item(work_item);
    if(!work_item.is_build_plan_entry) {
        // TRANSLATORS: The placeholders are the literal PackageBase, AUR, and BuildPlan identities.
        throw std::logic_error(
                localization::format_translated_message(
                        "{} set source-build execution requires an {} {} work item.",
                        "PackageBase", "AUR", "BuildPlan"));
    }
    if(work_item.request.only_if_updated) {
        throw std::logic_error(
                localization::format_translated_message(
                        "{} set source-build execution does not support only-if-updated requests.",
                        "PackageBase"));
    }

    // TRANSLATORS: The placeholders are AUR and PackageBase identities and an AUR PackageBase name.
    Logger::info(localization::format_translated_message(
            "Building {} {}: {}", "AUR", "PackageBase",
            work_item.request.checkout_name));
    // TRANSLATORS: The placeholder is a comma-separated list of package names.
    Logger::info(localization::format_translated_message(
            "Target package(s): {}",
            join_required_package_names(work_item.required_targets)));
    return execute_source_build_package_base_typed(
            work_item.request, work_item.required_targets,
            require_prepared_cache_root(work_item),
            database_paths, config);
}

SourceBuildExecutionResult execute_prepared_source_build_work_item_typed(
        const ProductionSourceBuildWorkItem& work_item,
        const PacmanDatabasePaths& database_paths,
        const AppConfig& config) {
    const RequiredPackageArtifactTarget& target =
            require_singular_required_package_target(work_item);
    if(work_item.is_build_plan_entry) {
        // TRANSLATORS: The placeholders are AUR and PackageBase identities and an AUR PackageBase name.
        Logger::info(localization::format_translated_message(
                "Building {} {}: {}", "AUR", "PackageBase",
                work_item.request.checkout_name));
        // TRANSLATORS: The placeholder is a comma-separated list of package names.
        Logger::info(localization::format_translated_message(
                "Target package(s): {}",
                join_required_package_names(work_item.required_targets)));
    }

    try {
        return execute_source_build_typed(
                work_item.request, require_prepared_cache_root(work_item),
                target.desired_reason,
                database_paths, config);
    } catch(const SeparatedSourceBuildCleanupError&) {
        // POLICY(#242): install成功後cleanup失敗の型とdiagnosticをgeneric
        // build/install failureへflattenしない。
        throw;
    } catch(const TrustedCacheError&) {
        // Cache authority failureはtyped callerがphase/codeを保持できるよう、
        // generic build/install diagnosticへwrapしない。
        throw;
    } catch(const std::exception& error) {
        // TRANSLATORS: The placeholders are the PackageBase identity, an AUR PackageBase name, package name, and build/install diagnostic.
        throw std::runtime_error(localization::format_translated_message(
                "Failed while building/installing {} {} ({}): {}",
                "PackageBase",
                work_item.request.checkout_name,
                work_item.request.package_name,
                error.what()));
    }
}

std::optional<ArtifactInstallExecutionOutcome>
execute_prepared_source_build_work_item(
        const ProductionSourceBuildWorkItem& work_item,
        const PacmanDatabasePaths& database_paths,
        const AppConfig& config) {
    return flatten_source_build_result(
            execute_prepared_source_build_work_item_typed(
                    work_item, database_paths, config));
}

void execute_prepared_source_build_invocation(
        PreparedProductionSourceBuildInvocation invocation,
        const AppConfig& config) {
    activate_production_source_build_cache(invocation);
    SelectedRepositoryProviderTransactionResult provider_transaction =
            execute_selected_repository_provider_transaction(
                    invocation, config);
    if(!provider_transaction.is_success()) {
        throw std::runtime_error(
                provider_transaction.diagnostic.value_or(
                        localization::translate_message(
                                "Failed to install selected repository providers.")));
    }
    for(const auto& work_item : invocation.work_items) {
        if(work_item.is_build_plan_entry) {
            try {
                PackageBaseSourceBuildExecutionResult result =
                        execute_prepared_package_base_source_build_work_item_typed(
                                work_item, invocation.database_paths, config);
                present_package_base_result(work_item, result);
            } catch(const SeparatedPackageBaseSourceBuildCleanupError& error) {
                // Transaction完了済みのchild outcomeを失わず表示し、
                // callerがcleanup failureを成功と扱わないようtypedで再throwする。
                present_package_base_result(work_item, error.result());
                throw;
            }
        } else {
            execute_prepared_source_build_work_item(
                    work_item, invocation.database_paths, config);
        }
    }
}
