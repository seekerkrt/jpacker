#include "reviewed_source_patch.hpp"

#include <iostream>
#include <stdexcept>
#include <string>
#include <variant>

namespace {

constexpr const char* SHA1_OLD =
        "1111111111111111111111111111111111111111";
constexpr const char* SHA1_NEW =
        "2222222222222222222222222222222222222222";
constexpr const char* SHA256_OLD =
        "1111111111111111111111111111111111111111111111111111111111111111";
constexpr const char* SHA256_NEW =
        "2222222222222222222222222222222222222222222222222222222222222222";

void require(bool condition, const std::string& message) {
    if(!condition) throw std::runtime_error(message);
}

template<typename Expected, typename Variant>
const Expected& require_arm(
        const Variant& value, std::string_view message) {
    const Expected* result = std::get_if<Expected>(&value);
    if(result == nullptr) throw std::runtime_error(std::string(message));
    return *result;
}

std::string patch_header(
        const std::string& old_oid, const std::string& new_oid) {
    return "diff --git " + old_oid + " " + new_oid + "\n" +
           "index " + old_oid + ".." + new_oid + " 100644\n" +
           "--- " + old_oid + "\n" +
           "+++ " + new_oid + "\n";
}

std::string one_line_patch(
        const std::string& old_oid,
        const std::string& new_oid,
        const std::string& old_line,
        const std::string& new_line,
        bool old_has_newline = true,
        bool new_has_newline = true) {
    std::string output = patch_header(old_oid, new_oid);
    output += "@@ -1 +1 @@\n-" + old_line + "\n";
    if(!old_has_newline) output += "\\ No newline at end of file\n";
    output += "+" + new_line + "\n";
    if(!new_has_newline) output += "\\ No newline at end of file\n";
    return output;
}

void test_sha1_patch_and_replay() {
    const ReviewedSourceObjectId old_oid =
            ReviewedSourceObjectId::make(SHA1_OLD);
    const ReviewedSourceObjectId new_oid =
            ReviewedSourceObjectId::make(SHA1_NEW);
    const std::string output = one_line_patch(
            SHA1_OLD, SHA1_NEW, "old", "new");
    const auto parsed = parse_and_verify_reviewed_source_patch(
            output, old_oid, new_oid, "old\n", "new\n");
    const auto& patch = require_arm<ReviewedSourceTextPatch>(
            parsed, "Valid SHA-1 patch did not parse");
    require(patch.hunks.size() == 1,
            "Valid SHA-1 patch hunk count drifted");
    require(patch.hunks[0].lines.size() == 2 &&
                    patch.hunks[0].lines[0].kind ==
                            ReviewedSourcePatchLineKind::Removed &&
                    patch.hunks[0].lines[1].kind ==
                            ReviewedSourcePatchLineKind::Added,
            "Valid SHA-1 patch line kinds drifted");

    std::string with_hunk_context = output;
    const std::string header = "@@ -1 +1 @@";
    const std::size_t header_position = with_hunk_context.find(header);
    require(header_position != std::string::npos,
            "Patch fixture hunk header was missing");
    with_hunk_context.insert(
            header_position + header.size(), " untrusted context");
    require(std::holds_alternative<ReviewedSourceTextPatch>(
                    parse_and_verify_reviewed_source_patch(
                            with_hunk_context, old_oid, new_oid,
                            "old\n", "new\n")),
            "Git hunk-header context was not ignored after replay proof");
}

void test_sha256_patch() {
    const auto parsed = parse_and_verify_reviewed_source_patch(
            one_line_patch(SHA256_OLD, SHA256_NEW, "before", "after"),
            ReviewedSourceObjectId::make(SHA256_OLD),
            ReviewedSourceObjectId::make(SHA256_NEW),
            "before\n", "after\n");
    require(std::holds_alternative<ReviewedSourceTextPatch>(parsed),
            "Valid SHA-256 patch did not parse");
}

void test_missing_final_newline() {
    const auto parsed = parse_and_verify_reviewed_source_patch(
            one_line_patch(
                    SHA1_OLD, SHA1_NEW, "old", "new", false, false),
            ReviewedSourceObjectId::make(SHA1_OLD),
            ReviewedSourceObjectId::make(SHA1_NEW),
            "old", "new");
    const auto& patch = require_arm<ReviewedSourceTextPatch>(
            parsed, "No-final-newline patch did not replay");
    require(!patch.hunks[0].lines[0].line.has_newline &&
                    !patch.hunks[0].lines[1].line.has_newline,
            "No-final-newline markers were lost");
}

void test_logical_line_and_noop_rejection() {
    const ReviewedSourceObjectId old_oid =
            ReviewedSourceObjectId::make(SHA1_OLD);
    const ReviewedSourceObjectId new_oid =
            ReviewedSourceObjectId::make(SHA1_NEW);

    std::string split_added_line = patch_header(SHA1_OLD, SHA1_NEW);
    split_added_line +=
            "@@ -0,0 +1,2 @@\n"
            "+foo\n"
            "\\ No newline at end of file\n"
            "+bar\n";
    require(std::holds_alternative<ReviewedSourcePatchFailure>(
                    parse_and_verify_reviewed_source_patch(
                            split_added_line, old_oid, new_oid,
                            "", "foobar\n")),
            "One new logical line was accepted as two Added lines");

    std::string empty_hunk = patch_header(SHA1_OLD, SHA1_NEW);
    empty_hunk +=
            "@@ -0,0 +0,0 @@\n"
            "@@ -1 +1 @@\n"
            "-old\n"
            "+new\n";
    require(std::holds_alternative<ReviewedSourcePatchFailure>(
                    parse_and_verify_reviewed_source_patch(
                            empty_hunk, old_oid, new_oid,
                            "old\n", "new\n")),
            "Empty no-op hunk before a valid hunk was accepted");

    std::string context_only = patch_header(SHA1_OLD, SHA1_NEW);
    context_only += "@@ -1 +1 @@\n same\n";
    require(std::holds_alternative<ReviewedSourcePatchFailure>(
                    parse_and_verify_reviewed_source_patch(
                            context_only, old_oid, new_oid,
                            "same\n", "same\n")),
            "Context-only no-op hunk was accepted");

    std::string duplicate_insertion_anchor =
            patch_header(SHA1_OLD, SHA1_NEW);
    duplicate_insertion_anchor +=
            "@@ -0,0 +1 @@\n"
            "+first\n"
            "@@ -0,0 +2 @@\n"
            "+second\n";
    require(std::holds_alternative<ReviewedSourcePatchFailure>(
                    parse_and_verify_reviewed_source_patch(
                            duplicate_insertion_anchor,
                            old_oid, new_oid,
                            "", "first\nsecond\n")),
            "Duplicate old-side zero-width hunk was accepted");

    std::string duplicate_deletion_anchor =
            patch_header(SHA1_OLD, SHA1_NEW);
    duplicate_deletion_anchor +=
            "@@ -1 +0,0 @@\n"
            "-first\n"
            "@@ -2 +0,0 @@\n"
            "-second\n";
    require(std::holds_alternative<ReviewedSourcePatchFailure>(
                    parse_and_verify_reviewed_source_patch(
                            duplicate_deletion_anchor,
                            old_oid, new_oid,
                            "first\nsecond\n", "")),
            "Duplicate new-side zero-width hunk was accepted");
}

void test_empty_file_transitions_and_multi_hunk() {
    const ReviewedSourceObjectId old_oid =
            ReviewedSourceObjectId::make(SHA1_OLD);
    const ReviewedSourceObjectId new_oid =
            ReviewedSourceObjectId::make(SHA1_NEW);

    std::string from_empty = patch_header(SHA1_OLD, SHA1_NEW);
    from_empty += "@@ -0,0 +1 @@\n+new\n";
    require(std::holds_alternative<ReviewedSourceTextPatch>(
                    parse_and_verify_reviewed_source_patch(
                            from_empty, old_oid, new_oid,
                            "", "new\n")),
            "Valid empty-file addition was rejected");

    std::string to_empty = patch_header(SHA1_OLD, SHA1_NEW);
    to_empty += "@@ -1 +0,0 @@\n-old\n";
    require(std::holds_alternative<ReviewedSourceTextPatch>(
                    parse_and_verify_reviewed_source_patch(
                            to_empty, old_oid, new_oid,
                            "old\n", "")),
            "Valid empty-file deletion was rejected");

    std::string multi_hunk = patch_header(SHA1_OLD, SHA1_NEW);
    multi_hunk +=
            "@@ -2 +2 @@\n"
            "-old-a\n"
            "+new-a\n"
            "@@ -5 +5 @@\n"
            "-old-b\n"
            "+new-b\n";
    require(std::holds_alternative<ReviewedSourceTextPatch>(
                    parse_and_verify_reviewed_source_patch(
                            multi_hunk, old_oid, new_oid,
                            "one\nold-a\nthree\nfour\nold-b\nsix\n",
                            "one\nnew-a\nthree\nfour\nnew-b\nsix\n")),
            "Valid ordered multi-hunk patch was rejected");
}

void test_malformed_no_newline_marker_placement() {
    const ReviewedSourceObjectId old_oid =
            ReviewedSourceObjectId::make(SHA1_OLD);
    const ReviewedSourceObjectId new_oid =
            ReviewedSourceObjectId::make(SHA1_NEW);
    std::string marker_before_line = patch_header(SHA1_OLD, SHA1_NEW);
    marker_before_line +=
            "@@ -1 +1 @@\n"
            "\\ No newline at end of file\n"
            "-old\n"
            "+new\n";
    require(std::holds_alternative<ReviewedSourcePatchFailure>(
                    parse_and_verify_reviewed_source_patch(
                            marker_before_line, old_oid, new_oid,
                            "old\n", "new\n")),
            "No-newline marker before a logical line was accepted");
}

void test_malformed_and_replay_failures() {
    const ReviewedSourceObjectId old_oid =
            ReviewedSourceObjectId::make(SHA1_OLD);
    const ReviewedSourceObjectId new_oid =
            ReviewedSourceObjectId::make(SHA1_NEW);

    const auto wrong_header = parse_and_verify_reviewed_source_patch(
            one_line_patch(SHA1_NEW, SHA1_OLD, "old", "new"),
            old_oid, new_oid, "old\n", "new\n");
    require(require_arm<ReviewedSourcePatchFailure>(
                    wrong_header, "Wrong OID header was accepted")
                            .reason ==
                    ReviewedSourcePatchFailureReason::MalformedPatchOutput,
            "Wrong OID header failure reason drifted");

    std::string malformed = patch_header(SHA1_OLD, SHA1_NEW);
    malformed += "@@ -1,2 +1 @@\n-old\n+new\n";
    require(require_arm<ReviewedSourcePatchFailure>(
                    parse_and_verify_reviewed_source_patch(
                            malformed, old_oid, new_oid,
                            "old\n", "new\n"),
                    "Malformed hunk count was accepted")
                            .reason ==
                    ReviewedSourcePatchFailureReason::MalformedPatchOutput,
            "Malformed hunk failure reason drifted");

    const auto replay_mismatch = parse_and_verify_reviewed_source_patch(
            one_line_patch(SHA1_OLD, SHA1_NEW, "old", "other"),
            old_oid, new_oid, "old\n", "new\n");
    require(require_arm<ReviewedSourcePatchFailure>(
                    replay_mismatch, "Replay mismatch was accepted")
                            .reason ==
                    ReviewedSourcePatchFailureReason::ReplayMismatch,
            "Replay mismatch failure reason drifted");

    std::string embedded_nul = one_line_patch(
            SHA1_OLD, SHA1_NEW, "old", "new");
    embedded_nul.push_back('\0');
    require(require_arm<ReviewedSourcePatchFailure>(
                    parse_and_verify_reviewed_source_patch(
                            embedded_nul, old_oid, new_oid,
                            "old\n", "new\n"),
                    "NUL-containing raw patch was accepted")
                            .reason ==
                    ReviewedSourcePatchFailureReason::MalformedPatchOutput,
            "NUL-containing patch failure reason drifted");
}

void test_patch_and_line_resource_boundaries() {
    const ReviewedSourceObjectId old_oid =
            ReviewedSourceObjectId::make(SHA1_OLD);
    const ReviewedSourceObjectId new_oid =
            ReviewedSourceObjectId::make(SHA1_NEW);

    std::string exact_patch(REVIEWED_SOURCE_SINGLE_RAW_PATCH_LIMIT, 'x');
    const auto exact_result = parse_and_verify_reviewed_source_patch(
            exact_patch, old_oid, new_oid, "", "");
    require(require_arm<ReviewedSourcePatchFailure>(
                    exact_result, "Exact-limit malformed patch vanished")
                            .reason ==
                    ReviewedSourcePatchFailureReason::MalformedPatchOutput,
            "Exact patch limit was rejected as resource overflow");

    exact_patch.push_back('x');
    const auto oversized_result = parse_and_verify_reviewed_source_patch(
            exact_patch, old_oid, new_oid, "", "");
    require(require_arm<ReviewedSourcePatchFailure>(
                    oversized_result, "One-over patch limit was accepted")
                            .reason ==
                    ReviewedSourcePatchFailureReason::RawPatchLimitExceeded,
            "One-over patch failure reason drifted");

    const std::string exact_line(REVIEWED_SOURCE_LOGICAL_LINE_LIMIT, 'a');
    const auto exact_line_result = parse_and_verify_reviewed_source_patch(
            one_line_patch(
                    SHA1_OLD, SHA1_NEW, exact_line, exact_line),
            old_oid, new_oid, exact_line + "\n", exact_line + "\n");
    require(std::holds_alternative<ReviewedSourceTextPatch>(exact_line_result),
            "Exact logical line limit was rejected");

    const std::string oversized_line(
            REVIEWED_SOURCE_LOGICAL_LINE_LIMIT + 1, 'a');
    const auto oversized_line_result = parse_and_verify_reviewed_source_patch(
            one_line_patch(
                    SHA1_OLD, SHA1_NEW, oversized_line, oversized_line),
            old_oid, new_oid,
            oversized_line + "\n", oversized_line + "\n");
    require(require_arm<ReviewedSourcePatchFailure>(
                    oversized_line_result,
                    "One-over logical line limit was accepted")
                            .reason ==
                    ReviewedSourcePatchFailureReason::
                            LogicalLineLimitExceeded,
            "One-over logical line failure reason drifted");
}

void run_tests() {
    test_sha1_patch_and_replay();
    test_sha256_patch();
    test_missing_final_newline();
    test_logical_line_and_noop_rejection();
    test_empty_file_transitions_and_multi_hunk();
    test_malformed_no_newline_marker_placement();
    test_malformed_and_replay_failures();
    test_patch_and_line_resource_boundaries();
}

} // namespace

int main() {
    try {
        run_tests();
    } catch(const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "reviewed source patch tests: all checks passed\n";
    return 0;
}
