#include "devel_package_classification.hpp"

#include "package_identifier.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace {

constexpr std::array<std::pair<std::string_view, VcsKind>, 6>
        DEVEL_SUFFIXES = {{
                {"-git", VcsKind::Git},
                {"-svn", VcsKind::Svn},
                {"-hg", VcsKind::Hg},
                {"-bzr", VcsKind::Bzr},
                {"-cvs", VcsKind::Cvs},
                {"-darcs", VcsKind::Darcs},
        }};

void require_package_identity(
        const std::string& package_name, std::string_view role) {
    if(!is_valid_package_name(package_name)) {
        throw std::invalid_argument(
                std::string(role) + " is not a valid package identity.");
    }
}

void append_unique_source(
        std::vector<VcsSourceIdentity>& unique_sources,
        const VcsSourceIdentity& source) {
    if(std::find(unique_sources.begin(), unique_sources.end(), source) ==
       unique_sources.end()) {
        unique_sources.push_back(source);
    }
}

DevelSourceFormDisposition classify_confirmed_sources(
        const std::vector<TrustedDevelSourceMetadata>& trusted_metadata,
        const std::vector<SuccessfulBuildSourceConfirmation>&
                successful_build_confirmations) {
    std::vector<VcsSourceIdentity> unique_sources;
    unique_sources.reserve(
            trusted_metadata.size() +
            successful_build_confirmations.size());
    for(const auto& metadata : trusted_metadata) {
        append_unique_source(unique_sources, metadata.source());
    }
    for(const auto& confirmation : successful_build_confirmations) {
        append_unique_source(unique_sources, confirmation.source());
    }

    bool has_requires_check = false;
    bool has_unsupported = false;
    std::size_t floating_source_count = 0;
    for(const auto& source : unique_sources) {
        switch(classify_devel_source_form(source)) {
        case DevelSourceFormDisposition::TrackableCandidate:
            ++floating_source_count;
            break;
        case DevelSourceFormDisposition::Fixed:
            break;
        case DevelSourceFormDisposition::RequiresCheck:
            has_requires_check = true;
            break;
        case DevelSourceFormDisposition::Unsupported:
            has_unsupported = true;
            break;
        case DevelSourceFormDisposition::NotApplicable:
            throw std::logic_error(
                    "Confirmed devel source form is not applicable.");
        }
    }

    if(has_unsupported) {
        return DevelSourceFormDisposition::Unsupported;
    }
    if(has_requires_check || floating_source_count > 1) {
        return DevelSourceFormDisposition::RequiresCheck;
    }
    if(floating_source_count == 1) {
        return DevelSourceFormDisposition::TrackableCandidate;
    }
    return DevelSourceFormDisposition::Fixed;
}

} // namespace

std::optional<VcsKind> devel_suffix_candidate_kind(
        const std::string& package_name) {
    require_package_identity(package_name, "Devel suffix package");
    for(const auto& [suffix, kind] : DEVEL_SUFFIXES) {
        if(package_name.size() > suffix.size() &&
           package_name.ends_with(suffix)) {
            return kind;
        }
    }
    return std::nullopt;
}

DevelChildSuffixEvidence::DevelChildSuffixEvidence(
        std::string package_name,
        std::optional<VcsKind> candidate_kind) noexcept
    : package_name_(std::move(package_name)),
      candidate_kind_(candidate_kind) {}

DevelChildSuffixEvidence DevelChildSuffixEvidence::classify(
        std::string package_name) {
    std::optional<VcsKind> candidate_kind =
            devel_suffix_candidate_kind(package_name);
    return DevelChildSuffixEvidence(
            std::move(package_name), candidate_kind);
}

const std::string& DevelChildSuffixEvidence::package_name() const noexcept {
    return package_name_;
}

const VcsKind* DevelChildSuffixEvidence::candidate_kind() const noexcept {
    return candidate_kind_.has_value() ? &candidate_kind_.value() : nullptr;
}

DevelPackageSuffixEvidence::DevelPackageSuffixEvidence(
        std::string package_base,
        std::optional<VcsKind> package_base_candidate_kind,
        std::vector<DevelChildSuffixEvidence> installed_children) noexcept
    : package_base_(std::move(package_base)),
      package_base_candidate_kind_(package_base_candidate_kind),
      installed_children_(std::move(installed_children)) {}

DevelPackageSuffixEvidence DevelPackageSuffixEvidence::classify(
        std::string package_base,
        std::vector<std::string> installed_children) {
    std::optional<VcsKind> package_base_candidate_kind =
            devel_suffix_candidate_kind(package_base);
    std::vector<DevelChildSuffixEvidence> child_evidence;
    child_evidence.reserve(installed_children.size());
    for(auto& child : installed_children) {
        child_evidence.push_back(
                DevelChildSuffixEvidence::classify(std::move(child)));
    }
    return DevelPackageSuffixEvidence(
            std::move(package_base),
            package_base_candidate_kind,
            std::move(child_evidence));
}

const std::string& DevelPackageSuffixEvidence::package_base() const noexcept {
    return package_base_;
}

const VcsKind*
DevelPackageSuffixEvidence::package_base_candidate_kind() const noexcept {
    return package_base_candidate_kind_.has_value()
            ? &package_base_candidate_kind_.value()
            : nullptr;
}

const std::vector<DevelChildSuffixEvidence>&
DevelPackageSuffixEvidence::installed_children() const noexcept {
    return installed_children_;
}

