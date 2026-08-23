#include "operation_state_model.hpp"

#include <utility>

PackageStateObservationValue project_package_state_observation(
        PackageStateChange state_change,
        ObservationReason unverified_reason) noexcept {
    switch(state_change) {
    case PackageStateChange::Changed:
        return PackageStateObservationValue{
                PackageStateObservation::Changed, std::nullopt};
    case PackageStateChange::NoChange:
        return PackageStateObservationValue{
                PackageStateObservation::VerifiedUnchanged, std::nullopt};
    case PackageStateChange::Unknown:
        return PackageStateObservationValue{
                unverified_reason == ObservationReason::PhaseNotAttempted
                        ? PackageStateObservation::NotObserved
                        : PackageStateObservation::Unverified,
                unverified_reason};
    }
    return PackageStateObservationValue{
            PackageStateObservation::Unverified,
            ObservationReason::InconsistentEvidence};
}

OperationStateProjection project_operation_state(
        const OperationStateProjectionInput& input) noexcept {
    PackageStateObservationValue observation =
            project_package_state_observation(
                    input.package_state_change,
                    input.unverified_reason);

    OperationOutcome outcome = OperationOutcome::Failed;
    if(input.is_inconsistent) {
        outcome = OperationOutcome::Inconsistent;
    } else if(input.is_success) {
        // A successful operation with unverified state is still Succeeded.
        // No relevant work or authoritative unchanged evidence is required
        // before the outcome may be called NoOp.
        const bool is_no_op =
                input.no_op_basis ==
                        std::optional<NoOpBasis>{NoOpBasis::NoRelevantWork} ||
                (input.no_op_basis ==
                         std::optional<NoOpBasis>{
                                 NoOpBasis::VerifiedUnchanged} &&
                 observation.state ==
                         PackageStateObservation::VerifiedUnchanged);
        outcome = is_no_op
                ? OperationOutcome::NoOp
                : OperationOutcome::Succeeded;
    } else if(input.is_blocked) {
        outcome = OperationOutcome::Blocked;
    } else if(input.has_partial_completion) {
        outcome = OperationOutcome::PartialFailure;
    } else if(!input.was_attempted) {
        outcome = OperationOutcome::NotAttempted;
    }

    return OperationStateProjection{
            outcome, std::move(observation),
            outcome == OperationOutcome::NoOp
                    ? input.no_op_basis
                    : std::nullopt};
}
