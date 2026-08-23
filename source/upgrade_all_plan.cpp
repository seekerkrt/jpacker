#include "upgrade_all_plan.hpp"

#include "localization.hpp"

#include <algorithm>
#include <map>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace {

constexpr std::string_view PACKAGE_BASE_FIELD = "PackageBase";
constexpr std::string_view AUR_SERVICE = "AUR";
constexpr std::string_view COMMAND_NAME = "upgrade-all";
constexpr std::string_view COMMAND_SENTENCE_NAME = "Upgrade-all";

enum class IdentityState {
    Resolved,
    Absent,
    ResolutionFailed,
    EmptyResolvedValue
};

struct PackageBaseView {
    IdentityState              state = IdentityState::Absent;
    std::optional<std::string> package_base;
};

struct SourceIdentityView {
    IdentityState              state = IdentityState::Absent;
    std::optional<std::string> key;
};

struct NormalizedExplicitIdentity {
    std::optional<std::string>    key;
    std::vector<std::size_t>      explicit_source_indexes;
    std::vector<std::string>      package_names;
    std::vector<std::string>      claimed_package_bases;
    PackageBaseView               package_base_signature;
    bool                          definition_conflict = false;
    bool                          identity_incomplete = false;
};

struct ExplicitIdentityIndex {
    std::vector<NormalizedExplicitIdentity>          identities;
    std::map<std::string, std::vector<std::size_t>>  identities_by_package_name;
    std::map<std::string, std::vector<std::size_t>>  identities_by_package_base;
};

enum class ExplicitMatchStatus {
    None,
    Unique,
    Conflicting
};

struct ExplicitMatch {
    ExplicitMatchStatus       status = ExplicitMatchStatus::None;
    std::vector<std::size_t>  identity_indexes;
};

struct ExplicitPackageNameMatches {
    ExplicitMatchStatus       status = ExplicitMatchStatus::None;
    std::vector<std::size_t>  identity_indexes;
    std::vector<std::string>  package_names;
};

template <typename Value>
void append_unique(std::vector<Value>& values, const Value& value) {
    if(std::find(values.begin(), values.end(), value) == values.end()) {
        values.push_back(value);
    }
}

void add_issue(
        UpgradeAllPlan& plan, UpgradeAllPlanningIssueKind kind,
        std::vector<std::size_t> explicit_source_indexes = {},
        std::vector<std::size_t> original_target_indexes = {},
        std::vector<std::size_t> build_unit_indexes = {},
        std::optional<std::string> package_name = std::nullopt,
        std::optional<std::string> package_base = std::nullopt) {
    plan.issues.push_back(UpgradeAllPlanningIssue{
            kind,
            std::move(explicit_source_indexes),
            std::move(original_target_indexes),
            std::move(build_unit_indexes),
            std::move(package_name),
            std::move(package_base)});
}

PackageBaseView inspect_package_base(const UpgradeAllPackageBaseIdentity& identity) {
    if(const auto* resolved = std::get_if<UpgradeAllResolvedPackageBase>(&identity)) {
        if(resolved->package_base.empty()) {
            return PackageBaseView{IdentityState::EmptyResolvedValue, std::nullopt};
        }
        return PackageBaseView{IdentityState::Resolved, resolved->package_base};
    }
    if(std::holds_alternative<UpgradeAllPackageBaseAbsent>(identity)) {
        return PackageBaseView{IdentityState::Absent, std::nullopt};
    }
    if(std::holds_alternative<UpgradeAllPackageBaseResolutionFailed>(identity)) {
        return PackageBaseView{IdentityState::ResolutionFailed, std::nullopt};
    }

    // A valueless variant can only be introduced by violating the value-type contract.
    throw std::logic_error(localization::format_translated_message(
            // TRANSLATORS: The placeholders are the literal command name
            // "upgrade-all" and field name "PackageBase".
            "Unknown {} {} identity state.", COMMAND_NAME,
            PACKAGE_BASE_FIELD));
}

SourceIdentityView inspect_source_identity(const UpgradeAllSourceIdentity& identity) {
    if(const auto* resolved = std::get_if<UpgradeAllResolvedSourceIdentity>(&identity)) {
        if(resolved->key.empty()) {
            return SourceIdentityView{IdentityState::EmptyResolvedValue, std::nullopt};
        }
        return SourceIdentityView{IdentityState::Resolved, resolved->key};
    }
    if(std::holds_alternative<UpgradeAllSourceIdentityAbsent>(identity)) {
        return SourceIdentityView{IdentityState::Absent, std::nullopt};
    }
    if(std::holds_alternative<UpgradeAllSourceIdentityResolutionFailed>(identity)) {
        return SourceIdentityView{IdentityState::ResolutionFailed, std::nullopt};
    }

    throw std::logic_error(localization::format_translated_message(
            // TRANSLATORS: The placeholder is the literal command name
            // "upgrade-all".
            "Unknown {} source identity state.", COMMAND_NAME));
}

