#include "reviewed_source_git_parser.hpp"
#include "reviewed_source_projection.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace {

constexpr std::string_view SHA1_A =
    "1111111111111111111111111111111111111111";
constexpr std::string_view SHA1_B =
    "2222222222222222222222222222222222222222";
constexpr std::string_view SHA1_C =
    "3333333333333333333333333333333333333333";
constexpr std::string_view SHA256_A =
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";

void require(bool condition, const std::string& message) {
    if(!condition) throw std::runtime_error(message);
}

template <typename Expected, typename Variant>
const Expected& require_arm(
    const Variant& value, std::string_view message) {
    const Expected* arm = std::get_if<Expected>(&value);
    if(arm == nullptr) throw std::runtime_error(std::string(message));
    return *arm;
}

std::string tree_metadata_record(
    std::string_view mode, std::string_view type,
    std::string_view oid, std::string_view size) {
    std::string output;
    for(std::string_view field : {mode, type, oid, size}) {
        output.append(field);
        output.push_back('\0');
    }
    return output;
}

std::string normal_status(char code, std::string_view path) {
    std::string output(1, code);
    output.push_back('\0');
    output.append(path);
    output.push_back('\0');
    return output;
}

std::string rename_status(
    unsigned int score, std::string_view old_path,
    std::string_view new_path) {
    std::string output = "R" + std::to_string(score);
    output.push_back('\0');
    output.append(old_path);
    output.push_back('\0');
    output.append(new_path);
    output.push_back('\0');
    return output;
}

std::string normal_numstat(
    std::string_view added, std::string_view deleted,
    std::string_view path) {
    std::string output;
    output.append(added);
    output.push_back('\t');
    output.append(deleted);
    output.push_back('\t');
    output.append(path);
    output.push_back('\0');
    return output;
}

std::string rename_numstat(
    std::string_view added, std::string_view deleted,
    std::string_view old_path, std::string_view new_path) {
    std::string output;
    output.append(added);
    output.push_back('\t');
    output.append(deleted);
    output.push_back('\t');
    output.push_back('\0');
    output.append(old_path);
    output.push_back('\0');
    output.append(new_path);
    output.push_back('\0');
    return output;
}

ReviewedSourceFileVersion version(
    std::string path, ReviewedSourceFileMode mode,
    std::string_view oid = SHA1_A,
    std::optional<std::uintmax_t> size = 1) {
    if(mode == ReviewedSourceFileMode::Gitlink) size = std::nullopt;
    return ReviewedSourceFileVersion::make(
        ReviewedSourcePath::make(std::move(path)), mode,
        ReviewedSourceObjectId::make(std::string(oid)), size);
}

void test_oid_path_and_classification_model() {
    const auto sha1 = ReviewedSourceObjectId::make(std::string(SHA1_A));
    const auto sha256 = ReviewedSourceObjectId::make(std::string(SHA256_A));
    require(sha1.format() == GitObjectFormat::Sha1,
            "SHA-1 object format was not retained");
    require(sha256.format() == GitObjectFormat::Sha256,
            "SHA-256 object format was not retained");

    const std::string raw_path{
        'a', '\t', '\n', '\r', '\x1b', static_cast<char>(0xff), '"', '\\'};
    const ReviewedSourcePath path = ReviewedSourcePath::make(raw_path);
    require(path.raw_bytes() == raw_path,
            "Opaque path bytes changed");
    require(path.escaped_display() ==
                "\"a\\t\\n\\r\\x1B\\xFF\\\"\\\\\"",
            "Safe path display encoding drifted");

    require(version(".SRCINFO", ReviewedSourceFileMode::Regular)
                    .classification() ==
                ReviewedSourceFileClassification::GeneratedMetadata,
            "Root .SRCINFO was not generated metadata");
    require(version("nested/.SRCINFO", ReviewedSourceFileMode::Regular)
                    .classification() ==
                ReviewedSourceFileClassification::TrackedSource,
            "Nested .SRCINFO was incorrectly classified");
    require(reviewed_source_file_type(ReviewedSourceFileMode::Regular) ==
                reviewed_source_file_type(
                    ReviewedSourceFileMode::Executable),
            "Regular/executable type classes diverged");
    require(reviewed_source_file_type(
                ReviewedSourceFileMode::SymbolicLink) !=
                ReviewedSourceFileType::Regular,
            "Symlink type collapsed into regular");
}

