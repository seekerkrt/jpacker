#include "source_package_identity_projection.hpp"

#include "source_package_compatibility.hpp"

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <sys/stat.h>
#include <unistd.h>

namespace {

namespace fs = std::filesystem;

static_assert(
    !std::is_default_constructible_v<
        SourcePackageIdentityProjectionResult>);

void expect(bool condition, const std::string& message) {
    if(!condition) throw std::runtime_error(message);
}

void write_file(const fs::path& path, std::string_view contents) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if(!output) {
        throw std::runtime_error("Failed to create test file: " +
                                 path.string());
    }
    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    output.close();
    if(!output || ::chmod(path.c_str(), 0644) != 0) {
        throw std::runtime_error("Failed to finalize test file: " +
                                 path.string());
    }
}

class LocalSourceFixture final {
public:
    explicit LocalSourceFixture(std::string architecture = "x86_64") {
        const std::string template_text =
            (fs::temp_directory_path() /
             "moguet-source-identity-projection-XXXXXX")
                .string();
        std::vector<char> path_template(
            template_text.begin(), template_text.end());
        path_template.push_back('\0');
        char* created = ::mkdtemp(path_template.data());
        if(created == nullptr) {
            throw std::runtime_error(
                "Failed to create local identity projection fixture.");
        }
        root_ = created;
        if(::chmod(root_.c_str(), 0700) != 0) {
            throw std::runtime_error(
                "Failed to protect local identity projection fixture.");
        }

        write_file(
            root_ / "PKGBUILD",
            "pkgbase=local-suite\n"
            "pkgname=('local-cli' 'local-libs')\n"
            "pkgver=2.4.0\n"
            "pkgrel=3\n"
            "arch=('x86_64')\n");
        const std::string srcinfo =
            "pkgbase = local-suite\n"
            "\tpkgver = 2.4.0\n"
            "\tpkgrel = 3\n"
            "\tarch = " +
            architecture +
            "\n"
            "pkgname = local-cli\n"
            "pkgname = local-libs\n";
        write_file(root_ / ".SRCINFO", srcinfo);
    }

    LocalSourceFixture(const LocalSourceFixture&) = delete;
    LocalSourceFixture& operator=(const LocalSourceFixture&) = delete;

    ~LocalSourceFixture() noexcept {
        std::error_code error;
        fs::remove_all(root_, error);
    }

    const fs::path& root() const noexcept {
        return root_;
    }

private:
    fs::path root_;
};

SourceAwarePackageIdentity require_single(
    const SourcePackageIdentityProjectionResult& result,
    const std::string& context) {
    const SourcePackageIdentityProjectionSuccess* success = result.success();
    expect(result.is_success() && success != nullptr &&
               success->identities.size() == 1,
           context + " did not return exactly one identity.");
    return success->identities.front();
}

SourcePackageIdentityProjectionIssue require_single_issue(
    const SourcePackageIdentityProjectionResult& result,
    SourcePackageIdentityProjectionIssueKind kind,
    const std::string& context) {
    const SourcePackageIdentityProjectionFailure* failure = result.failure();
    expect(!result.is_success() && failure != nullptr &&
               failure->issues.size() == 1 &&
               failure->issues.front().kind == kind,
           context + " did not return the expected typed issue.");
    return failure->issues.front();
}

SourceAwarePackageIdentity aur_context(
    std::string package_name = "artifact-child",
    std::string version = "1.0-1") {
    return SourceAwarePackageIdentity::make(
        PackageChildIdentity::make(
            PackageBaseIdentity::make(
                PackageSourceIdentity::aur(
                    SourceLocationIdentity::known_git_remote(
                        "https://aur.archlinux.org/artifact-base.git")),
                "artifact-base"),
            std::move(package_name)),
        SourceRevisionIdentity::git_commit(std::string(40, 'a')),
        PackageVersionIdentity::composite(std::move(version)),
        PackageArchitectureIdentity::known({"x86_64"}));
}

