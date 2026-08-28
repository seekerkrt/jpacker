#pragma once

#include "package_metadata.hpp"

#include <optional>

struct UpgradeAllOperationResult;

enum class OperationOutcome {
    Succeeded,
    NoOp,
    Blocked,
    PartialFailure,
    Failed,
    NotAttempted,
    Inconsistent,
};

enum class PackageStateObservation {
    Changed,
    VerifiedUnchanged,
    Unverified,
    NotObserved,
};

enum class ObservationReason {
    BeforeSnapshotUnavailable,
    AfterSnapshotUnavailable,
    ObservationNotPrepared,
    PhaseNotAttempted,
    OperationFailed,
    AuthorityFailure,
    InconsistentEvidence,
};

enum class NoOpBasis {
    NoRelevantWork,
    VerifiedUnchanged,
};

struct PackageStateObservationValue {
    PackageStateObservation state =
        PackageStateObservation::NotObserved;
    std::optional<ObservationReason> reason =
        ObservationReason::ObservationNotPrepared;

    bool operator==(const PackageStateObservationValue&) const = default;
};

struct UpgradeAllPhasePackageStateObservations {
    PackageStateObservationValue system_source;
    PackageStateObservationValue aur;

    bool operator==(
        const UpgradeAllPhasePackageStateObservations&) const = default;
};

struct OperationStateProjectionInput {
    bool is_success = false;
    std::optional<NoOpBasis> no_op_basis;
    bool is_blocked = false;
    bool has_partial_completion = false;
    bool was_attempted = false;
    bool is_inconsistent = false;
    PackageStateChange package_state_change = PackageStateChange::Unknown;
    ObservationReason unverified_reason =
        ObservationReason::ObservationNotPrepared;
};

struct OperationStateProjection {
    OperationOutcome outcome = OperationOutcome::NotAttempted;
    PackageStateObservationValue package_state;
    std::optional<NoOpBasis> no_op_basis;

    bool operator==(const OperationStateProjection&) const = default;
};

PackageStateObservationValue project_package_state_observation(
    PackageStateChange state_change,
    ObservationReason unverified_reason) noexcept;

OperationStateProjection project_operation_state(
    const OperationStateProjectionInput& input) noexcept;

OperationStateProjection project_upgrade_all_operation_state(
    const UpgradeAllOperationResult& result) noexcept;

UpgradeAllPhasePackageStateObservations
project_upgrade_all_phase_package_state_observations(
    const UpgradeAllOperationResult& result) noexcept;
