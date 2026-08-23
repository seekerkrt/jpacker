#pragma once

#include <optional>
#include <string>
#include <variant>
#include <vector>

enum class RootPackageSourceKind {
    Repository,
    Aur
};

// Root discoveryのcandidate / selected targetであることを明示し、
// dependency plan固有のPackageRoleとは分離する。
enum class RootPackageTargetRole {
    Root
};

struct RepositoryRootPackageIdentity {
    std::string repository_name;
    std::string package_name;

    bool operator==(const RepositoryRootPackageIdentity&) const = default;
};

struct AurRootPackageIdentity {
    std::string package_name;
    std::string package_base;

    bool operator==(const AurRootPackageIdentity&) const = default;
};

using RootPackageIdentity =
        std::variant<RepositoryRootPackageIdentity, AurRootPackageIdentity>;

RootPackageSourceKind root_package_source_kind(
        const RootPackageIdentity& identity) noexcept;
const std::string& root_package_name(
        const RootPackageIdentity& identity) noexcept;
bool same_root_package_identity(
        const RootPackageIdentity& lhs,
        const RootPackageIdentity& rhs) noexcept;

struct RootPackageCandidatePresentation {
    std::optional<std::string> version;
    std::optional<std::string> description;

    bool operator==(const RootPackageCandidatePresentation&) const = default;
};

enum class RootPackageCandidateValidationIssueKind {
    InvalidRepositoryName,
    InvalidPackageName,
    InvalidPackageBase,
    InvalidVersion,
    InvalidDescription
};

// raw valueはtyped adapterの診断用に保持するが、このmodelでは表示しない。
struct RootPackageCandidateValidationIssue {
    RootPackageCandidateValidationIssueKind kind;
    std::string                             value;

    bool operator==(const RootPackageCandidateValidationIssue&) const = default;
};

struct RootPackageCandidateValidationFailure {
    RootPackageSourceKind                         source_kind;
    std::vector<RootPackageCandidateValidationIssue> issues;

    bool operator==(const RootPackageCandidateValidationFailure&) const =
            default;
};

class RootPackageCandidateValidationResult;
class RootPackageCandidatePairResult;
class SelectedRootPackageTarget;

class RootPackageCandidate final {
public:
    RootPackageCandidate(const RootPackageCandidate&) = default;
    RootPackageCandidate(RootPackageCandidate&&) noexcept = default;
    RootPackageCandidate& operator=(const RootPackageCandidate&) = default;
    RootPackageCandidate& operator=(RootPackageCandidate&&) noexcept = default;
    ~RootPackageCandidate() = default;

    [[nodiscard]] RootPackageSourceKind source_kind() const noexcept;
    [[nodiscard]] RootPackageTargetRole target_role() const noexcept;
    [[nodiscard]] const RootPackageIdentity& identity() const noexcept;
    [[nodiscard]] const std::string& package_name() const noexcept;
    [[nodiscard]] const RootPackageCandidatePresentation& presentation()
            const noexcept;

    bool operator==(const RootPackageCandidate&) const = default;

private:
    RootPackageCandidate(
            RootPackageIdentity identity,
            RootPackageCandidatePresentation presentation) noexcept;

    RootPackageIdentity              identity_;
    RootPackageTargetRole            target_role_ =
            RootPackageTargetRole::Root;
    RootPackageCandidatePresentation presentation_;

    friend RootPackageCandidateValidationResult
    make_repository_root_package_candidate(
            std::string repository_name, std::string package_name,
            std::optional<std::string> version,
            std::optional<std::string> description);
    friend RootPackageCandidateValidationResult
    make_aur_root_package_candidate(
            std::string package_name, std::string package_base,
            std::optional<std::string> version,
            std::optional<std::string> description);
    friend RootPackageCandidatePairResult assess_root_package_candidate_pair(
            const RootPackageCandidate& lhs,
            const RootPackageCandidate& rhs);
};

class RootPackageCandidateValidationResult final {
public:
    RootPackageCandidateValidationResult() = delete;
    RootPackageCandidateValidationResult(
            const RootPackageCandidateValidationResult&) = default;
    RootPackageCandidateValidationResult(
            RootPackageCandidateValidationResult&&) noexcept = default;
    RootPackageCandidateValidationResult& operator=(
            const RootPackageCandidateValidationResult&) = delete;
    RootPackageCandidateValidationResult& operator=(
            RootPackageCandidateValidationResult&&) noexcept = delete;
    ~RootPackageCandidateValidationResult() = default;

    [[nodiscard]] bool is_valid() const noexcept;
    [[nodiscard]] const RootPackageCandidate* candidate() const noexcept;
    [[nodiscard]] const RootPackageCandidateValidationFailure* failure()
            const noexcept;

private:
    explicit RootPackageCandidateValidationResult(
            RootPackageCandidate candidate) noexcept;
    explicit RootPackageCandidateValidationResult(
            RootPackageCandidateValidationFailure failure) noexcept;

    std::variant<RootPackageCandidate,
                 RootPackageCandidateValidationFailure>
            outcome_;