bool package_base_signatures_match(
        const PackageBaseView& lhs, const PackageBaseView& rhs) {
    if(lhs.state != rhs.state) return false;
    if(lhs.state != IdentityState::Resolved) return true;
    return lhs.package_base == rhs.package_base;
}

void add_explicit_package_base_issue(
        UpgradeAllPlan& plan, std::size_t explicit_source_index,
        const PackageBaseView& package_base) {
    switch(package_base.state) {
    case IdentityState::Resolved:
        return;
    case IdentityState::Absent:
        add_issue(
                plan, UpgradeAllPlanningIssueKind::ExplicitPackageBaseAbsent,
                {explicit_source_index});
        return;
    case IdentityState::ResolutionFailed:
        add_issue(
                plan,
                UpgradeAllPlanningIssueKind::ExplicitPackageBaseResolutionFailed,
                {explicit_source_index});
        return;
    case IdentityState::EmptyResolvedValue:
        add_issue(
                plan, UpgradeAllPlanningIssueKind::ExplicitPackageBaseEmpty,
                {explicit_source_index});
        return;
    default:
        throw std::logic_error(localization::format_translated_message(
                // TRANSLATORS: The placeholder is the literal field name
                // "PackageBase".
                "Unknown explicit {} state.", PACKAGE_BASE_FIELD));
    }
}

void add_explicit_source_identity_issue(
        UpgradeAllPlan& plan, std::size_t explicit_source_index,
        const SourceIdentityView& source_identity) {
    switch(source_identity.state) {
    case IdentityState::Resolved:
        return;
    case IdentityState::Absent:
        add_issue(
                plan, UpgradeAllPlanningIssueKind::ExplicitSourceIdentityAbsent,
                {explicit_source_index});
        return;
    case IdentityState::ResolutionFailed:
        add_issue(
                plan,
                UpgradeAllPlanningIssueKind::ExplicitSourceIdentityResolutionFailed,
                {explicit_source_index});
        return;
    case IdentityState::EmptyResolvedValue:
        add_issue(
                plan, UpgradeAllPlanningIssueKind::ExplicitSourceIdentityEmpty,
                {explicit_source_index});
        return;
    default:
        throw std::logic_error(localization::translate_message(
                "Unknown explicit source identity state."));
    }
}

std::vector<std::size_t> collect_explicit_source_indexes(
        const ExplicitIdentityIndex& index,
        const std::vector<std::size_t>& identity_indexes) {
    std::vector<std::size_t> source_indexes;
    for(const std::size_t identity_index : identity_indexes) {
        for(const std::size_t source_index :
            index.identities.at(identity_index).explicit_source_indexes) {
            append_unique(source_indexes, source_index);
        }
    }
    std::sort(source_indexes.begin(), source_indexes.end());
    return source_indexes;
}

UpgradeAllExplicitSourceAttribution make_attribution(
        const ExplicitIdentityIndex& index,
        const std::vector<std::size_t>& identity_indexes,
        std::optional<std::string> matched_package_name,
        std::optional<std::string> matched_package_base) {
    UpgradeAllExplicitSourceAttribution attribution;
    attribution.explicit_source_indexes =
            collect_explicit_source_indexes(index, identity_indexes);
    attribution.matched_package_name = std::move(matched_package_name);
    attribution.matched_package_base = std::move(matched_package_base);

    for(const std::size_t identity_index : identity_indexes) {
        const std::optional<std::string>& key =
                index.identities.at(identity_index).key;
        if(key.has_value()) {
            append_unique(attribution.source_identity_keys, *key);
        }
    }
    return attribution;
}

bool is_conflicting_match(
        const ExplicitIdentityIndex& index,
        const std::vector<std::size_t>& identity_indexes) {
    if(identity_indexes.size() != 1) return true;
    const NormalizedExplicitIdentity& identity =
            index.identities.at(identity_indexes.front());
    return identity.definition_conflict || identity.identity_incomplete;
}

ExplicitMatch find_explicit_match(
        const ExplicitIdentityIndex& index,
        const std::map<std::string, std::vector<std::size_t>>& matches,
        const std::string& value) {
    const auto found = matches.find(value);
    if(found == matches.end()) return ExplicitMatch{};

    return ExplicitMatch{
            is_conflicting_match(index, found->second)
                    ? ExplicitMatchStatus::Conflicting
                    : ExplicitMatchStatus::Unique,
            found->second};
}

ExplicitPackageNameMatches find_explicit_package_name_matches(
        const ExplicitIdentityIndex& index,
        const std::vector<std::string>& package_names) {
    ExplicitPackageNameMatches result;
    for(const std::string& package_name : package_names) {
        if(package_name.empty()) continue;
        const auto found = index.identities_by_package_name.find(package_name);
        if(found == index.identities_by_package_name.end()) continue;

        append_unique(result.package_names, package_name);
        for(const std::size_t identity_index : found->second) {
            append_unique(result.identity_indexes, identity_index);
        }
    }

    if(result.identity_indexes.empty()) return result;
    std::sort(result.identity_indexes.begin(), result.identity_indexes.end());
    result.status = is_conflicting_match(index, result.identity_indexes)
            ? ExplicitMatchStatus::Conflicting
            : ExplicitMatchStatus::Unique;
    return result;
}

