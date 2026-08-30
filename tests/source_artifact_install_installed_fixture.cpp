#include "app_config.hpp"
#include "artifact_workspace.hpp"
#include "build_plan_artifact_target_projection.hpp"
#include "invocation_owned_cleanup_adapter.hpp"
#include "package_base_artifact_install_executor.hpp"
#include "source_artifact_install_trusted_transport.hpp"
#include "source_package_identity.hpp"
#include "trusted_cache.hpp"
#include "xdg_directory_safety.hpp"
#include "xdg_paths.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace fs = std::filesystem;

bool ProductionSourceBuildInvocationResult::is_success() const noexcept {
    return std::all_of(
        work_items.begin(), work_items.end(),
        [](const ProductionSourceBuildWorkItemOutcome& outcome) {
            return outcome.status ==
                   ProductionSourceBuildWorkItemStatus::Succeeded;
        });
}

const std::optional<SelectedRepositoryProviderTrustedExecutionEvidence>&
SelectedRepositoryProviderTrustedReceiptExecutionResult::
    trusted_execution_evidence() const noexcept {
    return trusted_execution_evidence_;
}

namespace {

[[noreturn]] void fail(const std::string& message) {
    throw std::runtime_error(message);
}

SourceAwarePackageIdentity expected_identity(
    const ArtifactPackageIdentity& actual,
    const std::string& package_base) {
    const std::string* architecture = actual.architecture.value();
    if(architecture == nullptr) fail("archive architecture is unavailable");
    return SourceAwarePackageIdentity::make(
        PackageChildIdentity::make(
            PackageBaseIdentity::make(
                PackageSourceIdentity::aur(
                    SourceLocationIdentity::known_git_remote(
                        "https://aur.archlinux.org/" + package_base +
                        ".git")),
                package_base),
            actual.package_name),
        SourceRevisionIdentity::unknown(),
        PackageVersionIdentity::composite(actual.full_version),
        PackageArchitectureIdentity::known({*architecture}));
}

std::string status_name(SourceArtifactInstallTrustedExecutionStatus status) {
    switch(status) {
        case SourceArtifactInstallTrustedExecutionStatus::InvalidRequest:
            return "InvalidRequest";
        case SourceArtifactInstallTrustedExecutionStatus::
            TrustedExecutableUnavailable:
            return "TrustedExecutableUnavailable";
        case SourceArtifactInstallTrustedExecutionStatus::TokenGenerationFailed:
            return "TokenGenerationFailed";
        case SourceArtifactInstallTrustedExecutionStatus::ArtifactSnapshotFailed:
            return "ArtifactSnapshotFailed";
        case SourceArtifactInstallTrustedExecutionStatus::PrepareFailed:
            return "PrepareFailed";
        case SourceArtifactInstallTrustedExecutionStatus::PacmanFailed:
            return "PacmanFailed";
        case SourceArtifactInstallTrustedExecutionStatus::AbortFailed:
            return "AbortFailed";
        case SourceArtifactInstallTrustedExecutionStatus::ConsumeFailed:
            return "ConsumeFailed";
        case SourceArtifactInstallTrustedExecutionStatus::MalformedReceipt:
            return "MalformedReceipt";
        case SourceArtifactInstallTrustedExecutionStatus::Missing:
            return "Missing";
        case SourceArtifactInstallTrustedExecutionStatus::Complete:
            return "Complete";
        case SourceArtifactInstallTrustedExecutionStatus::OutcomeUnknown:
            return "OutcomeUnknown";
    }
    return "InvalidStatus";
}

std::string completeness_name(
    SourceArtifactInstallReceiptEvidenceCompleteness completeness) {
    switch(completeness) {
        case SourceArtifactInstallReceiptEvidenceCompleteness::Complete:
            return "Complete";
        case SourceArtifactInstallReceiptEvidenceCompleteness::Incomplete:
            return "Incomplete";
        case SourceArtifactInstallReceiptEvidenceCompleteness::Missing:
            return "Missing";
        case SourceArtifactInstallReceiptEvidenceCompleteness::Invalid:
            return "Invalid";
    }
    return "InvalidCompleteness";
}

struct CleanupLifecycleFixtureInput {
    fs::path archive;
    std::string package_name;
    std::string package_base;
    std::string version;
    std::string architecture;
};

std::optional<CleanupLifecycleFixtureInput> g_cleanup_lifecycle_input;

RootTargetIdentity cleanup_root() {
    return RootTargetIdentity{0, "moguet-cleanup-fixture-root"};
}

DependencyRequirement cleanup_requirement(
    const std::string& package_name) {
    return ConsumerDependencyRequirement(
        package_name, package_name, std::nullopt);
}

BuildPlan cleanup_lifecycle_plan(
    const CleanupLifecycleFixtureInput& input) {
    BuildPlan plan;
    plan.configured_repository_order = std::vector<std::string>{"core"};
    const RootTargetIdentity requested_root = cleanup_root();
    plan.root_targets.push_back(requested_root);
    plan.package_targets.push_back(PlannedPackageTarget{
        input.package_name,
        input.package_base,
        {PackageRole::BuildDependency},
        {requested_root}});
    plan.package_targets.push_back(PlannedPackageTarget{
        "moguet-cleanup-fixture-root",
        "moguet-cleanup-fixture-root-base",
        {PackageRole::Root},
        {requested_root}});
    plan.order.push_back(BuildPlanEntry{
        input.package_base, {input.package_name}});
    plan.order.push_back(BuildPlanEntry{
        "moguet-cleanup-fixture-root-base",
        {"moguet-cleanup-fixture-root"}});

    BuildPlanDependencyEdge edge;
    edge.parent_package_name = "moguet-cleanup-fixture-root";
    edge.parent_package_base = "moguet-cleanup-fixture-root-base";
    edge.dependency_spec = input.package_name;
    edge.role = PackageRole::BuildDependency;
    edge.kind = DependencyKind::Aur;
    edge.resolved_package_name = input.package_name;
    edge.resolved_package_base = input.package_base;
    edge.requirement = cleanup_requirement(input.package_name);
    edge.resolved_candidate = AurResolvedDependencyCandidate{
        input.package_name,
        input.package_base,
        ObservedVersion::available(
            ObservedVersionSource::AurExactPackage,
            input.version)};
    edge.constraint_evaluation = ConstraintEvaluation::satisfied();
    plan.dependency_edges.push_back(std::move(edge));
    return plan;
}

PreparedRemoteSourceBuild cleanup_lifecycle_prepared(
    const CleanupLifecycleFixtureInput& input) {
    BuildPlan plan = cleanup_lifecycle_plan(input);
    ProductionSourceBuildWorkItem dependency;
    dependency.request.checkout_name = input.package_base;
    dependency.request.package_name = input.package_name;
    dependency.request.git_url =
        "https://aur.archlinux.org/" + input.package_base + ".git";
    dependency.request.aur_review_identity =
        PackageBaseIdentity::make(
            PackageSourceIdentity::aur(
                SourceLocationIdentity::known_git_remote(
                    dependency.request.git_url)),
            input.package_base);
    dependency.required_targets.push_back(
        RequiredPackageArtifactTarget{
            input.package_base,
            input.package_name,
            DesiredInstallReason::Dependency});
    dependency.build_plan_dependency_edge_indices = {0};
    dependency.required_target_provenance =
        RequiredTargetProvenance::AurBuildPlanProjection;
    dependency.artifact_lifecycle_intent =
        ArtifactLifecycleIntent::PackageBaseSet;

    ProductionSourceBuildWorkItem root_work_item;
    root_work_item.request.checkout_name =
        "moguet-cleanup-fixture-root-base";
    root_work_item.request.package_name =
        "moguet-cleanup-fixture-root";
    root_work_item.request.git_url =
        "https://aur.archlinux.org/moguet-cleanup-fixture-root-base.git";
    root_work_item.request.aur_review_identity =
        PackageBaseIdentity::make(
            PackageSourceIdentity::aur(
                SourceLocationIdentity::known_git_remote(
                    root_work_item.request.git_url)),
            "moguet-cleanup-fixture-root-base");
    root_work_item.required_targets.push_back(
        RequiredPackageArtifactTarget{
            "moguet-cleanup-fixture-root-base",
            "moguet-cleanup-fixture-root",
            DesiredInstallReason::Explicit});
    root_work_item.required_target_provenance =
        RequiredTargetProvenance::AurBuildPlanProjection;
    root_work_item.artifact_lifecycle_intent =
        ArtifactLifecycleIntent::PackageBaseSet;

    PreparedProductionSourceBuildInvocation invocation{
        {std::move(dependency), std::move(root_work_item)},
        {},
        PacmanDatabasePaths{"/", "/var/lib/pacman"},
        std::nullopt,
        std::nullopt};
    return PreparedRemoteSourceBuild{
        ResolvedSourceBuildIdentity{ResolvedAurSourceBuildIdentity{
            "moguet-cleanup-fixture-root",
            "moguet-cleanup-fixture-root-base"}},
        std::move(plan), std::move(invocation)};
}

ProductionSourceBuildInvocationResult cleanup_success_result(
    const PreparedProductionSourceBuildInvocation& invocation) {
    ProductionSourceBuildInvocationResult result;
    for(const ProductionSourceBuildWorkItem& work_item :
        invocation.work_items) {
        ProductionSourceBuildWorkItemOutcome outcome;
        outcome.package_base = work_item.request.checkout_name;
        outcome.status = ProductionSourceBuildWorkItemStatus::Succeeded;
        outcome.production_outcome = ProductionSourceBuildStagedOutcome{
            .source_provenance = {},
            .build_outcome =
                ProductionSourceBuildCommandOutcome::Succeeded,
            .install_outcome =
                ProductionSourceInstallOutcome::Succeeded};
        result.work_items.push_back(std::move(outcome));
    }
    return result;
}

int run_cleanup_lifecycle_fixture(int argc, char* argv[]) {
    if(argc != 7) {
        std::cerr
            << "usage: source-artifact-install-installed-fixture --cleanup-lifecycle <archive> <package> <PackageBase> <version> <arch>\n";
        return 2;
    }
    CleanupLifecycleFixtureInput input{
        fs::path(argv[2]), argv[3], argv[4], argv[5], argv[6]};
    if(!input.archive.is_absolute() ||
       !fs::is_regular_file(input.archive)) {
        fail("cleanup lifecycle archive is unavailable");
    }
    g_cleanup_lifecycle_input = input;
    AppConfig config;
    RemoteAurCleanupCollectionResult result =
        collect_remote_aur_cleanup_candidates(
            cleanup_lifecycle_prepared(input), config);
    g_cleanup_lifecycle_input.reset();

    if(result.assessments().size() != 1) {
        fail("cleanup lifecycle assessment cardinality changed");
    }
    const RemoteAurCleanupCandidateAssessment& assessment =
        result.assessments().front();
    std::cout << "LIFECYCLE\t"
              << (result.completeness() ==
                          CleanupEvidenceCompleteness::Complete
                      ? "Complete"
                      : "Incomplete")
              << '\n';
    std::cout << "CLASSIFICATION\t"
              << (assessment.classification ==
                          CleanupClassification::Eligible
                      ? "Eligible"
                      : "NonEligible")
              << '\n';
    std::cout << "PACKAGE\t"
              << assessment.package.package().package_name() << '\n';
    std::cout << "END\n";
    return result.completeness() ==
                       CleanupEvidenceCompleteness::Complete &&
                   assessment.classification ==
                       CleanupClassification::Eligible
               ? 0
               : 1;
}

int run_fixture(int argc, char* argv[]) {
    if(argc != 8 && argc != 9) {
        std::cerr
            << "usage: source-artifact-install-installed-fixture <invocation> <work-index> <PackageBase> <package> <version> <arch> <archive> [--needed]\n";
        return 2;
    }
    // The legacy CLI field remains for container-lane compatibility only. A
    // caller-provided string is never promoted to cleanup authority.
    static_cast<void>(argv[1]);
    std::size_t parsed = 0;
    const unsigned long work_item_value = std::stoul(argv[2], &parsed, 10);
    if(parsed != std::string(argv[2]).size()) fail("invalid work-item index");
    const std::size_t work_item_index =
        static_cast<std::size_t>(work_item_value);
    const std::string package_base = argv[3];
    const std::string package_name = argv[4];
    const std::string expected_version = argv[5];
    const std::string expected_architecture = argv[6];
    const fs::path source_archive = argv[7];
    const bool needed = argc == 9 && std::string(argv[8]) == "--needed";
    if(argc == 9 && !needed) fail("invalid fixture option");
    if(!source_archive.is_absolute() || !fs::is_regular_file(source_archive)) {
        fail("fixture archive is unavailable");
    }

    xdg_paths::CachePaths cache_paths =
        xdg_paths::resolve_cache_process_environment();
    xdg_directory_safety::PreparedDirectory cache_directory =
        xdg_directory_safety::prepare_directory(cache_paths);
    ValidatedCacheRoot cache_root = adopt_trusted_cache_root(
        cache_paths, std::move(cache_directory));
    ArtifactWorkspace workspace = create_artifact_workspace(
        prepare_private_trusted_cache_root(cache_root));
    const fs::path staged_source = workspace.path() / source_archive.filename();
    ExpectedPackageArtifactSet expected_paths =
        validate_makepkg_packagelist_output_set(
            workspace, staged_source.string() + "\n");
    fs::copy_file(source_archive, staged_source);
    ValidatedPackageArtifactSet artifacts =
        validate_post_build_package_artifacts(
            std::move(workspace), expected_paths);

    PackageBaseArtifactInstallPreparationResult preparation =
        prepare_package_base_artifact_install(
            artifacts, package_base,
            {{package_base, package_name, DesiredInstallReason::Dependency}},
            ArtifactInstallPreparationOptions{needed, false},
            PacmanDatabasePaths{"/", "/var/lib/pacman"});
    PreparedPackageBaseArtifactInstall* install = preparation.prepared();
    if(!preparation.is_prepared() || install == nullptr ||
       install->selected_artifacts().size() != 1) {
        fail("fixture install preparation failed");
    }
    const auto& selected = install->selected_artifacts().front();
    if(selected.identity.package_name != package_name ||
       selected.identity.full_version != expected_version ||
       selected.identity.architecture.value() == nullptr ||
       *selected.identity.architecture.value() != expected_architecture) {
        fail("fixture archive identity differs from expected input");
    }

    const RootTargetIdentity root{0, "fixture-root"};
    const SourceArtifactInstallTrustedBinding binding{
        {std::nullopt,
         work_item_index,
         package_base,
         {root}},
        {{selected.artifact_index,
          expected_identity(selected.identity, package_base),
          DesiredInstallReason::Dependency,
          {PackageRole::BuildDependency},
          {root}}}};

    const SourceArtifactInstallTrustedExecutionResult result =
        execute_source_artifact_install_trusted_transaction(
            *install, binding, ArtifactInstallExecutionOptions{true});
    std::cout << "STATUS\t" << status_name(result.status()) << '\n';
    std::cout << "OPERATION\t"
              << (result.operation_result().has_value() &&
                          result.operation_result()->is_success()
                      ? "Succeeded"
                      : "Unavailable")
              << '\n';
    if(result.pacman_exit_status().has_value()) {
        std::cout << "PACMAN\t" << *result.pacman_exit_status() << '\n';
    }

    if(result.expectation().has_value() && result.observation().has_value()) {
        const SourceArtifactInstallReceiptEvidence evidence =
            establish_source_artifact_install_receipt_evidence(
                *result.expectation(), *result.observation());
        const auto causal = project_source_artifact_install_causal_evidence(
            evidence);
        std::cout << "EVIDENCE\t"
                  << completeness_name(evidence.completeness()) << '\n';
        std::cout << "CAUSAL\t" << (causal.has_value() ? "Established" : "Absent")
                  << '\n';
        for(const std::string& installed : evidence.actual_install_set()) {
            std::cout << "INSTALL\t" << installed << '\n';
        }
    } else {
        std::cout << "EVIDENCE\tUnavailable\nCAUSAL\tAbsent\n";
    }
    std::cout << "END\n";

    install->cleanup_workspace();
    return 0;
}

} // namespace