    friend RootPackageCandidateValidationResult
    make_repository_root_package_candidate(
            std::string repository_name, std::string package_name,
            std::optional<std::string> version,
            std::optional<std::string> description);
    friend RootPackageCandidateValidationResult
    make_aur_root_package_candidate(
            std::string package_name, std::string package_base,
            std::optional<std::string> version,
            std::optional<std::string> description);
};

RootPackageCandidateValidationResult make_repository_root_package_candidate(
        std::string repository_name, std::string package_name,
        std::optional<std::string> version = std::nullopt,
        std::optional<std::string> description = std::nullopt);
RootPackageCandidateValidationResult make_aur_root_package_candidate(
        std::string package_name, std::string package_base,
        std::optional<std::string> version = std::nullopt,
        std::optional<std::string> description = std::nullopt);

class SelectedRootPackageTarget final {
public:
    SelectedRootPackageTarget(const SelectedRootPackageTarget&) = default;
    SelectedRootPackageTarget(SelectedRootPackageTarget&&) noexcept = default;
    SelectedRootPackageTarget& operator=(
            const SelectedRootPackageTarget&) = default;
    SelectedRootPackageTarget& operator=(
            SelectedRootPackageTarget&&) noexcept = default;
    ~SelectedRootPackageTarget() = default;

    [[nodiscard]] RootPackageSourceKind source_kind() const noexcept;
    [[nodiscard]] RootPackageTargetRole target_role() const noexcept;
    [[nodiscard]] const RootPackageIdentity& identity() const noexcept;
    [[nodiscard]] const std::string& package_name() const noexcept;

    bool operator==(const SelectedRootPackageTarget&) const = default;

private:
    explicit SelectedRootPackageTarget(
            RootPackageIdentity identity) noexcept;

    RootPackageIdentity   identity_;
    RootPackageTargetRole target_role_ = RootPackageTargetRole::Root;

    friend SelectedRootPackageTarget select_root_package_target(
            const RootPackageCandidate& candidate);
};

SelectedRootPackageTarget select_root_package_target(
        const RootPackageCandidate& candidate);

struct DistinctRootPackageCandidates {
    bool operator==(const DistinctRootPackageCandidates&) const = default;
};

// Slice 3のcollection dedupとは分離し、pairで両立するmetadataだけを保持する。
struct DuplicateRootPackageCandidate {
    RootPackageCandidate candidate;

    bool operator==(const DuplicateRootPackageCandidate&) const = default;
};

struct InconsistentAurRootPackageBase {
    std::string package_name;
    std::string first_package_base;
    std::string second_package_base;

    bool operator==(const InconsistentAurRootPackageBase&) const = default;
};

enum class RootPackageCandidateMetadataField {
    Version,
    Description
};

struct ConflictingRootPackageCandidateMetadata {
    RootPackageIdentity                 identity;
    RootPackageCandidateMetadataField   field;
    std::string                         first_value;
    std::string                         second_value;

    bool operator==(
            const ConflictingRootPackageCandidateMetadata&) const = default;
};

using RootPackageCandidatePairIssue =
        std::variant<InconsistentAurRootPackageBase,
                     ConflictingRootPackageCandidateMetadata>;

struct InvalidRootPackageCandidatePair {
    std::vector<RootPackageCandidatePairIssue> issues;

    bool operator==(const InvalidRootPackageCandidatePair&) const = default;
};

class RootPackageCandidatePairResult final {
public:
    RootPackageCandidatePairResult() = delete;
    RootPackageCandidatePairResult(
            const RootPackageCandidatePairResult&) = default;
    RootPackageCandidatePairResult(
            RootPackageCandidatePairResult&&) noexcept = default;
    RootPackageCandidatePairResult& operator=(
            const RootPackageCandidatePairResult&) = delete;
    RootPackageCandidatePairResult& operator=(
            RootPackageCandidatePairResult&&) noexcept = delete;
    ~RootPackageCandidatePairResult() = default;

    [[nodiscard]] bool is_distinct() const noexcept;
    [[nodiscard]] bool is_duplicate() const noexcept;
    [[nodiscard]] bool is_invalid() const noexcept;
    [[nodiscard]] const DistinctRootPackageCandidates* distinct()
            const noexcept;
    [[nodiscard]] const DuplicateRootPackageCandidate* duplicate()
            const noexcept;
    [[nodiscard]] const InvalidRootPackageCandidatePair* invalid()
            const noexcept;

private:
    explicit RootPackageCandidatePairResult(
            DistinctRootPackageCandidates distinct) noexcept;
    explicit RootPackageCandidatePairResult(
            DuplicateRootPackageCandidate duplicate) noexcept;
    explicit RootPackageCandidatePairResult(
            InvalidRootPackageCandidatePair invalid) noexcept;

    std::variant<DistinctRootPackageCandidates,
                 DuplicateRootPackageCandidate,
                 InvalidRootPackageCandidatePair>
            outcome_;

    friend RootPackageCandidatePairResult assess_root_package_candidate_pair(
            const RootPackageCandidate& lhs,
            const RootPackageCandidate& rhs);
};

RootPackageCandidatePairResult assess_root_package_candidate_pair(
        const RootPackageCandidate& lhs,
        const RootPackageCandidate& rhs);