std::vector<std::size_t> combine_identity_indexes(
        const std::vector<std::size_t>& lhs,
        const std::vector<std::size_t>& rhs) {
    std::vector<std::size_t> combined = lhs;
    for(const std::size_t identity_index : rhs) {
        append_unique(combined, identity_index);
    }
    std::sort(combined.begin(), combined.end());
    return combined;
}

void add_build_unit_package_name_conflict_issues(
        UpgradeAllPlan& plan, const ExplicitIdentityIndex& index,
        UpgradeAllPlanningIssueKind kind,
        const ExplicitPackageNameMatches& package_name_matches,
        const std::vector<std::size_t>& conflict_identity_indexes,
        const std::vector<std::size_t>& affected_target_indexes,
        std::size_t build_unit_index,
        const std::optional<std::string>& package_base) {
    const std::vector<std::size_t> explicit_source_indexes =
            collect_explicit_source_indexes(index, conflict_identity_indexes);
    for(const std::string& package_name : package_name_matches.package_names) {
        add_issue(
                plan, kind, explicit_source_indexes, affected_target_indexes,
                {build_unit_index}, package_name, package_base);
    }
}

ExplicitIdentityIndex build_explicit_identity_index(
        const std::vector<UpgradeAllExplicitSourceIdentity>& explicit_sources,
        UpgradeAllPlan* plan) {
    ExplicitIdentityIndex index;
    std::map<std::string, std::size_t> identity_index_by_key;

    for(std::size_t source_index = 0; source_index < explicit_sources.size();
        ++source_index) {
        const UpgradeAllExplicitSourceIdentity& source = explicit_sources[source_index];
        const PackageBaseView package_base = inspect_package_base(source.package_base);
        const SourceIdentityView source_identity =
                inspect_source_identity(source.source_identity);

        if(plan != nullptr) {
            if(source.preference_package_name.empty()) {
                add_issue(
                        *plan,
                        UpgradeAllPlanningIssueKind::
                                ExplicitPreferencePackageNameMissing,
                        {source_index});
            }
            for(const std::string& produced_name : source.produced_package_names) {
                if(!produced_name.empty()) continue;
                add_issue(
                        *plan,
                        UpgradeAllPlanningIssueKind::ExplicitProducedPackageNameMissing,
                        {source_index});
            }
            add_explicit_package_base_issue(*plan, source_index, package_base);
            add_explicit_source_identity_issue(*plan, source_index, source_identity);
        }

        std::size_t normalized_index = index.identities.size();
        if(source_identity.state == IdentityState::Resolved) {
            const std::string& identity_key = *source_identity.key;
            auto [found, inserted] = identity_index_by_key.emplace(
                    identity_key, index.identities.size());
            normalized_index = found->second;
            if(inserted) {
                NormalizedExplicitIdentity identity;
                identity.key = identity_key;
                identity.package_base_signature = package_base;
                index.identities.push_back(std::move(identity));
            }
        } else {
            // Known names/bases still participate as an incomplete owner. This
            // prevents a missing normalization key from turning a known overlap
            // into an executable AUR target.
            NormalizedExplicitIdentity identity;
            identity.package_base_signature = package_base;
            identity.identity_incomplete = true;
            index.identities.push_back(std::move(identity));
        }

        NormalizedExplicitIdentity& normalized = index.identities[normalized_index];
        normalized.explicit_source_indexes.push_back(source_index);
        if(!source.preference_package_name.empty()) {
            append_unique(normalized.package_names, source.preference_package_name);
        }
        for(const std::string& produced_name : source.produced_package_names) {
            if(!produced_name.empty()) append_unique(normalized.package_names, produced_name);
        }
        if(package_base.package_base.has_value()) {
            append_unique(
                    normalized.claimed_package_bases, *package_base.package_base);
        }

        if(!package_base_signatures_match(
                   normalized.package_base_signature, package_base)) {
            normalized.definition_conflict = true;
        }
    }

    for(std::size_t identity_index = 0; identity_index < index.identities.size();
        ++identity_index) {
        const NormalizedExplicitIdentity& identity = index.identities[identity_index];
        if(identity.definition_conflict && plan != nullptr) {
            add_issue(
                    *plan,
                    UpgradeAllPlanningIssueKind::
                            ConflictingExplicitSourceIdentityDefinition,
                    identity.explicit_source_indexes);
        }

        for(const std::string& package_name : identity.package_names) {
            index.identities_by_package_name[package_name].push_back(identity_index);
        }
        for(const std::string& package_base : identity.claimed_package_bases) {
            index.identities_by_package_base[package_base].push_back(identity_index);
        }
    }

    if(plan != nullptr) {
        for(const auto& [package_name, identity_indexes] :
            index.identities_by_package_name) {
            if(!is_conflicting_match(index, identity_indexes)) continue;
            add_issue(
                    *plan,
                    UpgradeAllPlanningIssueKind::ConflictingExplicitPackageName,
                    collect_explicit_source_indexes(index, identity_indexes), {}, {},
                    package_name);
        }
        for(const auto& [package_base, identity_indexes] :
            index.identities_by_package_base) {
            if(!is_conflicting_match(index, identity_indexes)) continue;
            add_issue(
                    *plan,
                    UpgradeAllPlanningIssueKind::ConflictingExplicitPackageBase,
                    collect_explicit_source_indexes(index, identity_indexes), {}, {},
                    std::nullopt, package_base);
        }
    }

    return index;
}

