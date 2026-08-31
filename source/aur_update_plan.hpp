#pragma once

#include "devel_package_classification.hpp"
#include "devel_update_model.hpp"
#include "installed_package.hpp"

#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

// Version文字列の比較は外部境界で済ませ、pure modelには比較結果だけを渡す。
enum class AurVersionRelation {
    OlderThanInstalled,
    SameAsInstalled,
    NewerThanInstalled,
    Unavailable
};

// AUR RPC metadataを、更新判定に必要な値だけへprojectしたもの。
struct AurUpdateRemotePackage {
    std::string aur_name;
    std::string package_base;
    std::string version;
    AurVersionRelation version_relation = AurVersionRelation::Unavailable;

    bool operator==(const AurUpdateRemotePackage&) const = default;
};

// AUR上に対象packageが存在しないことを確認できた状態。
struct AurUpdateMetadataNotFound {};

// query失敗により、AUR上の存在有無を確認できなかった状態。
struct AurUpdateMetadataUnavailable {};

using AurUpdateMetadataResult = std::variant<
    AurUpdateRemotePackage,
    AurUpdateMetadataNotFound,
    AurUpdateMetadataUnavailable>;

enum class AurUpdateClassification {
    UpdateAvailable,
    UpToDate,
    NonAurForeign,
    MetadataUnavailable,
    VersionComparisonUnavailable
};

// The effective state projects the two orthogonal authorities without
// changing AurUpdateClassification or AurVersionRelation. Inconsistent keeps
// assessment states outside this conservative connection from becoming a
// silent success before their producer contract is connected.
enum class AurUpdateEffectiveState {
    UpdateAvailable,
    UpToDate,
    RequiresCheck,
    NonAurForeign,
    MetadataUnavailable,
    VersionComparisonUnavailable,
    Inconsistent,
};

struct AurUpdatePlanInput {
    std::string installed_name;
    std::string installed_version;
    InstalledPackageReason install_reason = InstalledPackageReason::Unknown;
    AurUpdateMetadataResult aur_metadata = AurUpdateMetadataUnavailable{};
};

struct AurUpdatePlanEntry {
    std::string installed_name;
    std::string installed_version;
    InstalledPackageReason install_reason = InstalledPackageReason::Unknown;
    std::optional<AurUpdateRemotePackage> aur_package;
    AurUpdateClassification classification =
        AurUpdateClassification::MetadataUnavailable;
    std::optional<DevelPackageClassification> devel_classification;
    DevelUpdateAssessment devel_assessment =
        DevelUpdateAssessment::not_applicable();

    AurUpdatePlanEntry() = default;

    AurUpdatePlanEntry(
        std::string installed_name_value,
        std::string installed_version_value,
        InstalledPackageReason install_reason_value,
        std::optional<AurUpdateRemotePackage> aur_package_value,
        AurUpdateClassification classification_value,
        std::optional<DevelPackageClassification>
            devel_classification_value = std::nullopt,
        DevelUpdateAssessment devel_assessment_value =
            DevelUpdateAssessment::not_applicable())
        : installed_name(std::move(installed_name_value)),
          installed_version(std::move(installed_version_value)),
          install_reason(install_reason_value),
          aur_package(std::move(aur_package_value)),
          classification(classification_value),
          devel_classification(std::move(devel_classification_value)),
          devel_assessment(std::move(devel_assessment_value)) {
    }

    bool operator==(const AurUpdatePlanEntry&) const = default;
};

// installed inventoryの順序を保った、表示や将来のupdate workflow向けのpure plan。
struct AurUpdatePlan {
    std::vector<AurUpdatePlanEntry> entries;

    bool operator==(const AurUpdatePlan&) const = default;
};

AurUpdatePlanEntry classify_aur_update(const AurUpdatePlanInput& input);
AurUpdatePlan make_aur_update_plan(const std::vector<AurUpdatePlanInput>& inputs);

// Conservative production producer: only suffix-candidate evidence is
// connected. Trusted metadata and build provenance remain disconnected.
DevelUpdateAssessment project_conservative_devel_update_assessment(
    const DevelPackageClassification& classification);

// Normal AUR UpdateAvailable always wins. Other normal failure/absence states
// retain their existing authority; only normal UpToDate can expose the
// conservative devel assessment.
AurUpdateEffectiveState project_aur_update_effective_state(
    const AurUpdatePlanEntry& entry) noexcept;
