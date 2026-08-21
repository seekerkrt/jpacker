#include "reviewed_source_pinned_build.hpp"

#include <cerrno>
#include <stdexcept>
#include <sys/file.h>
#include <utility>

ReviewedSourcePackageBaseLease::ReviewedSourcePackageBaseLease(
        RetainedTrustedCacheDirectory directory,
        int descriptor) noexcept
    : directory_(std::move(directory)), descriptor_(descriptor) {}

ReviewedSourcePackageBaseLease::ReviewedSourcePackageBaseLease(
        ReviewedSourcePackageBaseLease&& other) noexcept
    : directory_(std::move(other.directory_)),
      descriptor_(std::exchange(other.descriptor_, -1)),
      valid_(std::exchange(other.valid_, false)) {}

ReviewedSourcePackageBaseLease::~ReviewedSourcePackageBaseLease() noexcept =
        default;

bool ReviewedSourcePackageBaseLease::valid() const noexcept {
    return valid_ && descriptor_ >= 0;
}

const ValidatedCachePath& ReviewedSourcePackageBaseLease::path() const {
    if(!valid()) {
        throw std::logic_error(
                "A moved-from PackageBase lease has no authority.");
    }
    return directory_.path();
}

std::uintmax_t ReviewedSourcePackageBaseLease::device() const {
    return path().device();
}

std::uintmax_t ReviewedSourcePackageBaseLease::inode() const {
    return path().inode();
}

void ReviewedSourcePackageBaseLease::require_unchanged_identity() const {
    if(!valid()) {
        throw std::logic_error(
                "A moved-from PackageBase lease has no authority.");
    }
    directory_.require_unchanged_identity();
}

ReviewedSourcePackageBaseLease
acquire_reviewed_source_package_base_lease(
        RetainedTrustedCacheDirectory directory) {
    directory.require_unchanged_identity();
    const int descriptor = directory.descriptor_;
    int lock_result;
    do {
        lock_result = ::flock(descriptor, LOCK_EX | LOCK_NB);
    } while(lock_result != 0 && errno == EINTR);
    if(lock_result != 0) {
        throw std::system_error(
                errno, std::generic_category(),
                "Failed to acquire reviewed source PackageBase lease");
    }
    directory.require_unchanged_identity();
    return ReviewedSourcePackageBaseLease(
            std::move(directory), descriptor);
}

struct AcceptedReviewedSourceCheckout::State {
    AcceptedReviewedSourceTarget target;
    ReviewedSourcePackageBaseLease lease;
    TrustedGitPinnedCheckout       checkout;

    State(
            AcceptedReviewedSourceTarget value_target,
            ReviewedSourcePackageBaseLease value_lease,
            TrustedGitPinnedCheckout value_checkout)
        : target(std::move(value_target)), lease(std::move(value_lease)),
          checkout(std::move(value_checkout)) {}
};

struct AlreadyReviewedSourceCheckout::State {
    ReviewedSourceAlreadyReviewedContinue target;
    ReviewedSourcePackageBaseLease         lease;
    TrustedGitPinnedCheckout               checkout;

    State(
            ReviewedSourceAlreadyReviewedContinue value_target,
            ReviewedSourcePackageBaseLease value_lease,
            TrustedGitPinnedCheckout value_checkout)
        : target(std::move(value_target)), lease(std::move(value_lease)),
          checkout(std::move(value_checkout)) {}
};

using ReviewedSourceCheckoutAuthority = std::variant<
        AcceptedReviewedSourceCheckout,
        AlreadyReviewedSourceCheckout>;

struct PinnedReviewedSourceBuild::State {
    ReviewedSourceCheckoutAuthority checkout;
    ReviewedSourcePublicationStatus publication_status;
    ReviewedSourceState             state;
    ReviewedSourceStateObservedRecord observed;

    State(
            ReviewedSourceCheckoutAuthority value_checkout,
            ReviewedSourcePublicationStatus value_publication_status,
            ReviewedSourceState value_state,
            ReviewedSourceStateObservedRecord value_observed)
        : checkout(std::move(value_checkout)),
          publication_status(value_publication_status),
          state(std::move(value_state)), observed(std::move(value_observed)) {}
};

