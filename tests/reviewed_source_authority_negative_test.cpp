#include "artifact_workspace.hpp"
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
static_assert(!std::is_default_constructible_v<
              ProductionArtifactSourceTree>);
static_assert(!std::is_copy_constructible_v<
              ProductionArtifactSourceTree>);
static_assert(!std::is_constructible_v<
              ProductionArtifactSourceTree,
              ValidatedCachePath>);
static_assert(!std::is_default_constructible_v<
              ReviewedSourceFatalStatePreflight>);
static_assert(!std::is_constructible_v<
              ReviewedSourceFatalStatePreflight,
              PackageBaseIdentity,
              ReviewedSourceStateStoreRead>);
static_assert(!std::is_default_constructible_v<
              ReviewedSourceEditorBoundary>);
static_assert(!std::is_copy_constructible_v<
              ReviewedSourceEditorBoundary>);
static_assert(!std::is_default_constructible_v<
              ReviewedSourceEditorOverlayProof>);
static_assert(!std::is_copy_constructible_v<
              ReviewedSourceEditorOverlayProof>);
static_assert(!std::is_constructible_v<
              ReviewedSourceEditorOverlayProof,
              ReviewedSourceEditorOverlayStatus>);
static_assert(!std::is_constructible_v<
              ReviewedSourceEditorOverlayProof,
              bool>);
static_assert(!std::is_constructible_v<
              ReviewedSourceEditorOverlayProof,
              ValidatedCachePath>);
static_assert(!std::is_constructible_v<
              ReviewedSourceEditorOverlayProof,
              std::filesystem::path>);

#if defined(MOGUET_FORGE_LIFECYCLE_EXPECTED)
class ReviewedSourceLifecycleAuthority final {
public:
    static ReviewedSourceExpectedStateObservation forge(
        ReviewedSourceStateStoreRead store_read) {
        return ReviewedSourceExpectedStateObservation(
            std::move(store_read));
    }
};
#elif defined(MOGUET_FORGE_FATAL_PREFLIGHT)
class ReviewedSourceLifecycleAuthority final {
public:
    static ReviewedSourceFatalStatePreflight forge(
        PackageBaseIdentity package_base,
        ReviewedSourceStateStoreRead store_read) {
        return ReviewedSourceFatalStatePreflight(
            std::move(package_base), std::move(store_read));
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
        ReviewedSourceStateObservedRecord observed,
        ReviewedSourceEditorOverlayProof editor_overlay) {
        return PinnedReviewedSourceBuild(
            std::move(checkout), publication_status,
            std::move(state), std::move(observed),
            std::move(editor_overlay));
    }
};
#elif defined(MOGUET_FORGE_PINNED_ALREADY)
struct ReviewedSourcePinnedBuildAccess {
    static PinnedReviewedSourceBuild forge(
        AlreadyReviewedSourceCheckout checkout,
        ReviewedSourceState state,
        ReviewedSourceStateObservedRecord observed,
        ReviewedSourceEditorOverlayProof editor_overlay) {
        return PinnedReviewedSourceBuild(
            std::move(checkout), std::move(state),
            std::move(observed),
            std::move(editor_overlay));
    }
};
#elif defined(MOGUET_FORGE_EDITOR_BOUNDARY)
struct ReviewedSourceEditorOverlayAccess {
    static ReviewedSourceEditorBoundary forge(
        AurReviewedSourceReviewIdentity identity,
        TrustedGitPinnedCheckoutOverlayObservation observation) {
        return ReviewedSourceEditorBoundary(
            std::move(identity), 1, 2,
            std::move(observation));
    }
};
#elif defined(MOGUET_FORGE_EDITOR_OVERLAY)
struct ReviewedSourceEditorOverlayAccess {
    static ReviewedSourceEditorOverlayProof forge(
        AurReviewedSourceReviewIdentity identity,
        TrustedGitPinnedCheckoutOverlayObservation pre_editor,
        TrustedGitPinnedCheckoutOverlayObservation post_editor) {
        return ReviewedSourceEditorOverlayProof(
            std::move(identity), 1, 2,
            std::move(pre_editor), std::move(post_editor));
    }
};
#else
int reviewed_source_authority_negative_fixture_baseline() {
    return 0;
}
#endif
