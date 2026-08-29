#include "source_package_identity_projection.hpp"

#include "package_identifier.hpp"

#include <algorithm>
#include <stdexcept>
#include <type_traits>
#include <utility>

struct SourcePackageIdentityProjectionAccess {
    static SourcePackageIdentityProjectionResult success(
        std::vector<SourceAwarePackageIdentity> identities) {
        return SourcePackageIdentityProjectionResult(
            SourcePackageIdentityProjectionSuccess{
                std::move(identities)});
    }

    static SourcePackageIdentityProjectionResult failure(
        std::vector<SourcePackageIdentityProjectionIssue> issues) {
        return SourcePackageIdentityProjectionResult(
            SourcePackageIdentityProjectionFailure{std::move(issues)});
    }
};

namespace {

SourcePackageIdentityProjectionIssue make_issue(
    SourcePackageIdentityProjectionInputKind input_kind,
    SourcePackageIdentityProjectionIssueKind kind,
    std::optional<std::size_t> input_index = std::nullopt,
    std::optional<std::string> package_name = std::nullopt,
    std::optional<std::string> package_base = std::nullopt,
    std::optional<PackageSourceIdentity> source = std::nullopt) {
    return SourcePackageIdentityProjectionIssue{
        input_kind, kind, input_index, std::move(source),
        std::move(package_name), std::move(package_base)};
}

SourcePackageIdentityProjectionResult failure(
    SourcePackageIdentityProjectionIssue issue) {
    std::vector<SourcePackageIdentityProjectionIssue> issues;
    issues.push_back(std::move(issue));
    return SourcePackageIdentityProjectionAccess::failure(std::move(issues));
}

SourcePackageIdentityProjectionResult single_success(
    SourceAwarePackageIdentity identity) {
    std::vector<SourceAwarePackageIdentity> identities;
    identities.push_back(std::move(identity));
    return SourcePackageIdentityProjectionAccess::success(
        std::move(identities));
}

PackageVersionIdentity project_observed_package_version(
    const ObservedVersion& version,
    ObservedVersionSource expected_source) {
    if(version.source() != expected_source ||
       version.invalid_reason() != nullptr) {
        return PackageVersionIdentity::unavailable(
            IdentityUnavailableReason::InvalidObservation);
    }
    if(const std::string* value = version.version(); value != nullptr) {
        return PackageVersionIdentity::composite(*value);
    }

    const ObservedVersionUnknownReason* reason = version.unknown_reason();
    if(reason == nullptr) {
        return PackageVersionIdentity::unavailable(
            IdentityUnavailableReason::InvalidObservation);
    }
    switch(*reason) {
        case ObservedVersionUnknownReason::MetadataQueryFailure:
        case ObservedVersionUnknownReason::PartialSourceFailure:
            return PackageVersionIdentity::unavailable(
                IdentityUnavailableReason::ObservationFailed);
        case ObservedVersionUnknownReason::ComparisonAuthorityUnavailable:
            return PackageVersionIdentity::unavailable(
                IdentityUnavailableReason::AuthorityUnavailable);
        case ObservedVersionUnknownReason::MissingVersionMetadata:
        case ObservedVersionUnknownReason::UnversionedProviderCapability:
        case ObservedVersionUnknownReason::CandidateVersionUnavailable:
        case ObservedVersionUnknownReason::RelationKindNotComparable:
            return PackageVersionIdentity::unknown();
    }
    return PackageVersionIdentity::unavailable(
        IdentityUnavailableReason::InvalidObservation);
}

SourceAwarePackageIdentity make_identity(
    PackageSourceIdentity source,
    std::string package_base,
    std::string package_name,
    SourceRevisionIdentity revision,
    PackageVersionIdentity version,
    PackageArchitectureIdentity architecture) {
    PackageBaseIdentity base = PackageBaseIdentity::make(
        std::move(source), std::move(package_base));
    PackageChildIdentity child = PackageChildIdentity::make(
        std::move(base), std::move(package_name));
    return SourceAwarePackageIdentity::make(
        std::move(child), std::move(revision), std::move(version),
        std::move(architecture));
}

SourcePackageIdentityProjectionResult invalid_identity_failure(
    SourcePackageIdentityProjectionInputKind input_kind,
    std::optional<std::size_t> input_index,
    std::optional<std::string> package_name,
    std::optional<std::string> package_base) {
    return failure(make_issue(
        input_kind,
        SourcePackageIdentityProjectionIssueKind::InvalidIdentity,
        input_index, std::move(package_name), std::move(package_base)));
}

PackageVersionIdentity provider_package_version(
    const ProvidedDependency& provider,
    ObservedVersionSource expected_source) {
    if(provider.constraint_metadata.has_value()) {
        return project_observed_package_version(
            provider.constraint_metadata->package_version,
            expected_source);
    }
    if(provider.package_version.has_value()) {
        return PackageVersionIdentity::composite(
            provider.package_version.value());
    }
    return PackageVersionIdentity::unknown();
}

std::optional<SourcePackageIdentityProjectionIssueKind>
artifact_package_base_issue(
    const ArtifactPackageBaseIdentity& package_base) noexcept {
    switch(package_base.state()) {
        case ArtifactMetadataValueState::Known:
            return package_base.value() == nullptr ||
                           !is_valid_package_name(*package_base.value())
                       ? std::optional<
                             SourcePackageIdentityProjectionIssueKind>{
                             SourcePackageIdentityProjectionIssueKind::
                                 ArtifactPackageBaseMalformed}
                       : std::nullopt;
        case ArtifactMetadataValueState::Missing:
            return SourcePackageIdentityProjectionIssueKind::
                ArtifactPackageBaseMissing;
        case ArtifactMetadataValueState::Malformed:
            return SourcePackageIdentityProjectionIssueKind::
                ArtifactPackageBaseMalformed;
        case ArtifactMetadataValueState::Unavailable:
            return SourcePackageIdentityProjectionIssueKind::
                ArtifactPackageBaseUnavailable;
    }
    return SourcePackageIdentityProjectionIssueKind::
        ArtifactPackageBaseMalformed;
}

std::optional<SourcePackageIdentityProjectionIssueKind>
artifact_architecture_issue(
    const ArtifactPackageArchitectureIdentity& architecture) noexcept {
    switch(architecture.state()) {
        case ArtifactMetadataValueState::Known:
            return architecture.value() == nullptr ||
                           architecture.value()->empty() ||
                           std::any_of(
                               architecture.value()->begin(),
                               architecture.value()->end(),
                               [](unsigned char character) {
                                   return character <= 0x20 ||
                                          character == 0x7f;
                               })
                       ? std::optional<
                             SourcePackageIdentityProjectionIssueKind>{
                             SourcePackageIdentityProjectionIssueKind::
                                 ArtifactArchitectureMalformed}
                       : std::nullopt;
        case ArtifactMetadataValueState::Missing:
            return SourcePackageIdentityProjectionIssueKind::
                ArtifactArchitectureMissing;
        case ArtifactMetadataValueState::Malformed:
            return SourcePackageIdentityProjectionIssueKind::
                ArtifactArchitectureMalformed;
        case ArtifactMetadataValueState::Unavailable:
            return SourcePackageIdentityProjectionIssueKind::
                ArtifactArchitectureUnavailable;
    }
    return SourcePackageIdentityProjectionIssueKind::
        ArtifactArchitectureMalformed;
}

SourcePackageIdentityProjectionResult project_provider_dependency(
    const ProviderResolvedDependencyCandidate& candidate) {
    const ProvidedDependency& provider = candidate.provider;
    if(provider.package_base.empty()) {
        try {
            if(const auto* repository =
                   std::get_if<RepositoryProviderOrigin>(
                       &provider.origin);
               repository != nullptr) {
                return failure(make_issue(
                    SourcePackageIdentityProjectionInputKind::Dependency,
                    SourcePackageIdentityProjectionIssueKind::
                        MissingPackageBase,
                    std::nullopt, provider.package_name, std::nullopt,
                    PackageSourceIdentity::repository(
                        repository->repository_name,
                        SourceLocationIdentity::unknown(
                            SourceLocationKind::GitRemote))));
            }
            return failure(make_issue(
                SourcePackageIdentityProjectionInputKind::Dependency,
                SourcePackageIdentityProjectionIssueKind::
                    MissingPackageBase,
                std::nullopt, provider.package_name, std::nullopt,
                PackageSourceIdentity::aur(
                    SourceLocationIdentity::unknown(
                        SourceLocationKind::GitRemote))));
        } catch(const std::invalid_argument&) {
            return invalid_identity_failure(
                SourcePackageIdentityProjectionInputKind::Dependency,
                std::nullopt, provider.package_name, std::nullopt);
        }
    }

    try {
        if(const auto* repository =
               std::get_if<RepositoryProviderOrigin>(&provider.origin);
           repository != nullptr) {
            return single_success(make_identity(
                PackageSourceIdentity::repository(
                    repository->repository_name,
                    SourceLocationIdentity::unknown(
                        SourceLocationKind::GitRemote)),
                provider.package_base, provider.package_name,
                SourceRevisionIdentity::unknown(),
                provider_package_version(
                    provider,
                    ObservedVersionSource::RepositoryExactPackage),
                PackageArchitectureIdentity::unknown()));
        }
        return single_success(make_identity(
            PackageSourceIdentity::aur(
                SourceLocationIdentity::unknown(
                    SourceLocationKind::GitRemote)),
            provider.package_base, provider.package_name,
            SourceRevisionIdentity::unknown(),
            provider_package_version(
                provider, ObservedVersionSource::AurExactPackage),
            PackageArchitectureIdentity::unknown()));
    } catch(const std::invalid_argument&) {
        return invalid_identity_failure(
            SourcePackageIdentityProjectionInputKind::Dependency,
            std::nullopt, provider.package_name, provider.package_base);
    }
}

} // namespace