namespace {

ReviewedSourcePinnedCheckoutFailure checkout_failure(
        ReviewedSourcePinnedCheckoutFailureReason reason) {
    return ReviewedSourcePinnedCheckoutFailure{
            reason, std::nullopt, std::nullopt, std::nullopt};
}

ReviewedSourcePublicationFailure publication_failure(
        ReviewedSourcePublicationFailureReason reason) {
    return ReviewedSourcePublicationFailure{
            reason, std::nullopt, std::nullopt};
}

ReviewedSourcePublicationFailure store_publication_failure(
        ReviewedSourcePublicationFailureReason reason,
        ReviewedSourceStateStoreFailure failure) {
    ReviewedSourcePublicationFailure result = publication_failure(reason);
    result.store_failure = std::move(failure);
    return result;
}

ReviewedSourcePublicationFailure checkout_publication_failure(
        TrustedGitPinnedCheckoutFailure failure) {
    ReviewedSourcePublicationFailure result = publication_failure(
            ReviewedSourcePublicationFailureReason::
                    CheckoutRevalidationFailed);
    result.checkout_failure = std::move(failure);
    return result;
}

bool checkout_name_matches(
        const ValidatedCachePath& checkout,
        const AurReviewedSourceReviewIdentity& identity) {
    return checkout.exists() && checkout.is_directory() &&
           checkout.canonical_path().filename() ==
                   identity.package_base().package_base();
}

struct LeasedPinnedCheckout {
    ReviewedSourcePackageBaseLease lease;
    TrustedGitPinnedCheckout       checkout;
};

using LeasedPinnedCheckoutResult = std::variant<
        LeasedPinnedCheckout,
        ReviewedSourcePinnedCheckoutFailure>;

LeasedPinnedCheckoutResult acquire_leased_pinned_checkout(
        const AurReviewedSourceReviewIdentity& identity,
        const ValidatedCachePath& checkout) {
    if(!checkout_name_matches(checkout, identity)) {
        return checkout_failure(
                ReviewedSourcePinnedCheckoutFailureReason::
                        PackageBaseMismatch);
    }

    try {
        RetainedTrustedCacheDirectory retained =
                retain_trusted_cache_directory(checkout);
        ReviewedSourcePackageBaseLease lease =
                acquire_reviewed_source_package_base_lease(
                        std::move(retained));
        lease.require_unchanged_identity();

        const ValidatedCachePath current = revalidate_trusted_cache_path(
                checkout, CachePathRequirement::ExistingDirectory);
        if(current.device() != lease.device() ||
           current.inode() != lease.inode()) {
            return checkout_failure(
                    ReviewedSourcePinnedCheckoutFailureReason::
                            CheckoutBoundaryInvalid);
        }

        TrustedGitPinnedCheckoutResult materialized =
                trusted_git_materialize_pinned_checkout(
                        current, identity, lease);
        if(auto* failure =
                   std::get_if<TrustedGitPinnedCheckoutFailure>(
                           &materialized)) {
            ReviewedSourcePinnedCheckoutFailure result = checkout_failure(
                    ReviewedSourcePinnedCheckoutFailureReason::
                            GitMaterializationFailed);
            result.git_failure = std::move(*failure);
            return result;
        }
        return LeasedPinnedCheckout{
                std::move(lease),
                std::move(std::get<TrustedGitPinnedCheckout>(materialized))};
    } catch(const std::system_error& error) {
        const int lock_error = error.code().value();
        ReviewedSourcePinnedCheckoutFailure failure = checkout_failure(
                lock_error == EWOULDBLOCK || lock_error == EAGAIN
                        ? ReviewedSourcePinnedCheckoutFailureReason::
                                  LeaseContended
                        : ReviewedSourcePinnedCheckoutFailureReason::
                                  LeaseUnavailable);
        failure.system_error = error.code();
        return failure;
    } catch(const TrustedCacheError& error) {
        ReviewedSourcePinnedCheckoutFailure failure = checkout_failure(
                ReviewedSourcePinnedCheckoutFailureReason::
                        CheckoutBoundaryInvalid);
        failure.boundary_failure = error.failure();
        return failure;
    }
}

} // namespace

