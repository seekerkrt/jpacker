#include "reviewed_source_acceptance.hpp"
#include "reviewed_source_production_failure.hpp"

#include <algorithm>
#include <exception>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <streambuf>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

namespace {

static_assert(!std::is_default_constructible_v<
              ReviewedSourceVerifiedLifecycleTarget>);
static_assert(!std::is_copy_constructible_v<
              ReviewedSourceVerifiedLifecycleTarget>);
static_assert(std::is_move_constructible_v<
              ReviewedSourceVerifiedLifecycleTarget>);
static_assert(!std::is_default_constructible_v<
              PresentedReviewedSourceTarget>);
static_assert(!std::is_copy_constructible_v<
              PresentedReviewedSourceTarget>);
static_assert(std::is_move_constructible_v<
              PresentedReviewedSourceTarget>);
static_assert(!std::is_default_constructible_v<
              AcceptedReviewedSourceTarget>);
static_assert(!std::is_copy_constructible_v<
              AcceptedReviewedSourceTarget>);
static_assert(std::is_move_constructible_v<
              AcceptedReviewedSourceTarget>);
static_assert(!std::is_copy_assignable_v<
              AcceptedReviewedSourceTarget>);
static_assert(!std::is_constructible_v<
              AcceptedReviewedSourceTarget,
              PresentedReviewedSourceTarget,
              ConfirmationDecisionOrigin>);
static_assert(!std::is_constructible_v<
              AcceptedReviewedSourceTarget,
              PresentedReviewedSourceTarget,
              ExplicitConfirmationAcceptance>);
static_assert(!std::is_constructible_v<
              PresentedReviewedSourceTarget,
              ReviewedSourceVerifiedLifecycleTarget>);
static_assert(!std::is_constructible_v<
              PresentedReviewedSourceTarget,
              ReviewedSourceRenderedPresentation>);
static_assert(!std::is_default_constructible_v<
              ReviewedSourceCompatibilityBuildWithoutReview>);
static_assert(!std::is_copy_constructible_v<
              ReviewedSourceCompatibilityBuildWithoutReview>);
static_assert(!std::is_default_constructible_v<
              TrustedAurReviewedSourceReview>);
static_assert(!std::is_copy_constructible_v<
              TrustedAurReviewedSourceReview>);
static_assert(!std::is_constructible_v<
              TrustedAurReviewedSourceReview,
              AurReviewedSourceReviewIdentity,
              ReviewedSourceVerifiedMaterializedReview>);
static_assert(std::is_invocable_v<
              decltype(make_trusted_aur_reviewed_source_review_fixture_for_test),
              ReviewedSourceVerifiedMaterializedReview>);
static_assert(!std::is_invocable_v<
              decltype(make_trusted_aur_reviewed_source_review_fixture_for_test),
              AurReviewedSourceReviewIdentity,
              ReviewedSourceVerifiedMaterializedReview>);
static_assert(!std::is_invocable_v<
              decltype(decide_reviewed_source_acceptance),
              PresentedReviewedSourceTarget,
              ConfirmationResult>);
static_assert(std::is_invocable_v<
              decltype(decide_reviewed_source_unsealed_confirmation),
              PresentedReviewedSourceTarget,
              const ConfirmationResult&>);

template <typename T>
concept HasExpectedStateObservation = requires(const T& value) {
    value.expected_state_observation();
};

template <typename T>
concept HasConfirmationOrigin = requires(const T& value) {
    value.confirmation_origin();
};

static_assert(HasExpectedStateObservation<AcceptedReviewedSourceTarget>);
static_assert(!HasExpectedStateObservation<
              ReviewedSourceCompatibilityBuildWithoutReview>);
static_assert(!HasExpectedStateObservation<ReviewedSourceOperationStop>);
static_assert(HasConfirmationOrigin<AcceptedReviewedSourceTarget>);
static_assert(!HasConfirmationOrigin<PresentedReviewedSourceTarget>);
static_assert(!HasConfirmationOrigin<
              ReviewedSourceCompatibilityBuildWithoutReview>);
static_assert(!std::is_invocable_v<
              decltype(continue_reviewed_source_without_review),
              AurReviewedSourceReviewIdentity,
              ReviewedSourceReviewBypassReason>);
static_assert(!std::is_invocable_v<
              decltype(continue_reviewed_source_without_review),
              ReviewedSourceOperationStop,
              ReviewedSourceReviewBypassReason>);

constexpr std::string_view SHA1_A =
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
constexpr std::string_view SHA1_B =
    "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
constexpr std::string_view SHA1_C =
    "cccccccccccccccccccccccccccccccccccccccc";
constexpr std::string_view SHA256_A =
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
constexpr std::string_view SHA256_B =
    "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";

void require(bool condition, const std::string& message) {
    if(!condition) throw std::runtime_error(message);
}

template <typename Function>
void expect_invalid_argument(Function&& function) {
    try {
        std::forward<Function>(function)();
    } catch(const std::invalid_argument&) {
        return;
    }
    throw std::runtime_error("Expected std::invalid_argument.");
}

template <typename Arm, typename Variant>
const Arm& require_arm(const Variant& value, std::string_view message) {
    const Arm* arm = std::get_if<Arm>(&value);
    if(arm == nullptr) throw std::runtime_error(std::string(message));
    return *arm;
}

template <typename Arm, typename Variant>
Arm take_arm(Variant& value, std::string_view message) {
    Arm* arm = std::get_if<Arm>(&value);
    if(arm == nullptr) throw std::runtime_error(std::string(message));
    return std::move(*arm);
}

PackageBaseIdentity aur_package_base(
    const std::string& package_base = "example-base",
    const std::string& remote =
        "https://aur.archlinux.org/example-base.git") {
    return PackageBaseIdentity::make(
        PackageSourceIdentity::aur(
            SourceLocationIdentity::known_git_remote(remote)),
        package_base);
}

AurReviewedSourceReviewIdentity review_identity(
    const std::string& commit = std::string(SHA1_A),
    const std::string& package_base = "example-base",
    const std::string& remote =
        "https://aur.archlinux.org/example-base.git") {
    return AurReviewedSourceReviewIdentity::make(
        aur_package_base(package_base, remote),
        SourceRevisionIdentity::git_commit(commit));
}

ReviewedSourceStateObservedRecord observed_record(
    std::string raw_contents,
    std::uint64_t generation = 5) {
    return ReviewedSourceStateObservedRecord{
        generation,
        std::to_string(generation) + ".toml",
        ReviewedSourceStateRecordIdentity{
            101, 202, 303, 0600, 1, 404, 505, 606, 707, 808},
        std::move(raw_contents)};
}

ReviewedSourceStateObservation observation_from_document(
    const std::string& document,
    const PackageBaseIdentity& expected_package_base) {
    return std::visit(
        [](const auto& value) -> ReviewedSourceStateObservation {
            return value;
        },
        interpret_reviewed_source_state(
            document, expected_package_base));
}

ReviewedSourceReviewRequirement missing_requirement(
    const AurReviewedSourceReviewIdentity& identity) {
    ReviewedSourceLifecyclePlanResult planned =
        plan_reviewed_source_lifecycle(
            identity,
            ReviewedSourceStateStoreRead{
                ReviewedSourceStateMissing{}, std::nullopt});
    return take_arm<ReviewedSourceReviewRequirement>(
        planned, "Missing did not produce a review requirement.");
}

ReviewedSourceReviewRequirement loaded_requirement(
    const AurReviewedSourceReviewIdentity& identity,
    const SourceRevisionIdentity& baseline,
    ReviewedSourceStateStoreRead* exact_read = nullptr) {
    const ReviewedSourceState state = ReviewedSourceState::make(
        identity.package_base(), baseline);
    const std::string document = encode_reviewed_source_state(state);
    ReviewedSourceStateStoreRead read{
        ReviewedSourceStateLoaded{state}, observed_record(document)};
    if(exact_read != nullptr) *exact_read = read;
    ReviewedSourceLifecyclePlanResult planned =
        plan_reviewed_source_lifecycle(identity, std::move(read));
    return take_arm<ReviewedSourceReviewRequirement>(
        planned, "Loaded different state did not require review.");
}

ReviewedSourceReviewRequirement abnormal_requirement(
    const AurReviewedSourceReviewIdentity& identity,
    const std::string& document,
    ReviewedSourceStateStoreRead* exact_read = nullptr) {
    ReviewedSourceStateStoreRead read{
        observation_from_document(document, identity.package_base()),
        observed_record(document, 12)};
    if(exact_read != nullptr) *exact_read = read;
    ReviewedSourceLifecyclePlanResult planned =
        plan_reviewed_source_lifecycle(identity, std::move(read));
    return take_arm<ReviewedSourceReviewRequirement>(
        planned, "Abnormal state did not require full rebind review.");
}

ReviewedSourceVerifiedMaterializedReview complete_initial_review(
    const AurReviewedSourceReviewIdentity& identity) {
    return seal_reviewed_source_materialized_review_for_test(
        ReviewedSourceMaterializedInitialFullReview{
            identity.target_revision(),
            ReviewedSourceReviewBody{
                ReviewedSourceReviewReadiness::Complete, {}}});
}

ReviewedSourceVerifiedMaterializedReview complete_update_review(
    const AurReviewedSourceReviewIdentity& identity,
    SourceRevisionIdentity baseline,
    ReviewedSourceHistoryRelation relation) {
    return seal_reviewed_source_materialized_review_for_test(
        ReviewedSourceMaterializedUpdateReview{
            std::move(baseline), identity.target_revision(), relation,
            ReviewedSourceReviewBody{
                ReviewedSourceReviewReadiness::Complete, {}}});
}

ReviewedSourceVerifiedMaterializedReview complete_rebaseline_review(
    const AurReviewedSourceReviewIdentity& identity,
    SourceRevisionIdentity baseline) {
    return seal_reviewed_source_materialized_review_for_test(
        ReviewedSourceMaterializedRebaselineFullReview{
            std::move(baseline), identity.target_revision(),
            ReviewedSourceBaselineUnavailableReason::
                MissingOrNotCommit,
            ReviewedSourceReviewBody{
                ReviewedSourceReviewReadiness::Complete, {}}});
}

ReviewedSourceVerifiedMaterializedReview nontext_initial_review(
    const AurReviewedSourceReviewIdentity& identity,
    std::string path) {
    const std::string blob_oid =
        identity.git_object_format() == GitObjectFormat::Sha1
            ? std::string(SHA1_C)
            : std::string(SHA256_B);
    const ReviewedSourceObjectId object_id =
        ReviewedSourceObjectId::make(blob_oid);
    const ReviewedSourceProjection projection =
        ReviewedSourceInitialFullReview{
            identity.target_revision(),
            {ReviewedSourceAdded{
                ReviewedSourceFileVersion::make(
                    ReviewedSourcePath::make(std::move(path)),
                    ReviewedSourceFileMode::Regular,
                    object_id,
                    1),
                ReviewedSourceBinaryChange{}}}};
    ReviewedSourceReviewPreparationResult prepared =
        prepare_reviewed_source_review(
            projection,
            {ReviewedSourceRawBlob{object_id, std::string(1, '\0')}});
    ReviewedSourceReviewPreparation preparation =
        take_arm<ReviewedSourceReviewPreparation>(
            prepared,
            "NUL review fixture could not be prepared.");
    ReviewedSourceReviewFinalizationResult finalized =
        finalize_reviewed_source_review(std::move(preparation), {});
    ReviewedSourceMaterializedReview materialized =
        take_arm<ReviewedSourceMaterializedReview>(
            finalized,
            "NUL review fixture could not be finalized.");
    return seal_reviewed_source_materialized_review_for_test(
        std::move(materialized));
}

ReviewedSourceVerifiedLifecycleTarget bind_requirement(
    ReviewedSourceReviewRequirement requirement,
    ReviewedSourceVerifiedMaterializedReview review) {
    TrustedAurReviewedSourceReview trusted =
        make_trusted_aur_reviewed_source_review_fixture_for_test(
            std::move(review));
    ReviewedSourceVerifiedLifecycleResult bound =
        bind_reviewed_source_verified_review(
            std::move(requirement), std::move(trusted));
    return take_arm<ReviewedSourceVerifiedLifecycleTarget>(
        bound, "Verified review did not bind to lifecycle.");
}

ReviewedSourceVerifiedLifecycleTarget complete_bound_target(
    const AurReviewedSourceReviewIdentity& identity) {
    return bind_requirement(
        missing_requirement(identity), complete_initial_review(identity));
}

PresentedReviewedSourceTarget present_complete_target(
    const AurReviewedSourceReviewIdentity& identity,
    std::string* output_text = nullptr) {
    std::ostringstream output;
    PresentedReviewedSourceTargetResult presented =
        present_reviewed_source_target(
            complete_bound_target(identity), output);
    if(output_text != nullptr) *output_text = output.str();
    return take_arm<PresentedReviewedSourceTarget>(
        presented, "Complete review presentation did not succeed.");
}

ExplicitConfirmationResult explicit_confirmation(std::string_view token) {
    ExplicitConfirmationInputParseResult parsed =
        parse_explicit_confirmation_input(token);
    if(auto* accepted =
           std::get_if<ExplicitConfirmationAcceptance>(&parsed)) {
        return std::move(*accepted);
    }
    throw std::runtime_error("Explicit yes token was not accepted by parser.");
}

class FailingOutputBuffer final : public std::streambuf {
protected:
    std::streamsize xsputn(const char*, std::streamsize) override {
        return 0;
    }

