#include "reviewed_source_git_parser.hpp"

#include <charconv>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

struct StatusRecord {
    char code = '\0';
    std::optional<std::uint8_t> similarity;
    std::string old_path;
    std::string new_path;
};

struct NumstatRecord {
    std::string old_path;
    std::string new_path;
    ReviewedSourceContentChange content;
};

ReviewedSourceProjectionFailure malformed(
        ReviewedSourceMachineStream stream, std::size_t record_index) {
    return ReviewedSourceProjectionFailure{
            ReviewedSourceProjectionFailureReason::MalformedMachineOutput,
            stream, record_index};
}

ReviewedSourceProjectionFailure inconsistent(std::size_t record_index) {
    return ReviewedSourceProjectionFailure{
            ReviewedSourceProjectionFailureReason::InconsistentMachineOutput,
            ReviewedSourceMachineStream::CrossStream, record_index};
}

std::optional<std::string_view> take_nul_field(
        std::string_view output, std::size_t& offset) {
    if(offset >= output.size()) return std::nullopt;
    const std::size_t end = output.find('\0', offset);
    if(end == std::string_view::npos) return std::nullopt;
    const std::string_view field = output.substr(offset, end - offset);
    offset = end + 1;
    return field;
}

template<typename Integer>
bool parse_unsigned_decimal(std::string_view value, Integer& result) {
    if(value.empty()) return false;
    const auto [end, error] = std::from_chars(
            value.data(), value.data() + value.size(), result);
    return error == std::errc{} && end == value.data() + value.size();
}

std::optional<ReviewedSourceFileMode> parse_mode(std::string_view value) {
    if(value == "100644") return ReviewedSourceFileMode::Regular;
    if(value == "100755") return ReviewedSourceFileMode::Executable;
    if(value == "120000") return ReviewedSourceFileMode::SymbolicLink;
    if(value == "160000") return ReviewedSourceFileMode::Gitlink;
    return std::nullopt;
}

bool object_type_matches_mode(
        std::string_view object_type, ReviewedSourceFileMode mode) {
    return mode == ReviewedSourceFileMode::Gitlink
            ? object_type == "commit"
            : object_type == "blob";
}

using InventoryMap = std::map<
        std::string,
        const ReviewedSourceFileVersion*>;

InventoryMap inventory_map(const ReviewedSourceTreeInventory& inventory) {
    InventoryMap result;
    for(const ReviewedSourceFileVersion& entry : inventory.entries) {
        result.emplace(entry.path().raw_bytes(), &entry);
    }
    return result;
}

std::variant<std::vector<StatusRecord>, ReviewedSourceProjectionFailure>
parse_name_status(std::string_view output, bool detect_renames) {
    std::vector<StatusRecord> records;
    std::size_t offset = 0;
    while(offset < output.size()) {
        const std::size_t record_index = records.size();
        const std::optional<std::string_view> status =
                take_nul_field(output, offset);
        if(!status.has_value() || status->empty()) {
            return malformed(
                    ReviewedSourceMachineStream::NameStatus, record_index);
        }

        StatusRecord record;
        if(status->front() == 'R') {
            if(!detect_renames || status->size() < 2) {
                return malformed(
                        ReviewedSourceMachineStream::NameStatus, record_index);
            }
            unsigned int score = 0;
            if(!parse_unsigned_decimal(status->substr(1), score) ||
               score < 50 || score > 100) {
                return malformed(
                        ReviewedSourceMachineStream::NameStatus, record_index);
            }
            const auto old_path = take_nul_field(output, offset);
            const auto new_path = take_nul_field(output, offset);
            if(!old_path.has_value() || old_path->empty() ||
               !new_path.has_value() || new_path->empty() ||
               *old_path == *new_path) {
                return malformed(
                        ReviewedSourceMachineStream::NameStatus, record_index);
            }
            record.code = 'R';
            record.similarity = static_cast<std::uint8_t>(score);
            record.old_path = std::string(*old_path);
            record.new_path = std::string(*new_path);
        } else {
            if(status->size() != 1 ||
               (*status != "A" && *status != "M" && *status != "D" &&
                *status != "T")) {
                return malformed(
                        ReviewedSourceMachineStream::NameStatus, record_index);
            }
            const auto path = take_nul_field(output, offset);
            if(!path.has_value() || path->empty()) {
                return malformed(
                        ReviewedSourceMachineStream::NameStatus, record_index);
            }
            record.code = status->front();
            record.old_path = std::string(*path);
            record.new_path = std::string(*path);
        }
        records.push_back(std::move(record));
    }
    return records;
}

