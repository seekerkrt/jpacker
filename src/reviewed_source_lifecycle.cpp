#include "reviewed_source_lifecycle.hpp"

#include <stdexcept>
#include <type_traits>
#include <utility>

namespace {

bool store_read_is_coherent(
        const ReviewedSourceStateStoreRead& store_read,
        const PackageBaseIdentity& expected_package_base) {
    if(std::holds_alternative<ReviewedSourceStateMissing>(
               store_read.observation)) {
        return !store_read.observed.has_value();
    }
    if(!store_read.observed.has_value()) return false;

    const ReviewedSourceStateInterpretation interpreted =
            interpret_reviewed_source_state(
                    store_read.observed->raw_contents,
                    expected_package_base);
    return std::visit(
            [&interpreted](const auto& observation) {
                using Observation = std::decay_t<decltype(observation)>;
                if constexpr(std::is_same_v<
                                     Observation,
                                     ReviewedSourceStateMissing>) {
                    return false;
                } else {
                    const auto* matching =
                            std::get_if<Observation>(&interpreted);
                    return matching != nullptr &&
                           *matching == observation;
                }
            },
            store_read.observation);
}

ReviewedSourceOperationStopReason stop_reason(
        ReviewedSourceFatalStateReason reason) noexcept {
    switch(reason) {
    case ReviewedSourceFatalStateReason::UnsupportedFuture:
        return ReviewedSourceOperationStopReason::UnsupportedFuture;
    case ReviewedSourceFatalStateReason::UnsafeHistory:
        return ReviewedSourceOperationStopReason::UnsafeHistory;
    case ReviewedSourceFatalStateReason::StoreFailure:
        return ReviewedSourceOperationStopReason::StoreFailure;
    case ReviewedSourceFatalStateReason::InconsistentStoreObservation:
        return ReviewedSourceOperationStopReason::
                InconsistentStoreObservation;
    }
    return ReviewedSourceOperationStopReason::InconsistentStoreObservation;
}

} // namespace

class ReviewedSourceLifecycleAuthority final {
public:
    static ReviewedSourceExpectedStateObservation expected(
            ReviewedSourceStateStoreRead store_read) {
        return ReviewedSourceExpectedStateObservation(
                std::move(store_read));
    }

    static ReviewedSourceReviewRequirement requirement(
            AurReviewedSourceReviewIdentity identity,
            ReviewedSourceReviewRequirementKind kind,
            std::optional<SourceRevisionIdentity> baseline,
            std::optional<ReviewedSourceAbnormalStateReason> abnormal_reason,
            ReviewedSourceExpectedStateObservation expected) {
        return ReviewedSourceReviewRequirement(
                std::move(identity), kind, std::move(baseline),
                abnormal_reason, std::move(expected));
    }

    static ReviewedSourceAlreadyReviewedContinue already_reviewed(
            AurReviewedSourceReviewIdentity identity,
            ReviewedSourceExpectedStateObservation expected) {
        return ReviewedSourceAlreadyReviewedContinue(
                std::move(identity), std::move(expected));
    }
};

AurReviewedSourceReviewIdentity::AurReviewedSourceReviewIdentity(
        PackageBaseIdentity package_base,
        SourceRevisionIdentity target_revision) noexcept
    : package_base_(std::move(package_base)),
      target_revision_(std::move(target_revision)) {}

AurReviewedSourceReviewIdentity AurReviewedSourceReviewIdentity::make(
        PackageBaseIdentity package_base,
        SourceRevisionIdentity target_revision) {
    const PackageSourceIdentity& source = package_base.source();
    const SourceLocationIdentity& location = source.location();
    if(source.kind() != PackageSourceKind::Aur ||
       location.kind() != SourceLocationKind::GitRemote ||
       location.state() != SourceLocationState::Known ||
       location.value() == nullptr) {
        throw std::invalid_argument(
                "A reviewed source target requires a known canonical AUR Git remote.");
    }
    const std::string expected_remote =
            "https://aur.archlinux.org/" + package_base.package_base() +
            ".git";
    if(*location.value() != expected_remote) {
        throw std::invalid_argument(
                "A reviewed source target requires the canonical AUR Git remote for its PackageBase.");
    }
    if(target_revision.state() != SourceRevisionState::Known ||
       target_revision.git_commit() == nullptr ||
       target_revision.git_object_format() == nullptr) {
        throw std::invalid_argument(
                "A reviewed source target requires an exact Git commit identity.");
    }
    return AurReviewedSourceReviewIdentity(
            std::move(package_base), std::move(target_revision));
}

