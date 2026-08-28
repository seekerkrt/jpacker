#include "source_package_compatibility.hpp"

#include <algorithm>
#include <utility>

namespace {

using DimensionState = SourcePackageCompatibilityDimensionState;

void append_reason(
    std::vector<SourcePackageMismatchReason>& reasons,
    SourcePackageMismatchReason reason) {
    if(std::find(reasons.begin(), reasons.end(), reason) == reasons.end()) {
        reasons.push_back(reason);
    }
}

void mark_mismatched(DimensionState& state) noexcept {
    state = DimensionState::Mismatched;
}

void mark_indeterminate(DimensionState& state) noexcept {
    if(state == DimensionState::Matched) {
        state = DimensionState::Indeterminate;
    }
}

DimensionState compare_source_location(
    const SourceLocationIdentity& expected,
    const SourceLocationIdentity& actual,
    std::vector<SourcePackageMismatchReason>& reasons) {
    DimensionState state = DimensionState::Matched;
    if(expected.kind() != actual.kind()) {
        mark_mismatched(state);
        append_reason(
            reasons,
            SourcePackageMismatchReason::SourceLocationMismatch);
        return state;
    }

    if(expected.state() == SourceLocationState::Unavailable ||
       actual.state() == SourceLocationState::Unavailable) {
        mark_indeterminate(state);
        append_reason(
            reasons,
            SourcePackageMismatchReason::SourceLocationUnavailable);
    }
    if(expected.state() == SourceLocationState::Unknown ||
       actual.state() == SourceLocationState::Unknown) {
        mark_indeterminate(state);
        append_reason(
            reasons,
            SourcePackageMismatchReason::SourceLocationUnknown);
    }
    if(state != DimensionState::Matched) return state;

    const std::string* expected_value = expected.value();
    const std::string* actual_value = actual.value();
    if(expected_value == nullptr || actual_value == nullptr) {
        mark_indeterminate(state);
        append_reason(
            reasons,
            SourcePackageMismatchReason::SourceLocationUnavailable);
        return state;
    }
    if(*expected_value != *actual_value) {
        mark_mismatched(state);
        append_reason(
            reasons,
            SourcePackageMismatchReason::SourceLocationMismatch);
    }
    return state;
}

DimensionState compare_source(
    const PackageSourceIdentity& expected,
    const PackageSourceIdentity& actual,
    std::vector<SourcePackageMismatchReason>& reasons) {
    DimensionState state = DimensionState::Matched;
    if(expected.kind() != actual.kind()) {
        mark_mismatched(state);
        append_reason(reasons, SourcePackageMismatchReason::SourceKindMismatch);
    } else if(expected.kind() == PackageSourceKind::Repository) {
        const std::string* expected_repository = expected.repository_name();
        const std::string* actual_repository = actual.repository_name();
        if(expected_repository == nullptr || actual_repository == nullptr ||
           *expected_repository != *actual_repository) {
            mark_mismatched(state);
            append_reason(
                reasons,
                SourcePackageMismatchReason::RepositoryMismatch);
        }
    }

    const DimensionState location = compare_source_location(
        expected.location(), actual.location(), reasons);
    if(location == DimensionState::Mismatched) {
        mark_mismatched(state);
    } else if(location == DimensionState::Indeterminate) {
        mark_indeterminate(state);
    }
    return state;
}

DimensionState compare_revision(
    const SourceRevisionIdentity& expected,
    const SourceRevisionIdentity& actual,
    std::vector<SourcePackageMismatchReason>& reasons) {
    if(expected.state() == SourceRevisionState::Known &&
       actual.state() == SourceRevisionState::Known) {
        const GitObjectFormat* expected_format = expected.git_object_format();
        const GitObjectFormat* actual_format = actual.git_object_format();
        const std::string* expected_commit = expected.git_commit();
        const std::string* actual_commit = actual.git_commit();
        if(expected_format != nullptr && actual_format != nullptr &&
           expected_commit != nullptr && actual_commit != nullptr &&
           *expected_format == *actual_format &&
           *expected_commit == *actual_commit) {
            return DimensionState::Matched;
        }
        append_reason(
            reasons, SourcePackageMismatchReason::SourceCommitMismatch);
        return DimensionState::Mismatched;
    }

    bool has_unknown = false;
    bool has_unavailable = false;
    for(const SourceRevisionState state : {expected.state(), actual.state()}) {
        switch(state) {
            case SourceRevisionState::Unknown:
                has_unknown = true;
                append_reason(
                    reasons,
                    SourcePackageMismatchReason::SourceRevisionUnknown);
                break;
            case SourceRevisionState::Unavailable:
                has_unavailable = true;
                append_reason(
                    reasons,
                    SourcePackageMismatchReason::SourceRevisionUnavailable);
                break;
            case SourceRevisionState::Absent:
                append_reason(
                    reasons,
                    SourcePackageMismatchReason::SourceRevisionAbsent);
                break;
            case SourceRevisionState::Inapplicable:
                append_reason(
                    reasons,
                    SourcePackageMismatchReason::SourceRevisionInapplicable);
                break;
            case SourceRevisionState::Known:
                break;
        }
    }
    if(has_unknown || has_unavailable) return DimensionState::Indeterminate;
    if(expected.state() == SourceRevisionState::Absent &&
       actual.state() == SourceRevisionState::Absent) {
        return DimensionState::Indeterminate;
    }
    if(expected.state() == SourceRevisionState::Inapplicable &&
       actual.state() == SourceRevisionState::Inapplicable) {
        return DimensionState::Inapplicable;
    }

    append_reason(
        reasons,
        SourcePackageMismatchReason::SourceRevisionStateMismatch);
    return DimensionState::Mismatched;
}

DimensionState compare_version(
    const PackageVersionIdentity& expected,
    const PackageVersionIdentity& actual,
    std::vector<SourcePackageMismatchReason>& reasons) {
    if(expected.state() == PackageVersionState::Unavailable ||
       actual.state() == PackageVersionState::Unavailable) {
        append_reason(
            reasons,
            SourcePackageMismatchReason::PackageVersionUnavailable);
        return DimensionState::Indeterminate;
    }
    if(expected.state() == PackageVersionState::Unknown ||
       actual.state() == PackageVersionState::Unknown) {
        append_reason(
            reasons,
            SourcePackageMismatchReason::PackageVersionUnknown);
        return DimensionState::Indeterminate;
    }

    const std::string* expected_version = expected.full_version();
    const std::string* actual_version = actual.full_version();
    if(expected_version == nullptr || actual_version == nullptr) {
        append_reason(
            reasons,
            SourcePackageMismatchReason::PackageVersionUnavailable);
        return DimensionState::Indeterminate;
    }
    if(*expected_version != *actual_version) {
        append_reason(
            reasons,
            SourcePackageMismatchReason::PackageVersionMismatch);
        return DimensionState::Mismatched;
    }
    return DimensionState::Matched;
}

DimensionState compare_architecture(
    const PackageArchitectureIdentity& expected,
    const PackageArchitectureIdentity& actual,
    std::vector<SourcePackageMismatchReason>& reasons) {
    if(expected.state() == PackageArchitectureState::Unavailable ||
       actual.state() == PackageArchitectureState::Unavailable) {
        append_reason(
            reasons,
            SourcePackageMismatchReason::ArchitectureUnavailable);
        return DimensionState::Indeterminate;
    }
    if(expected.state() == PackageArchitectureState::Unknown ||
       actual.state() == PackageArchitectureState::Unknown) {
        append_reason(
            reasons,
            SourcePackageMismatchReason::ArchitectureUnknown);
        return DimensionState::Indeterminate;
    }
    if(expected.architectures() != actual.architectures()) {
        append_reason(
            reasons,
            SourcePackageMismatchReason::ArchitectureMismatch);
        return DimensionState::Mismatched;
    }
    return DimensionState::Matched;
}

bool is_indeterminate(DimensionState state) noexcept {
    return state == DimensionState::Indeterminate ||
           state == DimensionState::Inapplicable;
}

SourcePackageCompatibilityKind classify(
    DimensionState source,
    DimensionState package_base,
    DimensionState package_child,
    DimensionState revision,
    DimensionState version,
    DimensionState architecture) noexcept {
    if(is_indeterminate(source) || is_indeterminate(package_base) ||
       is_indeterminate(package_child)) {
        return SourcePackageCompatibilityKind::Indeterminate;
    }
    if(source == DimensionState::Mismatched ||
       package_base == DimensionState::Mismatched) {
        return SourcePackageCompatibilityKind::Incompatible;
    }
    if(package_child == DimensionState::Mismatched) {
        return SourcePackageCompatibilityKind::SamePackageBase;
    }

    if(is_indeterminate(revision) || is_indeterminate(version) ||
       is_indeterminate(architecture)) {
        return SourcePackageCompatibilityKind::Indeterminate;
    }
    if(revision == DimensionState::Mismatched ||
       version == DimensionState::Mismatched ||
       architecture == DimensionState::Mismatched) {
        return SourcePackageCompatibilityKind::SamePackageChild;
    }
    return SourcePackageCompatibilityKind::ExactMatch;
}

} // namespace

