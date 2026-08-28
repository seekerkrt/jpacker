#pragma once

#include "source_package_identity.hpp"

#include <vector>

enum class SourcePackageCompatibilityKind {
    ExactMatch,
    SamePackageChild,
    SamePackageBase,
    Incompatible,
    Indeterminate,
};

enum class SourcePackageCompatibilityDimensionState {
    Matched,
    Mismatched,
    Indeterminate,
    Inapplicable,
};

enum class SourcePackageMismatchReason {
    SourceKindMismatch,
    RepositoryMismatch,
    SourceLocationMismatch,
    SourceLocationUnknown,
    SourceLocationUnavailable,
    PackageBaseMismatch,
    PackageChildMismatch,
    SourceCommitMismatch,
    SourceRevisionStateMismatch,
    SourceRevisionUnknown,
    SourceRevisionAbsent,
    SourceRevisionUnavailable,
    SourceRevisionInapplicable,
    PackageVersionMismatch,
    PackageVersionUnknown,
    PackageVersionUnavailable,
    ArchitectureMismatch,
    ArchitectureUnknown,
    ArchitectureUnavailable,
};

class SourcePackageCompatibilityEvaluation final {
public:
    SourcePackageCompatibilityEvaluation() = delete;
    SourcePackageCompatibilityEvaluation(
        const SourcePackageCompatibilityEvaluation&) = default;
    SourcePackageCompatibilityEvaluation(
        SourcePackageCompatibilityEvaluation&&) noexcept = default;
    SourcePackageCompatibilityEvaluation& operator=(
        const SourcePackageCompatibilityEvaluation&) = default;
    SourcePackageCompatibilityEvaluation& operator=(
        SourcePackageCompatibilityEvaluation&&) noexcept = default;
    ~SourcePackageCompatibilityEvaluation() = default;

    [[nodiscard]] SourcePackageCompatibilityKind kind() const noexcept;
    [[nodiscard]] SourcePackageCompatibilityDimensionState source_state()
        const noexcept;
    [[nodiscard]] SourcePackageCompatibilityDimensionState package_base_state()
        const noexcept;
    [[nodiscard]] SourcePackageCompatibilityDimensionState package_child_state()
        const noexcept;
    [[nodiscard]] SourcePackageCompatibilityDimensionState revision_state()
        const noexcept;
    [[nodiscard]] SourcePackageCompatibilityDimensionState version_state()
        const noexcept;
    [[nodiscard]] SourcePackageCompatibilityDimensionState architecture_state()
        const noexcept;
    [[nodiscard]] const std::vector<SourcePackageMismatchReason>& reasons()
        const noexcept;

    [[nodiscard]] bool is_exact_match() const noexcept;

    bool operator==(const SourcePackageCompatibilityEvaluation&) const =
        default;

private:
    SourcePackageCompatibilityEvaluation(
        SourcePackageCompatibilityKind kind,
        SourcePackageCompatibilityDimensionState source_state,
        SourcePackageCompatibilityDimensionState package_base_state,
        SourcePackageCompatibilityDimensionState package_child_state,
        SourcePackageCompatibilityDimensionState revision_state,
        SourcePackageCompatibilityDimensionState version_state,
        SourcePackageCompatibilityDimensionState architecture_state,
        std::vector<SourcePackageMismatchReason> reasons) noexcept;

    SourcePackageCompatibilityKind kind_;
    SourcePackageCompatibilityDimensionState source_state_;
    SourcePackageCompatibilityDimensionState package_base_state_;
    SourcePackageCompatibilityDimensionState package_child_state_;
    SourcePackageCompatibilityDimensionState revision_state_;
    SourcePackageCompatibilityDimensionState version_state_;
    SourcePackageCompatibilityDimensionState architecture_state_;
    std::vector<SourcePackageMismatchReason> reasons_;

    friend SourcePackageCompatibilityEvaluation
    evaluate_source_package_compatibility(
        const SourceAwarePackageIdentity& expected,
        const SourceAwarePackageIdentity& actual);
};

// This evaluator is read-only and policy-neutral. In particular, it never
// turns Unknown/Unavailable/Absent/Inapplicable evidence into ExactMatch.
SourcePackageCompatibilityEvaluation evaluate_source_package_compatibility(
    const SourceAwarePackageIdentity& expected,
    const SourceAwarePackageIdentity& actual);