AcceptedReviewedSourceCheckout::AcceptedReviewedSourceCheckout(
        AcceptedReviewedSourceTarget target,
        ReviewedSourcePackageBaseLease lease,
        TrustedGitPinnedCheckout checkout) {
    if(!target.valid() || !checkout.valid() ||
       target.identity() != checkout.identity() ||
       lease.device() != checkout.checkout_device() ||
       lease.inode() != checkout.checkout_inode()) {
        throw std::logic_error(
                "Accepted reviewed source checkout binding is inconsistent.");
    }
    state_ = std::make_unique<State>(
            std::move(target), std::move(lease), std::move(checkout));
}

AcceptedReviewedSourceCheckout::AcceptedReviewedSourceCheckout(
        AcceptedReviewedSourceCheckout&& other) noexcept = default;

AcceptedReviewedSourceCheckout& AcceptedReviewedSourceCheckout::operator=(
        AcceptedReviewedSourceCheckout&& other) noexcept = default;

AcceptedReviewedSourceCheckout::~AcceptedReviewedSourceCheckout() = default;

bool AcceptedReviewedSourceCheckout::valid() const noexcept {
    return state_ != nullptr;
}

const AcceptedReviewedSourceCheckout::State&
AcceptedReviewedSourceCheckout::require_state() const {
    if(!state_) {
        throw std::logic_error(
                "A moved-from accepted reviewed source checkout has no authority.");
    }
    return *state_;
}

AcceptedReviewedSourceCheckout::State&
AcceptedReviewedSourceCheckout::require_state() {
    if(!state_) {
        throw std::logic_error(
                "A moved-from accepted reviewed source checkout has no authority.");
    }
    return *state_;
}

const AurReviewedSourceReviewIdentity&
AcceptedReviewedSourceCheckout::identity() const {
    return require_state().target.identity();
}

const ReviewedSourceExpectedStateObservation&
AcceptedReviewedSourceCheckout::expected_state_observation() const {
    return require_state().target.expected_state_observation();
}

const std::filesystem::path&
AcceptedReviewedSourceCheckout::checkout_path() const {
    return require_state().checkout.checkout_path();
}

std::uintmax_t AcceptedReviewedSourceCheckout::checkout_device() const {
    return require_state().checkout.checkout_device();
}

std::uintmax_t AcceptedReviewedSourceCheckout::checkout_inode() const {
    return require_state().checkout.checkout_inode();
}

AcceptedReviewedSourceCheckoutResult
materialize_accepted_reviewed_source_checkout(
        AcceptedReviewedSourceTarget target,
        const ValidatedCachePath& checkout) {
    if(!target.valid()) {
        return checkout_failure(
                ReviewedSourcePinnedCheckoutFailureReason::
                        InvalidAcceptedCapability);
    }
    LeasedPinnedCheckoutResult acquired = acquire_leased_pinned_checkout(
            target.identity(), checkout);
    if(auto* failure = std::get_if<ReviewedSourcePinnedCheckoutFailure>(
               &acquired)) {
        return std::move(*failure);
    }
    LeasedPinnedCheckout authority =
            std::move(std::get<LeasedPinnedCheckout>(acquired));
    return AcceptedReviewedSourceCheckout(
            std::move(target), std::move(authority.lease),
            std::move(authority.checkout));
}

AlreadyReviewedSourceCheckout::AlreadyReviewedSourceCheckout(
        ReviewedSourceAlreadyReviewedContinue target,
        ReviewedSourcePackageBaseLease lease,
        TrustedGitPinnedCheckout checkout) {
    if(!target.valid() || !checkout.valid() ||
       target.identity() != checkout.identity() ||
       lease.device() != checkout.checkout_device() ||
       lease.inode() != checkout.checkout_inode()) {
        throw std::logic_error(
                "Already-reviewed source checkout binding is inconsistent.");
    }
    state_ = std::make_unique<State>(
            std::move(target), std::move(lease), std::move(checkout));
}

AlreadyReviewedSourceCheckout::AlreadyReviewedSourceCheckout(
        AlreadyReviewedSourceCheckout&& other) noexcept = default;

AlreadyReviewedSourceCheckout& AlreadyReviewedSourceCheckout::operator=(
        AlreadyReviewedSourceCheckout&& other) noexcept = default;

AlreadyReviewedSourceCheckout::~AlreadyReviewedSourceCheckout() = default;

