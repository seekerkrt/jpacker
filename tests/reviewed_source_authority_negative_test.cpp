#include "reviewed_source_pinned_build.hpp"

#include <type_traits>
#include <utility>

static_assert(std::is_invocable_v<
              decltype(plan_reviewed_source_lifecycle),
              AurReviewedSourceReviewIdentity>);
static_assert(!std::is_invocable_v<
              decltype(plan_reviewed_source_lifecycle),
              AurReviewedSourceReviewIdentity,
              ReviewedSourceStateStoreReadResult>);

#if defined(MOGUET_FORGE_LIFECYCLE_EXPECTED)
class ReviewedSourceLifecycleAuthority final {
public:
    static ReviewedSourceExpectedStateObservation forge(
            ReviewedSourceStateStoreRead store_read) {
        return ReviewedSourceExpectedStateObservation(
                std::move(store_read));
    }
};
#elif defined(MOGUET_FORGE_LIFECYCLE_ALREADY)
class ReviewedSourceLifecycleAuthority final {
public:
    static ReviewedSourceAlreadyReviewedContinue forge(
            AurReviewedSourceReviewIdentity identity,
            ReviewedSourceExpectedStateObservation expected) {
        return ReviewedSourceAlreadyReviewedContinue(
                std::move(identity), std::move(expected));
    }
};
#elif defined(MOGUET_FORGE_RETAINED_DESCRIPTOR)
struct ReviewedSourcePackageBaseLeaseAccess {
    static int descriptor(
            const RetainedTrustedCacheDirectory& directory) {
        return directory.descriptor_;
    }
};
#elif defined(MOGUET_FORGE_ACCEPTED_CHECKOUT)
struct ReviewedSourcePinnedBuildAccess {
    static AcceptedReviewedSourceCheckout forge(
            AcceptedReviewedSourceTarget target,
            ReviewedSourcePackageBaseLease lease,
            TrustedGitPinnedCheckout checkout) {
        return AcceptedReviewedSourceCheckout(
                std::move(target), std::move(lease),
                std::move(checkout));
    }
};
#elif defined(MOGUET_FORGE_ALREADY_CHECKOUT)
struct ReviewedSourcePinnedBuildAccess {
    static AlreadyReviewedSourceCheckout forge(
            ReviewedSourceAlreadyReviewedContinue target,
            ReviewedSourcePackageBaseLease lease,
            TrustedGitPinnedCheckout checkout) {
        return AlreadyReviewedSourceCheckout(
                std::move(target), std::move(lease),
                std::move(checkout));
    }
};
#elif defined(MOGUET_FORGE_PINNED_ACCEPTED)
struct ReviewedSourcePinnedBuildAccess {
    static PinnedReviewedSourceBuild forge(
            AcceptedReviewedSourceCheckout checkout,
            ReviewedSourcePublicationStatus publication_status,
            ReviewedSourceState state,
            ReviewedSourceStateObservedRecord observed) {
        return PinnedReviewedSourceBuild(
                std::move(checkout), publication_status,
                std::move(state), std::move(observed));
    }
};
#elif defined(MOGUET_FORGE_PINNED_ALREADY)
struct ReviewedSourcePinnedBuildAccess {
    static PinnedReviewedSourceBuild forge(
            AlreadyReviewedSourceCheckout checkout,
            ReviewedSourceState state,
            ReviewedSourceStateObservedRecord observed) {
        return PinnedReviewedSourceBuild(
                std::move(checkout), std::move(state),
                std::move(observed));
    }
};
#else
int reviewed_source_authority_negative_fixture_baseline() {
    return 0;
}
#endif