SourcePackageIdentityProjectionResult::
    SourcePackageIdentityProjectionResult(
        SourcePackageIdentityProjectionSuccess success) noexcept
    : outcome_(std::move(success)) {
}

SourcePackageIdentityProjectionResult::
    SourcePackageIdentityProjectionResult(
        SourcePackageIdentityProjectionFailure failure) noexcept
    : outcome_(std::move(failure)) {
}

bool SourcePackageIdentityProjectionResult::is_success() const noexcept {
    return std::holds_alternative<SourcePackageIdentityProjectionSuccess>(
        outcome_);
}

const SourcePackageIdentityProjectionSuccess*
SourcePackageIdentityProjectionResult::success() const noexcept {
    return std::get_if<SourcePackageIdentityProjectionSuccess>(&outcome_);
}

const SourcePackageIdentityProjectionFailure*
SourcePackageIdentityProjectionResult::failure() const noexcept {
    return std::get_if<SourcePackageIdentityProjectionFailure>(&outcome_);
}

SourcePackageIdentityProjectionResult project_root_source_package_identity(
    const RootPackageIdentity& root) {
    if(const auto* repository =
           std::get_if<RepositoryRootPackageIdentity>(&root);
       repository != nullptr) {
        try {
            return failure(make_issue(
                SourcePackageIdentityProjectionInputKind::RootPackage,
                SourcePackageIdentityProjectionIssueKind::
                    MissingPackageBase,
                std::nullopt, repository->package_name, std::nullopt,
                PackageSourceIdentity::repository(
                    repository->repository_name,
                    SourceLocationIdentity::unknown(
                        SourceLocationKind::GitRemote))));
        } catch(const std::invalid_argument&) {
            return invalid_identity_failure(
                SourcePackageIdentityProjectionInputKind::RootPackage,
                std::nullopt, repository->package_name, std::nullopt);
        }
    }

    const AurRootPackageIdentity& aur =
        std::get<AurRootPackageIdentity>(root);
    try {
        return single_success(make_identity(
            PackageSourceIdentity::aur(
                SourceLocationIdentity::unknown(
                    SourceLocationKind::GitRemote)),
            aur.package_base, aur.package_name,
            SourceRevisionIdentity::unknown(),
            PackageVersionIdentity::unknown(),
            PackageArchitectureIdentity::unknown()));
    } catch(const std::invalid_argument&) {
        return invalid_identity_failure(
            SourcePackageIdentityProjectionInputKind::RootPackage,
            std::nullopt, aur.package_name, aur.package_base);
    }
}