const PackageBaseIdentity&
AurReviewedSourceReviewIdentity::package_base() const noexcept {
    return package_base_;
}

const PackageSourceIdentity&
AurReviewedSourceReviewIdentity::source() const noexcept {
    return package_base_.source();
}

const std::string&
AurReviewedSourceReviewIdentity::canonical_git_remote() const noexcept {
    return *package_base_.source().location().value();
}

const SourceRevisionIdentity&
AurReviewedSourceReviewIdentity::target_revision() const noexcept {
    return target_revision_;
}

GitObjectFormat
AurReviewedSourceReviewIdentity::git_object_format() const noexcept {
    return *target_revision_.git_object_format();
}

ReviewedSourceExpectedStateObservation::
        ReviewedSourceExpectedStateObservation(
                ReviewedSourceStateStoreRead store_read) noexcept
    : store_read_(std::move(store_read)) {}

const ReviewedSourceStateObservation&
ReviewedSourceExpectedStateObservation::observation() const noexcept {
    return store_read_.observation;
}

const std::optional<ReviewedSourceStateObservedRecord>&
ReviewedSourceExpectedStateObservation::observed_record() const noexcept {
    return store_read_.observed;
}

const ReviewedSourceStateStoreRead&
ReviewedSourceExpectedStateObservation::store_read() const noexcept {
    return store_read_;
}

ReviewedSourceOperationStop::ReviewedSourceOperationStop(
        ReviewedSourceOperationStopReason reason,
        std::optional<ReviewedSourceIntegrationLifecycle> lifecycle) noexcept
    : reason_(reason), lifecycle_(std::move(lifecycle)) {}

ReviewedSourceOperationStop ReviewedSourceOperationStop::make(
        ReviewedSourceOperationStopReason reason) noexcept {
    return ReviewedSourceOperationStop(reason, std::nullopt);
}

ReviewedSourceOperationStop ReviewedSourceOperationStop::fatal(
        ReviewedSourceFatalStateReason reason) noexcept {
    return ReviewedSourceOperationStop(
            stop_reason(reason),
            ReviewedSourceIntegrationLifecycle(
                    ReviewedSourceLifecycleFatalState{reason}));
}

ReviewedSourceOperationStopReason
ReviewedSourceOperationStop::reason() const noexcept {
    return reason_;
}

const std::optional<ReviewedSourceIntegrationLifecycle>&
ReviewedSourceOperationStop::lifecycle() const noexcept {
    return lifecycle_;
}

ReviewedSourceReviewRequirement::ReviewedSourceReviewRequirement(
        AurReviewedSourceReviewIdentity identity,
        ReviewedSourceReviewRequirementKind kind,
        std::optional<SourceRevisionIdentity> baseline,
        std::optional<ReviewedSourceAbnormalStateReason> abnormal_reason,
        ReviewedSourceExpectedStateObservation expected) noexcept
    : identity_(std::move(identity)), kind_(kind),
      baseline_(std::move(baseline)), abnormal_reason_(abnormal_reason),
      expected_(std::move(expected)) {}

const AurReviewedSourceReviewIdentity&
ReviewedSourceReviewRequirement::identity() const noexcept {
    return identity_;
}

ReviewedSourceReviewRequirementKind
ReviewedSourceReviewRequirement::kind() const noexcept {
    return kind_;
}

const SourceRevisionIdentity*
ReviewedSourceReviewRequirement::baseline() const noexcept {
    return baseline_.has_value() ? &baseline_.value() : nullptr;
}

const ReviewedSourceAbnormalStateReason*
ReviewedSourceReviewRequirement::abnormal_reason() const noexcept {
    return abnormal_reason_.has_value() ? &abnormal_reason_.value() : nullptr;
}

const ReviewedSourceExpectedStateObservation&
ReviewedSourceReviewRequirement::expected_state_observation()
        const noexcept {
    return expected_;
}