ArtifactPackageIdentity archive_identity(
    std::string package_name = "artifact-child",
    std::string version = "1.0-1",
    ArtifactPackageBaseIdentity package_base =
        ArtifactPackageBaseIdentity::known("artifact-base"),
    ArtifactPackageArchitectureIdentity architecture =
        ArtifactPackageArchitectureIdentity::known("x86_64")) {
    return ArtifactPackageIdentity{
        std::move(package_name), std::move(version),
        std::move(package_base), std::move(architecture)};
}

void test_root_projection() {
    const RootPackageIdentity aur_root =
        AurRootPackageIdentity{"root-child", "root-base"};
    const SourceAwarePackageIdentity& identity = require_single(
        project_root_source_package_identity(aur_root), "AUR root");
    expect(identity.package().package_name() == "root-child" &&
               identity.package().package_base().package_base() ==
                   "root-base" &&
               identity.package().package_base().source().kind() ==
                   PackageSourceKind::Aur &&
               identity.package()
                       .package_base()
                       .source()
                       .location()
                       .state() == SourceLocationState::Unknown &&
               identity.source_revision().state() ==
                   SourceRevisionState::Unknown &&
               identity.package_version().state() ==
                   PackageVersionState::Unknown &&
               identity.architecture().state() ==
                   PackageArchitectureState::Unknown,
           "AUR root projection flattened unknown evidence.");

    const RootPackageIdentity repository_root =
        RepositoryRootPackageIdentity{"extra", "repo-child"};
    const auto& issue = require_single_issue(
        project_root_source_package_identity(repository_root),
        SourcePackageIdentityProjectionIssueKind::MissingPackageBase,
        "repository root");
    expect(issue.input_kind ==
                   SourcePackageIdentityProjectionInputKind::
                       RootPackage &&
               issue.source.has_value() &&
               issue.source->kind() == PackageSourceKind::Repository &&
               issue.source->repository_name() != nullptr &&
               *issue.source->repository_name() == "extra" &&
               issue.package_name ==
                   std::optional<std::string>{"repo-child"},
           "Repository root incomplete identity lost attribution.");
}

void test_resolved_source_build_projection() {
    RepositoryPackagePresent exact{
        "extra",
        1,
        "repo-child",
        "repo-base",
        ObservedVersion::available(
            ObservedVersionSource::RepositoryExactPackage,
            "3.2.1-4"),
        std::vector<std::string>{"core", "extra"},
        {}};
    const ResolvedSourceBuildIdentity repository{
        ResolvedRepositorySourceBuildIdentity{std::move(exact)}};
    const SourceAwarePackageIdentity& repository_identity = require_single(
        project_resolved_source_build_package_identity(repository),
        "resolved repository source");
    const PackageSourceIdentity& repository_source =
        repository_identity.package().package_base().source();
    expect(repository_source.kind() == PackageSourceKind::Repository &&
               repository_source.repository_name() != nullptr &&
               *repository_source.repository_name() == "extra" &&
               repository_source.location().state() ==
                   SourceLocationState::Known &&
               repository_source.location().value() != nullptr &&
               *repository_source.location().value() ==
                   "https://gitlab.archlinux.org/archlinux/packaging/packages/repo-base.git" &&
               repository_identity.package_version().full_version() !=
                   nullptr &&
               *repository_identity.package_version().full_version() ==
                   "3.2.1-4" &&
               repository_identity.source_revision().state() ==
                   SourceRevisionState::Unknown,
           "Resolved repository source projection lost owned provenance.");

    const ResolvedSourceBuildIdentity aur{
        ResolvedAurSourceBuildIdentity{"aur-child", "aur-base"}};
    const SourceAwarePackageIdentity& aur_identity = require_single(
        project_resolved_source_build_package_identity(aur),
        "resolved AUR source");
    expect(aur_identity.package().package_base().source().kind() ==
                   PackageSourceKind::Aur &&
               aur_identity.package()
                       .package_base()
                       .source()
                       .location()
                       .state() == SourceLocationState::Known &&
               aur_identity.package_version().state() ==
                   PackageVersionState::Unknown,
           "Resolved AUR source projection invented package metadata.");
}