void test_commit_output_parser() {
    const auto sha1 = parse_reviewed_source_commit_output(
        std::string(SHA1_A) + "\n");
    const auto& revision = require_arm<SourceRevisionIdentity>(
        sha1, "Complete SHA-1 commit output was rejected");
    require(revision.git_commit() != nullptr &&
                *revision.git_commit() == SHA1_A,
            "Resolved commit value changed");

    require(std::holds_alternative<SourceRevisionIdentity>(
                parse_reviewed_source_commit_output(
                    std::string(SHA256_A) + "\n")),
            "Complete SHA-256 commit output was rejected");
    for(const std::string& malformed : {
            std::string("11111111\n"),
            std::string(SHA1_A),
            std::string(SHA1_A) + "\r\n",
            std::string(SHA1_A) + "\nextra\n",
            std::string(SHA1_A) + std::string("\0\n", 2),
            std::string(40, 'A') + "\n",
            std::string(39, '1') + "g\n"}) {
        require(std::holds_alternative<ReviewedSourceProjectionFailure>(
                    parse_reviewed_source_commit_output(malformed)),
                "Malformed commit output was accepted");
    }
}

void test_tree_parser_modes_sizes_and_paths() {
    std::string output;
    output += tree_metadata_record("100644", "blob", SHA1_A, "0");
    output += tree_metadata_record("100755", "blob", SHA1_B, "12");
    output += tree_metadata_record("120000", "blob", SHA1_C, "8");
    output += tree_metadata_record("160000", "commit", SHA1_A, "-");
    output += tree_metadata_record("100644", "blob", SHA1_A, "4");
    std::string paths;
    const std::array<std::string_view, 5> path_values{
        ".SRCINFO", "PKGBUILD", "link", "submodule",
        std::string_view("nested/tab\tline\n", 16)};
    for(std::string_view path : path_values) {
        paths.append(path);
        paths.push_back('\0');
    }
    const auto parsed = parse_reviewed_source_tree_output(
        output, paths, GitObjectFormat::Sha1,
        ReviewedSourceMachineStream::TargetTree);
    const auto& inventory = require_arm<ReviewedSourceTreeInventory>(
        parsed, "Valid ls-tree output was rejected");
    require(inventory.entries.size() == 5,
            "Tree inventory entry count changed");
    require(inventory.entries[0].blob_size() == 0,
            "Zero-byte blob size was lost");
    require(inventory.entries[1].mode() ==
                ReviewedSourceFileMode::Executable,
            "Executable mode was lost");
    require(inventory.entries[2].mode() ==
                    ReviewedSourceFileMode::SymbolicLink &&
                inventory.entries[2].blob_size() == 8,
            "Symlink blob metadata was lost");
    require(inventory.entries[3].mode() ==
                    ReviewedSourceFileMode::Gitlink &&
                !inventory.entries[3].blob_size().has_value(),
            "Gitlink size semantics drifted");

    std::vector<std::string> malformed_outputs{
        tree_metadata_record("100600", "blob", SHA1_A, "1"),
        tree_metadata_record("160000", "blob", SHA1_A, "-"),
        tree_metadata_record("100644", "blob", SHA1_A, "-"),
        tree_metadata_record("100644", "blob", "1234", "1"),
        tree_metadata_record("100644", "blob", SHA256_A, "1")};
    std::string truncated = tree_metadata_record(
        "100644", "blob", SHA1_A, "1");
    truncated.pop_back();
    malformed_outputs.push_back(std::move(truncated));
    for(const std::string& malformed_output : malformed_outputs) {
        require(std::holds_alternative<ReviewedSourceProjectionFailure>(
                    parse_reviewed_source_tree_output(
                        malformed_output, std::string("path\0", 5),
                        GitObjectFormat::Sha1,
                        ReviewedSourceMachineStream::TargetTree)),
                "Malformed ls-tree output was accepted");
    }
    std::string duplicate_metadata = tree_metadata_record(
        "100644", "blob", SHA1_A, "1");
    duplicate_metadata += tree_metadata_record(
        "100644", "blob", SHA1_B, "2");
    require(std::holds_alternative<ReviewedSourceProjectionFailure>(
                parse_reviewed_source_tree_output(
                    duplicate_metadata,
                    std::string("same\0same\0", 10),
                    GitObjectFormat::Sha1,
                    ReviewedSourceMachineStream::TargetTree)),
            "Duplicate raw tree path was accepted");
}

