#include "local_package_metadata.hpp"

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

struct RelationFieldMatch {
    LocalPackageMetadataRelationKind kind;
    std::optional<std::string_view> architecture_qualifier;
};

struct ParsedRelationValue {
    LocalPackageMetadataRelationTarget target;
    std::optional<std::string> optdepends_description;
};

constexpr bool is_ascii_lower(char ch) noexcept {
    return ch >= 'a' && ch <= 'z';
}

constexpr bool is_ascii_upper(char ch) noexcept {
    return ch >= 'A' && ch <= 'Z';
}

constexpr bool is_ascii_digit(char ch) noexcept {
    return ch >= '0' && ch <= '9';
}

constexpr bool is_ascii_alphanumeric(char ch) noexcept {
    return is_ascii_lower(ch) || is_ascii_upper(ch) || is_ascii_digit(ch);
}

std::string_view trim_assignment_whitespace(std::string_view value) noexcept {
    const std::size_t first = value.find_first_not_of(" \t");
    if(first == std::string_view::npos) return {};
    const std::size_t last = value.find_last_not_of(" \t");
    return value.substr(first, last - first + 1);
}

bool has_disallowed_control_character(std::string_view line) noexcept {
    return std::any_of(line.begin(), line.end(), [](char ch) {
        const unsigned char byte = static_cast<unsigned char>(ch);
        return (byte < 0x20U && ch != '\t') || byte == 0x7fU;
    });
}

bool is_valid_field_name(std::string_view field_name) noexcept {
    if(field_name.empty() || !is_ascii_lower(field_name.front())) return false;
    return std::all_of(
        field_name.begin() + 1, field_name.end(), [](char ch) {
            return is_ascii_alphanumeric(ch) || ch == '_';
        });
}

bool is_valid_package_identity(std::string_view identity) noexcept {
    if(identity.empty() || identity.front() == '-' || identity.front() == '.') {
        return false;
    }
    return std::all_of(identity.begin(), identity.end(), [](char ch) {
        return is_ascii_alphanumeric(ch) || ch == '+' || ch == '_' ||
               ch == '.' || ch == '@' || ch == '-';
    });
}

bool is_valid_architecture(std::string_view architecture) noexcept {
    return !architecture.empty() &&
           std::all_of(
               architecture.begin(), architecture.end(), [](char ch) {
                   return is_ascii_alphanumeric(ch) || ch == '_';
               });
}

bool contains_only_ascii_digits(std::string_view value) noexcept {
    return !value.empty() &&
           std::all_of(value.begin(), value.end(), is_ascii_digit);
}

bool is_valid_pkgver(std::string_view value) noexcept {
    if(value.empty()) return false;
    return std::all_of(value.begin(), value.end(), [](char ch) {
        const unsigned char byte = static_cast<unsigned char>(ch);
        if(byte < 0x21U || byte > 0x7eU) return false;
        return ch != ':' && ch != '/' && ch != '-' && ch != '<' &&
               ch != '>' && ch != '=';
    });
}

bool is_valid_pkgrel(std::string_view value) noexcept {
    const std::size_t dot = value.find('.');
    if(dot == std::string_view::npos) {
        return contains_only_ascii_digits(value);
    }
    return contains_only_ascii_digits(value.substr(0, dot)) &&
           contains_only_ascii_digits(value.substr(dot + 1)) &&
           value.find('.', dot + 1) == std::string_view::npos;
}

bool is_valid_package_version(std::string_view value) noexcept {
    if(value.empty()) return false;

    std::string_view version = value;
    const std::size_t epoch_separator = version.find(':');
    if(epoch_separator != std::string_view::npos) {
        if(!contains_only_ascii_digits(version.substr(0, epoch_separator))) {
            return false;
        }
        version.remove_prefix(epoch_separator + 1);
        if(version.find(':') != std::string_view::npos) return false;
    }

    const std::size_t release_separator = version.find('-');
    if(release_separator == std::string_view::npos) {
        return is_valid_pkgver(version);
    }
    return is_valid_pkgver(version.substr(0, release_separator)) &&
           is_valid_pkgrel(version.substr(release_separator + 1));
}

