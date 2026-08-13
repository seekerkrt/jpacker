#include "cli_authority.hpp"
#include "cli_public_projection.hpp"

#include <array>
#include <cstddef>
#include <iostream>
#include <span>
#include <string>
#include <string_view>

namespace {

using cli_authority::GrammarOwnership;
using cli_authority::OperandContract;
using cli_authority::OperandKind;
using cli_authority::OperandOrderingRule;
using cli_authority::OperationId;
using cli_authority::OperationOptionRelationSet;
using cli_authority::OptionConflictSet;
using cli_authority::OptionId;
using cli_authority::OptionLexicalPlacement;
using cli_authority::OptionOccurrence;
using cli_authority::OptionPublicSyntax;
using cli_authority::SpecialOperationId;
using cli_authority::TargetPolicy;

template<typename Enum>
constexpr std::size_t enum_index(Enum value) noexcept {
    return static_cast<std::size_t>(value);
}

std::string_view occurrence_name(OptionOccurrence occurrence) noexcept {
    switch(occurrence) {
    case OptionOccurrence::Once:
        return "once";
    case OptionOccurrence::RepeatIdempotent:
        return "repeat-idempotent";
    case OptionOccurrence::RepeatSameValue:
        return "repeat-same-value";
    case OptionOccurrence::Delegated:
        return "delegated";
    }
    return "unknown";
}

std::string_view placement_name(
        OptionLexicalPlacement placement) noexcept {
    switch(placement) {
    case OptionLexicalPlacement::ParserGlobalNormalPosition:
        return "parser-global";
    case OptionLexicalPlacement::FirstNonGlobalToken:
        return "first-non-global";
    case OptionLexicalPlacement::OperationLocal:
        return "operation-local";
    case OptionLexicalPlacement::PacmanGrammar:
        return "pacman-grammar";
    case OptionLexicalPlacement::EndOfOptionsMarker:
        return "end-of-options";
    }
    return "unknown";
}

std::string_view target_policy_name(TargetPolicy policy) noexcept {
    switch(policy) {
    case TargetPolicy::None:
        return "none";
    case TargetPolicy::ExactlyOne:
        return "exactly-one";
    case TargetPolicy::OneOrMore:
        return "one-or-more";
    case TargetPolicy::OrderedItems:
        return "ordered-items";
    case TargetPolicy::Delegated:
        return "delegated";
    }
    return "unknown";
}

std::string_view operand_kind_name(OperandKind kind) noexcept {
    switch(kind) {
    case OperandKind::None:
        return "none";
    case OperandKind::Package:
        return "package";
    case OperandKind::Directory:
        return "directory";
    case OperandKind::Query:
        return "query";
    case OperandKind::SourcePreferenceItem:
        return "source-preference-item";
    case OperandKind::EnvironmentAssignment:
        return "environment-assignment";
    case OperandKind::DelegatedPacmanArgument:
        return "delegated-pacman-argument";
    }
    return "unknown";
}

std::string_view operand_ordering_name(
        OperandOrderingRule ordering) noexcept {
    switch(ordering) {
    case OperandOrderingRule::None:
        return "none";
    case OperandOrderingRule::PreserveInputOrder:
        return "preserve-input-order";
    case OperandOrderingRule::PrimaryThenEnvironmentAssignments:
        return "primary-then-environment-assignments";
    case OperandOrderingRule::PackageIntroducesFollowingAssignmentScope:
        return "package-introduces-following-assignment-scope";
    case OperandOrderingRule::Delegated:
        return "delegated";
    }
    return "unknown";
}

void print_operand_terms(const OperandContract& operands) {
    for(std::size_t index = 0; index < operands.term_count; ++index) {
        if(index != 0) std::cout << ',';
        const cli_authority::OperandTermSpec& term = operands.terms[index];
        std::cout << operand_kind_name(term.kind) << ':' << term.min_count
                  << ':';
        if(term.max_count == cli_authority::UNBOUNDED_OPERAND_COUNT) {
            std::cout << '*';
        } else {
            std::cout << term.max_count;
        }
    }
}

bool has_public_option_definition(
        OptionLexicalPlacement placement) noexcept {
    return placement != OptionLexicalPlacement::OperationLocal;
}

void print_ids(std::span<const OptionId> ids) {
    for(std::size_t index = 0; index < ids.size(); ++index) {
        if(index != 0) std::cout << ',';
        std::cout << enum_index(ids[index]);
    }
}

void print_conflicts(const OptionConflictSet& conflicts) {
    print_ids(std::span{conflicts.values}.first(conflicts.count));
}

void print_relations(const OperationOptionRelationSet& relations) {
    for(std::size_t index = 0; index < relations.count; ++index) {
        if(index != 0) std::cout << ',';
        std::cout << enum_index(relations.values[index].option);
    }
}

void print_public_selectors(
        const OperationOptionRelationSet& relations) {
    bool first = true;
    for(std::size_t index = 0; index < relations.count; ++index) {
        const cli_authority::OptionRelationContract& relation =
                relations.values[index];
        if(relation.public_syntax != OptionPublicSyntax::Required) continue;
        if(!first) std::cout << ',';
        first = false;
        std::cout << enum_index(relation.option);
    }
}

void print_option_token(OptionId id, std::string_view token) {
    const cli_authority::OptionContract& option =
            cli_authority::option_contract(id);
    std::cout << "OPTION\t" << enum_index(id) << '\t' << token << '\t';
    if(option.value.kind == cli_authority::OptionValueKind::AttachedEnum) {
        std::cout << token << '=';
    } else {
        std::cout << token;
    }
    std::cout << '\t' << occurrence_name(option.default_occurrence)
              << '\t' << placement_name(option.lexical_placement) << '\t';
    print_conflicts(option.conflicts);
    const std::string_view definition_role =
            has_public_option_definition(option.lexical_placement)
            ? "definition"
            : "syntax-only";
    std::cout << '\t' << definition_role << '\n';
}

void print_option(OptionId id) {
    const cli_authority::OptionContract& option =
            cli_authority::option_contract(id);
    for(std::size_t index = 0; index < option.aliases.count; ++index) {
        print_option_token(id, option.aliases.values[index]);
    }
    print_option_token(id, option.canonical_token);
}

void print_form(
        std::string_view token, std::string_view syntax,
        const OperandContract& operands, TargetPolicy target_policy,
        const OperationOptionRelationSet& relations) {
    std::cout << "FORM\t" << token << '\t' << syntax << '\t'
              << target_policy_name(target_policy) << '\t'
              << operand_ordering_name(operands.ordering) << '\t';
    print_operand_terms(operands);
    std::cout << '\t';
    print_relations(relations);
    std::cout << '\t';
    print_public_selectors(relations);
    std::cout << '\n';
}

bool validate_legacy_syntax() {
    for(OperationId id : cli_public_operation_order()) {
        const std::string projected = cli_operation_syntax(id);
        const std::string_view legacy =
                cli_authority::operation_spec(id).help_syntax;
        if(projected == legacy) continue;
        std::cerr << "CLI authority drift for "
                  << cli_authority::operation_spec(id).token
                  << ": legacy='" << legacy
                  << "', projected='" << projected << "'\n";
        return false;
    }

    const std::array special_syntax = {
            std::pair{SpecialOperationId::PkgbuildExport,
                      cli_authority::PKGBUILD_EXPORT_SYNTAX},
            std::pair{SpecialOperationId::PkgbuildPrint,
                      cli_authority::PKGBUILD_PRINT_SYNTAX},
            std::pair{SpecialOperationId::SyncSelect,
                      cli_authority::PACMAN_SYNC_SELECT_SYNTAX},
    };
    for(const auto& [id, legacy] : special_syntax) {
        const std::string projected = cli_special_operation_syntax(id);
        if(projected == legacy) continue;
        std::cerr << "CLI special-operation authority drift: legacy='"
                  << legacy << "', projected='" << projected << "'\n";
        return false;
    }
    return true;
}

} // namespace