void test_complete_change_assembly() {
    ReviewedSourceTreeInventory baseline{{version("modified", ReviewedSourceFileMode::Regular, SHA1_A, 10),
                                          version("deleted", ReviewedSourceFileMode::Regular, SHA1_A, 5),
                                          version("old name", ReviewedSourceFileMode::Regular, SHA1_B, 7),
                                          version("type", ReviewedSourceFileMode::Regular, SHA1_A, 4),
                                          version("mode", ReviewedSourceFileMode::Regular, SHA1_C, 6),
                                          version("unchanged", ReviewedSourceFileMode::Regular, SHA1_A, 1)}};
    ReviewedSourceTreeInventory target{{version("modified", ReviewedSourceFileMode::Regular, SHA1_B, 11),
                                        version("added.future", ReviewedSourceFileMode::Regular, SHA1_C, 3),
                                        version("new name", ReviewedSourceFileMode::Regular, SHA1_B, 7),
                                        version("type", ReviewedSourceFileMode::SymbolicLink, SHA1_C, 8),
                                        version("mode", ReviewedSourceFileMode::Executable, SHA1_C, 6),
                                        version("unchanged", ReviewedSourceFileMode::Regular, SHA1_A, 1)}};

    std::string status;
    status += normal_status('M', "modified");
    status += normal_status('D', "deleted");
    status += normal_status('A', "added.future");
    status += rename_status(100, "old name", "new name");
    status += normal_status('T', "type");
    status += normal_status('M', "mode");
    std::string numstat;
    numstat += normal_numstat("2", "1", "modified");
    numstat += normal_numstat("0", "1", "deleted");
    numstat += normal_numstat("-", "-", "added.future");
    numstat += rename_numstat("0", "0", "old name", "new name");
    numstat += normal_numstat("1", "1", "type");
    numstat += normal_numstat("0", "0", "mode");

    const auto assembled = assemble_reviewed_source_changes(
        baseline, target, status, numstat, true);
    const auto& changes = require_arm<std::vector<ReviewedSourceFileChange>>(
        assembled, "Consistent change streams were rejected");
    require(changes.size() == 6,
            "Change count drifted");
    require(std::holds_alternative<ReviewedSourceModified>(changes[0]),
            "Modified change was not typed");
    require(std::holds_alternative<ReviewedSourceDeleted>(changes[1]),
            "Deleted change was not typed");
    const auto& added = require_arm<ReviewedSourceAdded>(
        changes[2], "Added change was not typed");
    require(std::holds_alternative<ReviewedSourceBinaryChange>(added.content),
            "Binary marker was lost");
    const auto& renamed = require_arm<ReviewedSourceRenamed>(
        changes[3], "Rename change was not typed");
    require(renamed.old_version.path().raw_bytes() == "old name" &&
                renamed.new_version.path().raw_bytes() == "new name" &&
                renamed.similarity == 100,
            "Rename paths or similarity were lost");
    require(std::holds_alternative<ReviewedSourceTypeChanged>(changes[4]),
            "Type change was not typed");
    require(std::holds_alternative<ReviewedSourceModified>(changes[5]),
            "Regular-to-executable change was not Modified");

    require(std::holds_alternative<std::monostate>(
                preflight_reviewed_source_projection_resources(
                    baseline, target, true)),
            "Valid projection failed resource preflight");

    ReviewedSourceTreeInventory rename_baseline{{version("before", ReviewedSourceFileMode::Regular, SHA1_A, 1)}};
    ReviewedSourceTreeInventory rename_target{{version("after", ReviewedSourceFileMode::Regular, SHA1_B, 1)}};
    require(std::holds_alternative<std::vector<ReviewedSourceFileChange>>(
                assemble_reviewed_source_changes(
                    rename_baseline, rename_target,
                    rename_status(50, "before", "after"),
                    rename_numstat("1", "1", "before", "after"),
                    true)),
            "Exact 50% rename threshold was rejected");
}

void test_malformed_and_inconsistent_streams_fail_closed() {
    ReviewedSourceTreeInventory baseline{{version("file", ReviewedSourceFileMode::Regular, SHA1_A, 1)}};
    ReviewedSourceTreeInventory target{{version("file", ReviewedSourceFileMode::Regular, SHA1_B, 2)}};

    for(const std::string& status : {
            std::string("C100\0file\0copy\0", 15),
            std::string("R49\0old\0new\0", 12),
            std::string("R101\0old\0new\0", 14),
            std::string("M\0file", 6)}) {
        require(std::holds_alternative<ReviewedSourceProjectionFailure>(
                    assemble_reviewed_source_changes(
                        baseline, target, status,
                        normal_numstat("1", "1", "file"), true)),
                "Malformed name-status stream was accepted");
    }

    require(std::holds_alternative<ReviewedSourceProjectionFailure>(
                assemble_reviewed_source_changes(
                    baseline, target, normal_status('M', "file"),
                    normal_numstat("-", "1", "file"), true)),
            "Malformed numstat stream was accepted");
    require(std::holds_alternative<ReviewedSourceProjectionFailure>(
                assemble_reviewed_source_changes(
                    baseline, target, normal_status('M', "file"),
                    normal_numstat("1", "1", "other"), true)),
            "Cross-stream path mismatch was accepted");
    require(std::holds_alternative<ReviewedSourceProjectionFailure>(
                assemble_reviewed_source_changes(
                    baseline, target, "", "", true)),
            "Missing complete changed record was accepted as empty diff");
}