bool DevelPackageSuffixEvidence::has_candidate() const noexcept {
    if(package_base_candidate_kind_.has_value()) return true;
    return std::any_of(
            installed_children_.begin(),
            installed_children_.end(),
            [](const DevelChildSuffixEvidence& child) {
                return child.candidate_kind() != nullptr;
            });
}

TrustedDevelSourceMetadata::TrustedDevelSourceMetadata(
        VcsSourceIdentity source) noexcept
    : source_(std::move(source)) {}

TrustedDevelSourceMetadata TrustedDevelSourceMetadata::make(
        VcsSourceIdentity source) noexcept {
    return TrustedDevelSourceMetadata(std::move(source));
}

const VcsSourceIdentity& TrustedDevelSourceMetadata::source() const noexcept {
    return source_;
}

SuccessfulBuildSourceConfirmation::SuccessfulBuildSourceConfirmation(
        VcsSourceIdentity source) noexcept
    : source_(std::move(source)) {}

SuccessfulBuildSourceConfirmation SuccessfulBuildSourceConfirmation::make(
        VcsSourceIdentity source) noexcept {
    return SuccessfulBuildSourceConfirmation(std::move(source));
}

const VcsSourceIdentity&
SuccessfulBuildSourceConfirmation::source() const noexcept {
    return source_;
}

DevelSourceFormDisposition classify_devel_source_form(
        const VcsSourceIdentity& source) {
    switch(source.kind()) {
    case VcsKind::Cvs:
    case VcsKind::Darcs:
        return DevelSourceFormDisposition::Unsupported;
    case VcsKind::Git:
    case VcsKind::Svn:
    case VcsKind::Hg:
    case VcsKind::Bzr:
        break;
    }

    // Architecture resolution belongs to a later metadata adapter. Retaining
    // the qualifier is not evidence that this source applies to the effective
    // build architecture.
    if(source.architecture() != nullptr) {
        return DevelSourceFormDisposition::RequiresCheck;
    }

    switch(source.selector().tracking_behavior()) {
    case VcsSelectorTrackingBehavior::Fixed:
        return DevelSourceFormDisposition::Fixed;
    case VcsSelectorTrackingBehavior::Indeterminate:
        return DevelSourceFormDisposition::RequiresCheck;
    case VcsSelectorTrackingBehavior::Floating:
        break;
    }

    return source.kind() == VcsKind::Git
            ? DevelSourceFormDisposition::TrackableCandidate
            : DevelSourceFormDisposition::RequiresCheck;
}

DevelPackageClassification::DevelPackageClassification(
        DevelEvidenceLevel evidence_level,
        DevelSourceFormDisposition source_form_disposition,
        DevelPackageSuffixEvidence suffix_evidence,
        std::vector<TrustedDevelSourceMetadata> trusted_metadata,
        std::vector<SuccessfulBuildSourceConfirmation>
                successful_build_confirmations) noexcept
    : evidence_level_(evidence_level),
      source_form_disposition_(source_form_disposition),
      suffix_evidence_(std::move(suffix_evidence)),
      trusted_metadata_(std::move(trusted_metadata)),
      successful_build_confirmations_(
              std::move(successful_build_confirmations)) {}

DevelPackageClassification DevelPackageClassification::classify(
        DevelPackageSuffixEvidence suffix_evidence,
        std::vector<TrustedDevelSourceMetadata> trusted_metadata,
        std::vector<SuccessfulBuildSourceConfirmation>
                successful_build_confirmations) {
    DevelEvidenceLevel evidence_level;
    DevelSourceFormDisposition source_form_disposition;
    if(!successful_build_confirmations.empty()) {
        evidence_level = DevelEvidenceLevel::BuildSourceConfirmed;
        source_form_disposition = classify_confirmed_sources(
                trusted_metadata, successful_build_confirmations);
    } else if(!trusted_metadata.empty()) {
        evidence_level = DevelEvidenceLevel::SourceMetadataConfirmed;
        source_form_disposition = classify_confirmed_sources(
                trusted_metadata, successful_build_confirmations);
    } else if(suffix_evidence.has_candidate()) {
        evidence_level = DevelEvidenceLevel::SuffixCandidate;
        source_form_disposition =
                DevelSourceFormDisposition::RequiresCheck;
    } else {
        evidence_level = DevelEvidenceLevel::Normal;
        source_form_disposition =
                DevelSourceFormDisposition::NotApplicable;
    }

    return DevelPackageClassification(
            evidence_level,
            source_form_disposition,
            std::move(suffix_evidence),
            std::move(trusted_metadata),
            std::move(successful_build_confirmations));
}

DevelEvidenceLevel DevelPackageClassification::evidence_level()
        const noexcept {
    return evidence_level_;
}

DevelSourceFormDisposition
DevelPackageClassification::source_form_disposition() const noexcept {
    return source_form_disposition_;
}

const DevelPackageSuffixEvidence&
DevelPackageClassification::suffix_evidence() const noexcept {
    return suffix_evidence_;
}

const std::vector<TrustedDevelSourceMetadata>&
DevelPackageClassification::trusted_metadata() const noexcept {
    return trusted_metadata_;
}

const std::vector<SuccessfulBuildSourceConfirmation>&
DevelPackageClassification::successful_build_confirmations() const noexcept {
    return successful_build_confirmations_;
}