void validate_target_status(UpgradeAllAurTargetStatus status) {
    switch(status) {
    case UpgradeAllAurTargetStatus::Candidate:
    case UpgradeAllAurTargetStatus::Unsupported:
    case UpgradeAllAurTargetStatus::Incomplete:
        return;
    default:
        throw std::logic_error(localization::format_translated_message(
                // TRANSLATORS: The placeholders are the literal command name
                // "upgrade-all" and service name "AUR".
                "Unknown {} {} target status.", COMMAND_NAME, AUR_SERVICE));
    }
}

void validate_build_unit_role(UpgradeAllBuildUnitRole role) {
    switch(role) {
    case UpgradeAllBuildUnitRole::Root:
    case UpgradeAllBuildUnitRole::RuntimeDependency:
    case UpgradeAllBuildUnitRole::BuildDependency:
    case UpgradeAllBuildUnitRole::CheckDependency:
        return;
    default:
        throw std::logic_error(localization::format_translated_message(
                // TRANSLATORS: The placeholder is the literal command name
                // "upgrade-all".
                "Unknown {} build-unit role.", COMMAND_NAME));
    }
}

std::optional<std::string> inspect_target_package_base(
        UpgradeAllPlan& plan, std::size_t original_target_index,
        const UpgradeAllPackageBaseIdentity& identity) {
    const PackageBaseView package_base = inspect_package_base(identity);
    switch(package_base.state) {
    case IdentityState::Resolved:
        return package_base.package_base;
    case IdentityState::Absent:
        add_issue(
                plan, UpgradeAllPlanningIssueKind::AurTargetPackageBaseAbsent, {},
                {original_target_index});
        return std::nullopt;
    case IdentityState::ResolutionFailed:
        add_issue(
                plan,
                UpgradeAllPlanningIssueKind::AurTargetPackageBaseResolutionFailed,
                {}, {original_target_index});
        return std::nullopt;
    case IdentityState::EmptyResolvedValue:
        add_issue(
                plan, UpgradeAllPlanningIssueKind::AurTargetPackageBaseEmpty, {},
                {original_target_index});
        return std::nullopt;
    default:
        throw std::logic_error(localization::format_translated_message(
                // TRANSLATORS: The placeholders are the literal service name
                // "AUR" and field name "PackageBase".
                "Unknown {} target {} state.", AUR_SERVICE,
                PACKAGE_BASE_FIELD));
    }
}

std::optional<std::string> inspect_build_unit_package_base(
        UpgradeAllPlan& plan, std::size_t build_unit_index,
        const UpgradeAllPackageBaseIdentity& identity) {
    const PackageBaseView package_base = inspect_package_base(identity);
    switch(package_base.state) {
    case IdentityState::Resolved:
        return package_base.package_base;
    case IdentityState::Absent:
        add_issue(
                plan, UpgradeAllPlanningIssueKind::BuildUnitPackageBaseAbsent, {},
                {}, {build_unit_index});
        return std::nullopt;
    case IdentityState::ResolutionFailed:
        add_issue(
                plan,
                UpgradeAllPlanningIssueKind::BuildUnitPackageBaseResolutionFailed,
                {}, {}, {build_unit_index});
        return std::nullopt;
    case IdentityState::EmptyResolvedValue:
        add_issue(
                plan, UpgradeAllPlanningIssueKind::BuildUnitPackageBaseEmpty, {},
                {}, {build_unit_index});
        return std::nullopt;
    default:
        throw std::logic_error(localization::format_translated_message(
                // TRANSLATORS: The placeholder is the literal field name
                // "PackageBase".
                "Unknown build-unit {} state.", PACKAGE_BASE_FIELD));
    }
}

bool target_entry_is_selected(const UpgradeAllTargetPlanEntry& entry) {
    return entry.disposition == UpgradeAllTargetDisposition::Selected;
}

