#include "invocation_owned_cleanup_model.hpp"

#include "package_identifier.hpp"

#include <algorithm>
#include <utility>
#include <variant>

namespace {

void add_reason(
        std::vector<CleanupClassificationReason>& reasons,
        CleanupClassificationReason reason) {
    if(std::find(reasons.begin(), reasons.end(), reason) == reasons.end()) {
        reasons.push_back(reason);
    }
}

void canonicalize_reasons(
        std::vector<CleanupClassificationReason>& reasons) {
    std::sort(reasons.begin(), reasons.end(), [](auto lhs, auto rhs) {
        return static_cast<int>(lhs) < static_cast<int>(rhs);
    });
}

bool is_valid_baseline(CleanupBaselineObservation baseline) noexcept {
    switch(baseline) {
    case CleanupBaselineObservation::PreExisting:
    case CleanupBaselineObservation::NewlyObserved:
    case CleanupBaselineObservation::Unknown:
        return true;
    }
    return false;
}

bool is_valid_installed_state(CleanupInstalledState state) noexcept {
    switch(state) {
    case CleanupInstalledState::Present:
    case CleanupInstalledState::Absent:
    case CleanupInstalledState::Unknown:
        return true;
    }
    return false;
}

bool is_valid_install_reason(InstalledPackageReason reason) noexcept {
    switch(reason) {
    case InstalledPackageReason::Explicit:
    case InstalledPackageReason::Dependency:
    case InstalledPackageReason::Unknown:
        return true;
    }
    return false;
}

bool is_valid_causal_ownership(CleanupCausalOwnership ownership) noexcept {
    switch(ownership) {
    case CleanupCausalOwnership::InvocationOwned:
    case CleanupCausalOwnership::NotInvocationOwned:
    case CleanupCausalOwnership::Unknown:
        return true;
    }
    return false;
}

bool is_valid_shared_requirement(
        CleanupSharedRequirementState state) noexcept {
    switch(state) {
    case CleanupSharedRequirementState::StillRequired:
    case CleanupSharedRequirementState::NoLongerRequired:
    case CleanupSharedRequirementState::Unknown:
        return true;
    }
    return false;
}

bool is_valid_verification(CleanupEvidenceVerification verification) noexcept {
    switch(verification) {
    case CleanupEvidenceVerification::Verified:
    case CleanupEvidenceVerification::Unverified:
        return true;
    }
    return false;
}

bool is_valid_correlation_coverage(
        CleanupCorrelationCoverage coverage) noexcept {
    switch(coverage) {
    case CleanupCorrelationCoverage::Complete:
    case CleanupCorrelationCoverage::Incomplete:
    case CleanupCorrelationCoverage::Unknown:
        return true;
    }
    return false;
}

bool is_valid_policy_protection(CleanupPolicyProtection protection) noexcept {
    switch(protection) {
    case CleanupPolicyProtection::NotProtected:
    case CleanupPolicyProtection::Protected:
    case CleanupPolicyProtection::Unknown:
        return true;
    }
    return false;
}

bool is_valid_package_role(PackageRole role) noexcept {
    switch(role) {
    case PackageRole::Root:
    case PackageRole::RuntimeDependency:
    case PackageRole::BuildDependency:
    case PackageRole::CheckDependency:
        return true;
    }
    return false;
}

bool is_valid_provider_resolution(ProviderResolutionKind resolution) noexcept {
    switch(resolution) {
    case ProviderResolutionKind::Unique:
    case ProviderResolutionKind::UserSelected:
        return true;
    }
    return false;
}

bool provider_matches_package(
        const CleanupProviderCorrelation& correlation,
        const PackageChildIdentity& package) noexcept {
    const ProvidedDependency& provider = correlation.provider;
    if(provider.package_name != package.package_name()) return false;

    const PackageBaseIdentity& package_base = package.package_base();
    const PackageSourceIdentity& source = package_base.source();
    if(const auto* repository =
               std::get_if<RepositoryProviderOrigin>(&provider.origin);
       repository != nullptr) {
        if(source.kind() != PackageSourceKind::Repository) return false;
        const std::string* repository_name = source.repository_name();
        if(repository_name == nullptr ||
           *repository_name != repository->repository_name) {
            return false;
        }
        // Existing repository-provider identity may not retain PackageBase.
        // A nonempty value must agree; a verified correlation is the adapter's
        // authority for an omitted value and is never inferred from the name.
        return provider.package_base.empty() ||
               provider.package_base == package_base.package_base();
    }

    return source.kind() == PackageSourceKind::Aur &&
           provider.package_base == package_base.package_base();
}

bool direct_requirement_mismatches_package(
        const CleanupDependencyEdgeCorrelation& edge,
        const PackageChildIdentity& package) noexcept {
    if(edge.provider.has_value()) return false;
    const auto* requirement =
            std::get_if<ConsumerDependencyRequirement>(&edge.requirement);
    return requirement != nullptr &&
           requirement->package_name() != package.package_name();
}

bool provider_requirement_identity_mismatch(
        const CleanupDependencyEdgeCorrelation& edge) noexcept {
    if(!edge.provider.has_value()) return false;
    const auto* requirement =
            std::get_if<ConsumerDependencyRequirement>(&edge.requirement);
    if(requirement == nullptr) {
        // SONAME association remains adapter authority. The pure classifier
        // does not reinterpret its package:soname grammar.
        return false;
    }

    const ProvidedDependency& provider = edge.provider->provider;
    if(!provider.provided_dependency_name.empty() &&
       provider.provided_dependency_name != requirement->package_name()) {
        return true;
    }

    if(!provider.constraint_metadata.has_value()) return false;
    const ProviderCapability& capability =
            provider.constraint_metadata->provided_capability;
    return capability.package_name() != requirement->package_name() ||
           (!provider.provided_dependency_name.empty() &&
            provider.provided_dependency_name != capability.package_name()) ||
           (!provider.provided_dependency_specification.empty() &&
            provider.provided_dependency_specification !=
                    capability.raw_specification());
}

std::vector<CleanupClassificationReason> structural_reasons(
        const InvocationOwnedCleanupCandidate& candidate) {
    std::vector<CleanupClassificationReason> reasons;

    if(!is_valid_baseline(candidate.baseline) ||
       !is_valid_installed_state(candidate.current_package.state) ||
       !is_valid_verification(candidate.current_package.verification) ||
       !is_valid_causal_ownership(candidate.causal_ownership) ||
       !is_valid_shared_requirement(candidate.shared_requirement) ||
       !is_valid_correlation_coverage(candidate.correlation_coverage) ||
       !is_valid_policy_protection(candidate.policy_protection)) {
        add_reason(reasons, CleanupClassificationReason::InvalidTypedState);
    }

    if(candidate.current_package.metadata.has_value()) {
        const InstalledPackageMetadata& metadata =
                candidate.current_package.metadata.value();
        if(!is_valid_install_reason(metadata.reason)) {
            add_reason(
                    reasons, CleanupClassificationReason::InvalidTypedState);
        }
        if(metadata.name != candidate.package.package().package_name()) {
            add_reason(
                    reasons,
                    CleanupClassificationReason::
                            CurrentPackageIdentityMismatch);
        }
        const PackageVersionIdentity& package_version =
                candidate.package.package_version();
        if(candidate.current_package.state == CleanupInstalledState::Present &&
           package_version.state() == PackageVersionState::Known) {
            const std::string* full_version = package_version.full_version();
            if(full_version == nullptr) {
                add_reason(
                        reasons,
                        CleanupClassificationReason::InvalidTypedState);
            } else if(*full_version != metadata.version) {
                add_reason(
                        reasons,
                        CleanupClassificationReason::
                                CurrentPackageVersionMismatch);
            }
        }
    }

    for(const CleanupPackageCorrelation& correlation :
        candidate.correlations) {
        if(!is_valid_package_name(correlation.requested_root.requested_name)) {
            add_reason(
                    reasons,
                    CleanupClassificationReason::
                            MalformedRequestedRootIdentity);
        }
        if(correlation.package != candidate.package.package()) {
            add_reason(
                    reasons,
                    CleanupClassificationReason::
                            CorrelationPackageIdentityMismatch);
        }
        if(!is_valid_verification(correlation.verification)) {
            add_reason(
                    reasons, CleanupClassificationReason::InvalidTypedState);
        }
        if(correlation.role.has_value()) {
            if(!is_valid_package_role(correlation.role.value())) {
                add_reason(
                        reasons,
                        CleanupClassificationReason::InvalidTypedState);
            } else if(correlation.role.value() == PackageRole::Root &&
                      correlation.dependency_edge.has_value()) {
                add_reason(
                        reasons,
                        CleanupClassificationReason::
                                CorrelationShapeMismatch);
            }
        }

        if(!correlation.dependency_edge.has_value()) continue;
        const CleanupDependencyEdgeCorrelation& edge =
                correlation.dependency_edge.value();
        if(direct_requirement_mismatches_package(
                   edge, correlation.package)) {
            add_reason(
                    reasons,
                    CleanupClassificationReason::
                            DependencyRequirementIdentityMismatch);
        }
        if(!edge.provider.has_value()) continue;
        const CleanupProviderCorrelation& provider = edge.provider.value();
        if(!is_valid_provider_resolution(provider.resolution)) {
            add_reason(
                    reasons, CleanupClassificationReason::InvalidTypedState);
        }
        if(!provider_matches_package(provider, correlation.package)) {
            add_reason(
                    reasons,
                    CleanupClassificationReason::ProviderIdentityMismatch);
        }
        if(provider_requirement_identity_mismatch(edge)) {
            add_reason(
                    reasons,
                    CleanupClassificationReason::
                            ProviderRequirementIdentityMismatch);
        }
    }

    canonicalize_reasons(reasons);
    return reasons;
}

InstalledPackageReason current_install_reason(
        const InvocationOwnedCleanupCandidate& candidate) noexcept {
    return candidate.current_package.metadata.has_value()
            ? candidate.current_package.metadata->reason
            : InstalledPackageReason::Unknown;
}

std::vector<CleanupClassificationReason> protection_reasons(
        const InvocationOwnedCleanupCandidate& candidate) {
    std::vector<CleanupClassificationReason> reasons;
    if(candidate.baseline == CleanupBaselineObservation::PreExisting) {
        add_reason(reasons, CleanupClassificationReason::PreExisting);
    }
    if(candidate.current_package.state == CleanupInstalledState::Absent) {
        add_reason(
                reasons, CleanupClassificationReason::CurrentPackageAbsent);
    }
    if(current_install_reason(candidate) == InstalledPackageReason::Explicit) {
        add_reason(
                reasons,
                CleanupClassificationReason::ExplicitInstallReason);
    }
    if(candidate.causal_ownership ==
       CleanupCausalOwnership::NotInvocationOwned) {
        add_reason(
                reasons,
                CleanupClassificationReason::KnownNotInvocationOwned);
    }

    for(const CleanupPackageCorrelation& correlation :
        candidate.correlations) {
        if(!correlation.role.has_value()) continue;
        if(correlation.role.value() == PackageRole::Root) {
            add_reason(reasons, CleanupClassificationReason::RootTarget);
        } else if(correlation.role.value() ==
                  PackageRole::RuntimeDependency) {
            add_reason(
                    reasons,
                    CleanupClassificationReason::RuntimeDependency);
        }
    }

    if(candidate.shared_requirement ==
       CleanupSharedRequirementState::StillRequired) {
        add_reason(reasons, CleanupClassificationReason::StillRequired);
    }
    if(candidate.policy_protection == CleanupPolicyProtection::Protected) {
        add_reason(reasons, CleanupClassificationReason::PolicyProtected);
    }

    canonicalize_reasons(reasons);
    return reasons;
}

std::vector<CleanupClassificationReason> unknown_reasons(
        const InvocationOwnedCleanupCandidate& candidate) {
    std::vector<CleanupClassificationReason> reasons;
    if(candidate.baseline == CleanupBaselineObservation::Unknown) {
        add_reason(
                reasons,
                CleanupClassificationReason::BaselineObservationUnknown);
    }
    if(candidate.current_package.state == CleanupInstalledState::Unknown) {
        add_reason(
                reasons,
                CleanupClassificationReason::CurrentInstalledStateUnknown);
    }
    if(current_install_reason(candidate) == InstalledPackageReason::Unknown) {
        add_reason(
                reasons, CleanupClassificationReason::InstallReasonUnknown);
    }
    switch(candidate.package.package_version().state()) {
    case PackageVersionState::Known:
        break;
    case PackageVersionState::Unknown:
        add_reason(
                reasons,
                CleanupClassificationReason::CurrentPackageVersionUnknown);
        break;
    case PackageVersionState::Unavailable:
        add_reason(
                reasons,
                CleanupClassificationReason::
                        CurrentPackageVersionUnavailable);
        break;
    }
    if(candidate.causal_ownership == CleanupCausalOwnership::Unknown) {
        add_reason(
                reasons,
                CleanupClassificationReason::CausalOwnershipUnknown);
    }
    if(candidate.current_package.verification ==
       CleanupEvidenceVerification::Unverified) {
        add_reason(
                reasons,
                CleanupClassificationReason::
                        CurrentPackageEvidenceUnverified);
    }
    if(candidate.correlation_coverage ==
       CleanupCorrelationCoverage::Incomplete) {
        add_reason(
                reasons,
                CleanupClassificationReason::CorrelationCoverageIncomplete);
    } else if(candidate.correlation_coverage ==
              CleanupCorrelationCoverage::Unknown) {
        add_reason(
                reasons,
                CleanupClassificationReason::CorrelationCoverageUnknown);
    }
    if(candidate.correlations.empty()) {
        add_reason(
                reasons,
                CleanupClassificationReason::DependencyCorrelationMissing);
    }

    bool has_meaningful_build_or_check_correlation = false;
    for(const CleanupPackageCorrelation& correlation :
        candidate.correlations) {
        if(!correlation.role.has_value()) {
            add_reason(
                    reasons,
                    CleanupClassificationReason::DependencyRoleUnknown);
        } else if(correlation.role.value() == PackageRole::BuildDependency ||
                  correlation.role.value() == PackageRole::CheckDependency) {
            if(correlation.dependency_edge.has_value()) {
                has_meaningful_build_or_check_correlation = true;
            } else {
                add_reason(
                        reasons,
                        CleanupClassificationReason::
                                DependencyEdgeCorrelationMissing);
            }
        }

        if(correlation.verification ==
           CleanupEvidenceVerification::Unverified) {
            add_reason(
                    reasons,
                    CleanupClassificationReason::
                            DependencyCorrelationUnverified);
        }
    }
    if(!has_meaningful_build_or_check_correlation) {
        add_reason(
                reasons,
                CleanupClassificationReason::
                        BuildOrCheckDependencyCorrelationMissing);
    }

    if(candidate.shared_requirement ==
       CleanupSharedRequirementState::Unknown) {
        add_reason(
                reasons,
                CleanupClassificationReason::SharedRequirementUnknown);
    }
    if(candidate.policy_protection == CleanupPolicyProtection::Unknown) {
        add_reason(
                reasons,
                CleanupClassificationReason::PolicyProtectionUnknown);
    }

    canonicalize_reasons(reasons);
    return reasons;
}

} // namespace