std::variant<std::vector<NumstatRecord>, ReviewedSourceProjectionFailure>
parse_numstat(std::string_view output) {
    std::vector<NumstatRecord> records;
    std::size_t offset = 0;
    while(offset < output.size()) {
        const std::size_t record_index = records.size();
        const std::size_t first_tab = output.find('\t', offset);
        if(first_tab == std::string_view::npos) {
            return malformed(ReviewedSourceMachineStream::Numstat, record_index);
        }
        const std::size_t second_tab = output.find('\t', first_tab + 1);
        if(second_tab == std::string_view::npos) {
            return malformed(ReviewedSourceMachineStream::Numstat, record_index);
        }
        const std::string_view added = output.substr(offset, first_tab - offset);
        const std::string_view deleted = output.substr(
                first_tab + 1, second_tab - first_tab - 1);
        offset = second_tab + 1;

        ReviewedSourceContentChange content;
        if(added == "-" && deleted == "-") {
            content = ReviewedSourceBinaryChange{};
        } else {
            std::uintmax_t added_lines = 0;
            std::uintmax_t deleted_lines = 0;
            if(added == "-" || deleted == "-" ||
               !parse_unsigned_decimal(added, added_lines) ||
               !parse_unsigned_decimal(deleted, deleted_lines)) {
                return malformed(
                        ReviewedSourceMachineStream::Numstat, record_index);
            }
            content = ReviewedSourceTextChange{added_lines, deleted_lines};
        }

        NumstatRecord record;
        record.content = std::move(content);
        if(offset < output.size() && output[offset] == '\0') {
            ++offset;
            const auto old_path = take_nul_field(output, offset);
            const auto new_path = take_nul_field(output, offset);
            if(!old_path.has_value() || old_path->empty() ||
               !new_path.has_value() || new_path->empty() ||
               *old_path == *new_path) {
                return malformed(
                        ReviewedSourceMachineStream::Numstat, record_index);
            }
            record.old_path = std::string(*old_path);
            record.new_path = std::string(*new_path);
        } else {
            const auto path = take_nul_field(output, offset);
            if(!path.has_value() || path->empty()) {
                return malformed(
                        ReviewedSourceMachineStream::Numstat, record_index);
            }
            record.old_path = std::string(*path);
            record.new_path = std::string(*path);
        }
        records.push_back(std::move(record));
    }
    return records;
}

const ReviewedSourceFileVersion* find_entry(
        const InventoryMap& inventory, const std::string& path) {
    const auto found = inventory.find(path);
    return found == inventory.end() ? nullptr : found->second;
}

bool add_blob_size(
        const ReviewedSourceFileVersion& entry,
        std::uintmax_t& aggregate,
        ReviewedSourceProjectionFailure& failure) {
    if(!entry.blob_size().has_value()) return true;
    const std::uintmax_t size = *entry.blob_size();
    if(size > REVIEWED_SOURCE_SINGLE_BLOB_LIMIT) {
        failure = ReviewedSourceProjectionFailure{
                ReviewedSourceProjectionFailureReason::
                        SingleBlobSizeLimitExceeded,
                ReviewedSourceMachineStream::ResourcePreflight,
                0, size, REVIEWED_SOURCE_SINGLE_BLOB_LIMIT};
        return false;
    }
    if(size > REVIEWED_SOURCE_AGGREGATE_BLOB_LIMIT - aggregate) {
        failure = ReviewedSourceProjectionFailure{
                ReviewedSourceProjectionFailureReason::
                        AggregateBlobSizeLimitExceeded,
                ReviewedSourceMachineStream::ResourcePreflight,
                0, REVIEWED_SOURCE_AGGREGATE_BLOB_LIMIT + 1,
                REVIEWED_SOURCE_AGGREGATE_BLOB_LIMIT};
        return false;
    }
    aggregate += size;
    return true;
}

} // namespace

ReviewedSourceCommitParseResult parse_reviewed_source_commit_output(
        std::string_view output) {
    const bool valid_size = output.size() == 41 || output.size() == 65;
    if(!valid_size || output.back() != '\n') {
        return malformed(ReviewedSourceMachineStream::CommitResolution, 0);
    }
    const std::string_view object_id = output.substr(0, output.size() - 1);
    try {
        return SourceRevisionIdentity::git_commit(std::string(object_id));
    } catch(const std::invalid_argument&) {
        return malformed(ReviewedSourceMachineStream::CommitResolution, 0);
    }
}