ProductionSourceBuildInvocationResult
execute_prepared_remote_aur_cleanup_invocation(
    RemoteAurCleanupCandidateCollector& collector,
    const AppConfig&) {
    if(!g_cleanup_lifecycle_input.has_value()) {
        throw std::logic_error(
            "cleanup lifecycle fixture input is unavailable");
    }
    const CleanupLifecycleFixtureInput& input =
        g_cleanup_lifecycle_input.value();
    xdg_paths::CachePaths cache_paths =
        xdg_paths::resolve_cache_process_environment();
    xdg_directory_safety::PreparedDirectory cache_directory =
        xdg_directory_safety::prepare_directory(cache_paths);
    ValidatedCacheRoot cache_root = adopt_trusted_cache_root(
        cache_paths, std::move(cache_directory));
    ArtifactWorkspace workspace = create_artifact_workspace(
        prepare_private_trusted_cache_root(cache_root));
    const fs::path staged_source =
        workspace.path() / input.archive.filename();
    ExpectedPackageArtifactSet expected_paths =
        validate_makepkg_packagelist_output_set(
            workspace, staged_source.string() + "\n");
    fs::copy_file(input.archive, staged_source);
    ValidatedPackageArtifactSet artifacts =
        validate_post_build_package_artifacts(
            std::move(workspace), expected_paths);
    PackageBaseArtifactInstallPreparationResult preparation =
        prepare_package_base_artifact_install(
            artifacts, input.package_base,
            {{input.package_base,
              input.package_name,
              DesiredInstallReason::Dependency}},
            ArtifactInstallPreparationOptions{false, false},
            PacmanDatabasePaths{"/", "/var/lib/pacman"});
    PreparedPackageBaseArtifactInstall* install = preparation.prepared();
    if(!preparation.is_prepared() || install == nullptr) {
        throw std::runtime_error(
            "cleanup lifecycle artifact install preparation failed");
    }
    PackageBaseArtifactInstallExecutionResult operation =
        collector.execute_source_artifact_install_transaction(
            *install, 0, ArtifactInstallExecutionOptions{true});
    if(!operation.is_success() ||
       operation.selected_artifacts().size() != 1 ||
       operation.selected_artifacts().front().identity.package_name !=
           input.package_name) {
        throw std::runtime_error(
            "cleanup lifecycle actual dependency transaction was incoherent");
    }
    install->cleanup_workspace();
    return cleanup_success_result(
        collector.prepared_invocation_for_execution());
}

int main(int argc, char* argv[]) {
    try {
        if(argc > 1 && std::string(argv[1]) == "--cleanup-lifecycle") {
            return run_cleanup_lifecycle_fixture(argc, argv);
        }
        return run_fixture(argc, argv);
    } catch(const std::exception& error) {
        std::cerr << "source-artifact installed transport fixture: "
                  << error.what() << '\n';
        return 1;
    }
}