SourcePackageIdentityProjectionResult
project_resolved_source_build_package_identity(
    const ResolvedSourceBuildIdentity& source) {
    try {
        if(const ResolvedRepositorySourceBuildIdentity* repository =
               source.repository_identity();
           repository != nullptr) {
            const RepositoryPackagePresent& exact =
                repository->exact_package();
            PackageVersionIdentity version = exact.package_version.has_value()
                                                 ? project_observed_package_version(
                                                       exact.package_version.value(),
                                                       ObservedVersionSource::RepositoryExactPackage)
                                                 : PackageVersionIdentity::unknown();
            return single_success(make_identity(
                PackageSourceIdentity::repository(
                    exact.repository_name,
                    SourceLocationIdentity::known_git_remote(
                        source.git_url())),
                source.package_base(), source.requested_name(),
                SourceRevisionIdentity::unknown(), std::move(version),
                PackageArchitectureIdentity::unknown()));
        }
        return single_success(make_identity(
            PackageSourceIdentity::aur(
                SourceLocationIdentity::known_git_remote(
                    source.git_url())),
            source.package_base(), source.requested_name(),
            SourceRevisionIdentity::unknown(),
            PackageVersionIdentity::unknown(),
            PackageArchitectureIdentity::unknown()));
    } catch(const std::invalid_argument&) {
        return invalid_identity_failure(
            SourcePackageIdentityProjectionInputKind::ResolvedSourceBuild,
            std::nullopt, source.requested_name(), source.package_base());
    }
}

