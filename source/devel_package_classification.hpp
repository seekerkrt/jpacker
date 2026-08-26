#pragma once

#include "vcs_source_identity.hpp"

#include <optional>
#include <string>
#include <vector>

class DevelChildSuffixEvidence final {
public:
    DevelChildSuffixEvidence() = delete;
    DevelChildSuffixEvidence(const DevelChildSuffixEvidence&) = default;
    DevelChildSuffixEvidence(DevelChildSuffixEvidence&&) noexcept = default;
    DevelChildSuffixEvidence& operator=(
            const DevelChildSuffixEvidence&) = default;
    DevelChildSuffixEvidence& operator=(
            DevelChildSuffixEvidence&&) noexcept = default;
    ~DevelChildSuffixEvidence() = default;

    [[nodiscard]] static DevelChildSuffixEvidence classify(
            std::string package_name);

    [[nodiscard]] const std::string& package_name() const noexcept;
    [[nodiscard]] const VcsKind* candidate_kind() const noexcept;

    bool operator==(const DevelChildSuffixEvidence&) const = default;

private:
    DevelChildSuffixEvidence(
            std::string package_name,
            std::optional<VcsKind> candidate_kind) noexcept;

    std::string            package_name_;
    std::optional<VcsKind> candidate_kind_;
};

// PackageBase and installed child suffixes remain independent evidence. A
// match makes a candidate only; it never confirms source metadata or support.
class DevelPackageSuffixEvidence final {
public:
    DevelPackageSuffixEvidence() = delete;
    DevelPackageSuffixEvidence(const DevelPackageSuffixEvidence&) = default;
    DevelPackageSuffixEvidence(DevelPackageSuffixEvidence&&) noexcept =
            default;
    DevelPackageSuffixEvidence& operator=(
            const DevelPackageSuffixEvidence&) = default;
    DevelPackageSuffixEvidence& operator=(
            DevelPackageSuffixEvidence&&) noexcept = default;
    ~DevelPackageSuffixEvidence() = default;

    [[nodiscard]] static DevelPackageSuffixEvidence classify(
            std::string package_base,
            std::vector<std::string> installed_children);

    [[nodiscard]] const std::string& package_base() const noexcept;
    [[nodiscard]] const VcsKind* package_base_candidate_kind()
            const noexcept;
    [[nodiscard]] const std::vector<DevelChildSuffixEvidence>&
    installed_children() const noexcept;
    [[nodiscard]] bool has_candidate() const noexcept;

    bool operator==(const DevelPackageSuffixEvidence&) const = default;

private:
    DevelPackageSuffixEvidence(
            std::string package_base,
            std::optional<VcsKind> package_base_candidate_kind,
            std::vector<DevelChildSuffixEvidence> installed_children)
        noexcept;

    std::string                           package_base_;
    std::optional<VcsKind>                package_base_candidate_kind_;
    std::vector<DevelChildSuffixEvidence> installed_children_;
};

class TrustedDevelSourceMetadata final {
public:
    TrustedDevelSourceMetadata() = delete;
    TrustedDevelSourceMetadata(const TrustedDevelSourceMetadata&) = default;
    TrustedDevelSourceMetadata(TrustedDevelSourceMetadata&&) noexcept =
            default;
    TrustedDevelSourceMetadata& operator=(
            const TrustedDevelSourceMetadata&) = default;
    TrustedDevelSourceMetadata& operator=(
            TrustedDevelSourceMetadata&&) noexcept = default;
    ~TrustedDevelSourceMetadata() = default;

    [[nodiscard]] static TrustedDevelSourceMetadata make(
            VcsSourceIdentity source) noexcept;

    [[nodiscard]] const VcsSourceIdentity& source() const noexcept;

    bool operator==(const TrustedDevelSourceMetadata&) const = default;

private:
    explicit TrustedDevelSourceMetadata(VcsSourceIdentity source) noexcept;

    VcsSourceIdentity source_;
};