SourcePackageCompatibilityEvaluation::SourcePackageCompatibilityEvaluation(
    SourcePackageCompatibilityKind kind,
    SourcePackageCompatibilityDimensionState source_state,
    SourcePackageCompatibilityDimensionState package_base_state,
    SourcePackageCompatibilityDimensionState package_child_state,
    SourcePackageCompatibilityDimensionState revision_state,
    SourcePackageCompatibilityDimensionState version_state,
    SourcePackageCompatibilityDimensionState architecture_state,
    std::vector<SourcePackageMismatchReason> reasons) noexcept
    : kind_(kind), source_state_(source_state),
      package_base_state_(package_base_state),
      package_child_state_(package_child_state),
      revision_state_(revision_state), version_state_(version_state),
      architecture_state_(architecture_state), reasons_(std::move(reasons)) {
}

SourcePackageCompatibilityKind
SourcePackageCompatibilityEvaluation::kind() const noexcept {
    return kind_;
}

SourcePackageCompatibilityDimensionState
SourcePackageCompatibilityEvaluation::source_state() const noexcept {
    return source_state_;
}

SourcePackageCompatibilityDimensionState
SourcePackageCompatibilityEvaluation::package_base_state() const noexcept {
    return package_base_state_;
}

SourcePackageCompatibilityDimensionState
SourcePackageCompatibilityEvaluation::package_child_state() const noexcept {
    return package_child_state_;
}

