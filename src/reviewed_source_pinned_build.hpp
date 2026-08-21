#pragma once

#include "reviewed_source_acceptance.hpp"
#include "reviewed_source_state_store.hpp"
#include "trusted_git.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <system_error>
#include <variant>

// POLICY(#411): Slice 4B owns the production-disconnected transition from an
// explicitly accepted upstream review to an exact leased checkout, reviewed
// state CAS, and pinned build authority. It does not run an editor, makepkg,
// pacman, or any production source-build route.

class ReviewedSourcePackageBaseLease;
class AcceptedReviewedSourceCheckout;
class AlreadyReviewedSourceCheckout;
class PinnedReviewedSourceBuild;
struct ReviewedSourcePinnedCheckoutFailure;
struct ReviewedSourcePublicationConflict;
struct ReviewedSourcePublicationUncertain;
struct ReviewedSourcePublicationUnsafeHistory;
struct ReviewedSourcePublicationFailure;
struct ReviewedSourcePostPublicationCheckoutFailure;

using AcceptedReviewedSourceCheckoutResult = std::variant<
        AcceptedReviewedSourceCheckout,
        ReviewedSourcePinnedCheckoutFailure>;

using AlreadyReviewedSourceCheckoutResult = std::variant<
        AlreadyReviewedSourceCheckout,
        ReviewedSourcePinnedCheckoutFailure>;

using ReviewedSourcePublicationResult = std::variant<
        PinnedReviewedSourceBuild,
        ReviewedSourcePublicationConflict,
        ReviewedSourcePublicationUncertain,
        ReviewedSourcePublicationUnsafeHistory,
        ReviewedSourcePublicationFailure,
        ReviewedSourcePostPublicationCheckoutFailure>;

// These two exact transitions acquire a sealed PackageBase lease, materialize
// the detached target, clean all residue, and prove the final checkout.
[[nodiscard]] AcceptedReviewedSourceCheckoutResult
materialize_accepted_reviewed_source_checkout(
        AcceptedReviewedSourceTarget target,
        const ValidatedCachePath& checkout);

[[nodiscard]] AlreadyReviewedSourceCheckoutResult
materialize_already_reviewed_source_checkout(
        ReviewedSourceAlreadyReviewedContinue target,
        const ValidatedCachePath& checkout);

// Consumes exact checkout + accepted authority. A CAS conflict is re-read only
// to classify an exact same-target idempotent success; it is never retried as
// another write. PublishedUncertain is propagated without retry.
[[nodiscard]] ReviewedSourcePublicationResult
publish_accepted_reviewed_source_checkout(
        AcceptedReviewedSourceCheckout target);

// AlreadyReviewed never rewrites state. It re-reads the store after exact
// checkout materialization and produces build authority only while the current
// proven observation is still the same exact target.
[[nodiscard]] ReviewedSourcePublicationResult
confirm_already_reviewed_source_checkout(
        AlreadyReviewedSourceCheckout target);

enum class ReviewedSourcePinnedCheckoutFailureReason {
    InvalidAcceptedCapability,
    InvalidAlreadyReviewedCapability,
    PackageBaseMismatch,
    CheckoutBoundaryInvalid,
    LeaseContended,
    LeaseUnavailable,
    GitMaterializationFailed,
};

struct ReviewedSourcePinnedCheckoutFailure {
    ReviewedSourcePinnedCheckoutFailureReason reason;
    std::optional<std::error_code>             system_error;
    std::optional<TrustedCacheFailure>         boundary_failure;
    std::optional<TrustedGitPinnedCheckoutFailure> git_failure;

    bool operator==(const ReviewedSourcePinnedCheckoutFailure&) const =
            default;
};

// A successful non-blocking flock acquisition over the exact retained
// PackageBase directory. The descriptor number is not caller evidence: only
// acquire_reviewed_source_package_base_lease() derives and seals it.
class ReviewedSourcePackageBaseLease final {
public:
    ReviewedSourcePackageBaseLease() = delete;
    ReviewedSourcePackageBaseLease(
            const ReviewedSourcePackageBaseLease&) = delete;
    ReviewedSourcePackageBaseLease(
            ReviewedSourcePackageBaseLease&& other) noexcept;
    ReviewedSourcePackageBaseLease& operator=(
            const ReviewedSourcePackageBaseLease&) = delete;
    ReviewedSourcePackageBaseLease& operator=(
            ReviewedSourcePackageBaseLease&&) = delete;
    ~ReviewedSourcePackageBaseLease() noexcept;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] const ValidatedCachePath& path() const;
    [[nodiscard]] std::uintmax_t device() const;
    [[nodiscard]] std::uintmax_t inode() const;
    void require_unchanged_identity() const;