bool AlreadyReviewedSourceCheckout::valid() const noexcept {
    return state_ != nullptr;
}

const AlreadyReviewedSourceCheckout::State&
AlreadyReviewedSourceCheckout::require_state() const {
    if(!state_) {
        throw std::logic_error(
                "A moved-from already-reviewed checkout has no authority.");
    }
    return *state_;
}

const AurReviewedSourceReviewIdentity&
AlreadyReviewedSourceCheckout::identity() const {
    return require_state().target.identity();
}

const ReviewedSourceExpectedStateObservation&
AlreadyReviewedSourceCheckout::expected_state_observation() const {
    return require_state().target.expected_state_observation();
}

const std::filesystem::path&
AlreadyReviewedSourceCheckout::checkout_path() const {
    return require_state().checkout.checkout_path();
}

std::uintmax_t AlreadyReviewedSourceCheckout::checkout_device() const {
    return require_state().checkout.checkout_device();
}

std::uintmax_t AlreadyReviewedSourceCheckout::checkout_inode() const {
    return require_state().checkout.checkout_inode();
}

AlreadyReviewedSourceCheckoutResult
materialize_already_reviewed_source_checkout(
        ReviewedSourceAlreadyReviewedContinue target,
        const ValidatedCachePath& checkout) {
    if(!target.valid()) {
        return checkout_failure(
                ReviewedSourcePinnedCheckoutFailureReason::
                        InvalidAlreadyReviewedCapability);
    }
    LeasedPinnedCheckoutResult acquired = acquire_leased_pinned_checkout(
            target.identity(), checkout);
    if(auto* failure = std::get_if<ReviewedSourcePinnedCheckoutFailure>(
               &acquired)) {
        return std::move(*failure);
    }
    LeasedPinnedCheckout authority =
            std::move(std::get<LeasedPinnedCheckout>(acquired));
    return AlreadyReviewedSourceCheckout(
            std::move(target), std::move(authority.lease),
            std::move(authority.checkout));
}

PinnedReviewedSourceBuild::PinnedReviewedSourceBuild(
        AcceptedReviewedSourceCheckout checkout,
        ReviewedSourcePublicationStatus publication_status,
        ReviewedSourceState state,
        ReviewedSourceStateObservedRecord observed) {
    if(!checkout.valid() || state.package_base() != checkout.identity().package_base() ||
       state.reviewed_revision() != checkout.identity().target_revision()) {
        throw std::logic_error(
                "Pinned reviewed source build binding is inconsistent.");
    }
    state_ = std::make_unique<State>(
            std::move(checkout), publication_status, std::move(state),
            std::move(observed));
}

PinnedReviewedSourceBuild::PinnedReviewedSourceBuild(
        AlreadyReviewedSourceCheckout checkout,
        ReviewedSourceState state,
        ReviewedSourceStateObservedRecord observed) {
    if(!checkout.valid() ||
       state.package_base() != checkout.identity().package_base() ||
       state.reviewed_revision() != checkout.identity().target_revision()) {
        throw std::logic_error(
                "Pinned already-reviewed source build binding is inconsistent.");
    }
    state_ = std::make_unique<State>(
            std::move(checkout),
            ReviewedSourcePublicationStatus::AlreadyPublishedSameTarget,
            std::move(state), std::move(observed));
}

PinnedReviewedSourceBuild::PinnedReviewedSourceBuild(
        PinnedReviewedSourceBuild&& other) noexcept = default;

PinnedReviewedSourceBuild& PinnedReviewedSourceBuild::operator=(
        PinnedReviewedSourceBuild&& other) noexcept = default;

PinnedReviewedSourceBuild::~PinnedReviewedSourceBuild() = default;

bool PinnedReviewedSourceBuild::valid() const noexcept {
    return state_ != nullptr;
}

const PinnedReviewedSourceBuild::State&
PinnedReviewedSourceBuild::require_state() const {
    if(!state_) {
        throw std::logic_error(
                "A moved-from pinned reviewed source build has no authority.");
    }
    return *state_;
}

ReviewedSourcePublicationStatus
PinnedReviewedSourceBuild::publication_status() const {
    return require_state().publication_status;
}