bool is_valid_soname_payload(std::string_view payload) noexcept {
    if(payload.empty()) return false;
    return std::all_of(payload.begin(), payload.end(), [](char ch) {
        const unsigned char byte = static_cast<unsigned char>(ch);
        return byte >= 0x21U && byte <= 0x7eU && ch != '/' && ch != ':' &&
               ch != '<' && ch != '>' && ch != '=';
    });
}

std::optional<RelationFieldMatch> match_relation_field(
    std::string_view field_name) noexcept {
    using RelationKind = LocalPackageMetadataRelationKind;
    constexpr std::array<std::pair<std::string_view, RelationKind>, 7>
        RELATION_FIELDS{{
            {"depends", RelationKind::Depends},
            {"makedepends", RelationKind::Makedepends},
            {"checkdepends", RelationKind::Checkdepends},
            {"optdepends", RelationKind::Optdepends},
            {"provides", RelationKind::Provides},
            {"conflicts", RelationKind::Conflicts},
            {"replaces", RelationKind::Replaces},
        }};

    for(const auto& [base_name, kind] : RELATION_FIELDS) {
        if(field_name == base_name) {
            return RelationFieldMatch{kind, std::nullopt};
        }
        if(field_name.size() > base_name.size() &&
           field_name.substr(0, base_name.size()) == base_name &&
           field_name[base_name.size()] == '_') {
            return RelationFieldMatch{
                kind, field_name.substr(base_name.size() + 1)};
        }
    }
    return std::nullopt;
}

std::optional<LocalPackageMetadataComparison> parse_comparison(
    std::string_view value, std::size_t position,
    std::size_t& operator_size) noexcept {
    operator_size = 1;
    switch(value[position]) {
        case '<':
            if(position + 1 < value.size() && value[position + 1] == '=') {
                operator_size = 2;
                return LocalPackageMetadataComparison::LessThanOrEqual;
            }
            return LocalPackageMetadataComparison::LessThan;
        case '=':
            return LocalPackageMetadataComparison::Equal;
        case '>':
            if(position + 1 < value.size() && value[position + 1] == '=') {
                operator_size = 2;
                return LocalPackageMetadataComparison::GreaterThanOrEqual;
            }
            return LocalPackageMetadataComparison::GreaterThan;
    }
    return std::nullopt;
}

std::optional<std::size_t> find_optdepends_description_separator(
    std::string_view value) noexcept {
    for(std::size_t index = 0; index + 1 < value.size(); ++index) {
        if(value[index] == ':' &&
           (value[index + 1] == ' ' || value[index + 1] == '\t')) {
            return index;
        }
    }
    return std::nullopt;
}

std::optional<ParsedRelationValue> parse_relation_value(
    LocalPackageMetadataRelationKind kind, std::string_view raw_value) {
    std::string_view target_text = raw_value;
    std::optional<std::string> optdepends_description;
    if(kind == LocalPackageMetadataRelationKind::Optdepends) {
        const std::optional<std::size_t> separator =
            find_optdepends_description_separator(raw_value);
        if(separator.has_value()) {
            target_text = raw_value.substr(0, *separator);
            const std::string_view description = trim_assignment_whitespace(
                raw_value.substr(*separator + 1));
            if(description.empty()) return std::nullopt;
            optdepends_description = std::string(description);
        }
    }

    if(target_text.empty()) return std::nullopt;

    const bool can_use_soname =
        kind == LocalPackageMetadataRelationKind::Depends ||
        kind == LocalPackageMetadataRelationKind::Provides;
    const std::size_t comparison_position = target_text.find_first_of("<>=");
    const std::size_t soname_separator = target_text.find(':');
    if(can_use_soname && comparison_position == std::string_view::npos &&
       soname_separator != std::string_view::npos &&
       target_text.find(':', soname_separator + 1) ==
           std::string_view::npos &&
       is_valid_package_identity(target_text.substr(0, soname_separator)) &&
       is_valid_soname_payload(target_text.substr(soname_separator + 1))) {
        return ParsedRelationValue{
            LocalPackageMetadataRelationTarget{
                LocalPackageMetadataRelationTargetKind::Soname,
                std::string(target_text), std::nullopt, std::nullopt},
            std::move(optdepends_description)};
    }

    if(comparison_position == std::string_view::npos) {
        if(!is_valid_package_identity(target_text)) return std::nullopt;
        return ParsedRelationValue{
            LocalPackageMetadataRelationTarget{
                LocalPackageMetadataRelationTargetKind::Package,
                std::string(target_text), std::nullopt, std::nullopt},
            std::move(optdepends_description)};
    }

    const std::string_view name =
        target_text.substr(0, comparison_position);
    if(!is_valid_package_identity(name)) return std::nullopt;

    std::size_t operator_size = 0;
    const std::optional<LocalPackageMetadataComparison> comparison =
        parse_comparison(
            target_text, comparison_position, operator_size);
    if(!comparison.has_value()) return std::nullopt;
    if(kind == LocalPackageMetadataRelationKind::Provides &&
       *comparison != LocalPackageMetadataComparison::Equal) {
        return std::nullopt;
    }

    const std::string_view version =
        target_text.substr(comparison_position + operator_size);
    if(!is_valid_package_version(version)) return std::nullopt;

    return ParsedRelationValue{
        LocalPackageMetadataRelationTarget{
            LocalPackageMetadataRelationTargetKind::Package,
            std::string(name), comparison, std::string(version)},
        std::move(optdepends_description)};
}