ReviewedSourceTreeParseResult parse_reviewed_source_tree_output(
        std::string_view metadata_output,
        std::string_view path_output,
        GitObjectFormat object_format,
        ReviewedSourceMachineStream stream) {
    ReviewedSourceTreeInventory inventory;
    std::set<std::string> paths;
    std::size_t metadata_offset = 0;
    std::size_t path_offset = 0;
    while(metadata_offset < metadata_output.size()) {
        const std::size_t record_index = inventory.entries.size();
        const auto mode_field = take_nul_field(
                metadata_output, metadata_offset);
        const auto type_field = take_nul_field(
                metadata_output, metadata_offset);
        const auto object_field = take_nul_field(
                metadata_output, metadata_offset);
        const auto size_field = take_nul_field(
                metadata_output, metadata_offset);
        const auto path_field = take_nul_field(path_output, path_offset);
        if(!mode_field.has_value() || !type_field.has_value() ||
           !object_field.has_value() || !size_field.has_value() ||
           !path_field.has_value() || path_field->empty()) {
            return malformed(stream, record_index);
        }

        const auto mode = parse_mode(*mode_field);
        if(!mode.has_value() ||
           !object_type_matches_mode(*type_field, *mode)) {
            return malformed(stream, record_index);
        }

        std::optional<std::uintmax_t> blob_size;
        if(*mode == ReviewedSourceFileMode::Gitlink) {
            if(*size_field != "-") return malformed(stream, record_index);
        } else {
            std::uintmax_t parsed_size = 0;
            if(!parse_unsigned_decimal(*size_field, parsed_size)) {
                return malformed(stream, record_index);
            }
            blob_size = parsed_size;
        }

        try {
            ReviewedSourceObjectId object_id =
                    ReviewedSourceObjectId::make(std::string(*object_field));
            if(object_id.format() != object_format) {
                return malformed(stream, record_index);
            }
            ReviewedSourcePath path =
                    ReviewedSourcePath::make(std::string(*path_field));
            if(!paths.insert(path.raw_bytes()).second) {
                return malformed(stream, record_index);
            }
            inventory.entries.push_back(ReviewedSourceFileVersion::make(
                    std::move(path), *mode, std::move(object_id), blob_size));
        } catch(const std::invalid_argument&) {
            return malformed(stream, record_index);
        }
    }
    if(path_offset != path_output.size()) {
        return malformed(stream, inventory.entries.size());
    }
    return inventory;
}

ReviewedSourceResourcePreflightResult
preflight_reviewed_source_projection_resources(
        const ReviewedSourceTreeInventory& baseline,
        const ReviewedSourceTreeInventory& target,
        bool detect_renames) {
    const InventoryMap old_entries = inventory_map(baseline);
    const InventoryMap new_entries = inventory_map(target);
    std::size_t rename_candidates = 0;
    std::uintmax_t aggregate_size = 0;
    ReviewedSourceProjectionFailure failure{};

    for(const auto& [path, old_entry] : old_entries) {
        const ReviewedSourceFileVersion* new_entry =
                find_entry(new_entries, path);
        if(new_entry == nullptr) {
            ++rename_candidates;
            if(!add_blob_size(*old_entry, aggregate_size, failure)) return failure;
        } else if(*old_entry != *new_entry) {
            if(!add_blob_size(*old_entry, aggregate_size, failure) ||
               !add_blob_size(*new_entry, aggregate_size, failure)) {
                return failure;
            }
        }
    }
    for(const auto& [path, new_entry] : new_entries) {
        if(old_entries.find(path) == old_entries.end()) {
            ++rename_candidates;
            if(!add_blob_size(*new_entry, aggregate_size, failure)) return failure;
        }
    }

    if(detect_renames &&
       rename_candidates > REVIEWED_SOURCE_RENAME_CANDIDATE_LIMIT) {
        return ReviewedSourceProjectionFailure{
                ReviewedSourceProjectionFailureReason::
                        RenameCandidateLimitExceeded,
                ReviewedSourceMachineStream::ResourcePreflight,
                0, rename_candidates,
                REVIEWED_SOURCE_RENAME_CANDIDATE_LIMIT};
    }
    return std::monostate{};
}

