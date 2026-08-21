#pragma once

#include "reviewed_source_projection.hpp"
#include "reviewed_source_state_store.hpp"

#include <optional>
#include <string>
#include <variant>

// POLICY(#411): Slice 4A owns only typed lifecycle observation. It does not
// publish reviewed state, materialize a checkout, authorize a build, or connect
// any production route.

class ReviewedSourceLifecycleAuthority;

// PackageBase is the review/state unit. The canonical AUR source and exact
// upstream base revision remain existing typed identities rather than being
// flattened into a derived string key.
class AurReviewedSourceReviewIdentity final {
public:
    AurReviewedSourceReviewIdentity() = delete;
    AurReviewedSourceReviewIdentity(
            const AurReviewedSourceReviewIdentity&) = default;
    AurReviewedSourceReviewIdentity(
            AurReviewedSourceReviewIdentity&&) noexcept = default;
    AurReviewedSourceReviewIdentity& operator=(
            const AurReviewedSourceReviewIdentity&) = default;
    AurReviewedSourceReviewIdentity& operator=(
            AurReviewedSourceReviewIdentity&&) noexcept = default;
    ~AurReviewedSourceReviewIdentity() = default;

    [[nodiscard]] static AurReviewedSourceReviewIdentity make(
            PackageBaseIdentity package_base,
            SourceRevisionIdentity target_revision);

    [[nodiscard]] const PackageBaseIdentity& package_base() const noexcept;
    [[nodiscard]] const PackageSourceIdentity& source() const noexcept;
    [[nodiscard]] const std::string& canonical_git_remote() const noexcept;
    [[nodiscard]] const SourceRevisionIdentity& target_revision()
            const noexcept;
    [[nodiscard]] GitObjectFormat git_object_format() const noexcept;

    bool operator==(const AurReviewedSourceReviewIdentity&) const = default;

private:
    AurReviewedSourceReviewIdentity(
            PackageBaseIdentity package_base,
            SourceRevisionIdentity target_revision) noexcept;

    PackageBaseIdentity    package_base_;
    SourceRevisionIdentity target_revision_;
};

// Exact read-time CAS input. Missing alone has no observed record; every
// observed document retains the exact record identity and raw bytes returned by
// the store, including invalid/corrupted/source-mismatched records.
class ReviewedSourceExpectedStateObservation final {
public:
    ReviewedSourceExpectedStateObservation() = delete;
    ReviewedSourceExpectedStateObservation(
            const ReviewedSourceExpectedStateObservation&) = delete;
    ReviewedSourceExpectedStateObservation(
            ReviewedSourceExpectedStateObservation&&) noexcept = default;
    ReviewedSourceExpectedStateObservation& operator=(
            const ReviewedSourceExpectedStateObservation&) = delete;
    ReviewedSourceExpectedStateObservation& operator=(
            ReviewedSourceExpectedStateObservation&&) noexcept = default;
    ~ReviewedSourceExpectedStateObservation() = default;

    [[nodiscard]] const ReviewedSourceStateObservation& observation()
            const noexcept;
    [[nodiscard]] const std::optional<ReviewedSourceStateObservedRecord>&
    observed_record() const noexcept;
    [[nodiscard]] const ReviewedSourceStateStoreRead& store_read()
            const noexcept;

private:
    friend class ReviewedSourceLifecycleAuthority;

    explicit ReviewedSourceExpectedStateObservation(
            ReviewedSourceStateStoreRead store_read) noexcept;

    ReviewedSourceStateStoreRead store_read_;
};

enum class ReviewedSourceReviewRequirementKind {
    InitialFullReview,
    UpdateReview,
    AbnormalStateRebindFullReview,
};

enum class ReviewedSourceAbnormalStateReason {
    Invalid,
    Corrupted,
    SourceMismatch,
};