void populate_selected_targets(UpgradeAllPlan& plan) {
    plan.selected_targets.clear();
    plan.original_to_selected_index.assign(
            plan.target_dispositions.size(), std::nullopt);

    for(UpgradeAllTargetPlanEntry& entry : plan.target_dispositions) {
        entry.selected_index.reset();
        if(!target_entry_is_selected(entry)) continue;

        const PackageBaseView package_base = inspect_package_base(entry.target.package_base);
        if(!package_base.package_base.has_value()) {
            throw std::logic_error(localization::format_translated_message(
                    // TRANSLATORS: The placeholders are the literal command
                    // name "upgrade-all" and field name "PackageBase".
                    "The selected {} target lost its resolved {}.",
                    COMMAND_NAME, PACKAGE_BASE_FIELD));
        }

        const std::size_t selected_index = plan.selected_targets.size();
        entry.selected_index = selected_index;
        plan.original_to_selected_index[entry.original_target_index] = selected_index;
        plan.selected_targets.push_back(UpgradeAllSelectedAurTarget{
                selected_index,
                entry.original_target_index,
                entry.target.package_name,
                *package_base.package_base});
    }
}

void validate_target_plan_for_completion(const UpgradeAllPlan& plan) {
    if(!plan.build_unit_dispositions.empty() || !plan.selected_build_units.empty() ||
       !plan.externally_satisfied_build_unit_indexes.empty() ||
       !plan.externally_satisfied_package_bases.empty()) {
        throw std::logic_error(localization::format_translated_message(
                // TRANSLATORS: The placeholder is the display spelling of the
                // literal command name "upgrade-all" at sentence start.
                "{} target plan already contains build-unit planning results.",
                COMMAND_SENTENCE_NAME));
    }
    if(plan.target_dispositions.size() != plan.original_to_selected_index.size()) {
        throw std::logic_error(localization::format_translated_message(
                // TRANSLATORS: The placeholder is the display spelling of the
                // literal command name "upgrade-all" at sentence start.
                "{} target index mapping size is inconsistent.",
                COMMAND_SENTENCE_NAME));
    }

    std::size_t expected_selected_index = 0;
    for(std::size_t position = 0; position < plan.target_dispositions.size(); ++position) {
        const UpgradeAllTargetPlanEntry& entry = plan.target_dispositions[position];
        if(entry.original_target_index != position) {
            throw std::logic_error(localization::format_translated_message(
                    // TRANSLATORS: The placeholder is the display spelling of
                    // the literal command name "upgrade-all" at sentence start.
                    "{} original target order is inconsistent.",
                    COMMAND_SENTENCE_NAME));
        }

        if(target_entry_is_selected(entry)) {
            if(entry.selected_index != expected_selected_index ||
               plan.original_to_selected_index[position] != expected_selected_index) {
                throw std::logic_error(localization::format_translated_message(
                        // TRANSLATORS: The placeholder is the display spelling
                        // of the literal command name "upgrade-all".
                        "{} selected target mapping is inconsistent.",
                        COMMAND_SENTENCE_NAME));
            }
            ++expected_selected_index;
        } else if(entry.selected_index.has_value() ||
                  plan.original_to_selected_index[position].has_value()) {
            throw std::logic_error(localization::format_translated_message(
                    // TRANSLATORS: The placeholder is the literal command name
                    // "upgrade-all".
                    "Non-selected {} target has a selected index.",
                    COMMAND_NAME));
        }
    }
    if(expected_selected_index != plan.selected_targets.size()) {
        throw std::logic_error(localization::format_translated_message(
                // TRANSLATORS: The placeholder is the display spelling of the
                // literal command name "upgrade-all" at sentence start.
                "{} selected target list is inconsistent.",
                COMMAND_SENTENCE_NAME));
    }
}

void reject_duplicate_selected_build_unit_package_bases(UpgradeAllPlan& plan) {
    std::map<std::string, std::vector<std::size_t>> units_by_package_base;
    for(std::size_t entry_index = 0;
        entry_index < plan.build_unit_dispositions.size(); ++entry_index) {
        const UpgradeAllBuildUnitPlanEntry& entry =
                plan.build_unit_dispositions[entry_index];
        if(entry.disposition !=
           UpgradeAllBuildUnitDisposition::SelectedForAurExecution) {
            continue;
        }
        const PackageBaseView package_base = inspect_package_base(
                entry.build_unit.package_base);
        if(!package_base.package_base.has_value()) {
            throw std::logic_error(localization::format_translated_message(
                    // TRANSLATORS: The placeholders are the literal command
                    // name "upgrade-all" and field name "PackageBase".
                    "The selected {} build unit has no resolved {}.",
                    COMMAND_NAME, PACKAGE_BASE_FIELD));
        }
        units_by_package_base[*package_base.package_base].push_back(entry_index);
    }

    for(const auto& [package_base, entry_indexes] : units_by_package_base) {
        if(entry_indexes.size() < 2) continue;

        std::vector<std::size_t> build_unit_indexes;
        for(const std::size_t entry_index : entry_indexes) {
            UpgradeAllBuildUnitPlanEntry& entry =
                    plan.build_unit_dispositions[entry_index];
            entry.disposition =
                    UpgradeAllBuildUnitDisposition::
                            ConflictingSelectedPackageBase;
            build_unit_indexes.push_back(entry.original_build_plan_index);
        }
        add_issue(
                plan,
                UpgradeAllPlanningIssueKind::
                        DuplicateSelectedBuildUnitPackageBase,
                {}, {}, std::move(build_unit_indexes), std::nullopt,
                package_base);
    }
}