SourcePackageIdentityProjectionResult project_local_source_package_identities(
    const LocalSourceBuildProjectionAuthority& source) {
    const LocalPackageMetadata& metadata = source.accepted_metadata();
    if(!source.has_complete_identity()) {
        return failure(make_issue(
            SourcePackageIdentityProjectionInputKind::LocalSource,
            SourcePackageIdentityProjectionIssueKind::IncompleteAuthority,
            std::nullopt, std::nullopt, metadata.package_base));
    }

    std::vector<SourcePackageIdentityProjectionIssue> architecture_issues;
    for(const LocalDependencyPlanFailure& plan_failure :
        source.local_build_plan().failures()) {
        if(plan_failure.kind !=
           LocalDependencyPlanFailureKind::UnsupportedArchitecture) {
            continue;
        }
        architecture_issues.push_back(make_issue(
            SourcePackageIdentityProjectionInputKind::LocalSource,
            SourcePackageIdentityProjectionIssueKind::
                UnsupportedArchitecture,
            std::nullopt, plan_failure.parent_package_name,
            metadata.package_base));
    }
    if(!architecture_issues.empty()) {
        return SourcePackageIdentityProjectionAccess::failure(
            std::move(architecture_issues));
    }

    std::vector<SourceAwarePackageIdentity> identities;
    identities.reserve(metadata.children.size());
    try {
        const PackageSourceIdentity common_source =
            PackageSourceIdentity::local(
                SourceLocationIdentity::known_local_path(
                    source.source_root()
                        .canonical_path()
                        .string()));
        const PackageVersionIdentity version =
            PackageVersionIdentity::pkgver_pkgrel(
                metadata.epoch, metadata.pkgver, metadata.pkgrel);
        const PackageArchitectureIdentity architecture =
            PackageArchitectureIdentity::known(
                {source.effective_architecture()});
        for(std::size_t index = 0; index < metadata.children.size(); ++index) {
            const LocalPackageMetadataChild& child = metadata.children[index];
            identities.push_back(make_identity(
                common_source, metadata.package_base, child.name,
                SourceRevisionIdentity::inapplicable(), version,
                architecture));
        }
    } catch(const std::invalid_argument&) {
        return invalid_identity_failure(
            SourcePackageIdentityProjectionInputKind::LocalSource,
            std::nullopt, std::nullopt, metadata.package_base);
    }
    if(identities.empty()) {
        return invalid_identity_failure(
            SourcePackageIdentityProjectionInputKind::LocalSource,
            std::nullopt, std::nullopt, metadata.package_base);
    }
    return SourcePackageIdentityProjectionAccess::success(
        std::move(identities));
}