struct ReviewedSourceLifecycleInitialFullReview {
    bool operator==(
            const ReviewedSourceLifecycleInitialFullReview&) const = default;
};

struct ReviewedSourceLifecycleAlreadyReviewed {
    bool operator==(
            const ReviewedSourceLifecycleAlreadyReviewed&) const = default;
};

struct ReviewedSourceLifecycleUpdateReview {
    SourceRevisionIdentity          baseline;
    ReviewedSourceHistoryRelation   relation;

    bool operator==(
            const ReviewedSourceLifecycleUpdateReview&) const = default;
};

struct ReviewedSourceLifecycleRebaselineFullReview {
    SourceRevisionIdentity                   unavailable_baseline;
    ReviewedSourceBaselineUnavailableReason reason;

    bool operator==(
            const ReviewedSourceLifecycleRebaselineFullReview&) const =
            default;
};

struct ReviewedSourceLifecycleAbnormalStateRebindFullReview {
    ReviewedSourceAbnormalStateReason reason;

    bool operator==(
            const ReviewedSourceLifecycleAbnormalStateRebindFullReview&)
            const = default;
};

enum class ReviewedSourceFatalStateReason {
    UnsupportedFuture,
    UnsafeHistory,
    StoreFailure,
    InconsistentStoreObservation,
};

struct ReviewedSourceLifecycleFatalState {
    ReviewedSourceFatalStateReason reason;

    bool operator==(
            const ReviewedSourceLifecycleFatalState&) const = default;
};

using ReviewedSourceIntegrationLifecycle = std::variant<
        ReviewedSourceLifecycleInitialFullReview,
        ReviewedSourceLifecycleAlreadyReviewed,
        ReviewedSourceLifecycleUpdateReview,
        ReviewedSourceLifecycleRebaselineFullReview,
        ReviewedSourceLifecycleAbnormalStateRebindFullReview,
        ReviewedSourceLifecycleFatalState>;

enum class ReviewedSourceOperationStopReason {
    UnsupportedFuture,
    UnsafeHistory,
    StoreFailure,
    InconsistentStoreObservation,
    PackageBaseMismatch,
    SourceIdentityMismatch,
    GitObjectFormatMismatch,
    TargetRevisionMismatch,
    BaselineMismatch,
    LifecycleMismatch,
    MaterializationFailure,
    PresentationFailure,
    PresentationOutputFailure,
    ManualInspectionRequired,
    SensitiveSourceUnrenderable,
    NonExplicitAcceptance,
    ExplicitCancellation,
    EndOfInput,
    InputFailure,
};

// A stop result intentionally carries no review target, expected CAS record,
// acceptance token, or build continuation capability.
class ReviewedSourceOperationStop final {
public:
    ReviewedSourceOperationStop() = delete;

    [[nodiscard]] static ReviewedSourceOperationStop make(
            ReviewedSourceOperationStopReason reason) noexcept;
    [[nodiscard]] static ReviewedSourceOperationStop fatal(
            ReviewedSourceFatalStateReason reason) noexcept;

    [[nodiscard]] ReviewedSourceOperationStopReason reason() const noexcept;
    [[nodiscard]] const std::optional<ReviewedSourceIntegrationLifecycle>&
    lifecycle() const noexcept;

    bool operator==(const ReviewedSourceOperationStop&) const = default;

private:
    ReviewedSourceOperationStop(
            ReviewedSourceOperationStopReason reason,
            std::optional<ReviewedSourceIntegrationLifecycle> lifecycle)
        noexcept;

    ReviewedSourceOperationStopReason                  reason_;
    std::optional<ReviewedSourceIntegrationLifecycle> lifecycle_;
};

