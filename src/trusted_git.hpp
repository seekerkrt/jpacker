#pragma once

#include "reviewed_source_review.hpp"
#include "reviewed_source_projection.hpp"
#include "reviewed_source_trusted_review.hpp"
#include "trusted_cache.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <variant>

// Moguet-owned persistent checkoutで許可するGit operationだけを公開する。
// Filesystem mutation authorityはValidatedCachePath側に残し、Gitへ渡すpathは
// explicit repository/worktree binding用のlogical viewとしてのみ使用する。
std::string trusted_git_remote_origin_url(
        const ValidatedCachePath& checkout);
int trusted_git_fetch_origin(
        const ValidatedCachePath& checkout,
        const std::string& expected_remote_url);
std::string trusted_git_detect_remote_branch(
        const ValidatedCachePath& checkout,
        const std::string& expected_remote_url);
int trusted_git_diff_quiet(
        const ValidatedCachePath& checkout,
        const std::string& expected_remote_url,
        const std::string& branch);
std::string trusted_git_diff_name_only(
        const ValidatedCachePath& checkout,
        const std::string& expected_remote_url,
        const std::string& branch);
int trusted_git_show_diff(
        const ValidatedCachePath& checkout,
        const std::string& expected_remote_url,
        const std::string& branch);
int trusted_git_reset_hard(
        const ValidatedCachePath& checkout,
        const std::string& expected_remote_url,
        const std::string& branch);
int trusted_git_clone_persistent_checkout(
        const ValidatedCachePath& destination,
        const std::string& remote_url);

enum class TrustedGitReviewStage {
    TargetResolution,
    TargetValidation,
    BaselineValidation,
    ShallowRepositoryCheck,
    AncestryCheck,
    BaselineTree,
    TargetTree,
    NameStatus,
    Numstat,
    AttributeGuard,
    HistoryGuard,
    ResourcePreflight,
    CrossStream,
    BlobRead,
    PatchGeneration,
};

enum class TrustedGitReviewFailureReason {
    CommandFailed,
    CaptureLimitExceeded,
    MalformedMachineOutput,
    InconsistentMachineOutput,
    RenameCandidateLimitExceeded,
    SingleBlobSizeLimitExceeded,
    AggregateBlobSizeLimitExceeded,
    LocalAttributeOverride,
    LocalHistoryOverride,
    ShallowRepositoryUnsupported,
    ObjectFormatMismatch,
    ReviewIdentityMismatch,
};

struct TrustedGitReviewFailure {
    TrustedGitReviewFailureReason reason;
    TrustedGitReviewStage stage;
    std::optional<int> exit_code;
    std::size_t record_index = 0;
    std::uintmax_t observed = 0;
    std::uintmax_t limit = 0;

    bool operator==(const TrustedGitReviewFailure&) const = default;
};

using TrustedGitCommitResolutionResult = std::variant<
        SourceRevisionIdentity,
        TrustedGitReviewFailure>;

using TrustedGitReviewedSourceProjectionResult = std::variant<
        ReviewedSourceInitialFullReview,
        ReviewedSourceAlreadyReviewed,
        ReviewedSourceUpdateReview,
        ReviewedSourceRebaselineFullReview,
        TrustedGitReviewFailure>;

using TrustedGitAurReviewedSourceProjectionResult = std::variant<
        TrustedAurReviewedSourceProjection,
        TrustedGitReviewFailure>;

using TrustedGitReviewedSourceMaterializationResult = std::variant<
        ReviewedSourceVerifiedMaterializedReview,
        ReviewedSourceReviewFailure,
        TrustedGitReviewFailure>;

using TrustedGitAurReviewedSourceMaterializationResult = std::variant<
        TrustedAurReviewedSourceReview,
        ReviewedSourceReviewFailure,
        TrustedGitReviewFailure>;

// Resolve the mutable remote-tracking ref once. Callers must retain and use
// only the returned complete commit OID for later projection.
TrustedGitCommitResolutionResult trusted_git_resolve_remote_commit(
        const ValidatedCachePath& checkout,
        const std::string& expected_remote_url,
        const std::string& branch);

// Read commit trees only. This API does not update refs, index, working tree,
// reviewed state, or any production source-build lifecycle.
TrustedGitReviewedSourceProjectionResult trusted_git_project_reviewed_source(
        const ValidatedCachePath& checkout,
        const std::string& expected_remote_url,
        const SourceRevisionIdentity& target,
        const std::optional<SourceRevisionIdentity>& baseline);

// Project and seal the complete Slice 3A inventory together with its AUR
// PackageBase/source/remote/format/target/baseline provenance.
TrustedGitAurReviewedSourceProjectionResult
trusted_git_project_aur_reviewed_source(
        const ValidatedCachePath& checkout,
        AurReviewedSourceReviewIdentity identity,
        std::optional<SourceRevisionIdentity> baseline);

// Materialize exact blobs named by a sealed Slice 3A projection capability.
// This API does not resolve refs, read worktree/index content, render human
// output, publish reviewed state, or connect a production lifecycle.
TrustedGitReviewedSourceMaterializationResult
trusted_git_materialize_reviewed_source_review(
        const ValidatedCachePath& checkout,
        TrustedAurReviewedSourceProjection projection);

// Seals PackageBase/source/remote/exact target and the verified 3B review in
// one trusted construction boundary. The returned capability is the only 4A
// input that can bind trusted Git review provenance to acceptance.
TrustedGitAurReviewedSourceMaterializationResult
trusted_git_materialize_aur_reviewed_source_review(
        const ValidatedCachePath& checkout,
        TrustedAurReviewedSourceProjection projection);

#ifdef MOGUET_ENABLE_REVIEWED_SOURCE_GIT_TEST_HOOKS
void set_trusted_git_review_machine_stream_limit_for_test(
        std::optional<std::size_t> limit);
#endif

// PKGBUILD exportはcache checkoutとは別のdescriptor-anchored temporary
// lifecycle。Git process isolationだけを共有し、cache capabilityへは昇格しない。
int trusted_git_clone_aur_export(
        const std::string& remote_url,
        const std::filesystem::path& anchored_destination);
std::string trusted_git_aur_export_remote_origin_url(
        const std::filesystem::path& anchored_checkout);