SourcePackageIdentityProjectionResult
project_dependency_source_package_identity(
    const ResolvedDependencyCandidate& candidate) {
    return std::visit(
        [](const auto& source_candidate)
            -> SourcePackageIdentityProjectionResult {
            using Candidate = std::decay_t<decltype(source_candidate)>;
            if constexpr(std::is_same_v<Candidate,
                                        InstalledExactPackage>) {
                return failure(make_issue(
                    SourcePackageIdentityProjectionInputKind::
                        Dependency,
                    SourcePackageIdentityProjectionIssueKind::
                        UnsupportedSource,
                    std::nullopt, source_candidate.package_name,
                    std::nullopt));
            } else if constexpr(std::is_same_v<
                                    Candidate,
                                    RepositoryExactPackage>) {
                try {
                    return single_success(make_identity(
                        PackageSourceIdentity::repository(
                            source_candidate.repository
                                .repository_name,
                            SourceLocationIdentity::unknown(
                                SourceLocationKind::
                                    GitRemote)),
                        source_candidate.package_base,
                        source_candidate.package_name,
                        SourceRevisionIdentity::unknown(),
                        project_observed_package_version(
                            source_candidate.package_version,
                            ObservedVersionSource::
                                RepositoryExactPackage),
                        PackageArchitectureIdentity::unknown()));
                } catch(const std::invalid_argument&) {
                    return invalid_identity_failure(
                        SourcePackageIdentityProjectionInputKind::
                            Dependency,
                        std::nullopt,
                        source_candidate.package_name,
                        source_candidate.package_base);
                }
            } else if constexpr(std::is_same_v<
                                    Candidate,
                                    AurResolvedDependencyCandidate>) {
                try {
                    return single_success(make_identity(
                        PackageSourceIdentity::aur(
                            SourceLocationIdentity::unknown(
                                SourceLocationKind::
                                    GitRemote)),
                        source_candidate.package_base,
                        source_candidate.package_name,
                        SourceRevisionIdentity::unknown(),
                        project_observed_package_version(
                            source_candidate.package_version,
                            ObservedVersionSource::
                                AurExactPackage),
                        PackageArchitectureIdentity::unknown()));
                } catch(const std::invalid_argument&) {
                    return invalid_identity_failure(
                        SourcePackageIdentityProjectionInputKind::
                            Dependency,
                        std::nullopt,
                        source_candidate.package_name,
                        source_candidate.package_base);
                }
            } else if constexpr(std::is_same_v<
                                    Candidate,
                                    LocalResolvedDependencyCandidate>) {
                try {
                    return single_success(make_identity(
                        PackageSourceIdentity::local(
                            SourceLocationIdentity::unknown(
                                SourceLocationKind::
                                    LocalPath)),
                        source_candidate.package_base,
                        source_candidate.package_name,
                        SourceRevisionIdentity::inapplicable(),
                        project_observed_package_version(
                            source_candidate.observed_version,
                            ObservedVersionSource::
                                LocalExactPackage),
                        PackageArchitectureIdentity::unknown()));
                } catch(const std::invalid_argument&) {
                    return invalid_identity_failure(
                        SourcePackageIdentityProjectionInputKind::
                            Dependency,
                        std::nullopt,
                        source_candidate.package_name,
                        source_candidate.package_base);
                }
            } else {
                return project_provider_dependency(source_candidate);
            }
        },
        candidate);
}

