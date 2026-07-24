#pragma once

#include <optional>
#include <string>
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
    std::string        aur_name;
    std::string        package_base;
    std::string        version;
    AurVersionRelation version_relation = AurVersionRelation::Unavailable;
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

struct AurUpdatePlanInput {
    std::string             installed_name;
    std::string             installed_version;
    AurUpdateMetadataResult aur_metadata = AurUpdateMetadataUnavailable{};
};

struct AurUpdatePlanEntry {
    std::string                           installed_name;
    std::string                           installed_version;
    std::optional<AurUpdateRemotePackage> aur_package;
    AurUpdateClassification               classification =
            AurUpdateClassification::MetadataUnavailable;
};

// installed inventoryの順序を保った、表示や将来のupdate workflow向けのpure plan。
struct AurUpdatePlan {
    std::vector<AurUpdatePlanEntry> entries;
};

AurUpdatePlanEntry classify_aur_update(const AurUpdatePlanInput& input);
AurUpdatePlan make_aur_update_plan(const std::vector<AurUpdatePlanInput>& inputs);