LocalPackageMetadataParseFailure make_failure(
    LocalPackageMetadataParseErrorCode code,
    std::size_t line) noexcept {
    return LocalPackageMetadataParseFailure{code, line};
}

LocalPackageMetadataScope make_scope(
    const LocalPackageMetadata& metadata,
    const std::optional<std::size_t>& current_child_index) {
    if(!current_child_index.has_value()) {
        return LocalPackageMetadataScope{
            LocalPackageMetadataScopeKind::PackageBase, std::nullopt};
    }
    return LocalPackageMetadataScope{
        LocalPackageMetadataScopeKind::ChildPackage,
        metadata.children[*current_child_index].name};
}

bool contains_architecture(
    const std::vector<std::string>& architectures,
    std::string_view candidate) {
    return std::find(
               architectures.begin(), architectures.end(), candidate) !=
           architectures.end();
}

std::optional<LocalPackageMetadataParseFailure> add_architecture(
    std::vector<std::string>& architectures,
    std::vector<std::size_t>& architecture_lines,
    std::string_view architecture, std::size_t line) {
    if(!is_valid_architecture(architecture)) {
        return make_failure(
            LocalPackageMetadataParseErrorCode::InvalidArchitecture,
            line);
    }
    if(contains_architecture(architectures, architecture)) {
        return make_failure(
            LocalPackageMetadataParseErrorCode::DuplicateArchitecture,
            line);
    }
    if((architecture == "any" && !architectures.empty()) ||
       (architecture != "any" && contains_architecture(architectures, "any"))) {
        return make_failure(
            LocalPackageMetadataParseErrorCode::ConflictingArchitecture,
            line);
    }
    architectures.emplace_back(architecture);
    architecture_lines.push_back(line);
    return std::nullopt;
}

std::size_t source_line_count(std::string_view source) noexcept {
    if(source.empty()) return 0;
    const std::size_t newline_count = static_cast<std::size_t>(
        std::count(source.begin(), source.end(), '\n'));
    return source.back() == '\n' ? newline_count : newline_count + 1;
}

} // namespace

LocalPackageMetadataParseResult::LocalPackageMetadataParseResult(
    LocalPackageMetadata metadata) noexcept
    : outcome_(std::move(metadata)) {
}

LocalPackageMetadataParseResult::LocalPackageMetadataParseResult(
    LocalPackageMetadataParseFailure failure) noexcept
    : outcome_(failure) {
}

bool LocalPackageMetadataParseResult::is_success() const noexcept {
    return std::holds_alternative<LocalPackageMetadata>(outcome_);
}

const LocalPackageMetadata*
LocalPackageMetadataParseResult::metadata() const noexcept {
    return std::get_if<LocalPackageMetadata>(&outcome_);
}

const LocalPackageMetadataParseFailure*
LocalPackageMetadataParseResult::failure() const noexcept {
    return std::get_if<LocalPackageMetadataParseFailure>(&outcome_);
}