// This is typed classification input only. It does not define a provenance
// schema, persistence path, installed-artifact binding, or publication rule.
class SuccessfulBuildSourceConfirmation final {
public:
    SuccessfulBuildSourceConfirmation() = delete;
    SuccessfulBuildSourceConfirmation(
            const SuccessfulBuildSourceConfirmation&) = default;
    SuccessfulBuildSourceConfirmation(
            SuccessfulBuildSourceConfirmation&&) noexcept = default;
    SuccessfulBuildSourceConfirmation& operator=(
            const SuccessfulBuildSourceConfirmation&) = default;
    SuccessfulBuildSourceConfirmation& operator=(
            SuccessfulBuildSourceConfirmation&&) noexcept = default;
    ~SuccessfulBuildSourceConfirmation() = default;

    [[nodiscard]] static SuccessfulBuildSourceConfirmation make(
            VcsSourceIdentity source) noexcept;

    [[nodiscard]] const VcsSourceIdentity& source() const noexcept;

    bool operator==(
            const SuccessfulBuildSourceConfirmation&) const = default;

private:
    explicit SuccessfulBuildSourceConfirmation(
            VcsSourceIdentity source) noexcept;

    VcsSourceIdentity source_;
};

// Evidence level records why a package is classified, not whether an update is
// available. BuildSourceConfirmed retains a typed classification input only;
// it is not installed-artifact provenance or execution readiness.
enum class DevelEvidenceLevel {
    Normal,
    SuffixCandidate,
    SourceMetadataConfirmed,
    BuildSourceConfirmed,
};

// TrackableCandidate is a source-form classification only. It does not prove
// transport support, an authoritative installed baseline, remote equality,
// update availability, or execution readiness.
enum class DevelSourceFormDisposition {
    NotApplicable,
    TrackableCandidate,
    Fixed,
    RequiresCheck,
    Unsupported,
};

// Evidence and source-form disposition are orthogonal. Neither axis converts
// to DevelUpdateAssessment or authorizes production execution.
class DevelPackageClassification final {
public:
    DevelPackageClassification() = delete;
    DevelPackageClassification(const DevelPackageClassification&) = default;
    DevelPackageClassification(DevelPackageClassification&&) noexcept =
            default;
    DevelPackageClassification& operator=(
            const DevelPackageClassification&) = default;
    DevelPackageClassification& operator=(
            DevelPackageClassification&&) noexcept = default;
    ~DevelPackageClassification() = default;

    [[nodiscard]] static DevelPackageClassification classify(
            DevelPackageSuffixEvidence suffix_evidence,
            std::vector<TrustedDevelSourceMetadata> trusted_metadata = {},
            std::vector<SuccessfulBuildSourceConfirmation>
                    successful_build_confirmations = {});

    [[nodiscard]] DevelEvidenceLevel evidence_level() const noexcept;
    [[nodiscard]] DevelSourceFormDisposition source_form_disposition()
            const noexcept;
    [[nodiscard]] const DevelPackageSuffixEvidence& suffix_evidence()
            const noexcept;
    [[nodiscard]] const std::vector<TrustedDevelSourceMetadata>&
    trusted_metadata() const noexcept;
    [[nodiscard]] const std::vector<SuccessfulBuildSourceConfirmation>&
    successful_build_confirmations() const noexcept;

    bool operator==(const DevelPackageClassification&) const = default;

private:
    DevelPackageClassification(
            DevelEvidenceLevel evidence_level,
            DevelSourceFormDisposition source_form_disposition,
            DevelPackageSuffixEvidence suffix_evidence,
            std::vector<TrustedDevelSourceMetadata> trusted_metadata,
            std::vector<SuccessfulBuildSourceConfirmation>
                    successful_build_confirmations) noexcept;

    DevelEvidenceLevel          evidence_level_;
    DevelSourceFormDisposition source_form_disposition_;
    DevelPackageSuffixEvidence suffix_evidence_;
    std::vector<TrustedDevelSourceMetadata> trusted_metadata_;
    std::vector<SuccessfulBuildSourceConfirmation>
            successful_build_confirmations_;
};

[[nodiscard]] std::optional<VcsKind> devel_suffix_candidate_kind(
        const std::string& package_name);

[[nodiscard]] DevelSourceFormDisposition classify_devel_source_form(
        const VcsSourceIdentity& source);