void test_dependency_projection() {
    const ResolvedDependencyCandidate repository = RepositoryExactPackage{
        ConfiguredRepositoryIdentity{"extra", 1},
        "repo-dependency",
        "repo-dependency-base",
        ObservedVersion::available(
            ObservedVersionSource::RepositoryExactPackage,
            "1.0-2"),
        {}};
    const SourceAwarePackageIdentity& repository_identity = require_single(
        project_dependency_source_package_identity(repository),
        "repository dependency");
    expect(repository_identity.package().package_base().source().kind() ==
                   PackageSourceKind::Repository &&
               repository_identity.package_version().full_version() !=
                   nullptr &&
               *repository_identity.package_version().full_version() ==
                   "1.0-2",
           "Repository dependency projection lost exact metadata.");

    const ResolvedDependencyCandidate aur = AurResolvedDependencyCandidate{
        "aur-dependency",
        "aur-dependency-base",
        ObservedVersion::available(
            ObservedVersionSource::AurExactPackage, "2.0-1")};
    expect(require_single(
               project_dependency_source_package_identity(aur),
               "AUR dependency")
                   .package()
                   .package_base()
                   .source()
                   .kind() == PackageSourceKind::Aur,
           "AUR dependency source kind was lost.");

    const ResolvedDependencyCandidate local = LocalResolvedDependencyCandidate{
        "local-dependency",
        "local-dependency-base",
        std::nullopt,
        ObservedVersion::available(
            ObservedVersionSource::LocalExactPackage, "4.0-1")};
    const SourceAwarePackageIdentity& local_identity = require_single(
        project_dependency_source_package_identity(local),
        "local dependency");
    expect(local_identity.package().package_base().source().kind() ==
                   PackageSourceKind::Local &&
               local_identity.package()
                       .package_base()
                       .source()
                       .location()
                       .state() == SourceLocationState::Unknown &&
               local_identity.source_revision().state() ==
                   SourceRevisionState::Inapplicable,
           "Local dependency projection fabricated local location/revision.");

    const ProvidedDependency aur_provider = ProvidedDependency::from_aur(
        "provider-package", "provider-base", "virtual-capability",
        "virtual-capability=99", "5.0-3");
    const ResolvedDependencyCandidate provider =
        ProviderResolvedDependencyCandidate{
            aur_provider,
            ObservedVersion::available(
                ObservedVersionSource::AurProviderCapability,
                "99")};
    const SourceAwarePackageIdentity& provider_identity = require_single(
        project_dependency_source_package_identity(provider),
        "AUR provider dependency");
    expect(provider_identity.package_version().full_version() != nullptr &&
               *provider_identity.package_version().full_version() ==
                   "5.0-3",
           "Provider capability version replaced provider package version.");

    const ResolvedDependencyCandidate repository_provider =
        ProviderResolvedDependencyCandidate{
            ProvidedDependency::from_repository(
                "extra", "repo-provider"),
            ObservedVersion::unknown(
                ObservedVersionSource::
                    RepositoryProviderCapability,
                ObservedVersionUnknownReason::
                    UnversionedProviderCapability)};
    const SourcePackageIdentityProjectionIssue repository_provider_issue =
        require_single_issue(
            project_dependency_source_package_identity(
                repository_provider),
            SourcePackageIdentityProjectionIssueKind::
                MissingPackageBase,
            "repository provider without PackageBase");
    expect(repository_provider_issue.source.has_value() &&
               repository_provider_issue.source->kind() ==
                   PackageSourceKind::Repository &&
               repository_provider_issue.source->repository_name() !=
                   nullptr &&
               *repository_provider_issue.source->repository_name() ==
                   "extra",
           "Repository provider failure lost source authority.");

    const ResolvedDependencyCandidate installed = InstalledExactPackage{
        "installed-only",
        ObservedVersion::available(
            ObservedVersionSource::InstalledExactPackage,
            "1.0-1")};
    require_single_issue(
        project_dependency_source_package_identity(installed),
        SourcePackageIdentityProjectionIssueKind::UnsupportedSource,
        "installed-only dependency");

    const ResolvedDependencyCandidate invalid_version_source =
        AurResolvedDependencyCandidate{
            "wrong-version-source",
            "wrong-version-source-base",
            ObservedVersion::available(
                ObservedVersionSource::RepositoryExactPackage,
                "1.0-1")};
    const SourceAwarePackageIdentity& invalid_version_identity = require_single(
        project_dependency_source_package_identity(
            invalid_version_source),
        "invalid dependency version source");
    expect(invalid_version_identity.package_version().state() ==
                   PackageVersionState::Unavailable &&
               invalid_version_identity
                       .package_version()
                       .unavailable_reason() != nullptr &&
               *invalid_version_identity
                       .package_version()
                       .unavailable_reason() ==
                   IdentityUnavailableReason::InvalidObservation,
           "Invalid dependency version source was presented as known.");
}