ReviewedSourceTreeInventory candidate_inventory(
    std::string_view prefix, std::size_t count) {
    ReviewedSourceTreeInventory inventory;
    inventory.entries.reserve(count);
    for(std::size_t i = 0; i < count; ++i) {
        inventory.entries.push_back(version(
            std::string(prefix) + std::to_string(i),
            ReviewedSourceFileMode::Regular, SHA1_A, 0));
    }
    return inventory;
}

void test_resource_limits() {
    const ReviewedSourceTreeInventory empty;
    ReviewedSourceTreeInventory exact_single{{version(
        "large", ReviewedSourceFileMode::Regular, SHA1_A,
        REVIEWED_SOURCE_SINGLE_BLOB_LIMIT)}};
    require(std::holds_alternative<std::monostate>(
                preflight_reviewed_source_projection_resources(
                    empty, exact_single, false)),
            "Exact single-blob limit was rejected");

    ReviewedSourceTreeInventory oversized{{version(
        "oversized", ReviewedSourceFileMode::Regular, SHA1_A,
        REVIEWED_SOURCE_SINGLE_BLOB_LIMIT + 1)}};
    const auto oversized_result =
        preflight_reviewed_source_projection_resources(
            empty, oversized, false);
    require(require_arm<ReviewedSourceProjectionFailure>(
                oversized_result,
                "Oversized blob was accepted")
                    .reason ==
                ReviewedSourceProjectionFailureReason::
                    SingleBlobSizeLimitExceeded,
            "Oversized blob failure reason drifted");

    ReviewedSourceTreeInventory aggregate{{version("a", ReviewedSourceFileMode::Regular, SHA1_A,
                                                   REVIEWED_SOURCE_SINGLE_BLOB_LIMIT),
                                           version("b", ReviewedSourceFileMode::Regular, SHA1_B,
                                                   REVIEWED_SOURCE_SINGLE_BLOB_LIMIT),
                                           version("c", ReviewedSourceFileMode::Regular, SHA1_C,
                                                   REVIEWED_SOURCE_SINGLE_BLOB_LIMIT),
                                           version("d", ReviewedSourceFileMode::Regular, SHA1_A,
                                                   REVIEWED_SOURCE_SINGLE_BLOB_LIMIT),
                                           version("e", ReviewedSourceFileMode::Regular, SHA1_B, 1)}};
    ReviewedSourceTreeInventory aggregate_exact;
    aggregate_exact.entries.assign(
        aggregate.entries.begin(), aggregate.entries.end() - 1);
    require(std::holds_alternative<std::monostate>(
                preflight_reviewed_source_projection_resources(
                    empty, aggregate_exact, false)),
            "Exact aggregate blob limit was rejected");
    require(require_arm<ReviewedSourceProjectionFailure>(
                preflight_reviewed_source_projection_resources(
                    empty, aggregate, false),
                "Aggregate overflow was accepted")
                    .reason ==
                ReviewedSourceProjectionFailureReason::
                    AggregateBlobSizeLimitExceeded,
            "Aggregate overflow reason drifted");

    const auto old_500 = candidate_inventory("old-", 500);
    const auto new_500 = candidate_inventory("new-", 500);
    require(std::holds_alternative<std::monostate>(
                preflight_reviewed_source_projection_resources(
                    old_500, new_500, true)),
            "Exact rename-candidate limit was rejected");
    const auto old_501 = candidate_inventory("old-", 501);
    const auto rename_overflow =
        preflight_reviewed_source_projection_resources(
            old_501, new_500, true);
    require(require_arm<ReviewedSourceProjectionFailure>(
                rename_overflow,
                "Rename-candidate overflow was accepted")
                    .reason ==
                ReviewedSourceProjectionFailureReason::
                    RenameCandidateLimitExceeded,
            "Rename-candidate failure reason drifted");
}

void run_tests() {
    test_oid_path_and_classification_model();
    test_commit_output_parser();
    test_tree_parser_modes_sizes_and_paths();
    test_complete_change_assembly();
    test_malformed_and_inconsistent_streams_fail_closed();
    test_resource_limits();
}

} // namespace

int main() {
    try {
        run_tests();
    } catch(const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "reviewed source projection tests: all checks passed\n";
    return 0;
}