const AurReviewedSourceReviewIdentity&
PinnedReviewedSourceBuild::identity() const {
    return std::visit(
            [](const auto& checkout)
                    -> const AurReviewedSourceReviewIdentity& {
                return checkout.identity();
            },
            require_state().checkout);
}

const SourceRevisionIdentity&
PinnedReviewedSourceBuild::reviewed_upstream_base_revision() const {
    return identity().target_revision();
}

const ReviewedSourceState&
PinnedReviewedSourceBuild::reviewed_state() const {
    return require_state().state;
}

const ReviewedSourceStateObservedRecord&
PinnedReviewedSourceBuild::published_record() const {
    return require_state().observed;
}

const std::filesystem::path&
PinnedReviewedSourceBuild::checkout_path() const {
    return std::visit(
            [](const auto& checkout) -> const std::filesystem::path& {
                return checkout.checkout_path();
            },
            require_state().checkout);
}

std::uintmax_t PinnedReviewedSourceBuild::checkout_device() const {
    return std::visit(
            [](const auto& checkout) {
                return checkout.checkout_device();
            },
            require_state().checkout);
}

std::uintmax_t PinnedReviewedSourceBuild::checkout_inode() const {
    return std::visit(
            [](const auto& checkout) {
                return checkout.checkout_inode();
            },
            require_state().checkout);
}

ReviewedSourcePublicationResult
publish_accepted_reviewed_source_checkout(
        AcceptedReviewedSourceCheckout target) {
    if(!target.valid()) {
        return publication_failure(
                ReviewedSourcePublicationFailureReason::
                        InvalidCheckoutCapability);
    }

    const TrustedGitPinnedCheckout& trusted_checkout =
            target.require_state().checkout;
    TrustedGitPinnedCheckoutRevalidationResult prepublication =
            revalidate_trusted_git_pinned_checkout(trusted_checkout);
    if(auto* failure = std::get_if<TrustedGitPinnedCheckoutFailure>(
               &prepublication)) {
        return checkout_publication_failure(std::move(*failure));
    }

    const AurReviewedSourceReviewIdentity identity = target.identity();
    const ReviewedSourceState next_state = ReviewedSourceState::make(
            identity.package_base(), identity.target_revision());
    ReviewedSourceStateStorePublishResult published =
            publish_reviewed_source_state(
                    next_state,
                    target.expected_state_observation().observed_record());

    const auto finish_pinned =
            [&target](
                    ReviewedSourcePublicationStatus publication_status,
                    ReviewedSourceState state,
                    ReviewedSourceStateObservedRecord observed)
            -> ReviewedSourcePublicationResult {
        TrustedGitPinnedCheckoutRevalidationResult revalidated =
                revalidate_trusted_git_pinned_checkout(
                        target.require_state().checkout);
        if(auto* failure = std::get_if<TrustedGitPinnedCheckoutFailure>(
                   &revalidated)) {
            return ReviewedSourcePostPublicationCheckoutFailure{
                    publication_status, std::move(state),
                    std::move(observed), std::move(*failure)};
        }
        return PinnedReviewedSourceBuild(
                std::move(target), publication_status, std::move(state),
                std::move(observed));
    };

    if(auto* success =
               std::get_if<ReviewedSourceStateStorePublished>(&published)) {
        if(success->state != next_state) {
            return publication_failure(
                    ReviewedSourcePublicationFailureReason::
                            InconsistentStoreResult);
        }
        return finish_pinned(
                ReviewedSourcePublicationStatus::Published,
                std::move(success->state), std::move(success->observed));
    }
    if(auto* uncertain = std::get_if<
               ReviewedSourceStateStorePublishedUncertain>(&published)) {
        return ReviewedSourcePublicationUncertain{
                std::move(*uncertain)};
    }
    if(auto* unsafe =
               std::get_if<ReviewedSourceStateStoreUnsafeHistory>(
                       &published)) {
        return ReviewedSourcePublicationUnsafeHistory{std::move(*unsafe)};
    }

    ReviewedSourceStateStoreFailure failure =
            std::move(std::get<ReviewedSourceStateStoreFailure>(published));
    if(failure.kind !=
       ReviewedSourceStateStoreFailureKind::ConcurrentReplacement) {
        return store_publication_failure(
                ReviewedSourcePublicationFailureReason::StoreFailure,
                std::move(failure));
    }

    ReviewedSourceStateStoreReadResult current_result =
            read_reviewed_source_state(identity.package_base());
    if(auto* unsafe = std::get_if<ReviewedSourceStateStoreUnsafeHistory>(
               &current_result)) {
        return ReviewedSourcePublicationUnsafeHistory{std::move(*unsafe)};
    }
    if(auto* read_failure = std::get_if<ReviewedSourceStateStoreFailure>(
               &current_result)) {
        return store_publication_failure(
                ReviewedSourcePublicationFailureReason::
                        ConflictObservationFailure,
                std::move(*read_failure));
    }

    ReviewedSourceStateStoreRead current =
            std::move(std::get<ReviewedSourceStateStoreRead>(current_result));
    const auto* loaded =
            std::get_if<ReviewedSourceStateLoaded>(&current.observation);
    if(loaded != nullptr && loaded->state == next_state) {
        if(!current.observed.has_value()) {
            return publication_failure(
                    ReviewedSourcePublicationFailureReason::
                            InconsistentStoreResult);
        }
        return finish_pinned(
                ReviewedSourcePublicationStatus::AlreadyPublishedSameTarget,
                loaded->state, *current.observed);
    }
    return ReviewedSourcePublicationConflict{
            std::move(identity), std::move(current)};
}