void test_artifact_projection() {
    const ArtifactPackageIdentity artifact = archive_identity();
    require_single_issue(
        project_artifact_source_package_identity(std::nullopt, artifact),
        SourcePackageIdentityProjectionIssueKind::MissingSourceContext,
        "context-free artifact");

    require_single_issue(
        project_artifact_source_package_identity(
            aur_context("different-child"), artifact),
        SourcePackageIdentityProjectionIssueKind::PackageNameMismatch,
        "artifact child mismatch");

    const SourceAwarePackageIdentity context = aur_context();
    const SourceAwarePackageIdentity& projected = require_single(
        project_artifact_source_package_identity(context, artifact),
        "contextual artifact");
    expect(projected.package() == context.package() &&
               projected.source_revision() == context.source_revision() &&
               projected.architecture() == context.architecture() &&
               projected.package_version().full_version() != nullptr &&
               *projected.package_version().full_version() == "1.0-1",
           "Artifact projection did not retain exact archive identity.");

    require_single_issue(
        project_artifact_source_package_identity(
            context, archive_identity("artifact-child", "1.1-2")),
        SourcePackageIdentityProjectionIssueKind::PackageVersionMismatch,
        "artifact version mismatch");
    require_single_issue(
        project_artifact_source_package_identity(
            context,
            archive_identity(
                "artifact-child", "1.0-1",
                ArtifactPackageBaseIdentity::known("other-base"))),
        SourcePackageIdentityProjectionIssueKind::PackageBaseMismatch,
        "artifact PackageBase mismatch");
    require_single_issue(
        project_artifact_source_package_identity(
            context,
            archive_identity(
                "artifact-child", "1.0-1",
                ArtifactPackageBaseIdentity::missing())),
        SourcePackageIdentityProjectionIssueKind::ArtifactPackageBaseMissing,
        "artifact PackageBase missing");
    require_single_issue(
        project_artifact_source_package_identity(
            context,
            archive_identity(
                "artifact-child", "1.0-1",
                ArtifactPackageBaseIdentity::unavailable())),
        SourcePackageIdentityProjectionIssueKind::
            ArtifactPackageBaseUnavailable,
        "artifact PackageBase unavailable");
    require_single_issue(
        project_artifact_source_package_identity(
            context,
            archive_identity(
                "artifact-child", "1.0-1",
                ArtifactPackageBaseIdentity::known("artifact-base"),
                ArtifactPackageArchitectureIdentity::known("armv7h"))),
        SourcePackageIdentityProjectionIssueKind::ArchitectureMismatch,
        "artifact architecture mismatch");
    require_single_issue(
        project_artifact_source_package_identity(
            context,
            archive_identity(
                "artifact-child", "1.0-1",
                ArtifactPackageBaseIdentity::known("artifact-base"),
                ArtifactPackageArchitectureIdentity::missing())),
        SourcePackageIdentityProjectionIssueKind::
            ArtifactArchitectureMissing,
        "artifact architecture missing");
}