SourcePackageIdentityProjectionResult project_artifact_source_package_identity(
    const std::optional<SourceAwarePackageIdentity>& source_context,
    const ArtifactPackageIdentity& artifact) {
    if(!source_context.has_value()) {
        return failure(make_issue(
            SourcePackageIdentityProjectionInputKind::Artifact,
            SourcePackageIdentityProjectionIssueKind::MissingSourceContext,
            std::nullopt, artifact.package_name, std::nullopt));
    }

    const SourceAwarePackageIdentity& context = source_context.value();
    if(context.package().package_name() != artifact.package_name) {
        return failure(make_issue(
            SourcePackageIdentityProjectionInputKind::Artifact,
            SourcePackageIdentityProjectionIssueKind::PackageNameMismatch,
            std::nullopt, artifact.package_name,
            context.package().package_base().package_base(),
            context.package().package_base().source()));
    }

    if(const auto issue = artifact_package_base_issue(artifact.package_base);
       issue.has_value()) {
        return failure(make_issue(
            SourcePackageIdentityProjectionInputKind::Artifact,
            issue.value(), std::nullopt, artifact.package_name,
            context.package().package_base().package_base(),
            context.package().package_base().source()));
    }
    if(const auto issue = artifact_architecture_issue(artifact.architecture);
       issue.has_value()) {
        return failure(make_issue(
            SourcePackageIdentityProjectionInputKind::Artifact,
            issue.value(), std::nullopt, artifact.package_name,
            context.package().package_base().package_base(),
            context.package().package_base().source()));
    }

    const std::string* actual_package_base = artifact.package_base.value();
    const std::string* actual_architecture = artifact.architecture.value();
    if(actual_package_base == nullptr || actual_architecture == nullptr) {
        return invalid_identity_failure(
            SourcePackageIdentityProjectionInputKind::Artifact,
            std::nullopt, artifact.package_name,
            context.package().package_base().package_base());
    }
    if(*actual_package_base !=
       context.package().package_base().package_base()) {
        return failure(make_issue(
            SourcePackageIdentityProjectionInputKind::Artifact,
            SourcePackageIdentityProjectionIssueKind::PackageBaseMismatch,
            std::nullopt, artifact.package_name, *actual_package_base,
            context.package().package_base().source()));
    }

    const std::string* expected_version =
        context.package_version().full_version();
    if(context.package_version().state() != PackageVersionState::Known ||
       expected_version == nullptr) {
        return failure(make_issue(
            SourcePackageIdentityProjectionInputKind::Artifact,
            SourcePackageIdentityProjectionIssueKind::IncompleteAuthority,
            std::nullopt, artifact.package_name, *actual_package_base,
            context.package().package_base().source()));
    }
    if(*expected_version != artifact.full_version) {
        return failure(make_issue(
            SourcePackageIdentityProjectionInputKind::Artifact,
            SourcePackageIdentityProjectionIssueKind::PackageVersionMismatch,
            std::nullopt, artifact.package_name, *actual_package_base,
            context.package().package_base().source()));
    }

    if(context.architecture().state() != PackageArchitectureState::Known ||
       context.architecture().architectures().size() != 1) {
        return failure(make_issue(
            SourcePackageIdentityProjectionInputKind::Artifact,
            SourcePackageIdentityProjectionIssueKind::IncompleteAuthority,
            std::nullopt, artifact.package_name, *actual_package_base,
            context.package().package_base().source()));
    }
    if(context.architecture().architectures().front() !=
       *actual_architecture) {
        // POLICY(#485): exact expected correlation only. In particular,
        // "any" is retained as an actual value; this adapter is not an
        // architecture compatibility solver.
        return failure(make_issue(
            SourcePackageIdentityProjectionInputKind::Artifact,
            SourcePackageIdentityProjectionIssueKind::ArchitectureMismatch,
            std::nullopt, artifact.package_name, *actual_package_base,
            context.package().package_base().source()));
    }

    try {
        return single_success(SourceAwarePackageIdentity::make(
            context.package(), context.source_revision(),
            PackageVersionIdentity::composite(artifact.full_version),
            PackageArchitectureIdentity::known({*actual_architecture})));
    } catch(const std::invalid_argument&) {
        return invalid_identity_failure(
            SourcePackageIdentityProjectionInputKind::Artifact,
            std::nullopt, artifact.package_name,
            context.package().package_base().package_base());
    }
}