ReviewedSourcePublicationResult
confirm_already_reviewed_source_checkout(
        AlreadyReviewedSourceCheckout target) {
    if(!target.valid()) {
        return publication_failure(
                ReviewedSourcePublicationFailureReason::
                        InvalidCheckoutCapability);
    }

    const AurReviewedSourceReviewIdentity identity = target.identity();
    const ReviewedSourceState expected_state = ReviewedSourceState::make(
            identity.package_base(), identity.target_revision());
    const auto* expected_loaded = std::get_if<ReviewedSourceStateLoaded>(
            &target.expected_state_observation().observation());
    if(expected_loaded == nullptr ||
       expected_loaded->state != expected_state ||
       !target.expected_state_observation().observed_record().has_value()) {
        return publication_failure(
                ReviewedSourcePublicationFailureReason::
                        InconsistentStoreResult);
    }

    TrustedGitPinnedCheckoutRevalidationResult preconfirmation =
            revalidate_trusted_git_pinned_checkout(
                    target.require_state().checkout);
    if(auto* failure = std::get_if<TrustedGitPinnedCheckoutFailure>(
               &preconfirmation)) {
        return checkout_publication_failure(std::move(*failure));
    }

    ReviewedSourceStateStoreReadResult current_result =
            read_reviewed_source_state(identity.package_base());
    if(auto* unsafe = std::get_if<ReviewedSourceStateStoreUnsafeHistory>(
               &current_result)) {
        return ReviewedSourcePublicationUnsafeHistory{std::move(*unsafe)};
    }
    if(auto* failure = std::get_if<ReviewedSourceStateStoreFailure>(
               &current_result)) {
        return store_publication_failure(
                ReviewedSourcePublicationFailureReason::StoreFailure,
                std::move(*failure));
    }

    ReviewedSourceStateStoreRead current =
            std::move(std::get<ReviewedSourceStateStoreRead>(current_result));
    const auto* loaded =
            std::get_if<ReviewedSourceStateLoaded>(&current.observation);
    if(loaded == nullptr || loaded->state != expected_state) {
        return ReviewedSourcePublicationConflict{
                std::move(identity), std::move(current)};
    }
    if(!current.observed.has_value()) {
        return publication_failure(
                ReviewedSourcePublicationFailureReason::
                        InconsistentStoreResult);
    }

    TrustedGitPinnedCheckoutRevalidationResult revalidated =
            revalidate_trusted_git_pinned_checkout(
                    target.require_state().checkout);
    if(auto* failure = std::get_if<TrustedGitPinnedCheckoutFailure>(
               &revalidated)) {
        return ReviewedSourcePostPublicationCheckoutFailure{
                ReviewedSourcePublicationStatus::AlreadyPublishedSameTarget,
                loaded->state, *current.observed, std::move(*failure)};
    }
    return PinnedReviewedSourceBuild(
            std::move(target), loaded->state, *current.observed);
}