void test_update_projection() {
    AurUpdatePlanEntry unavailable;
    unavailable.installed_name = "missing-update";
    require_single_issue(
        project_aur_update_package_identity(unavailable),
        SourcePackageIdentityProjectionIssueKind::
            SourceMetadataUnavailable,
        "update without remote metadata");

    AurUpdatePlanEntry not_found;
    not_found.installed_name = "non-aur-foreign";
    not_found.classification = AurUpdateClassification::NonAurForeign;
    require_single_issue(
        project_aur_update_package_identity(not_found),
        SourcePackageIdentityProjectionIssueKind::SourceNotFound,
        "confirmed non-AUR update target");

    AurUpdatePlanEntry inconsistent;
    inconsistent.installed_name = "inconsistent-update";
    inconsistent.classification = AurUpdateClassification::UpdateAvailable;
    require_single_issue(
        project_aur_update_package_identity(inconsistent),
        SourcePackageIdentityProjectionIssueKind::InvalidIdentity,
        "update classification without remote identity");

    AurUpdatePlanEntry mismatch;
    mismatch.installed_name = "installed-name";
    mismatch.aur_package = AurUpdateRemotePackage{
        "different-name", "update-base", "2.0-1",
        AurVersionRelation::NewerThanInstalled};
    mismatch.classification = AurUpdateClassification::UpdateAvailable;
    require_single_issue(
        project_aur_update_package_identity(mismatch),
        SourcePackageIdentityProjectionIssueKind::PackageNameMismatch,
        "update package mismatch");

    AurUpdatePlanEntry contradictory;
    contradictory.installed_name = "contradictory-update";
    contradictory.aur_package = AurUpdateRemotePackage{
        "contradictory-update", "update-base", "2.0-1",
        AurVersionRelation::NewerThanInstalled};
    contradictory.classification =
        AurUpdateClassification::MetadataUnavailable;
    require_single_issue(
        project_aur_update_package_identity(contradictory),
        SourcePackageIdentityProjectionIssueKind::InvalidIdentity,
        "remote update with unavailable classification");

    AurUpdatePlanEntry update;
    update.installed_name = "update-child";
    update.installed_version = "1.0-1";
    update.aur_package = AurUpdateRemotePackage{
        "update-child", "update-base", "2.0-1",
        AurVersionRelation::NewerThanInstalled};
    update.classification = AurUpdateClassification::UpdateAvailable;
    const SourceAwarePackageIdentity& identity = require_single(
        project_aur_update_package_identity(update), "AUR update");
    expect(identity.package().package_name() == "update-child" &&
               identity.package().package_base().package_base() ==
                   "update-base" &&
               identity.package_version().full_version() != nullptr &&
               *identity.package_version().full_version() == "2.0-1" &&
               identity.source_revision().state() ==
                   SourceRevisionState::Unknown,
           "AUR update projection lost remote identity.");
}