ReviewedSourceAlreadyReviewedContinue::
        ReviewedSourceAlreadyReviewedContinue(
                AurReviewedSourceReviewIdentity identity,
                ReviewedSourceExpectedStateObservation expected) noexcept
    : identity_(std::move(identity)),
      lifecycle_(ReviewedSourceLifecycleAlreadyReviewed{}),
      expected_(std::move(expected)) {}

const AurReviewedSourceReviewIdentity&
ReviewedSourceAlreadyReviewedContinue::identity() const noexcept {
    return identity_;
}

const ReviewedSourceIntegrationLifecycle&
ReviewedSourceAlreadyReviewedContinue::lifecycle() const noexcept {
    return lifecycle_;
}

const ReviewedSourceExpectedStateObservation&
ReviewedSourceAlreadyReviewedContinue::expected_state_observation()
        const noexcept {
    return expected_;
}

ReviewedSourceLifecyclePlanResult plan_reviewed_source_lifecycle(
        AurReviewedSourceReviewIdentity identity,
        ReviewedSourceStateStoreReadResult store_result) {
    if(std::holds_alternative<ReviewedSourceStateStoreUnsafeHistory>(
               store_result)) {
        return ReviewedSourceOperationStop::fatal(
                ReviewedSourceFatalStateReason::UnsafeHistory);
    }
    if(std::holds_alternative<ReviewedSourceStateStoreFailure>(store_result)) {
        return ReviewedSourceOperationStop::fatal(
                ReviewedSourceFatalStateReason::StoreFailure);
    }

    ReviewedSourceStateStoreRead store_read =
            std::get<ReviewedSourceStateStoreRead>(std::move(store_result));
    if(!store_read_is_coherent(store_read, identity.package_base())) {
        return ReviewedSourceOperationStop::fatal(
                ReviewedSourceFatalStateReason::
                        InconsistentStoreObservation);
    }

    if(std::holds_alternative<ReviewedSourceStateUnsupportedFuture>(
               store_read.observation)) {
        return ReviewedSourceOperationStop::fatal(
                ReviewedSourceFatalStateReason::UnsupportedFuture);
    }

    ReviewedSourceExpectedStateObservation expected =
            ReviewedSourceLifecycleAuthority::expected(
                    std::move(store_read));
    const ReviewedSourceStateObservation& observation =
            expected.observation();

    if(std::holds_alternative<ReviewedSourceStateMissing>(observation)) {
        return ReviewedSourceLifecycleAuthority::requirement(
                std::move(identity),
                ReviewedSourceReviewRequirementKind::InitialFullReview,
                std::nullopt, std::nullopt, std::move(expected));
    }
    if(const auto* loaded =
               std::get_if<ReviewedSourceStateLoaded>(&observation)) {
        const SourceRevisionIdentity baseline =
                loaded->state.reviewed_revision();
        if(baseline == identity.target_revision()) {
            return ReviewedSourceLifecycleAuthority::already_reviewed(
                    std::move(identity), std::move(expected));
        }
        return ReviewedSourceLifecycleAuthority::requirement(
                std::move(identity),
                ReviewedSourceReviewRequirementKind::UpdateReview,
                baseline, std::nullopt, std::move(expected));
    }
    if(std::holds_alternative<ReviewedSourceStateInvalid>(observation)) {
        return ReviewedSourceLifecycleAuthority::requirement(
                std::move(identity),
                ReviewedSourceReviewRequirementKind::
                        AbnormalStateRebindFullReview,
                std::nullopt, ReviewedSourceAbnormalStateReason::Invalid,
                std::move(expected));
    }
    if(std::holds_alternative<ReviewedSourceStateCorrupted>(observation)) {
        return ReviewedSourceLifecycleAuthority::requirement(
                std::move(identity),
                ReviewedSourceReviewRequirementKind::
                        AbnormalStateRebindFullReview,
                std::nullopt, ReviewedSourceAbnormalStateReason::Corrupted,
                std::move(expected));
    }
    if(std::holds_alternative<ReviewedSourceStateSourceMismatch>(
               observation)) {
        return ReviewedSourceLifecycleAuthority::requirement(
                std::move(identity),
                ReviewedSourceReviewRequirementKind::
                        AbnormalStateRebindFullReview,
                std::nullopt,
                ReviewedSourceAbnormalStateReason::SourceMismatch,
                std::move(expected));
    }

    return ReviewedSourceOperationStop::fatal(
            ReviewedSourceFatalStateReason::InconsistentStoreObservation);
}