private:
    friend ReviewedSourcePackageBaseLease
    acquire_reviewed_source_package_base_lease(
            RetainedTrustedCacheDirectory directory);
    friend TrustedGitPinnedCheckoutResult
    trusted_git_materialize_pinned_checkout(
            const ValidatedCachePath& checkout,
            AurReviewedSourceReviewIdentity identity,
            const ReviewedSourcePackageBaseLease& lease);

    ReviewedSourcePackageBaseLease(
            RetainedTrustedCacheDirectory directory,
            int descriptor) noexcept;

    RetainedTrustedCacheDirectory directory_;
    int                           descriptor_ = -1;
    bool                          valid_ = true;
};

// Throws std::system_error when flock cannot be acquired. The retained
// directory is consumed so every successful capability is one single owner.
[[nodiscard]] ReviewedSourcePackageBaseLease
acquire_reviewed_source_package_base_lease(
        RetainedTrustedCacheDirectory directory);

class AcceptedReviewedSourceCheckout final {
public:
    AcceptedReviewedSourceCheckout() = delete;
    AcceptedReviewedSourceCheckout(
            const AcceptedReviewedSourceCheckout&) = delete;
    AcceptedReviewedSourceCheckout(
            AcceptedReviewedSourceCheckout&& other) noexcept;
    AcceptedReviewedSourceCheckout& operator=(
            const AcceptedReviewedSourceCheckout&) = delete;
    AcceptedReviewedSourceCheckout& operator=(
            AcceptedReviewedSourceCheckout&& other) noexcept;
    ~AcceptedReviewedSourceCheckout();

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] const AurReviewedSourceReviewIdentity& identity() const;
    [[nodiscard]] const ReviewedSourceExpectedStateObservation&
    expected_state_observation() const;
    [[nodiscard]] const std::filesystem::path& checkout_path() const;
    [[nodiscard]] std::uintmax_t checkout_device() const;
    [[nodiscard]] std::uintmax_t checkout_inode() const;

private:
    friend AcceptedReviewedSourceCheckoutResult
    materialize_accepted_reviewed_source_checkout(
            AcceptedReviewedSourceTarget target,
            const ValidatedCachePath& checkout);
    friend ReviewedSourcePublicationResult
    publish_accepted_reviewed_source_checkout(
            AcceptedReviewedSourceCheckout target);

    struct State;

    AcceptedReviewedSourceCheckout(
            AcceptedReviewedSourceTarget target,
            ReviewedSourcePackageBaseLease lease,
            TrustedGitPinnedCheckout checkout);

    [[nodiscard]] const State& require_state() const;
    [[nodiscard]] State& require_state();

    std::unique_ptr<State> state_;
};

class AlreadyReviewedSourceCheckout final {
public:
    AlreadyReviewedSourceCheckout() = delete;
    AlreadyReviewedSourceCheckout(
            const AlreadyReviewedSourceCheckout&) = delete;
    AlreadyReviewedSourceCheckout(
            AlreadyReviewedSourceCheckout&& other) noexcept;
    AlreadyReviewedSourceCheckout& operator=(
            const AlreadyReviewedSourceCheckout&) = delete;
    AlreadyReviewedSourceCheckout& operator=(
            AlreadyReviewedSourceCheckout&& other) noexcept;
    ~AlreadyReviewedSourceCheckout();

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] const AurReviewedSourceReviewIdentity& identity() const;
    [[nodiscard]] const ReviewedSourceExpectedStateObservation&
    expected_state_observation() const;
    [[nodiscard]] const std::filesystem::path& checkout_path() const;
    [[nodiscard]] std::uintmax_t checkout_device() const;
    [[nodiscard]] std::uintmax_t checkout_inode() const;