ReviewedSourceChangeAssemblyResult assemble_reviewed_source_changes(
        const ReviewedSourceTreeInventory& baseline,
        const ReviewedSourceTreeInventory& target,
        std::string_view name_status_output,
        std::string_view numstat_output,
        bool detect_renames) {
    auto parsed_status = parse_name_status(name_status_output, detect_renames);
    if(std::holds_alternative<ReviewedSourceProjectionFailure>(parsed_status)) {
        return std::get<ReviewedSourceProjectionFailure>(parsed_status);
    }
    auto parsed_numstat = parse_numstat(numstat_output);
    if(std::holds_alternative<ReviewedSourceProjectionFailure>(parsed_numstat)) {
        return std::get<ReviewedSourceProjectionFailure>(parsed_numstat);
    }
    const auto& statuses = std::get<std::vector<StatusRecord>>(parsed_status);
    const auto& numstats = std::get<std::vector<NumstatRecord>>(parsed_numstat);
    if(statuses.size() != numstats.size()) return inconsistent(0);

    const InventoryMap old_entries = inventory_map(baseline);
    const InventoryMap new_entries = inventory_map(target);
    std::set<std::string> consumed_old;
    std::set<std::string> consumed_new;
    std::vector<ReviewedSourceFileChange> changes;
    changes.reserve(statuses.size());

    for(std::size_t i = 0; i < statuses.size(); ++i) {
        const StatusRecord& status = statuses[i];
        const NumstatRecord& numstat = numstats[i];
        if(status.old_path != numstat.old_path ||
           status.new_path != numstat.new_path) {
            return inconsistent(i);
        }

        const ReviewedSourceFileVersion* old_entry =
                find_entry(old_entries, status.old_path);
        const ReviewedSourceFileVersion* new_entry =
                find_entry(new_entries, status.new_path);
        switch(status.code) {
        case 'A':
            if(old_entry != nullptr || new_entry == nullptr ||
               !consumed_new.insert(status.new_path).second) {
                return inconsistent(i);
            }
            changes.push_back(ReviewedSourceAdded{
                    *new_entry, numstat.content});
            break;
        case 'D':
            if(old_entry == nullptr || new_entry != nullptr ||
               !consumed_old.insert(status.old_path).second) {
                return inconsistent(i);
            }
            changes.push_back(ReviewedSourceDeleted{
                    *old_entry, numstat.content});
            break;
        case 'M':
            if(old_entry == nullptr || new_entry == nullptr ||
               *old_entry == *new_entry ||
               reviewed_source_file_type(old_entry->mode()) !=
                       reviewed_source_file_type(new_entry->mode()) ||
               !consumed_old.insert(status.old_path).second ||
               !consumed_new.insert(status.new_path).second) {
                return inconsistent(i);
            }
            changes.push_back(ReviewedSourceModified{
                    *old_entry, *new_entry, numstat.content});
            break;
        case 'T':
            if(old_entry == nullptr || new_entry == nullptr ||
               reviewed_source_file_type(old_entry->mode()) ==
                       reviewed_source_file_type(new_entry->mode()) ||
               !consumed_old.insert(status.old_path).second ||
               !consumed_new.insert(status.new_path).second) {
                return inconsistent(i);
            }
            changes.push_back(ReviewedSourceTypeChanged{
                    *old_entry, *new_entry, numstat.content});
            break;
        case 'R':
            if(!status.similarity.has_value() || old_entry == nullptr ||
               new_entry == nullptr ||
               find_entry(new_entries, status.old_path) != nullptr ||
               find_entry(old_entries, status.new_path) != nullptr ||
               !consumed_old.insert(status.old_path).second ||
               !consumed_new.insert(status.new_path).second) {
                return inconsistent(i);
            }
            changes.push_back(ReviewedSourceRenamed{
                    *old_entry, *new_entry, *status.similarity,
                    numstat.content});
            break;
        default:
            return inconsistent(i);
        }
    }

    for(const auto& [path, old_entry] : old_entries) {
        const ReviewedSourceFileVersion* new_entry =
                find_entry(new_entries, path);
        const bool changed = new_entry == nullptr || *old_entry != *new_entry;
        if(changed != consumed_old.contains(path)) return inconsistent(changes.size());
    }
    for(const auto& [path, new_entry] : new_entries) {
        const ReviewedSourceFileVersion* old_entry =
                find_entry(old_entries, path);
        const bool changed = old_entry == nullptr || *old_entry != *new_entry;
        if(changed != consumed_new.contains(path)) return inconsistent(changes.size());
    }
    return changes;
}