class ReviewedSourceReviewRequirement final {
public:
    ReviewedSourceReviewRequirement() = delete;
    ReviewedSourceReviewRequirement(
            const ReviewedSourceReviewRequirement&) = delete;
    ReviewedSourceReviewRequirement(
            ReviewedSourceReviewRequirement&&) noexcept = default;
    ReviewedSourceReviewRequirement& operator=(
            const ReviewedSourceReviewRequirement&) = delete;
    ReviewedSourceReviewRequirement& operator=(
            ReviewedSourceReviewRequirement&&) noexcept = default;
    ~ReviewedSourceReviewRequirement() = default;

    [[nodiscard]] const AurReviewedSourceReviewIdentity& identity()
            const noexcept;
    [[nodiscard]] ReviewedSourceReviewRequirementKind kind() const noexcept;
    [[nodiscard]] const SourceRevisionIdentity* baseline() const noexcept;
    [[nodiscard]] const ReviewedSourceAbnormalStateReason* abnormal_reason()
            const noexcept;
    [[nodiscard]] const ReviewedSourceExpectedStateObservation&
    expected_state_observation() const noexcept;

private:
    friend class ReviewedSourceLifecycleAuthority;

    ReviewedSourceReviewRequirement(
            AurReviewedSourceReviewIdentity identity,
            ReviewedSourceReviewRequirementKind kind,
            std::optional<SourceRevisionIdentity> baseline,
            std::optional<ReviewedSourceAbnormalStateReason> abnormal_reason,
            ReviewedSourceExpectedStateObservation expected) noexcept;

    AurReviewedSourceReviewIdentity                    identity_;
    ReviewedSourceReviewRequirementKind                kind_;
    std::optional<SourceRevisionIdentity>               baseline_;
    std::optional<ReviewedSourceAbnormalStateReason>    abnormal_reason_;
    ReviewedSourceExpectedStateObservation              expected_;
};

// AlreadyReviewed is a typed continue disposition. It deliberately has no
// presentation/acceptance step and is not reviewed-state rewrite authority.
class ReviewedSourceAlreadyReviewedContinue final {
public:
    ReviewedSourceAlreadyReviewedContinue() = delete;
    ReviewedSourceAlreadyReviewedContinue(
            const ReviewedSourceAlreadyReviewedContinue&) = delete;
    ReviewedSourceAlreadyReviewedContinue(
            ReviewedSourceAlreadyReviewedContinue&&) noexcept = default;
    ReviewedSourceAlreadyReviewedContinue& operator=(
            const ReviewedSourceAlreadyReviewedContinue&) = delete;
    ReviewedSourceAlreadyReviewedContinue& operator=(
            ReviewedSourceAlreadyReviewedContinue&&) noexcept = default;
    ~ReviewedSourceAlreadyReviewedContinue() = default;

    [[nodiscard]] const AurReviewedSourceReviewIdentity& identity()
            const noexcept;
    [[nodiscard]] const ReviewedSourceIntegrationLifecycle& lifecycle()
            const noexcept;
    [[nodiscard]] const ReviewedSourceExpectedStateObservation&
    expected_state_observation() const noexcept;

private:
    friend class ReviewedSourceLifecycleAuthority;

    ReviewedSourceAlreadyReviewedContinue(
            AurReviewedSourceReviewIdentity identity,
            ReviewedSourceExpectedStateObservation expected) noexcept;

    AurReviewedSourceReviewIdentity       identity_;
    ReviewedSourceIntegrationLifecycle    lifecycle_;
    ReviewedSourceExpectedStateObservation expected_;
};

using ReviewedSourceLifecyclePlanResult = std::variant<
        ReviewedSourceReviewRequirement,
        ReviewedSourceAlreadyReviewedContinue,
        ReviewedSourceOperationStop>;

// Maps only the exact store result observed at review start. Git history
// relation and baseline object availability are bound later from the verified
// 3B review rather than being flattened into this observation phase.
[[nodiscard]] ReviewedSourceLifecyclePlanResult
plan_reviewed_source_lifecycle(
        AurReviewedSourceReviewIdentity identity,
        ReviewedSourceStateStoreReadResult store_result);