private:
    friend AlreadyReviewedSourceCheckoutResult
    materialize_already_reviewed_source_checkout(
            ReviewedSourceAlreadyReviewedContinue target,
            const ValidatedCachePath& checkout);
    friend ReviewedSourcePublicationResult
    confirm_already_reviewed_source_checkout(
            AlreadyReviewedSourceCheckout target);

    struct State;

    AlreadyReviewedSourceCheckout(
            ReviewedSourceAlreadyReviewedContinue target,
            ReviewedSourcePackageBaseLease lease,
            TrustedGitPinnedCheckout checkout);

    [[nodiscard]] const State& require_state() const;

    std::unique_ptr<State> state_;
};

enum class ReviewedSourcePublicationStatus {
    Published,
    AlreadyPublishedSameTarget,
};

class PinnedReviewedSourceBuild final {
public:
    PinnedReviewedSourceBuild() = delete;
    PinnedReviewedSourceBuild(const PinnedReviewedSourceBuild&) = delete;
    PinnedReviewedSourceBuild(PinnedReviewedSourceBuild&& other) noexcept;
    PinnedReviewedSourceBuild& operator=(
            const PinnedReviewedSourceBuild&) = delete;
    PinnedReviewedSourceBuild& operator=(
            PinnedReviewedSourceBuild&& other) noexcept;
    ~PinnedReviewedSourceBuild();

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] ReviewedSourcePublicationStatus publication_status() const;
    [[nodiscard]] const AurReviewedSourceReviewIdentity& identity() const;
    [[nodiscard]] const SourceRevisionIdentity& reviewed_upstream_base_revision()
            const;
    [[nodiscard]] const ReviewedSourceState& reviewed_state() const;
    [[nodiscard]] const ReviewedSourceStateObservedRecord& published_record()
            const;
    [[nodiscard]] const std::filesystem::path& checkout_path() const;
    [[nodiscard]] std::uintmax_t checkout_device() const;
    [[nodiscard]] std::uintmax_t checkout_inode() const;

private:
    friend ReviewedSourcePublicationResult
    publish_accepted_reviewed_source_checkout(
            AcceptedReviewedSourceCheckout target);
    friend ReviewedSourcePublicationResult
    confirm_already_reviewed_source_checkout(
            AlreadyReviewedSourceCheckout target);

    struct State;

    PinnedReviewedSourceBuild(
            AcceptedReviewedSourceCheckout checkout,
            ReviewedSourcePublicationStatus publication_status,
            ReviewedSourceState state,
            ReviewedSourceStateObservedRecord observed);
    PinnedReviewedSourceBuild(
            AlreadyReviewedSourceCheckout checkout,
            ReviewedSourceState state,
            ReviewedSourceStateObservedRecord observed);

    [[nodiscard]] const State& require_state() const;

    std::unique_ptr<State> state_;
};

struct ReviewedSourcePublicationConflict {
    AurReviewedSourceReviewIdentity identity;
    ReviewedSourceStateStoreRead    current;
};

struct ReviewedSourcePublicationUncertain {
    ReviewedSourceStateStorePublishedUncertain store_result;
};

struct ReviewedSourcePublicationUnsafeHistory {
    ReviewedSourceStateStoreUnsafeHistory store_result;
};

enum class ReviewedSourcePublicationFailureReason {
    InvalidCheckoutCapability,
    StoreFailure,
    ConflictObservationFailure,
    CheckoutRevalidationFailed,
    InconsistentStoreResult,
};

struct ReviewedSourcePublicationFailure {
    ReviewedSourcePublicationFailureReason reason;
    std::optional<ReviewedSourceStateStoreFailure> store_failure;
    std::optional<TrustedGitPinnedCheckoutFailure> checkout_failure;
};

// A successful/confirmed state result is never rolled back merely because the
// leased checkout lost its exact proof before pinned authority could be
// returned. This arm reports both facts and carries no build capability.
struct ReviewedSourcePostPublicationCheckoutFailure {
    ReviewedSourcePublicationStatus       publication_status;
    ReviewedSourceState                   state;
    ReviewedSourceStateObservedRecord     observed;
    TrustedGitPinnedCheckoutFailure       checkout_failure;
};
