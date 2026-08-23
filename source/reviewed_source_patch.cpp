#include "reviewed_source_patch.hpp"

#include <charconv>
#include <limits>
#include <optional>
#include <string>
#include <utility>

namespace {

constexpr std::string_view NO_NEWLINE_MARKER =
        "\\ No newline at end of file";

ReviewedSourcePatchFailure patch_failure(
        ReviewedSourcePatchFailureReason reason,
        std::size_t hunk_index = 0,
        std::uintmax_t observed = 0,
        std::uintmax_t limit = 0) {
    return ReviewedSourcePatchFailure{
            reason, hunk_index, observed, limit};
}

std::optional<std::string_view> take_line(
        std::string_view output, std::size_t& offset) {
    if(offset >= output.size()) return std::nullopt;
    const std::size_t end = output.find('\n', offset);
    if(end == std::string_view::npos) return std::nullopt;
    const std::string_view line = output.substr(offset, end - offset);
    offset = end + 1;
    return line;
}

bool take_exact_line(
        std::string_view output,
        std::size_t& offset,
        std::string_view expected) {
    const auto line = take_line(output, offset);
    return line.has_value() && *line == expected;
}

bool parse_size(std::string_view value, std::size_t& result) {
    if(value.empty()) return false;
    const auto [end, error] = std::from_chars(
            value.data(), value.data() + value.size(), result);
    return error == std::errc{} && end == value.data() + value.size();
}

bool parse_range(
        std::string_view value, std::size_t& start, std::size_t& count) {
    const std::size_t comma = value.find(',');
    if(comma == std::string_view::npos) {
        count = 1;
        return parse_size(value, start);
    }
    return value.find(',', comma + 1) == std::string_view::npos &&
           parse_size(value.substr(0, comma), start) &&
           parse_size(value.substr(comma + 1), count);
}

bool parse_hunk_header(
        std::string_view line, ReviewedSourcePatchHunk& hunk) {
    constexpr std::string_view prefix = "@@ -";
    constexpr std::string_view close = " @@";
    if(!line.starts_with(prefix)) return false;
    const std::size_t close_position = line.find(close, prefix.size());
    if(close_position == std::string_view::npos ||
       (close_position + close.size() < line.size() &&
        line[close_position + close.size()] != ' ')) {
        return false;
    }

    const std::string_view ranges = line.substr(
            prefix.size(), close_position - prefix.size());
    const std::size_t separator = ranges.find(" +");
    if(separator == std::string_view::npos ||
       ranges.find(" +", separator + 2) != std::string_view::npos) {
        return false;
    }
    return parse_range(
                   ranges.substr(0, separator),
                   hunk.old_start, hunk.old_count) &&
           parse_range(
                   ranges.substr(separator + 2),
                   hunk.new_start, hunk.new_count);
}

std::vector<ReviewedSourceTextLine> split_blob_lines(std::string_view blob) {
    std::vector<ReviewedSourceTextLine> lines;
    std::size_t offset = 0;
    while(offset < blob.size()) {
        const std::size_t end = blob.find('\n', offset);
        if(end == std::string_view::npos) {
            lines.push_back(ReviewedSourceTextLine{
                    std::string(blob.substr(offset)), false});
            break;
        }
        lines.push_back(ReviewedSourceTextLine{
                std::string(blob.substr(offset, end - offset)), true});
        offset = end + 1;
    }
    return lines;
}

void append_line(std::string& output, const ReviewedSourceTextLine& line) {
    output += line.bytes;
    if(line.has_newline) output.push_back('\n');
}

std::optional<std::size_t> hunk_position(
        std::size_t start, std::size_t count) {
    if(count == 0) return start;
    if(start == 0) return std::nullopt;
    return start - 1;
}

bool line_matches(
        const ReviewedSourceTextLine& expected,
        const ReviewedSourceTextLine& actual) {
    return expected == actual;
}

bool apply_no_newline_marker(
        ReviewedSourcePatchHunk& hunk,
        bool& old_side_closed,
        bool& new_side_closed) {
    if(hunk.lines.empty() || !hunk.lines.back().line.has_newline) {
        return false;
    }
    hunk.lines.back().line.has_newline = false;
    switch(hunk.lines.back().kind) {
    case ReviewedSourcePatchLineKind::Context:
        old_side_closed = true;
        new_side_closed = true;
        break;
    case ReviewedSourcePatchLineKind::Removed:
        old_side_closed = true;
        break;
    case ReviewedSourcePatchLineKind::Added:
        new_side_closed = true;
        break;
    }
    return true;
}

bool replay_patch_lines(
        const std::vector<ReviewedSourcePatchHunk>& hunks,
        const std::vector<ReviewedSourceTextLine>& old_lines,
        const std::vector<ReviewedSourceTextLine>& new_lines,
        std::string* replayed) {
    std::size_t old_index = 0;
    std::size_t new_index = 0;

    for(const ReviewedSourcePatchHunk& hunk : hunks) {
        const auto old_position = hunk_position(hunk.old_start, hunk.old_count);
        const auto new_position = hunk_position(hunk.new_start, hunk.new_count);
        if(!old_position.has_value() || !new_position.has_value() ||
           *old_position < old_index || *old_position > old_lines.size()) {
            return false;
        }

        while(old_index < *old_position) {
            if(new_index >= new_lines.size() ||
               !line_matches(old_lines[old_index], new_lines[new_index])) {
                return false;
            }
            if(replayed != nullptr) {
                append_line(*replayed, old_lines[old_index]);
            }
            ++old_index;
            ++new_index;
        }
        if(new_index != *new_position) return false;

        std::size_t observed_old_count = 0;
        std::size_t observed_new_count = 0;
        for(const ReviewedSourcePatchLine& patch_line : hunk.lines) {
            switch(patch_line.kind) {
            case ReviewedSourcePatchLineKind::Context:
                if(old_index >= old_lines.size() ||
                   new_index >= new_lines.size() ||
                   !line_matches(old_lines[old_index], patch_line.line) ||
                   !line_matches(new_lines[new_index], patch_line.line)) {
                    return false;
                }
                if(replayed != nullptr) append_line(*replayed, patch_line.line);
                ++old_index;
                ++new_index;
                ++observed_old_count;
                ++observed_new_count;
                break;
            case ReviewedSourcePatchLineKind::Removed:
                if(old_index >= old_lines.size() ||
                   !line_matches(old_lines[old_index], patch_line.line)) {
                    return false;
                }
                ++old_index;
                ++observed_old_count;
                break;
            case ReviewedSourcePatchLineKind::Added:
                if(new_index >= new_lines.size() ||
                   !line_matches(new_lines[new_index], patch_line.line)) {
                    return false;
                }
                if(replayed != nullptr) append_line(*replayed, patch_line.line);
                ++new_index;
                ++observed_new_count;
                break;
            }
        }
        if(observed_old_count != hunk.old_count ||
           observed_new_count != hunk.new_count) {
            return false;
        }
    }

    while(old_index < old_lines.size()) {
        if(new_index >= new_lines.size() ||
           !line_matches(old_lines[old_index], new_lines[new_index])) {
            return false;
        }
        if(replayed != nullptr) append_line(*replayed, old_lines[old_index]);
        ++old_index;
        ++new_index;
    }
    return new_index == new_lines.size();
}

bool replay_patch(
        const std::vector<ReviewedSourcePatchHunk>& hunks,
        std::string_view old_blob,
        std::string_view new_blob) {
    const std::vector<ReviewedSourceTextLine> old_lines =
            split_blob_lines(old_blob);
    const std::vector<ReviewedSourceTextLine> new_lines =
            split_blob_lines(new_blob);
    std::string replayed;
    replayed.reserve(new_blob.size());
    return replay_patch_lines(hunks, old_lines, new_lines, &replayed) &&
           replayed == new_blob;
}

} // namespace