CleanupClassificationResult::CleanupClassificationResult(
        CleanupClassification classification,
        std::vector<CleanupClassificationReason> reasons) noexcept
    : classification_(classification), reasons_(std::move(reasons)) {}

CleanupClassification CleanupClassificationResult::classification()
        const noexcept {
    return classification_;
}

const std::vector<CleanupClassificationReason>&
CleanupClassificationResult::reasons() const noexcept {
    return reasons_;
}

CleanupClassificationResult classify_invocation_owned_cleanup(
        const InvocationOwnedCleanupCandidate& candidate) {
    std::vector<CleanupClassificationReason> reasons =
            structural_reasons(candidate);
    if(!reasons.empty()) {
        return CleanupClassificationResult(
                CleanupClassification::Invalid, std::move(reasons));
    }

    reasons = protection_reasons(candidate);
    if(!reasons.empty()) {
        return CleanupClassificationResult(
                CleanupClassification::Protected, std::move(reasons));
    }

    reasons = unknown_reasons(candidate);
    if(!reasons.empty()) {
        return CleanupClassificationResult(
                CleanupClassification::Unknown, std::move(reasons));
    }

    return CleanupClassificationResult(
            CleanupClassification::Eligible,
            {CleanupClassificationReason::EligibleEvidenceComplete});
}