    int_type overflow(int_type) override {
        return traits_type::eof();
    }
};

class PartialOutputBuffer final : public std::streambuf {
public:
    [[nodiscard]] std::size_t bytes_written() const noexcept {
        return bytes_written_;
    }

protected:
    std::streamsize xsputn(const char*, std::streamsize count) override {
        const std::streamsize written = std::min<std::streamsize>(count, 7);
        bytes_written_ += static_cast<std::size_t>(written);
        return written;
    }

    int_type overflow(int_type) override {
        return traits_type::eof();
    }

private:
    std::size_t bytes_written_ = 0;
};

class FlushFailureBuffer final : public std::streambuf {
protected:
    std::streamsize xsputn(const char*, std::streamsize count) override {
        return count;
    }

    int sync() override {
        return -1;
    }
};

class ThrowingOutputBuffer final : public std::streambuf {
protected:
    std::streamsize xsputn(const char*, std::streamsize) override {
        throw std::runtime_error("injected streambuf write failure");
    }
};

void test_initial_update_rebaseline_and_abnormal_lifecycles_bind() {
    const AurReviewedSourceReviewIdentity identity = review_identity();

    ReviewedSourceVerifiedLifecycleTarget initial = complete_bound_target(
        identity);
    require(std::holds_alternative<
                ReviewedSourceLifecycleInitialFullReview>(
                initial.lifecycle()),
            "InitialFullReview integration lifecycle was not bound.");

    const SourceRevisionIdentity baseline =
        SourceRevisionIdentity::git_commit(std::string(SHA1_B));
    for(const ReviewedSourceHistoryRelation relation : {
            ReviewedSourceHistoryRelation::Ancestor,
            ReviewedSourceHistoryRelation::NonAncestor}) {
        ReviewedSourceVerifiedLifecycleTarget update = bind_requirement(
            loaded_requirement(identity, baseline),
            complete_update_review(identity, baseline, relation));
        const auto& lifecycle =
            require_arm<ReviewedSourceLifecycleUpdateReview>(
                update.lifecycle(),
                "UpdateReview integration lifecycle was not bound.");
        require(lifecycle.baseline == baseline &&
                    lifecycle.relation == relation,
                "UpdateReview lost baseline or history relation.");
    }

    ReviewedSourceVerifiedLifecycleTarget rebaseline = bind_requirement(
        loaded_requirement(identity, baseline),
        complete_rebaseline_review(identity, baseline));
    const auto& rebaseline_lifecycle =
        require_arm<ReviewedSourceLifecycleRebaselineFullReview>(
            rebaseline.lifecycle(),
            "Unavailable baseline did not map to RebaselineFullReview.");
    require(rebaseline_lifecycle.unavailable_baseline == baseline &&
                rebaseline_lifecycle.reason ==
                    ReviewedSourceBaselineUnavailableReason::
                        MissingOrNotCommit,
            "RebaselineFullReview lost baseline/reason evidence.");

    const std::string corrupted_document;
    ReviewedSourceStateStoreRead exact_abnormal;
    ReviewedSourceVerifiedLifecycleTarget abnormal = bind_requirement(
        abnormal_requirement(
            identity, corrupted_document, &exact_abnormal),
        complete_initial_review(identity));
    const auto& abnormal_lifecycle = require_arm<
        ReviewedSourceLifecycleAbnormalStateRebindFullReview>(
        abnormal.lifecycle(),
        "Corrupted state was flattened into InitialFullReview.");
    require(abnormal_lifecycle.reason ==
                    ReviewedSourceAbnormalStateReason::Corrupted &&
                abnormal.expected_state_observation().store_read() ==
                    exact_abnormal,
            "Abnormal rebind lost reason or exact expected record.");
}

void test_identity_and_revision_rebinding_is_rejected() {
    const AurReviewedSourceReviewIdentity expected = review_identity();

    const auto expect_stop = [&](
                                 const AurReviewedSourceReviewIdentity& required_identity,
                                 ReviewedSourceOperationStopReason reason,
                                 ReviewedSourceVerifiedMaterializedReview review) {
        TrustedAurReviewedSourceReview trusted =
            make_trusted_aur_reviewed_source_review_fixture_for_test(
                std::move(review));
        ReviewedSourceVerifiedLifecycleResult bound =
            bind_reviewed_source_verified_review(
                missing_requirement(required_identity),
                std::move(trusted));
        const auto& stopped = require_arm<ReviewedSourceOperationStop>(
            bound, "Mismatched review identity was rebound.");
        require(stopped.reason() == reason,
                "Mismatched review identity produced wrong stop reason.");
    };

    const AurReviewedSourceReviewIdentity wrong_package = review_identity(
        std::string(SHA1_A), "other-base",
        "https://aur.archlinux.org/other-base.git");
    expect_stop(
        wrong_package,
        ReviewedSourceOperationStopReason::PackageBaseMismatch,
        complete_initial_review(expected));

    expect_invalid_argument([] {
        static_cast<void>(review_identity(
            std::string(SHA1_A), "example-base",
            "https://mirror.invalid/example-base.git"));
    });

    const AurReviewedSourceReviewIdentity wrong_revision =
        review_identity(std::string(SHA1_B));
    expect_stop(
        expected,
        ReviewedSourceOperationStopReason::TargetRevisionMismatch,
        complete_initial_review(wrong_revision));

    const AurReviewedSourceReviewIdentity wrong_format =
        review_identity(std::string(SHA256_A));
    expect_stop(
        expected,
        ReviewedSourceOperationStopReason::GitObjectFormatMismatch,
        complete_initial_review(wrong_format));
}

void test_presentation_completion_is_required_and_all_or_nothing() {
    const AurReviewedSourceReviewIdentity identity = review_identity();
    std::string output;
    PresentedReviewedSourceTarget presented =
        present_complete_target(identity, &output);
    require(output.find("review type: initial full review") !=
                    std::string::npos &&
                output.find(std::string(SHA1_A)) != std::string::npos,
            "Presented capability was created without complete rendered output.");
    require(presented.identity() == identity &&
                presented.readiness() ==
                    ReviewedSourceReviewReadiness::Complete,
            "Presented capability lost identity/readiness provenance.");

    FailingOutputBuffer buffer;
    std::ostream failing_output(&buffer);
    PresentedReviewedSourceTargetResult failed_output =
        present_reviewed_source_target(
            complete_bound_target(identity), failing_output);
    require(require_arm<ReviewedSourceOperationStop>(
                failed_output,
                "Failed output produced a Presented capability.")
                    .reason() ==
                ReviewedSourceOperationStopReason::
                    PresentationOutputFailure,
            "Presentation output failure produced wrong stop reason.");

    FailingOutputBuffer throwing_buffer;
    std::ostream throwing_output(&throwing_buffer);
    throwing_output.exceptions(std::ios::badbit | std::ios::failbit);
    PresentedReviewedSourceTargetResult throwing_failure =
        present_reviewed_source_target(
            complete_bound_target(identity), throwing_output);
    require(require_arm<ReviewedSourceOperationStop>(
                throwing_failure,
                "Throwing output failure escaped the typed boundary.")
                    .reason() ==
                ReviewedSourceOperationStopReason::
                    PresentationOutputFailure,
            "Throwing presentation output produced wrong stop reason.");

    PartialOutputBuffer partial_buffer;
    std::ostream partial_output(&partial_buffer);
    PresentedReviewedSourceTargetResult partial_failure =
        present_reviewed_source_target(
            complete_bound_target(identity), partial_output);
    require(partial_buffer.bytes_written() > 0 &&
                require_arm<ReviewedSourceOperationStop>(
                    partial_failure,
                    "Partial output produced Presented capability.")
                        .reason() ==
                    ReviewedSourceOperationStopReason::
                        PresentationOutputFailure,
            "Partial output did not fail closed as output failure.");

    FlushFailureBuffer flush_buffer;
    std::ostream flush_output(&flush_buffer);
    PresentedReviewedSourceTargetResult flush_failure =
        present_reviewed_source_target(
            complete_bound_target(identity), flush_output);
    require(require_arm<ReviewedSourceOperationStop>(
                flush_failure,
                "Flush failure produced Presented capability.")
                    .reason() ==
                ReviewedSourceOperationStopReason::
                    PresentationOutputFailure,
            "Flush failure did not retain typed output failure.");

    ThrowingOutputBuffer non_ios_buffer;
    std::ostream non_ios_output(&non_ios_buffer);
    non_ios_output.exceptions(std::ios::badbit | std::ios::failbit);
    PresentedReviewedSourceTargetResult non_ios_failure =
        present_reviewed_source_target(
            complete_bound_target(identity), non_ios_output);
    require(require_arm<ReviewedSourceOperationStop>(
                non_ios_failure,
                "Non-ios stream exception escaped or produced Presented.")
                    .reason() ==
                ReviewedSourceOperationStopReason::
                    PresentationOutputFailure,
            "Non-ios stream exception lost typed output failure.");

    ReviewedSourceVerifiedMaterializedReview inconsistent =
        seal_reviewed_source_materialized_review_for_test(
            ReviewedSourceMaterializedInitialFullReview{
                identity.target_revision(),
                ReviewedSourceReviewBody{
                    static_cast<
                        ReviewedSourceReviewReadiness>(99),
                    {}}});
    ReviewedSourceVerifiedLifecycleTarget bound_inconsistent =
        bind_requirement(
            missing_requirement(identity), std::move(inconsistent));
    std::ostringstream rejected_output;
    PresentedReviewedSourceTargetResult rejected =
        present_reviewed_source_target(
            std::move(bound_inconsistent), rejected_output);
    require(require_arm<ReviewedSourceOperationStop>(
                rejected,
                "Inconsistent renderer input produced Presented.")
                        .reason() ==
                    ReviewedSourceOperationStopReason::PresentationFailure &&
                rejected_output.str().empty(),
            "Presentation failure leaked partial output or wrong disposition.");

    const ReviewedSourceReviewFailure materialization_failure{
        ReviewedSourceReviewFailureReason::BlobContentHashMismatch,
        std::nullopt,
        0,
        0,
        0,
        0,
        0};
    require(stop_after_reviewed_source_materialization_failure(
                missing_requirement(identity), materialization_failure)
                    .reason() ==
                ReviewedSourceOperationStopReason::MaterializationFailure,
            "Materialization failure did not map to OperationStop.");
}

void test_only_complete_explicit_yes_creates_accepted_capability() {
    for(const std::string_view token : {std::string_view("y"),
                                        std::string_view("yes")}) {
        const AurReviewedSourceReviewIdentity identity = review_identity();
        ReviewedSourceAcceptanceDisposition disposition =
            decide_reviewed_source_acceptance(
                present_complete_target(identity),
                explicit_confirmation(token));
        const auto& accepted = require_arm<AcceptedReviewedSourceTarget>(
            disposition, "Explicit yes did not create Accepted.");
        require(accepted.identity() == identity &&
                    accepted.readiness() ==
                        ReviewedSourceReviewReadiness::Complete &&
                    accepted.confirmation_origin() ==
                        ConfirmationDecisionOrigin::ExplicitToken &&
                    accepted.reviewed_upstream_base_revision() ==
                        identity.target_revision(),
                "Accepted capability lost required provenance.");
        require(std::holds_alternative<ReviewedSourceStateMissing>(
                    accepted.expected_state_observation().observation()) &&
                    !accepted.expected_state_observation()
                         .observed_record()
                         .has_value(),
                "Initial acceptance did not retain expected-null Missing.");
    }

    const AurReviewedSourceReviewIdentity sha256 =
        review_identity(std::string(SHA256_A));
    ReviewedSourceAcceptanceDisposition sha256_disposition =
        decide_reviewed_source_acceptance(
            present_complete_target(sha256),
            explicit_confirmation("yes"));
    const auto& sha256_accepted =
        require_arm<AcceptedReviewedSourceTarget>(
            sha256_disposition,
            "SHA-256 explicit review was not accepted.");
    require(sha256_accepted.identity().git_object_format() ==
                    GitObjectFormat::Sha256 &&
                sha256_accepted.reviewed_upstream_base_revision()
                        .git_commit() != nullptr &&
                *sha256_accepted.reviewed_upstream_base_revision()
                        .git_commit() == SHA256_A,
            "Accepted capability did not retain exact SHA-256 identity.");
}

void test_capabilities_are_single_owner_and_one_shot() {
    const AurReviewedSourceReviewIdentity identity = review_identity();
    const std::string invalid_document = "schema_version = 1\n";
    ReviewedSourceStateStoreRead exact_read;
    ReviewedSourceVerifiedLifecycleTarget verified = bind_requirement(
        abnormal_requirement(identity, invalid_document, &exact_read),
        complete_initial_review(identity));
    ReviewedSourceVerifiedLifecycleTarget moved_verified(
        std::move(verified));
    require(!verified.valid() && moved_verified.valid(),
            "Moving VerifiedLifecycleTarget did not transfer single ownership.");

    std::ostringstream invalid_verified_output;
    PresentedReviewedSourceTargetResult invalid_verified =
        present_reviewed_source_target(
            std::move(verified), invalid_verified_output);
    require(require_arm<ReviewedSourceOperationStop>(
                invalid_verified,
                "Moved-from VerifiedLifecycleTarget produced Presented.")
                    .reason() ==
                ReviewedSourceOperationStopReason::InvalidCapability,
            "Moved-from VerifiedLifecycleTarget did not fail closed.");

    std::ostringstream output;
    PresentedReviewedSourceTargetResult presented_result =
        present_reviewed_source_target(
            std::move(moved_verified), output);
    PresentedReviewedSourceTarget presented =
        take_arm<PresentedReviewedSourceTarget>(
            presented_result,
            "Moved-to VerifiedLifecycleTarget did not present.");

    ReviewedSourceAcceptanceDisposition first =
        decide_reviewed_source_acceptance(
            std::move(presented), explicit_confirmation("yes"));
    require(!presented.valid(),
            "First Presented consume did not invalidate its source.");
    ReviewedSourceAcceptanceDisposition second =
        decide_reviewed_source_acceptance(
            std::move(presented), explicit_confirmation("yes"));
    require(require_arm<ReviewedSourceOperationStop>(
                second,
                "Second Presented consume produced Accepted.")
                    .reason() ==
                ReviewedSourceOperationStopReason::InvalidCapability,
            "Second Presented consume did not fail closed.");

    AcceptedReviewedSourceTarget accepted =
        take_arm<AcceptedReviewedSourceTarget>(
            first, "First Presented consume did not produce Accepted.");
    AcceptedReviewedSourceTarget moved_accepted(std::move(accepted));
    require(!accepted.valid() && moved_accepted.valid(),
            "Moving Accepted did not transfer single ownership.");
    require(moved_accepted.identity() == identity &&
                moved_accepted.expected_state_observation().store_read() ==
                    exact_read &&
                moved_accepted.expected_state_observation()
                    .observed_record()
                    .has_value() &&
                moved_accepted.expected_state_observation()
                        .observed_record()
                        ->raw_contents == invalid_document,
            "Moved-to Accepted lost identity or exact CAS observation.");

    AcceptedReviewedSourceTarget second_move(std::move(accepted));
    require(!second_move.valid(),
            "Moving an already moved-from Accepted recreated authority.");
}

void test_decline_and_compatibility_bypasses_never_accept() {
    const AurReviewedSourceReviewIdentity identity = review_identity();
    ReviewedSourceAcceptanceDisposition declined =
        decide_reviewed_source_unsealed_confirmation(
            present_complete_target(identity),
            ConfirmationDeclined{
                ConfirmationDecisionOrigin::ExplicitToken});
    const auto& declined_compatibility = require_arm<
        ReviewedSourceCompatibilityBuildWithoutReview>(
        declined,
        "Explicit review decline did not preserve compatibility build.");
    require(declined_compatibility.reason() ==
                    ReviewedSourceCompatibilityBuildReason::
                        ExplicitReviewDecline &&
                declined_compatibility.identity() == identity,
            "Explicit decline compatibility disposition lost its target.");

    const auto no_diff = continue_reviewed_source_without_review(
        missing_requirement(identity),
        ReviewedSourceReviewBypassReason::NoDiff);
    const auto no_confirm = continue_reviewed_source_without_review(
        missing_requirement(identity),
        ReviewedSourceReviewBypassReason::NoConfirm);
    const auto non_interactive = continue_reviewed_source_without_review(
        missing_requirement(identity),
        ReviewedSourceReviewBypassReason::NonInteractiveInput);
    require(no_diff.reason() ==
                    ReviewedSourceCompatibilityBuildReason::NoDiff &&
                no_confirm.reason() ==
                    ReviewedSourceCompatibilityBuildReason::
                        NoConfirm &&
                non_interactive.reason() ==
                    ReviewedSourceCompatibilityBuildReason::
                        NonInteractiveInput,
            "Compatibility bypass reasons were flattened.");

    ReviewedSourceAcceptanceDisposition unavailable_no_confirm =
        decide_reviewed_source_unsealed_confirmation(
            present_complete_target(identity),
            ConfirmationUnavailable{
                ConfirmationUnavailableReason::NoConfirm});
    require(require_arm<ReviewedSourceCompatibilityBuildWithoutReview>(
                unavailable_no_confirm,
                "--noconfirm created acceptance or stopped unexpectedly.")
                    .reason() ==
                ReviewedSourceCompatibilityBuildReason::NoConfirm,
            "--noconfirm did not remain a no-state compatibility build.");

    ReviewedSourceAcceptanceDisposition unavailable_non_tty =
        decide_reviewed_source_unsealed_confirmation(
            present_complete_target(identity),
            ConfirmationUnavailable{
                ConfirmationUnavailableReason::
                    NonInteractiveInput});
    require(require_arm<ReviewedSourceCompatibilityBuildWithoutReview>(
                unavailable_non_tty,
                "Non-TTY created acceptance or stopped unexpectedly.")
                    .reason() ==
                ReviewedSourceCompatibilityBuildReason::
                    NonInteractiveInput,
            "Non-TTY did not remain a no-state compatibility build.");
}

void test_automatic_yes_never_creates_accepted_capability() {
    const AurReviewedSourceReviewIdentity identity = review_identity();
    ReviewedSourceAcceptanceDisposition forged_explicit =
        decide_reviewed_source_unsealed_confirmation(
            present_complete_target(identity),
            ConfirmationAccepted{
                ConfirmationDecisionOrigin::ExplicitToken});
    require(require_arm<ReviewedSourceOperationStop>(
                forged_explicit,
                "Publicly constructed ExplicitToken created Accepted.")
                    .reason() ==
                ReviewedSourceOperationStopReason::NonExplicitAcceptance,
            "Forged ExplicitToken did not fail closed.");

    ConfirmationAccepted relabelled{
        ConfirmationDecisionOrigin::NoConfirm};
    relabelled.origin = ConfirmationDecisionOrigin::ExplicitToken;
    ReviewedSourceAcceptanceDisposition relabelled_automatic =
        decide_reviewed_source_unsealed_confirmation(
            present_complete_target(identity), relabelled);
    require(require_arm<ReviewedSourceOperationStop>(
                relabelled_automatic,
                "Relabelled automatic yes created Accepted.")
                    .reason() ==
                ReviewedSourceOperationStopReason::NonExplicitAcceptance,
            "Relabelled automatic yes did not fail closed.");

    ReviewedSourceAcceptanceDisposition default_yes =
        decide_reviewed_source_unsealed_confirmation(
            present_complete_target(identity),
            ConfirmationAccepted{
                ConfirmationDecisionOrigin::Default});
    require(require_arm<ReviewedSourceOperationStop>(
                default_yes,
                "Default yes created Accepted capability.")
                    .reason() ==
                ReviewedSourceOperationStopReason::NonExplicitAcceptance,
            "Default yes did not fail closed as non-explicit acceptance.");

    ReviewedSourceAcceptanceDisposition no_confirm_yes =
        decide_reviewed_source_unsealed_confirmation(
            present_complete_target(identity),
            ConfirmationAccepted{
                ConfirmationDecisionOrigin::NoConfirm});
    require(require_arm<ReviewedSourceCompatibilityBuildWithoutReview>(
                no_confirm_yes,
                "--noconfirm automatic yes created Accepted.")
                    .reason() ==
                ReviewedSourceCompatibilityBuildReason::NoConfirm,
            "--noconfirm automatic yes gained state authority.");

    ReviewedSourceAcceptanceDisposition non_tty_yes =
        decide_reviewed_source_unsealed_confirmation(
            present_complete_target(identity),
            ConfirmationAccepted{
                ConfirmationDecisionOrigin::
                    NonInteractiveDefault});
    require(require_arm<ReviewedSourceCompatibilityBuildWithoutReview>(
                non_tty_yes,
                "Non-TTY automatic yes created Accepted.")
                    .reason() ==
                ReviewedSourceCompatibilityBuildReason::
                    NonInteractiveInput,
            "Non-TTY automatic yes gained state authority.");
}

void test_cancel_eof_and_input_failure_stop() {
    const AurReviewedSourceReviewIdentity identity = review_identity();
    const struct {
        ConfirmationResult confirmation;
        ReviewedSourceOperationStopReason reason;
    } cases[]{
        {ConfirmationCancelled{
             ConfirmationCancellationReason::ExplicitToken},
         ReviewedSourceOperationStopReason::ExplicitCancellation},
        {ConfirmationCancelled{
             ConfirmationCancellationReason::EndOfInput},
         ReviewedSourceOperationStopReason::EndOfInput},
        {ConfirmationInputFailure{},
         ReviewedSourceOperationStopReason::InputFailure},
    };
    for(const auto& test_case : cases) {
        ReviewedSourceAcceptanceDisposition disposition =
            decide_reviewed_source_unsealed_confirmation(
                present_complete_target(identity),
                test_case.confirmation);
        require(require_arm<ReviewedSourceOperationStop>(
                    disposition,
                    "Cancellation/input failure did not stop.")
                        .reason() == test_case.reason,
                "Cancellation/input failure reasons were flattened.");
    }
}

void test_manual_and_sensitive_readiness_stop_even_on_explicit_yes() {
    const AurReviewedSourceReviewIdentity identity = review_identity();
    const struct {
        std::string path;
        ReviewedSourceReviewReadiness readiness;
        ReviewedSourceOperationStopReason stop_reason;
        std::string_view expected_diagnostic;
    } cases[]{
        {"payload.bin",
         ReviewedSourceReviewReadiness::ManualInspectionRequired,
         ReviewedSourceOperationStopReason::ManualInspectionRequired,
         "requires manual inspection"},
        {"PKGBUILD",
         ReviewedSourceReviewReadiness::SensitiveSourceUnrenderable,
         ReviewedSourceOperationStopReason::
             SensitiveSourceUnrenderable,
         "could not be rendered safely"},
        {"example.install",
         ReviewedSourceReviewReadiness::SensitiveSourceUnrenderable,
         ReviewedSourceOperationStopReason::
             SensitiveSourceUnrenderable,
         "could not be rendered safely"},
    };

    for(const auto& test_case : cases) {
        ReviewedSourceVerifiedLifecycleTarget bound = bind_requirement(
            missing_requirement(identity),
            nontext_initial_review(identity, test_case.path));
        require(bound.readiness() == test_case.readiness,
                "Typed 3B readiness was not retained before presentation.");
        std::ostringstream output;
        PresentedReviewedSourceTargetResult presented_result =
            present_reviewed_source_target(std::move(bound), output);
        PresentedReviewedSourceTarget presented =
            take_arm<PresentedReviewedSourceTarget>(
                presented_result,
                "Manual/sensitive metadata presentation failed.");
        ReviewedSourceAcceptanceDisposition disposition =
            decide_reviewed_source_acceptance(
                std::move(presented), explicit_confirmation("yes"));
        ReviewedSourceOperationStop stop =
            take_arm<ReviewedSourceOperationStop>(
                disposition,
                "Manual/sensitive explicit yes created acceptance.");
        require(stop.reason() == test_case.stop_reason,
                "Manual/sensitive readiness did not stop the invocation.");
        const std::string diagnostic =
            reviewed_source_production_failure_diagnostic(
                ReviewedSourceProductionFailure{
                    ReviewedSourceProductionFailureStage::
                        Acceptance,
                    ReviewedSourceProductionFailureReason::
                        ReviewOperationStopped,
                    std::move(stop)});
        require(diagnostic.find(test_case.expected_diagnostic) !=
                        std::string::npos &&
                    diagnostic.find("without state publication or compatibility fallback") !=
                        std::string::npos,
                "Manual/sensitive STOP diagnostic was flattened.");
    }
}

void test_abnormal_acceptance_retains_exact_cas_observation() {
    const AurReviewedSourceReviewIdentity identity = review_identity();
    const std::string invalid_document = "schema_version = 1\n";
    ReviewedSourceStateStoreRead exact_read;
    ReviewedSourceReviewRequirement requirement = abnormal_requirement(
        identity, invalid_document, &exact_read);
    ReviewedSourceVerifiedLifecycleTarget bound = bind_requirement(
        std::move(requirement), complete_initial_review(identity));
    std::ostringstream output;
    PresentedReviewedSourceTargetResult presented_result =
        present_reviewed_source_target(std::move(bound), output);
    PresentedReviewedSourceTarget presented =
        take_arm<PresentedReviewedSourceTarget>(
            presented_result,
            "Abnormal full review was not presented.");
    ReviewedSourceAcceptanceDisposition disposition =
        decide_reviewed_source_acceptance(
            std::move(presented), explicit_confirmation("yes"));
    const auto& accepted = require_arm<AcceptedReviewedSourceTarget>(
        disposition,
        "Explicit abnormal rebind review was not accepted.");
    require(std::holds_alternative<
                ReviewedSourceLifecycleAbnormalStateRebindFullReview>(
                accepted.lifecycle()) &&
                accepted.expected_state_observation().store_read() ==
                    exact_read &&
                accepted.expected_state_observation()
                    .observed_record()
                    .has_value(),
            "Accepted abnormal rebind lost lifecycle or exact CAS guard.");
}

} // namespace

int main() {
    try {
        test_initial_update_rebaseline_and_abnormal_lifecycles_bind();
        test_identity_and_revision_rebinding_is_rejected();
        test_presentation_completion_is_required_and_all_or_nothing();
        test_only_complete_explicit_yes_creates_accepted_capability();
        test_capabilities_are_single_owner_and_one_shot();
        test_decline_and_compatibility_bypasses_never_accept();
        test_automatic_yes_never_creates_accepted_capability();
        test_cancel_eof_and_input_failure_stop();
        test_manual_and_sensitive_readiness_stop_even_on_explicit_yes();
        test_abnormal_acceptance_retains_exact_cas_observation();
    } catch(const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    std::cout << "reviewed source acceptance tests: all checks passed\n";
    return 0;
}