bool reviewed_source_text_patch_replays(
        const ReviewedSourceTextPatch& patch,
        const ReviewedSourceTextContent& old_content,
        const ReviewedSourceTextContent& new_content) {
    return !patch.hunks.empty() &&
           replay_patch_lines(
                   patch.hunks, old_content.lines, new_content.lines, nullptr);
}

ReviewedSourcePatchParseResult parse_and_verify_reviewed_source_patch(
        std::string_view patch_output,
        const ReviewedSourceObjectId& old_object_id,
        const ReviewedSourceObjectId& new_object_id,
        std::string_view old_blob,
        std::string_view new_blob) {
    if(patch_output.size() > REVIEWED_SOURCE_SINGLE_RAW_PATCH_LIMIT) {
        return patch_failure(
                ReviewedSourcePatchFailureReason::RawPatchLimitExceeded,
                0, patch_output.size(),
                REVIEWED_SOURCE_SINGLE_RAW_PATCH_LIMIT);
    }
    if(patch_output.empty() || patch_output.find('\0') != std::string_view::npos) {
        return patch_failure(
                ReviewedSourcePatchFailureReason::MalformedPatchOutput);
    }

    const std::string& old_oid = old_object_id.value();
    const std::string& new_oid = new_object_id.value();
    std::size_t offset = 0;
    if(!take_exact_line(
               patch_output, offset,
               "diff --git " + old_oid + " " + new_oid) ||
       !take_exact_line(
               patch_output, offset,
               "index " + old_oid + ".." + new_oid + " 100644") ||
       !take_exact_line(patch_output, offset, "--- " + old_oid) ||
       !take_exact_line(patch_output, offset, "+++ " + new_oid)) {
        return patch_failure(
                ReviewedSourcePatchFailureReason::MalformedPatchOutput);
    }

    std::vector<ReviewedSourcePatchHunk> hunks;
    std::optional<std::size_t> previous_zero_width_old_position;
    std::optional<std::size_t> previous_zero_width_new_position;
    bool old_side_closed = false;
    bool new_side_closed = false;
    while(offset < patch_output.size()) {
        const std::size_t hunk_index = hunks.size();
        const auto header = take_line(patch_output, offset);
        ReviewedSourcePatchHunk hunk;
        if(!header.has_value() || !parse_hunk_header(*header, hunk)) {
            return patch_failure(
                    ReviewedSourcePatchFailureReason::MalformedPatchOutput,
                    hunk_index);
        }

        std::size_t observed_old = 0;
        std::size_t observed_new = 0;
        bool can_mark_no_newline = false;
        bool has_change = false;
        while(observed_old < hunk.old_count ||
              observed_new < hunk.new_count) {
            const auto line = take_line(patch_output, offset);
            if(!line.has_value()) {
                return patch_failure(
                        ReviewedSourcePatchFailureReason::MalformedPatchOutput,
                        hunk_index);
            }
            if(*line == NO_NEWLINE_MARKER) {
                if(!can_mark_no_newline ||
                   !apply_no_newline_marker(
                           hunk, old_side_closed, new_side_closed)) {
                    return patch_failure(
                            ReviewedSourcePatchFailureReason::MalformedPatchOutput,
                            hunk_index);
                }
                can_mark_no_newline = false;
                continue;
            }
            if(line->empty()) {
                return patch_failure(
                        ReviewedSourcePatchFailureReason::MalformedPatchOutput,
                        hunk_index);
            }

            ReviewedSourcePatchLineKind kind;
            switch(line->front()) {
            case ' ':
                kind = ReviewedSourcePatchLineKind::Context;
                if(old_side_closed || new_side_closed) {
                    return patch_failure(
                            ReviewedSourcePatchFailureReason::
                                    MalformedPatchOutput,
                            hunk_index);
                }
                ++observed_old;
                ++observed_new;
                break;
            case '-':
                kind = ReviewedSourcePatchLineKind::Removed;
                if(old_side_closed) {
                    return patch_failure(
                            ReviewedSourcePatchFailureReason::
                                    MalformedPatchOutput,
                            hunk_index);
                }
                ++observed_old;
                has_change = true;
                break;
            case '+':
                kind = ReviewedSourcePatchLineKind::Added;
                if(new_side_closed) {
                    return patch_failure(
                            ReviewedSourcePatchFailureReason::
                                    MalformedPatchOutput,
                            hunk_index);
                }
                ++observed_new;
                has_change = true;
                break;
            default:
                return patch_failure(
                        ReviewedSourcePatchFailureReason::MalformedPatchOutput,
                        hunk_index);
            }
            if(observed_old > hunk.old_count ||
               observed_new > hunk.new_count) {
                return patch_failure(
                        ReviewedSourcePatchFailureReason::MalformedPatchOutput,
                        hunk_index);
            }

            const std::string_view content = line->substr(1);
            if(content.size() > REVIEWED_SOURCE_LOGICAL_LINE_LIMIT) {
                return patch_failure(
                        ReviewedSourcePatchFailureReason::
                                LogicalLineLimitExceeded,
                        hunk_index, content.size(),
                        REVIEWED_SOURCE_LOGICAL_LINE_LIMIT);
            }
            hunk.lines.push_back(ReviewedSourcePatchLine{
                    kind, ReviewedSourceTextLine{std::string(content), true}});
            can_mark_no_newline = true;
        }

        if(offset < patch_output.size()) {
            const std::size_t marker_offset = offset;
            const auto possible_marker = take_line(patch_output, offset);
            if(possible_marker == std::optional<std::string_view>(
                                              NO_NEWLINE_MARKER)) {
                if(!can_mark_no_newline ||
                   !apply_no_newline_marker(
                           hunk, old_side_closed, new_side_closed)) {
                    return patch_failure(
                            ReviewedSourcePatchFailureReason::MalformedPatchOutput,
                            hunk_index);
                }
            } else {
                offset = marker_offset;
            }
        }
        if(hunk.lines.empty() || !has_change) {
            return patch_failure(
                    ReviewedSourcePatchFailureReason::MalformedPatchOutput,
                    hunk_index);
        }
        const auto old_position = hunk_position(hunk.old_start, hunk.old_count);
        const auto new_position = hunk_position(hunk.new_start, hunk.new_count);
        if(!old_position.has_value() || !new_position.has_value()) {
            return patch_failure(
                    ReviewedSourcePatchFailureReason::MalformedPatchOutput,
                    hunk_index);
        }
        if(hunk.old_count == 0) {
            if(previous_zero_width_old_position == old_position) {
                return patch_failure(
                        ReviewedSourcePatchFailureReason::MalformedPatchOutput,
                        hunk_index);
            }
            previous_zero_width_old_position = old_position;
        }
        if(hunk.new_count == 0) {
            if(previous_zero_width_new_position == new_position) {
                return patch_failure(
                        ReviewedSourcePatchFailureReason::MalformedPatchOutput,
                        hunk_index);
            }
            previous_zero_width_new_position = new_position;
        }
        hunks.push_back(std::move(hunk));
    }

    if(hunks.empty()) {
        return patch_failure(
                ReviewedSourcePatchFailureReason::MalformedPatchOutput);
    }
    if(!replay_patch(hunks, old_blob, new_blob)) {
        return patch_failure(
                ReviewedSourcePatchFailureReason::ReplayMismatch);
    }
    return ReviewedSourceTextPatch{
            old_object_id, new_object_id, std::move(hunks)};
}