SourcePackageIdentityProjectionResult project_aur_update_package_identity(
    const AurUpdatePlanEntry& update) {
    if(!update.aur_package.has_value()) {
        SourcePackageIdentityProjectionIssueKind issue_kind;
        switch(update.classification) {
            case AurUpdateClassification::NonAurForeign:
                issue_kind =
                    SourcePackageIdentityProjectionIssueKind::SourceNotFound;
                break;
            case AurUpdateClassification::MetadataUnavailable:
                issue_kind = SourcePackageIdentityProjectionIssueKind::
                    SourceMetadataUnavailable;
                break;
            case AurUpdateClassification::UpdateAvailable:
            case AurUpdateClassification::UpToDate:
            case AurUpdateClassification::VersionComparisonUnavailable:
            default:
                return invalid_identity_failure(
                    SourcePackageIdentityProjectionInputKind::AurUpdate,
                    std::nullopt, update.installed_name, std::nullopt);
        }
        return failure(make_issue(
            SourcePackageIdentityProjectionInputKind::AurUpdate,
            issue_kind, std::nullopt, update.installed_name,
            std::nullopt,
            PackageSourceIdentity::aur(
                SourceLocationIdentity::unknown(
                    SourceLocationKind::GitRemote))));
    }
    const AurUpdateRemotePackage& remote = update.aur_package.value();
    switch(update.classification) {
        case AurUpdateClassification::UpdateAvailable:
        case AurUpdateClassification::UpToDate:
        case AurUpdateClassification::VersionComparisonUnavailable:
            break;
        case AurUpdateClassification::NonAurForeign:
        case AurUpdateClassification::MetadataUnavailable:
        default:
            return invalid_identity_failure(
                SourcePackageIdentityProjectionInputKind::AurUpdate,
                std::nullopt, remote.aur_name, remote.package_base);
    }
    if(update.installed_name != remote.aur_name) {
        return failure(make_issue(
            SourcePackageIdentityProjectionInputKind::AurUpdate,
            SourcePackageIdentityProjectionIssueKind::PackageNameMismatch,
            std::nullopt, remote.aur_name, remote.package_base,
            PackageSourceIdentity::aur(
                SourceLocationIdentity::unknown(
                    SourceLocationKind::GitRemote))));
    }

    try {
        return single_success(make_identity(
            PackageSourceIdentity::aur(
                SourceLocationIdentity::unknown(
                    SourceLocationKind::GitRemote)),
            remote.package_base, remote.aur_name,
            SourceRevisionIdentity::unknown(),
            PackageVersionIdentity::composite(remote.version),
            PackageArchitectureIdentity::unknown()));
    } catch(const std::invalid_argument&) {
        return invalid_identity_failure(
            SourcePackageIdentityProjectionInputKind::AurUpdate,
            std::nullopt, remote.aur_name, remote.package_base);
    }
}