SourcePackageCompatibilityDimensionState
SourcePackageCompatibilityEvaluation::revision_state() const noexcept {
    return revision_state_;
}

SourcePackageCompatibilityDimensionState
SourcePackageCompatibilityEvaluation::version_state() const noexcept {
    return version_state_;
}

SourcePackageCompatibilityDimensionState
SourcePackageCompatibilityEvaluation::architecture_state() const noexcept {
    return architecture_state_;
}

const std::vector<SourcePackageMismatchReason>&
SourcePackageCompatibilityEvaluation::reasons() const noexcept {
    return reasons_;
}

bool SourcePackageCompatibilityEvaluation::is_exact_match() const noexcept {
    return kind_ == SourcePackageCompatibilityKind::ExactMatch;
}

SourcePackageCompatibilityEvaluation evaluate_source_package_compatibility(
    const SourceAwarePackageIdentity& expected,
    const SourceAwarePackageIdentity& actual) {
    std::vector<SourcePackageMismatchReason> reasons;
    const PackageBaseIdentity& expected_base =
        expected.package().package_base();
    const PackageBaseIdentity& actual_base = actual.package().package_base();

    const DimensionState source = compare_source(
        expected_base.source(), actual_base.source(), reasons);

    DimensionState package_base;
    if(expected_base.package_base() != actual_base.package_base()) {
        package_base = DimensionState::Mismatched;
        append_reason(
            reasons, SourcePackageMismatchReason::PackageBaseMismatch);
    } else {
        package_base = source;
    }

    DimensionState package_child;
    if(expected.package().package_name() != actual.package().package_name()) {
        package_child = DimensionState::Mismatched;
        append_reason(
            reasons, SourcePackageMismatchReason::PackageChildMismatch);
    } else {
        package_child = package_base;
    }

    const DimensionState revision = compare_revision(
        expected.source_revision(), actual.source_revision(), reasons);
    const DimensionState version = compare_version(
        expected.package_version(), actual.package_version(), reasons);
    const DimensionState architecture = compare_architecture(
        expected.architecture(), actual.architecture(), reasons);

    return SourcePackageCompatibilityEvaluation(
        classify(
            source, package_base, package_child, revision, version,
            architecture),
        source, package_base, package_child, revision, version,
        architecture, std::move(reasons));
}
