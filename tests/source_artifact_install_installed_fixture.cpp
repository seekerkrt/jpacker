#include "artifact_workspace.hpp"
#include "package_base_artifact_install_executor.hpp"
#include "source_artifact_install_trusted_transport.hpp"
#include "source_package_identity.hpp"
#include "trusted_cache.hpp"
#include "xdg_directory_safety.hpp"
#include "xdg_paths.hpp"

#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace fs = std::filesystem;

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

int main(int argc, char* argv[]) {
    try {
        return run_fixture(argc, argv);
    } catch(const std::exception& error) {
        std::cerr << "source-artifact installed transport fixture: "
                  << error.what() << '\n';
        return 1;
    }
}