void populate_selected_build_units(UpgradeAllPlan& plan) {
    plan.selected_build_units.clear();
    for(UpgradeAllBuildUnitPlanEntry& entry : plan.build_unit_dispositions) {
        entry.selected_execution_index.reset();
        if(entry.disposition !=
           UpgradeAllBuildUnitDisposition::SelectedForAurExecution) {
            continue;
        }

        const PackageBaseView package_base = inspect_package_base(
                entry.build_unit.package_base);
        if(!package_base.package_base.has_value()) {
            throw std::logic_error(localization::format_translated_message(
                    // TRANSLATORS: The placeholders are the literal command
                    // name "upgrade-all" and field name "PackageBase".
                    "The selected {} build unit lost its resolved {}.",
                    COMMAND_NAME, PACKAGE_BASE_FIELD));
        }

        const std::size_t selected_index = plan.selected_build_units.size();
        entry.selected_execution_index = selected_index;
        plan.selected_build_units.push_back(UpgradeAllSelectedBuildUnit{
                selected_index,
                entry.original_build_plan_index,
                *package_base.package_base});
    }
}

} // namespace

UpgradeAllPlan make_upgrade_all_target_plan(
        const std::vector<UpgradeAllExplicitSourceIdentity>& explicit_sources,
        const std::vector<UpgradeAllAurTarget>& aur_targets) {
    UpgradeAllPlan plan;
    plan.explicit_sources = explicit_sources;
    plan.target_dispositions.reserve(aur_targets.size());
    plan.excluded_duplicate_target_indexes.reserve(aur_targets.size());

    const ExplicitIdentityIndex explicit_index =
            build_explicit_identity_index(explicit_sources, &plan);

    // POLICY(#281): original query order is the stable target identity.
    for(std::size_t target_index = 0; target_index < aur_targets.size();
        ++target_index) {
        const UpgradeAllAurTarget& target = aur_targets[target_index];
        validate_target_status(target.status);

        UpgradeAllTargetPlanEntry entry;
        entry.original_target_index = target_index;
        entry.target = target;

        if(target.package_name.empty()) {
            add_issue(
                    plan, UpgradeAllPlanningIssueKind::AurTargetPackageNameMissing,
                    {}, {target_index});
        }
        const std::optional<std::string> package_base =
                inspect_target_package_base(plan, target_index, target.package_base);

        if(target.status == UpgradeAllAurTargetStatus::Unsupported) {
            add_issue(
                    plan, UpgradeAllPlanningIssueKind::UnsupportedAurTarget, {},
                    {target_index}, {},
                    target.package_name.empty()
                            ? std::nullopt
                            : std::optional<std::string>{target.package_name},
                    package_base);
        }
        if(target.status == UpgradeAllAurTargetStatus::Incomplete) {
            add_issue(
                    plan, UpgradeAllPlanningIssueKind::IncompleteAurTarget, {},
                    {target_index}, {},
                    target.package_name.empty()
                            ? std::nullopt
                            : std::optional<std::string>{target.package_name},
                    package_base);
        }

        // POLICY(#281): a reliable explicit match takes precedence over the
        // target's unsupported/incomplete status. The status and its issue stay
        // on the owned target so later phases can still fail closed.
        const ExplicitMatch package_name_match = target.package_name.empty()
                ? ExplicitMatch{}
                : find_explicit_match(
                          explicit_index,
                          explicit_index.identities_by_package_name,
                          target.package_name);
        const ExplicitMatch package_base_match = package_base.has_value()
                ? find_explicit_match(
                          explicit_index,
                          explicit_index.identities_by_package_base,
                          *package_base)
                : ExplicitMatch{};

        if(package_name_match.status == ExplicitMatchStatus::Conflicting) {
            entry.disposition =
                    UpgradeAllTargetDisposition::ConflictingExplicitSourceIdentity;
            entry.explicit_source = make_attribution(
                    explicit_index, package_name_match.identity_indexes,
                    target.package_name, package_base);
            plan.target_dispositions.push_back(std::move(entry));
            continue;
        }

        if(package_name_match.status == ExplicitMatchStatus::Unique) {
            entry.disposition =
                    UpgradeAllTargetDisposition::ExcludedByExplicitPackageName;
            entry.explicit_source = make_attribution(
                    explicit_index, package_name_match.identity_indexes,
                    target.package_name, package_base);
            plan.excluded_duplicate_target_indexes.push_back(target_index);

            // A name match wins, but a different PackageBase owner is still an
            // input inconsistency that the next phase must fail closed on.
            if(package_base_match.status != ExplicitMatchStatus::None &&
               package_base_match.identity_indexes !=
                       package_name_match.identity_indexes) {
                std::vector<std::size_t> combined_identities =
                        package_name_match.identity_indexes;
                for(const std::size_t identity_index :
                    package_base_match.identity_indexes) {
                    append_unique(combined_identities, identity_index);
                }
                add_issue(
                        plan,
                        UpgradeAllPlanningIssueKind::ConflictingExplicitPackageBase,
                        collect_explicit_source_indexes(
                                explicit_index, combined_identities),
                        {target_index}, {}, target.package_name, package_base);
            }

            plan.target_dispositions.push_back(std::move(entry));
            continue;
        }

        if(package_base_match.status == ExplicitMatchStatus::Conflicting) {
            entry.disposition =
                    UpgradeAllTargetDisposition::ConflictingExplicitSourceIdentity;
            entry.explicit_source = make_attribution(
                    explicit_index, package_base_match.identity_indexes,
                    std::nullopt, package_base);
            plan.target_dispositions.push_back(std::move(entry));
            continue;
        }

        if(package_base_match.status == ExplicitMatchStatus::Unique) {
            entry.disposition =
                    UpgradeAllTargetDisposition::ExcludedByExplicitPackageBase;
            entry.explicit_source = make_attribution(
                    explicit_index, package_base_match.identity_indexes,
                    std::nullopt, package_base);
            plan.excluded_duplicate_target_indexes.push_back(target_index);
            plan.target_dispositions.push_back(std::move(entry));
            continue;
        }

        if(target.status == UpgradeAllAurTargetStatus::Unsupported) {
            entry.disposition = UpgradeAllTargetDisposition::Unsupported;
            plan.target_dispositions.push_back(std::move(entry));
            continue;
        }

        if(target.status == UpgradeAllAurTargetStatus::Incomplete ||
           target.package_name.empty() || !package_base.has_value()) {
            entry.disposition = UpgradeAllTargetDisposition::IdentityIncomplete;
            plan.target_dispositions.push_back(std::move(entry));
            continue;
        }

        entry.disposition = UpgradeAllTargetDisposition::Selected;
        plan.target_dispositions.push_back(std::move(entry));
    }

    populate_selected_targets(plan);
    return plan;
}