int main() {
    if(!validate_legacy_syntax()) return 1;

    constexpr std::array OPTION_ORDER = {
            OptionId::Help,
            OptionId::Version,
            OptionId::Edit,
            OptionId::NoEdit,
            OptionId::Diff,
            OptionId::NoDiff,
            OptionId::NoConfirm,
            OptionId::DryRun,
            OptionId::BuildMode,
            OptionId::Rebuild,
            OptionId::CleanBuild,
            OptionId::RmDeps,
            OptionId::Select,
            OptionId::Aur,
            OptionId::Repo,
            OptionId::LocalSource,
            OptionId::Recursive,
            OptionId::Needed,
    };
    for(OptionId id : OPTION_ORDER) print_option(id);

    for(OperationId id : cli_public_operation_order()) {
        const cli_authority::OperationMetadata& metadata =
                cli_authority::operation_metadata(id);
        std::cout << "OPERATION\t" << metadata.canonical_token
                  << "\tclosed\n";
        for(std::size_t form_index = 0;
            form_index < metadata.form_count; ++form_index) {
            const cli_authority::OperationFormSpec& form =
                    cli_authority::operation_form(metadata, form_index);
            print_form(
                    metadata.canonical_token,
                    cli_operation_form_syntax(metadata, form),
                    form.operands, form.target_policy,
                    form.option_relations);
        }
    }

    for(SpecialOperationId id : cli_public_special_operation_order()) {
        const cli_authority::SpecialOperationSpec& operation =
                cli_authority::special_operation_spec(id);
        std::cout << "OPERATION\t" << operation.canonical_token
                  << "\tclosed\n";
        print_form(
                operation.canonical_token, cli_special_operation_syntax(id),
                operation.operands, operation.target_policy,
                operation.option_relations);
    }

    constexpr std::array DELEGATED_EXAMPLE_SYNTAX = {
            cli_authority::PACMAN_SYNC_INSTALL_SYNTAX,
            cli_authority::PACMAN_SYSTEM_UPGRADE_SYNTAX,
            cli_authority::PACMAN_SYNC_SEARCH_SYNTAX,
            cli_authority::PACMAN_SYNC_INFO_SYNTAX,
            cli_authority::PACMAN_FOREIGN_UPDATES_SYNTAX,
    };
    for(std::string_view syntax : DELEGATED_EXAMPLE_SYNTAX) {
        const std::size_t separator = syntax.find(' ');
        const std::string_view token = syntax.substr(0, separator);
        std::cout << "OPERATION\t" << token << "\topen\n";
    }

    const cli_authority::SpecialOperationSpec& delegated =
            cli_authority::special_operation_spec(
                    SpecialOperationId::DelegatedPacmanGrammar);
    std::cout << "DELEGATED_OPTIONS\t";
    print_relations(delegated.option_relations);
    std::cout << '\n';

    for(OptionId id : {OptionId::Help, OptionId::Version}) {
        const cli_authority::OptionContract& option =
                cli_authority::option_contract(id);
        for(std::size_t index = 0; index < option.aliases.count; ++index) {
            std::cout << "TERMINAL\t" << option.aliases.values[index]
                      << '\n';
        }
        std::cout << "TERMINAL\t" << option.canonical_token << '\n';
    }

    for(const std::string& syntax : cli_canonical_grammar()) {
        std::cout << "CANONICAL\t" << syntax << '\n';
    }
    return 0;
}