LocalPackageMetadataParseResult parse_local_package_metadata(
    std::string_view source) {
    LocalPackageMetadata metadata;
    std::optional<std::size_t> current_child_index;
    std::vector<std::size_t> base_architecture_lines;
    std::vector<std::vector<std::size_t>> child_architecture_lines;
    std::vector<std::size_t> relation_lines;
    std::vector<std::optional<std::size_t>> relation_child_indices;

    std::size_t offset = 0;
    std::size_t line_number = 1;
    while(offset < source.size()) {
        const std::size_t newline = source.find('\n', offset);
        const std::size_t line_end =
            newline == std::string_view::npos ? source.size() : newline;
        std::string_view line = source.substr(offset, line_end - offset);
        if(!line.empty() && line.back() == '\r') line.remove_suffix(1);

        if(has_disallowed_control_character(line)) {
            return LocalPackageMetadataParseResult(make_failure(
                LocalPackageMetadataParseErrorCode::ControlCharacter,
                line_number));
        }

        const std::string_view trimmed_line =
            trim_assignment_whitespace(line);
        if(!trimmed_line.empty() && trimmed_line.front() != '#') {
            const std::size_t separator = trimmed_line.find('=');
            if(separator == std::string_view::npos) {
                return LocalPackageMetadataParseResult(make_failure(
                    LocalPackageMetadataParseErrorCode::MalformedLine,
                    line_number));
            }

            const std::string_view field_name = trim_assignment_whitespace(
                trimmed_line.substr(0, separator));
            const std::string_view raw_value = trim_assignment_whitespace(
                trimmed_line.substr(separator + 1));
            if(!is_valid_field_name(field_name)) {
                return LocalPackageMetadataParseResult(make_failure(
                    LocalPackageMetadataParseErrorCode::MalformedLine,
                    line_number));
            }

            // POLICY(#271): pkgbase is the first section identity. Other
            // fields never start a PackageBase section implicitly.
            if(metadata.package_base.empty() && field_name != "pkgbase") {
                return LocalPackageMetadataParseResult(make_failure(
                    LocalPackageMetadataParseErrorCode::InvalidFieldScope,
                    line_number));
            }

            if(field_name == "pkgbase") {
                if(!is_valid_package_identity(raw_value)) {
                    return LocalPackageMetadataParseResult(make_failure(
                        LocalPackageMetadataParseErrorCode::
                            InvalidPackageIdentity,
                        line_number));
                }
                if(!metadata.package_base.empty()) {
                    const auto code = raw_value == metadata.package_base
                                          ? LocalPackageMetadataParseErrorCode::
                                                DuplicatePackageBase
                                          : LocalPackageMetadataParseErrorCode::
                                                ConflictingPackageBase;
                    return LocalPackageMetadataParseResult(
                        make_failure(code, line_number));
                }
                metadata.package_base = std::string(raw_value);
            } else if(field_name == "pkgname") {
                if(!is_valid_package_identity(raw_value)) {
                    return LocalPackageMetadataParseResult(make_failure(
                        LocalPackageMetadataParseErrorCode::
                            InvalidPackageIdentity,
                        line_number));
                }
                const auto duplicate = std::find_if(
                    metadata.children.begin(), metadata.children.end(),
                    [raw_value](const LocalPackageMetadataChild& child) {
                        return child.name == raw_value;
                    });
                if(duplicate != metadata.children.end()) {
                    return LocalPackageMetadataParseResult(make_failure(
                        LocalPackageMetadataParseErrorCode::
                            DuplicatePackageName,
                        line_number));
                }
                metadata.children.push_back(LocalPackageMetadataChild{
                    std::string(raw_value), false, false, {}});
                child_architecture_lines.emplace_back();
                current_child_index = metadata.children.size() - 1;
            } else if(field_name == "epoch" || field_name == "pkgver" ||
                      field_name == "pkgrel") {
                if(current_child_index.has_value()) {
                    return LocalPackageMetadataParseResult(make_failure(
                        LocalPackageMetadataParseErrorCode::
                            InvalidFieldScope,
                        line_number));
                }

                const bool is_duplicate =
                    (field_name == "epoch" && metadata.epoch.has_value()) ||
                    (field_name == "pkgver" && !metadata.pkgver.empty()) ||
                    (field_name == "pkgrel" && !metadata.pkgrel.empty());
                if(is_duplicate) {
                    const auto code = field_name == "epoch"
                                          ? LocalPackageMetadataParseErrorCode::DuplicateEpoch
                                      : field_name == "pkgver"
                                          ? LocalPackageMetadataParseErrorCode::DuplicatePkgver
                                          : LocalPackageMetadataParseErrorCode::DuplicatePkgrel;
                    return LocalPackageMetadataParseResult(
                        make_failure(code, line_number));
                }
                if(raw_value.empty()) {
                    return LocalPackageMetadataParseResult(make_failure(
                        LocalPackageMetadataParseErrorCode::
                            EmptyRequiredValue,
                        line_number));
                }

                if(field_name == "epoch") {
                    if(!contains_only_ascii_digits(raw_value)) {
                        return LocalPackageMetadataParseResult(make_failure(
                            LocalPackageMetadataParseErrorCode::InvalidEpoch,
                            line_number));
                    }
                    metadata.epoch = std::string(raw_value);
                } else if(field_name == "pkgver") {
                    if(!is_valid_pkgver(raw_value)) {
                        return LocalPackageMetadataParseResult(make_failure(
                            LocalPackageMetadataParseErrorCode::InvalidPkgver,
                            line_number));
                    }
                    metadata.pkgver = std::string(raw_value);
                } else {
                    if(!is_valid_pkgrel(raw_value)) {
                        return LocalPackageMetadataParseResult(make_failure(
                            LocalPackageMetadataParseErrorCode::InvalidPkgrel,
                            line_number));
                    }
                    metadata.pkgrel = std::string(raw_value);
                }
            } else if(field_name == "arch") {
                if(!current_child_index.has_value()) {
                    if(raw_value.empty()) {
                        return LocalPackageMetadataParseResult(make_failure(
                            LocalPackageMetadataParseErrorCode::
                                EmptyRequiredValue,
                            line_number));
                    }
                    const auto failure = add_architecture(
                        metadata.architectures, base_architecture_lines,
                        raw_value, line_number);
                    if(failure.has_value()) {
                        return LocalPackageMetadataParseResult(*failure);
                    }
                } else {
                    LocalPackageMetadataChild& child =
                        metadata.children[*current_child_index];
                    if(raw_value.empty()) {
                        if(child.clears_inherited_architectures) {
                            return LocalPackageMetadataParseResult(make_failure(
                                LocalPackageMetadataParseErrorCode::
                                    DuplicateArchitecture,
                                line_number));
                        }
                        if(child.has_architecture_override) {
                            return LocalPackageMetadataParseResult(make_failure(
                                LocalPackageMetadataParseErrorCode::
                                    ConflictingArchitecture,
                                line_number));
                        }
                        child.has_architecture_override = true;
                        child.clears_inherited_architectures = true;
                    } else {
                        child.has_architecture_override = true;
                        const auto failure = add_architecture(
                            child.architectures,
                            child_architecture_lines[*current_child_index],
                            raw_value, line_number);
                        if(failure.has_value()) {
                            return LocalPackageMetadataParseResult(*failure);
                        }
                    }
                }
            } else {
                const std::optional<RelationFieldMatch> relation =
                    match_relation_field(field_name);
                if(relation.has_value()) {
                    if(relation->architecture_qualifier.has_value() &&
                       (!is_valid_architecture(
                            *relation->architecture_qualifier) ||
                        *relation->architecture_qualifier == "any")) {
                        return LocalPackageMetadataParseResult(make_failure(
                            LocalPackageMetadataParseErrorCode::
                                InvalidArchitectureQualifier,
                            line_number));
                    }
                    if(current_child_index.has_value() &&
                       relation->kind ==
                           LocalPackageMetadataRelationKind::Makedepends) {
                        return LocalPackageMetadataParseResult(make_failure(
                            LocalPackageMetadataParseErrorCode::
                                InvalidFieldScope,
                            line_number));
                    }
                    if(raw_value.empty() &&
                       !current_child_index.has_value()) {
                        return LocalPackageMetadataParseResult(make_failure(
                            LocalPackageMetadataParseErrorCode::
                                EmptyRequiredValue,
                            line_number));
                    }

                    std::optional<LocalPackageMetadataRelationTarget> target;
                    std::optional<std::string> optdepends_description;
                    const bool is_explicit_unset = raw_value.empty();
                    if(!is_explicit_unset) {
                        std::optional<ParsedRelationValue> parsed =
                            parse_relation_value(
                                relation->kind, raw_value);
                        if(!parsed.has_value()) {
                            return LocalPackageMetadataParseResult(make_failure(
                                LocalPackageMetadataParseErrorCode::
                                    InvalidRelation,
                                line_number));
                        }
                        target = std::move(parsed->target);
                        optdepends_description =
                            std::move(parsed->optdepends_description);
                    }

                    std::optional<std::string> architecture_qualifier;
                    if(relation->architecture_qualifier.has_value()) {
                        architecture_qualifier = std::string(
                            *relation->architecture_qualifier);
                    }
                    metadata.relations.push_back(
                        LocalPackageMetadataRelation{
                            relation->kind,
                            std::string(raw_value),
                            std::move(target),
                            make_scope(metadata, current_child_index),
                            std::move(architecture_qualifier),
                            std::move(optdepends_description),
                            is_explicit_unset});
                    relation_lines.push_back(line_number);
                    relation_child_indices.push_back(current_child_index);
                }
                // Unrelated valid `.SRCINFO` fields do not change section
                // state and remain outside this Slice 2 typed projection.
            }
        }

        if(newline == std::string_view::npos) break;
        offset = newline + 1;
        ++line_number;
    }

    const std::size_t eof_line = source_line_count(source) + 1;
    if(metadata.package_base.empty()) {
        return LocalPackageMetadataParseResult(make_failure(
            LocalPackageMetadataParseErrorCode::MissingPackageBase,
            eof_line));
    }
    if(metadata.pkgver.empty()) {
        return LocalPackageMetadataParseResult(make_failure(
            LocalPackageMetadataParseErrorCode::MissingPkgver,
            eof_line));
    }
    if(metadata.pkgrel.empty()) {
        return LocalPackageMetadataParseResult(make_failure(
            LocalPackageMetadataParseErrorCode::MissingPkgrel,
            eof_line));
    }
    if(metadata.architectures.empty()) {
        return LocalPackageMetadataParseResult(make_failure(
            LocalPackageMetadataParseErrorCode::MissingArchitecture,
            eof_line));
    }
    if(metadata.children.empty()) {
        return LocalPackageMetadataParseResult(make_failure(
            LocalPackageMetadataParseErrorCode::MissingPackageName,
            eof_line));
    }

    for(std::size_t child_index = 0;
        child_index < metadata.children.size(); ++child_index) {
        const LocalPackageMetadataChild& child =
            metadata.children[child_index];
        if(!child.has_architecture_override ||
           contains_architecture(child.architectures, "any")) {
            continue;
        }
        for(std::size_t index = 0; index < child.architectures.size(); ++index) {
            if(!contains_architecture(
                   metadata.architectures,
                   child.architectures[index])) {
                return LocalPackageMetadataParseResult(make_failure(
                    LocalPackageMetadataParseErrorCode::
                        ConflictingArchitecture,
                    child_architecture_lines[child_index][index]));
            }
        }
    }

    for(std::size_t index = 0; index < metadata.relations.size(); ++index) {
        const LocalPackageMetadataRelation& relation =
            metadata.relations[index];
        if(!relation.architecture_qualifier.has_value()) continue;

        const std::vector<std::string>* effective_architectures =
            &metadata.architectures;
        if(relation_child_indices[index].has_value()) {
            const LocalPackageMetadataChild& child =
                metadata.children[*relation_child_indices[index]];
            if(child.has_architecture_override) {
                effective_architectures = &child.architectures;
            }
        }
        if(!contains_architecture(
               *effective_architectures,
               *relation.architecture_qualifier)) {
            return LocalPackageMetadataParseResult(make_failure(
                LocalPackageMetadataParseErrorCode::
                    InvalidArchitectureQualifier,
                relation_lines[index]));
        }
    }

    return LocalPackageMetadataParseResult(std::move(metadata));
}