void test_local_projection() {
    LocalSourceFixture fixture;
    LocalSourceRoot root = open_local_source_root(fixture.root());
    const LocalPackageMetadataParseResult* parse_result =
        root.metadata().parse_result();
    expect(parse_result != nullptr && parse_result->is_success() &&
               parse_result->metadata() != nullptr,
           "Local projection fixture metadata was not accepted.");

    LocalSourceBuildMetadata bound =
        bind_existing_local_source_metadata(root, "x86_64");
    LocalBuildPlan plan = resolve_local_build_plan(
        *parse_result->metadata(), "x86_64");
    LocalSourceBuildProjectionAuthority authority =
        make_local_source_build_projection_authority(root, plan, bound);
    const SourcePackageIdentityProjectionResult result =
        project_local_source_package_identities(authority);
    const SourcePackageIdentityProjectionSuccess* success = result.success();
    expect(result.is_success() && success != nullptr &&
               success->identities.size() == 2,
           "Local source projection did not retain every child.");
    expect(success->identities[0].package().package_name() == "local-cli" &&
               success->identities[1].package().package_name() ==
                   "local-libs",
           "Local source child order changed.");
    for(const SourceAwarePackageIdentity& identity : success->identities) {
        const PackageSourceIdentity& source =
            identity.package().package_base().source();
        expect(source.kind() == PackageSourceKind::Local &&
                   source.location().state() ==
                       SourceLocationState::Known &&
                   source.location().value() != nullptr &&
                   *source.location().value() ==
                       root.canonical_path().string() &&
                   identity.source_revision().state() ==
                       SourceRevisionState::Inapplicable &&
                   identity.package_version().representation() != nullptr &&
                   *identity.package_version().representation() ==
                       PackageVersionRepresentation::PkgverPkgrel &&
                   identity.package_version().full_version() != nullptr &&
                   *identity.package_version().full_version() ==
                       "2.4.0-3" &&
                   identity.architecture().architectures() ==
                       std::vector<std::string>{"x86_64"},
               "Local source projection lost accepted metadata evidence.");
    }

    LocalSourceFixture unsupported_fixture("aarch64");
    LocalSourceRoot unsupported_root =
        open_local_source_root(unsupported_fixture.root());
    const LocalPackageMetadataParseResult* unsupported_parse =
        unsupported_root.metadata().parse_result();
    expect(unsupported_parse != nullptr && unsupported_parse->is_success() &&
               unsupported_parse->metadata() != nullptr,
           "Unsupported architecture fixture metadata was not accepted.");
    LocalSourceBuildMetadata unsupported_bound =
        bind_existing_local_source_metadata(
            unsupported_root, "x86_64");
    LocalBuildPlan unsupported_plan = resolve_local_build_plan(
        *unsupported_parse->metadata(), "x86_64");
    LocalSourceBuildProjectionAuthority unsupported_authority =
        make_local_source_build_projection_authority(
            unsupported_root, unsupported_plan, unsupported_bound);
    const SourcePackageIdentityProjectionResult unsupported =
        project_local_source_package_identities(unsupported_authority);
    expect(!unsupported.is_success() && unsupported.failure() != nullptr &&
               unsupported.failure()->issues.size() == 2 &&
               std::all_of(
                   unsupported.failure()->issues.begin(),
                   unsupported.failure()->issues.end(),
                   [](const auto& issue) {
                       return issue.kind ==
                              SourcePackageIdentityProjectionIssueKind::
                                  UnsupportedArchitecture;
                   }),
           "Unsupported local architecture exposed a partial identity set.");
}

void test_projection_compatibility_boundary() {
    const SourceAwarePackageIdentity context = aur_context();
    const SourcePackageIdentityProjectionResult artifact_projection =
        project_artifact_source_package_identity(
            context, archive_identity());
    const SourceAwarePackageIdentity artifact = require_single(
        artifact_projection, "artifact compatibility boundary");
    const SourcePackageCompatibilityEvaluation artifact_evaluation =
        evaluate_source_package_compatibility(context, artifact);
    expect(artifact_evaluation.kind() ==
                   SourcePackageCompatibilityKind::ExactMatch &&
               artifact_evaluation.reasons().empty(),
           "Projection output did not feed exact compatibility evaluation.");

    const RootPackageIdentity root =
        AurRootPackageIdentity{"unknown-child", "unknown-base"};
    const SourcePackageIdentityProjectionResult root_projection =
        project_root_source_package_identity(root);
    const SourceAwarePackageIdentity unknown = require_single(
        root_projection, "root compatibility boundary");
    const SourcePackageCompatibilityEvaluation unknown_evaluation =
        evaluate_source_package_compatibility(unknown, unknown);
    expect(unknown_evaluation.kind() ==
                   SourcePackageCompatibilityKind::Indeterminate &&
               !unknown_evaluation.is_exact_match(),
           "Projection Unknown evidence became an exact match.");
}

} // namespace

int main() {
    try {
        test_root_projection();
        test_resolved_source_build_projection();
        test_dependency_projection();
        test_artifact_projection();
        test_update_projection();
        test_local_projection();
        test_projection_compatibility_boundary();
    } catch(const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "source package identity projection tests: all checks passed\n";
    return 0;
}