UpgradeAllPlan complete_upgrade_all_build_unit_plan(
        const UpgradeAllPlan& target_plan,
        const std::vector<UpgradeAllAurBuildUnit>& build_units) {
    validate_target_plan_for_completion(target_plan);

    UpgradeAllPlan plan = target_plan;
    plan.build_unit_dispositions.reserve(build_units.size());
    plan.externally_satisfied_build_unit_indexes.reserve(build_units.size());
    plan.externally_satisfied_package_bases.reserve(build_units.size());

    const ExplicitIdentityIndex explicit_index =
            build_explicit_identity_index(plan.explicit_sources, nullptr);

    // POLICY(#281): BuildPlan order is preserved even when execution candidates
    // are filtered; selected_execution_index is the compacted order only.
    for(std::size_t build_unit_index = 0; build_unit_index < build_units.size();
        ++build_unit_index) {
        const UpgradeAllAurBuildUnit& build_unit = build_units[build_unit_index];
        UpgradeAllBuildUnitPlanEntry entry;
        entry.original_build_plan_index = build_unit_index;
        entry.build_unit = build_unit;

        bool has_correlation_issue = false;
        bool has_selected_root = false;
        std::vector<std::size_t> affected_target_indexes;
        if(build_unit.root_attributions.empty()) {
            add_issue(
                    plan,
                    UpgradeAllPlanningIssueKind::BuildUnitHasNoRootAttribution,
                    {}, {}, {build_unit_index});
            has_correlation_issue = true;
        }
        for(const UpgradeAllBuildUnitRootAttribution& attribution :
            build_unit.root_attributions) {
            validate_build_unit_role(attribution.role);
            if(attribution.original_target_index >=
               plan.target_dispositions.size()) {
                add_issue(
                        plan,
                        UpgradeAllPlanningIssueKind::
                                BuildUnitTargetIndexOutOfRange,
                        {}, {attribution.original_target_index},
                        {build_unit_index});
                has_correlation_issue = true;
                continue;
            }
            append_unique(
                    affected_target_indexes,
                    attribution.original_target_index);
            if(plan.original_to_selected_index[attribution.original_target_index]
                       .has_value()) {
                has_selected_root = true;
            }
        }

        const std::optional<std::string> package_base =
                inspect_build_unit_package_base(
                        plan, build_unit_index, build_unit.package_base);
        const ExplicitMatch package_base_match = package_base.has_value()
                ? find_explicit_match(
                          explicit_index,
                          explicit_index.identities_by_package_base,
                          *package_base)
                : ExplicitMatch{};
        const ExplicitPackageNameMatches package_name_matches =
                find_explicit_package_name_matches(
                        explicit_index, build_unit.package_names);

        if(package_base_match.status == ExplicitMatchStatus::Conflicting) {
            entry.disposition = UpgradeAllBuildUnitDisposition::
                    ConflictingExplicitSourceIdentity;
            entry.explicit_source = make_attribution(
                    explicit_index, package_base_match.identity_indexes,
                    std::nullopt, package_base);
            plan.build_unit_dispositions.push_back(std::move(entry));
            continue;
        }

        if(package_base_match.status == ExplicitMatchStatus::Unique) {
            const bool names_match_same_identity =
                    package_name_matches.status == ExplicitMatchStatus::None ||
                    (package_name_matches.status == ExplicitMatchStatus::Unique &&
                     package_name_matches.identity_indexes ==
                             package_base_match.identity_indexes);
            if(!names_match_same_identity) {
                const std::vector<std::size_t> conflict_identity_indexes =
                        combine_identity_indexes(
                                package_name_matches.identity_indexes,
                                package_base_match.identity_indexes);
                entry.disposition = UpgradeAllBuildUnitDisposition::
                        ConflictingExplicitSourceIdentity;
                entry.explicit_source = make_attribution(
                        explicit_index, conflict_identity_indexes,
                        package_name_matches.package_names.size() == 1
                                ? std::optional<std::string>{
                                          package_name_matches.package_names.front()}
                                : std::nullopt,
                        package_base);
                add_build_unit_package_name_conflict_issues(
                        plan, explicit_index,
                        package_name_matches.status ==
                                        ExplicitMatchStatus::Conflicting
                                ? UpgradeAllPlanningIssueKind::
                                          ConflictingExplicitPackageName
                                : UpgradeAllPlanningIssueKind::
                                          ConflictingExplicitPackageBase,
                        package_name_matches, conflict_identity_indexes,
                        affected_target_indexes, build_unit_index,
                        package_base);
                plan.build_unit_dispositions.push_back(std::move(entry));
                continue;
            }

            entry.disposition = UpgradeAllBuildUnitDisposition::
                    ExternallySatisfiedByExplicitSourcePackageBase;
            entry.explicit_source = make_attribution(
                    explicit_index, package_base_match.identity_indexes,
                    std::nullopt, package_base);
            plan.externally_satisfied_build_unit_indexes.push_back(
                    build_unit_index);
            append_unique(
                    plan.externally_satisfied_package_bases, *package_base);
            plan.build_unit_dispositions.push_back(std::move(entry));
            continue;
        }

        if(has_correlation_issue) {
            entry.disposition = UpgradeAllBuildUnitDisposition::IdentityIncomplete;
            plan.build_unit_dispositions.push_back(std::move(entry));
            continue;
        }

        // A name match alone cannot satisfy an atomic PackageBase build unit:
        // another output from that unit may still be required. Keep it out of
        // execution and retain the base mismatch as a typed planning issue.
        if(has_selected_root &&
           package_name_matches.status != ExplicitMatchStatus::None) {
            entry.disposition = UpgradeAllBuildUnitDisposition::
                    ConflictingExplicitSourceIdentity;
            entry.explicit_source = make_attribution(
                    explicit_index, package_name_matches.identity_indexes,
                    package_name_matches.package_names.size() == 1
                            ? std::optional<std::string>{
                                      package_name_matches.package_names.front()}
                            : std::nullopt,
                    package_base);
            add_build_unit_package_name_conflict_issues(
                    plan, explicit_index,
                    package_name_matches.status ==
                                    ExplicitMatchStatus::Conflicting
                            ? UpgradeAllPlanningIssueKind::
                                      ConflictingExplicitPackageName
                            : UpgradeAllPlanningIssueKind::
                                      ConflictingExplicitPackageBase,
                    package_name_matches,
                    package_name_matches.identity_indexes,
                    affected_target_indexes, build_unit_index,
                    package_base);
            plan.build_unit_dispositions.push_back(std::move(entry));
            continue;
        }

        if(!package_base.has_value()) {
            entry.disposition = UpgradeAllBuildUnitDisposition::IdentityIncomplete;
            plan.build_unit_dispositions.push_back(std::move(entry));
            continue;
        }

        if(!has_selected_root) {
            entry.disposition =
                    UpgradeAllBuildUnitDisposition::NotRequiredBySelectedTarget;
            plan.build_unit_dispositions.push_back(std::move(entry));
            continue;
        }

        entry.disposition =
                UpgradeAllBuildUnitDisposition::SelectedForAurExecution;
        plan.build_unit_dispositions.push_back(std::move(entry));
    }

    reject_duplicate_selected_build_unit_package_bases(plan);
    populate_selected_build_units(plan);
    return plan;
}

UpgradeAllPlan make_upgrade_all_plan(const UpgradeAllPlanInput& input) {
    return complete_upgrade_all_build_unit_plan(
            make_upgrade_all_target_plan(input.explicit_sources, input.aur_targets),
            input.build_units);
}

bool has_upgrade_all_planning_issues(const UpgradeAllPlan& plan) noexcept {
    return !plan.issues.empty();
}
