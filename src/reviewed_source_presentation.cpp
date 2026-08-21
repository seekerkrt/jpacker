#include "reviewed_source_presentation.hpp"

#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace {

using SizeCheckResult = std::variant<
        std::uintmax_t,
        ReviewedSourcePresentationFailure>;

ReviewedSourcePresentationFailure inconsistent_failure(
        std::size_t entry_index) {
    return ReviewedSourcePresentationFailure{
            ReviewedSourcePresentationFailureReason::
                    InconsistentMaterializedReview,
            entry_index, 0, 0};
}

SizeCheckResult checked_rendered_output_size(
        std::uintmax_t current,
        std::uintmax_t additional,
        std::uintmax_t limit,
        std::size_t entry_index) {
    const bool overflow = additional >
            std::numeric_limits<std::uintmax_t>::max() - current;
    const std::uintmax_t observed = overflow
            ? std::numeric_limits<std::uintmax_t>::max()
            : current + additional;
    if(overflow || observed > limit) {
        return ReviewedSourcePresentationFailure{
                ReviewedSourcePresentationFailureReason::
                        RenderedOutputLimitExceeded,
                entry_index, observed, limit};
    }
    return observed;
}

class RenderState final {
public:
    explicit RenderState(std::uintmax_t limit) noexcept : limit_(limit) {}

    bool append(std::string_view value, std::size_t entry_index = 0) {
        if(failure_.has_value()) return false;
        const SizeCheckResult checked = checked_rendered_output_size(
                output_.size(), value.size(), limit_, entry_index);
        if(const auto* failure =
                   std::get_if<ReviewedSourcePresentationFailure>(&checked)) {
            failure_ = *failure;
            return false;
        }
        output_.append(value);
        return true;
    }

    bool append_number(
            std::uintmax_t value, std::size_t entry_index = 0) {
        return append(std::to_string(value), entry_index);
    }

    void fail_inconsistent(std::size_t entry_index) {
        if(!failure_.has_value()) {
            failure_ = inconsistent_failure(entry_index);
        }
    }

    [[nodiscard]] bool failed() const noexcept {
        return failure_.has_value();
    }

    ReviewedSourcePresentationResult finish() {
        if(failure_.has_value()) return *failure_;
        return ReviewedSourceRenderedPresentation{std::move(output_)};
    }

private:
    std::uintmax_t limit_;
    std::string output_;
    std::optional<ReviewedSourcePresentationFailure> failure_;
};

void append_escaped_byte(
        RenderState& state,
        unsigned char byte,
        std::size_t entry_index) {
    constexpr char HEX[] = "0123456789ABCDEF";
    const char escaped[4]{
            '\\', 'x', HEX[(byte >> 4) & 0x0f], HEX[byte & 0x0f]};
    state.append(std::string_view(escaped, sizeof(escaped)), entry_index);
}

bool is_utf8_continuation_byte(unsigned char byte) noexcept {
    return byte >= 0x80 && byte <= 0xbf;
}

bool decode_utf8_code_point(
        std::string_view value,
        std::size_t offset,
        char32_t& code_point,
        std::size_t& length) noexcept {
    const auto byte_at = [&value](std::size_t index) {
        return static_cast<unsigned char>(value[index]);
    };

    const unsigned char first = byte_at(offset);
    if(first <= 0x7f) {
        code_point = first;
        length = 1;
        return true;
    }
    if(first >= 0xc2 && first <= 0xdf) {
        if(offset + 1 >= value.size()) return false;
        const unsigned char second = byte_at(offset + 1);
        if(!is_utf8_continuation_byte(second)) return false;
        code_point =
                (static_cast<char32_t>(first & 0x1f) << 6) |
                static_cast<char32_t>(second & 0x3f);
        length = 2;
        return true;
    }
    if(first >= 0xe0 && first <= 0xef) {
        if(offset + 2 >= value.size()) return false;
        const unsigned char second = byte_at(offset + 1);
        const unsigned char third = byte_at(offset + 2);
        const bool valid_second =
                first == 0xe0 ? second >= 0xa0 && second <= 0xbf
                              : first == 0xed
                                      ? second >= 0x80 && second <= 0x9f
                                      : is_utf8_continuation_byte(second);
        if(!valid_second || !is_utf8_continuation_byte(third)) return false;
        code_point =
                (static_cast<char32_t>(first & 0x0f) << 12) |
                (static_cast<char32_t>(second & 0x3f) << 6) |
                static_cast<char32_t>(third & 0x3f);
        length = 3;
        return true;
    }
    if(first >= 0xf0 && first <= 0xf4) {
        if(offset + 3 >= value.size()) return false;
        const unsigned char second = byte_at(offset + 1);
        const unsigned char third = byte_at(offset + 2);
        const unsigned char fourth = byte_at(offset + 3);
        const bool valid_second =
                first == 0xf0 ? second >= 0x90 && second <= 0xbf
                              : first == 0xf4
                                      ? second >= 0x80 && second <= 0x8f
                                      : is_utf8_continuation_byte(second);
        if(!valid_second || !is_utf8_continuation_byte(third) ||
           !is_utf8_continuation_byte(fourth)) {
            return false;
        }
        code_point =
                (static_cast<char32_t>(first & 0x07) << 18) |
                (static_cast<char32_t>(second & 0x3f) << 12) |
                (static_cast<char32_t>(third & 0x3f) << 6) |
                static_cast<char32_t>(fourth & 0x3f);
        length = 4;
        return true;
    }
    return false;
}

bool is_bidi_control(char32_t code_point) noexcept {
    return code_point == 0x061c || code_point == 0x200e ||
           code_point == 0x200f ||
           (code_point >= 0x202a && code_point <= 0x202e) ||
           (code_point >= 0x2066 && code_point <= 0x2069);
}

bool is_terminal_safe_code_point(char32_t code_point) noexcept {
    return code_point >= 0x20 &&
           !(code_point >= 0x7f && code_point <= 0x9f) &&
           code_point != static_cast<char32_t>('\\') &&
           code_point != 0x2028 && code_point != 0x2029 &&
           code_point != 0xfeff && !is_bidi_control(code_point);
}

void append_safe_content(
        RenderState& state,
        std::string_view bytes,
        std::size_t entry_index) {
    std::size_t offset = 0;
    while(offset < bytes.size() && !state.failed()) {
        char32_t code_point = 0;
        std::size_t length = 0;
        if(!decode_utf8_code_point(bytes, offset, code_point, length)) {
            append_escaped_byte(
                    state,
                    static_cast<unsigned char>(bytes[offset]),
                    entry_index);
            ++offset;
            continue;
        }
        if(is_terminal_safe_code_point(code_point)) {
            state.append(bytes.substr(offset, length), entry_index);
        } else {
            for(std::size_t index = 0;
                index < length && !state.failed(); ++index) {
                append_escaped_byte(
                        state,
                        static_cast<unsigned char>(bytes[offset + index]),
                        entry_index);
            }
        }
        offset += length;
    }
}

std::optional<std::string_view> object_format_display(
        GitObjectFormat format) noexcept {
    switch(format) {
    case GitObjectFormat::Sha1:
        return "SHA-1";
    case GitObjectFormat::Sha256:
        return "SHA-256";
    }
    return std::nullopt;
}

std::optional<std::string_view> mode_display(
        ReviewedSourceFileMode mode) noexcept {
    switch(mode) {
    case ReviewedSourceFileMode::Regular:
        return "100644 (Regular)";
    case ReviewedSourceFileMode::Executable:
        return "100755 (Executable)";
    case ReviewedSourceFileMode::SymbolicLink:
        return "120000 (SymbolicLink)";
    case ReviewedSourceFileMode::Gitlink:
        return "160000 (Gitlink)";
    }
    return std::nullopt;
}

std::optional<std::string_view> classification_display(
        ReviewedSourceFileClassification classification) noexcept {
    switch(classification) {
    case ReviewedSourceFileClassification::TrackedSource:
        return "TrackedSource";
    case ReviewedSourceFileClassification::GeneratedMetadata:
        return "GeneratedMetadata";
    }
    return std::nullopt;
}

std::optional<std::string_view> observation_kind_display(
        ReviewedSourceBlobContentKind kind) noexcept {
    switch(kind) {
    case ReviewedSourceBlobContentKind::LineReviewable:
        return "LineReviewable";
    case ReviewedSourceBlobContentKind::ContainsNul:
        return "ContainsNul";
    case ReviewedSourceBlobContentKind::Gitlink:
        return "Gitlink";
    }
    return std::nullopt;
}

std::optional<std::string_view> representation_display(
        ReviewedSourceReviewRepresentation representation) noexcept {
    switch(representation) {
    case ReviewedSourceReviewRepresentation::CompleteTextPatch:
        return "CompleteTextPatch";
    case ReviewedSourceReviewRepresentation::CompleteFullText:
        return "CompleteFullText";
    case ReviewedSourceReviewRepresentation::NoContentChange:
        return "NoContentChange";
    case ReviewedSourceReviewRepresentation::ContainsNul:
        return "ContainsNul";
    case ReviewedSourceReviewRepresentation::GitlinkMetadata:
        return "GitlinkMetadata";
    case ReviewedSourceReviewRepresentation::MixedTextAndNonText:
        return "MixedTextAndNonText";
    }
    return std::nullopt;
}

std::optional<std::string_view> readiness_display(
        ReviewedSourceReviewReadiness readiness) noexcept {
    switch(readiness) {
    case ReviewedSourceReviewReadiness::Complete:
        return "Complete";
    case ReviewedSourceReviewReadiness::ManualInspectionRequired:
        return "ManualInspectionRequired";
    case ReviewedSourceReviewReadiness::SensitiveSourceUnrenderable:
        return "SensitiveSourceUnrenderable";
    }
    return std::nullopt;
}

std::optional<std::string_view> emphasis_display(
        ReviewedSourceReviewEmphasis emphasis) noexcept {
    switch(emphasis) {
    case ReviewedSourceReviewEmphasis::Ordinary:
        return "Ordinary";
    case ReviewedSourceReviewEmphasis::Sensitive:
        return "Sensitive";
    }
    return std::nullopt;
}

struct ChangeView {
    const ReviewedSourceFileVersion* old_version = nullptr;
    const ReviewedSourceFileVersion* new_version = nullptr;
    const ReviewedSourceContentChange* content = nullptr;
    std::string_view status;
    std::optional<std::uint8_t> rename_similarity;
};

ChangeView change_view(const ReviewedSourceFileChange& change) {
    return std::visit(
            [](const auto& value) -> ChangeView {
                using Change = std::decay_t<decltype(value)>;
                if constexpr(std::is_same_v<Change, ReviewedSourceAdded>) {
                    return ChangeView{
                            nullptr, &value.new_version, &value.content,
                            "Added", std::nullopt};
                } else if constexpr(std::is_same_v<
                                            Change,
                                            ReviewedSourceModified>) {
                    return ChangeView{
                            &value.old_version, &value.new_version,
                            &value.content, "Modified", std::nullopt};
                } else if constexpr(std::is_same_v<
                                            Change,
                                            ReviewedSourceDeleted>) {
                    return ChangeView{
                            &value.old_version, nullptr, &value.content,
                            "Deleted", std::nullopt};
                } else if constexpr(std::is_same_v<
                                            Change,
                                            ReviewedSourceRenamed>) {
                    return ChangeView{
                            &value.old_version, &value.new_version,
                            &value.content, "Renamed", value.similarity};
                } else {
                    return ChangeView{
                            &value.old_version, &value.new_version,
                            &value.content, "TypeChanged", std::nullopt};
                }
            },
            change);
}

bool append_mapped_value(
        RenderState& state,
        const std::optional<std::string_view>& value,
        std::size_t entry_index) {
    if(!value.has_value()) {
        state.fail_inconsistent(entry_index);
        return false;
    }
    return state.append(*value, entry_index);
}

void render_revision(
        RenderState& state,
        std::string_view label,
        const SourceRevisionIdentity& revision) {
    const GitObjectFormat* format = revision.git_object_format();
    const std::string* object_id = revision.git_commit();
    if(revision.state() != SourceRevisionState::Known || format == nullptr ||
       object_id == nullptr) {
        state.fail_inconsistent(0);
        return;
    }
    state.append(label);
    append_mapped_value(state, object_format_display(*format), 0);
    state.append(" ");
    state.append(*object_id);
    state.append("\n");
}

void render_version_fields(
        RenderState& state,
        std::string_view side,
        const ReviewedSourceFileVersion* version,
        std::size_t entry_index) {
    state.append("  ", entry_index);
    state.append(side, entry_index);
    state.append(" path: ", entry_index);
    if(version == nullptr) {
        state.append("none\n", entry_index);
    } else {
        state.append(version->path().escaped_display(), entry_index);
        state.append("\n", entry_index);
    }

    state.append("  ", entry_index);
    state.append(side, entry_index);
    state.append(" mode: ", entry_index);
    if(version == nullptr) {
        state.append("none\n", entry_index);
    } else {
        append_mapped_value(
                state, mode_display(version->mode()), entry_index);
        state.append("\n", entry_index);
    }

    state.append("  ", entry_index);
    state.append(side, entry_index);
    state.append(" object: ", entry_index);
    if(version == nullptr) {
        state.append("none\n", entry_index);
    } else {
        append_mapped_value(
                state,
                object_format_display(version->object_id().format()),
                entry_index);
        state.append(" ", entry_index);
        state.append(version->object_id().value(), entry_index);
        state.append("\n", entry_index);
    }

    state.append("  ", entry_index);
    state.append(side, entry_index);
    state.append(" size: ", entry_index);
    if(version == nullptr) {
        state.append("none\n", entry_index);
    } else if(!version->blob_size().has_value()) {
        state.append("unavailable\n", entry_index);
    } else {
        state.append_number(*version->blob_size(), entry_index);
        state.append(" bytes\n", entry_index);
    }

    state.append("  ", entry_index);
    state.append(side, entry_index);
    state.append(" classification: ", entry_index);
    if(version == nullptr) {
        state.append("none\n", entry_index);
    } else {
        append_mapped_value(
                state,
                classification_display(version->classification()),
                entry_index);
        state.append("\n", entry_index);
    }
}

void render_observation_kind(
        RenderState& state,
        std::string_view side,
        const std::optional<ReviewedSourceBlobObservation>& observation,
        std::size_t entry_index) {
    state.append("  ", entry_index);
    state.append(side, entry_index);
    state.append(" content kind: ", entry_index);
    if(!observation.has_value()) {
        state.append("none\n", entry_index);
        return;
    }
    append_mapped_value(
            state, observation_kind_display(observation->kind), entry_index);
    state.append("\n", entry_index);
}

void render_git_marker(
        RenderState& state,
        const ReviewedSourceContentChange& content,
        std::size_t entry_index) {
    state.append("  Git marker: ", entry_index);
    std::visit(
            [&state, entry_index](const auto& value) {
                using Content = std::decay_t<decltype(value)>;
                if constexpr(std::is_same_v<
                                     Content,
                                     ReviewedSourceTextChange>) {
                    state.append("Text (added lines: ", entry_index);
                    state.append_number(value.added_lines, entry_index);
                    state.append("; deleted lines: ", entry_index);
                    state.append_number(value.deleted_lines, entry_index);
                    state.append(")", entry_index);
                } else {
                    state.append("Binary", entry_index);
                }
            },
            content);
    state.append("\n", entry_index);
}

bool has_generated_metadata(const ChangeView& view) noexcept {
    return (view.old_version != nullptr &&
            view.old_version->classification() ==
                    ReviewedSourceFileClassification::GeneratedMetadata) ||
           (view.new_version != nullptr &&
            view.new_version->classification() ==
                    ReviewedSourceFileClassification::GeneratedMetadata);
}

void render_status_summary(
        RenderState& state,
        const ChangeView& view,
        std::size_t entry_index) {
    state.append("  status: ", entry_index);
    state.append(view.status, entry_index);
    if(view.status == "Renamed" && view.old_version != nullptr &&
       view.new_version != nullptr) {
        state.append(" ", entry_index);
        state.append(
                view.old_version->path().escaped_display(), entry_index);
        state.append(" -> ", entry_index);
        state.append(
                view.new_version->path().escaped_display(), entry_index);
    } else if(view.status == "TypeChanged" &&
              view.old_version != nullptr && view.new_version != nullptr) {
        state.append(" ", entry_index);
        append_mapped_value(
                state, mode_display(view.old_version->mode()), entry_index);
        state.append(" -> ", entry_index);
        append_mapped_value(
                state, mode_display(view.new_version->mode()), entry_index);
    }
    state.append("\n", entry_index);
}

void render_readiness_diagnostic(
        RenderState& state,
        ReviewedSourceReviewReadiness readiness,
        std::size_t entry_index) {
    state.append("  readiness diagnostic: ", entry_index);
    switch(readiness) {
    case ReviewedSourceReviewReadiness::Complete:
        state.append(
                "complete terminal-safe content presentation\n",
                entry_index);
        return;
    case ReviewedSourceReviewReadiness::ManualInspectionRequired:
        state.append(
                "WARNING: manual inspection required; metadata is not a complete content review\n",
                entry_index);
        return;
    case ReviewedSourceReviewReadiness::SensitiveSourceUnrenderable:
        state.append(
                "WARNING: sensitive source content is unrenderable; metadata does not complete review\n",
                entry_index);
        return;
    }
    state.fail_inconsistent(entry_index);
}

const ReviewedSourceTextContent* line_content(
        RenderState& state,
        const std::optional<ReviewedSourceBlobObservation>& observation,
        std::size_t entry_index) {
    if(!observation.has_value() ||
       observation->kind != ReviewedSourceBlobContentKind::LineReviewable) {
        return nullptr;
    }
    if(!observation->text) {
        state.fail_inconsistent(entry_index);
        return nullptr;
    }
    return observation->text.get();
}

void render_text_content(
        RenderState& state,
        const ReviewedSourceTextContent& content,
        char prefix,
        std::size_t entry_index) {
    if(content.lines.empty()) {
        state.append("    (empty)\n", entry_index);
        return;
    }
    for(const ReviewedSourceTextLine& line : content.lines) {
        if(state.failed()) return;
        state.append("    ", entry_index);
        const char prefix_text[1]{prefix};
        state.append(
                std::string_view(prefix_text, sizeof(prefix_text)),
                entry_index);
        append_safe_content(state, line.bytes, entry_index);
        state.append("\n", entry_index);
        if(!line.has_newline) {
            state.append(
                    "    \\ No newline at end of file\n",
                    entry_index);
        }
    }
}

void render_complete_full_text(
        RenderState& state,
        const ReviewedSourceReviewEntry& entry,
        std::size_t entry_index) {
    const ReviewedSourceTextContent* old_text =
            line_content(state, entry.old_observation, entry_index);
    const ReviewedSourceTextContent* new_text =
            line_content(state, entry.new_observation, entry_index);
    if(state.failed()) return;
    const std::size_t content_count =
            static_cast<std::size_t>(old_text != nullptr) +
            static_cast<std::size_t>(new_text != nullptr);
    if(content_count != 1) {
        state.fail_inconsistent(entry_index);
        return;
    }
    if(old_text != nullptr) {
        state.append("  old full text:\n", entry_index);
        render_text_content(state, *old_text, '-', entry_index);
    } else {
        state.append("  new full text:\n", entry_index);
        render_text_content(state, *new_text, '+', entry_index);
    }
}

std::optional<char> patch_line_prefix(
        ReviewedSourcePatchLineKind kind) noexcept {
    switch(kind) {
    case ReviewedSourcePatchLineKind::Context:
        return ' ';
    case ReviewedSourcePatchLineKind::Removed:
        return '-';
    case ReviewedSourcePatchLineKind::Added:
        return '+';
    }
    return std::nullopt;
}

void render_patch(
        RenderState& state,
        const ReviewedSourceReviewEntry& entry,
        const ChangeView& view,
        std::size_t entry_index) {
    if(!entry.patch.has_value() || entry.patch->hunks.empty() ||
       view.old_version == nullptr || view.new_version == nullptr ||
       entry.patch->old_object_id != view.old_version->object_id() ||
       entry.patch->new_object_id != view.new_version->object_id()) {
        state.fail_inconsistent(entry_index);
        return;
    }
    state.append("  text patch:\n", entry_index);
    for(const ReviewedSourcePatchHunk& hunk : entry.patch->hunks) {
        if(state.failed()) return;
        state.append("    @@ -", entry_index);
        state.append_number(hunk.old_start, entry_index);
        state.append(",", entry_index);
        state.append_number(hunk.old_count, entry_index);
        state.append(" +", entry_index);
        state.append_number(hunk.new_start, entry_index);
        state.append(",", entry_index);
        state.append_number(hunk.new_count, entry_index);
        state.append(" @@\n", entry_index);
        for(const ReviewedSourcePatchLine& line : hunk.lines) {
            const auto prefix = patch_line_prefix(line.kind);
            if(!prefix.has_value()) {
                state.fail_inconsistent(entry_index);
                return;
            }
            state.append("    ", entry_index);
            const char prefix_text[1]{*prefix};
            state.append(
                    std::string_view(prefix_text, sizeof(prefix_text)),
                    entry_index);
            append_safe_content(state, line.line.bytes, entry_index);
            state.append("\n", entry_index);
            if(!line.line.has_newline) {
                state.append(
                        "    \\ No newline at end of file\n",
                        entry_index);
            }
        }
    }
}

void render_no_content_change(
        RenderState& state,
        const ChangeView& view,
        std::size_t entry_index) {
    state.append("  content: content unchanged\n", entry_index);
    if(view.status == "Renamed") {
        state.append(
                "  metadata change: rename only (content unchanged)\n",
                entry_index);
    }
    if(view.old_version != nullptr && view.new_version != nullptr &&
       view.old_version->mode() != view.new_version->mode()) {
        state.append(
                "  metadata change: mode changed (content unchanged)\n",
                entry_index);
    }
    if(view.status != "Renamed" && view.old_version != nullptr &&
       view.new_version != nullptr &&
       view.old_version->mode() == view.new_version->mode()) {
        state.append(
                "  metadata change: typed status retained despite unchanged content\n",
                entry_index);
    }
}

void render_mixed_text(
        RenderState& state,
        const ReviewedSourceReviewEntry& entry,
        std::size_t entry_index) {
    state.append(
            "  content: mixed text and non-line-reviewable content; raw bytes withheld\n",
            entry_index);
    const ReviewedSourceTextContent* old_text =
            line_content(state, entry.old_observation, entry_index);
    const ReviewedSourceTextContent* new_text =
            line_content(state, entry.new_observation, entry_index);
    if(state.failed()) return;
    if(old_text != nullptr) {
        state.append("  line-reviewable old content:\n", entry_index);
        render_text_content(state, *old_text, '-', entry_index);
    }
    if(new_text != nullptr) {
        state.append("  line-reviewable new content:\n", entry_index);
        render_text_content(state, *new_text, '+', entry_index);
    }
}

void render_representation(
        RenderState& state,
        const ReviewedSourceReviewEntry& entry,
        const ChangeView& view,
        std::size_t entry_index) {
    const bool expects_patch = entry.representation ==
            ReviewedSourceReviewRepresentation::CompleteTextPatch;
    if(expects_patch != entry.patch.has_value()) {
        state.fail_inconsistent(entry_index);
        return;
    }
    switch(entry.representation) {
    case ReviewedSourceReviewRepresentation::CompleteTextPatch:
        render_patch(state, entry, view, entry_index);
        return;
    case ReviewedSourceReviewRepresentation::CompleteFullText:
        render_complete_full_text(state, entry, entry_index);
        return;
    case ReviewedSourceReviewRepresentation::NoContentChange:
        render_no_content_change(state, view, entry_index);
        return;
    case ReviewedSourceReviewRepresentation::ContainsNul:
        state.append(
                "  content: binary/non-line-reviewable content; raw bytes withheld\n",
                entry_index);
        return;
    case ReviewedSourceReviewRepresentation::GitlinkMetadata:
        state.append(
                "  content: gitlink/submodule metadata only; recursive review not performed\n",
                entry_index);
        return;
    case ReviewedSourceReviewRepresentation::MixedTextAndNonText:
        render_mixed_text(state, entry, entry_index);
        return;
    }
    state.fail_inconsistent(entry_index);
}

void render_entry(
        RenderState& state,
        const ReviewedSourceReviewEntry& entry,
        std::size_t entry_index) {
    const ChangeView view = change_view(entry.change);
    if(view.content == nullptr) {
        state.fail_inconsistent(entry_index);
        return;
    }

    state.append("entry ", entry_index);
    state.append_number(entry_index + 1, entry_index);
    state.append(":\n", entry_index);
    render_status_summary(state, view, entry_index);
    render_version_fields(
            state, "old", view.old_version, entry_index);
    render_version_fields(
            state, "new", view.new_version, entry_index);
    state.append("  rename similarity: ", entry_index);
    if(view.rename_similarity.has_value()) {
        state.append_number(*view.rename_similarity, entry_index);
        state.append("%\n", entry_index);
    } else {
        state.append("none\n", entry_index);
    }
    render_git_marker(state, *view.content, entry_index);
    render_observation_kind(
            state, "old", entry.old_observation, entry_index);
    render_observation_kind(
            state, "new", entry.new_observation, entry_index);

    state.append("  review emphasis: ", entry_index);
    append_mapped_value(
            state, emphasis_display(entry.emphasis), entry_index);
    state.append("\n", entry_index);
    state.append("  presentation priority: ", entry_index);
    if(entry.emphasis == ReviewedSourceReviewEmphasis::Sensitive) {
        state.append("Sensitive\n", entry_index);
        state.append(
                "  sensitive guidance: explicitly inspect root PKGBUILD/install source content\n",
                entry_index);
    } else if(has_generated_metadata(view)) {
        state.append("LowerGeneratedMetadata\n", entry_index);
        state.append(
                "  generated metadata guidance: lower emphasis; not source-review authority\n",
                entry_index);
    } else {
        state.append("Ordinary\n", entry_index);
    }

    state.append("  representation: ", entry_index);
    append_mapped_value(
            state,
            representation_display(entry.representation),
            entry_index);
    state.append("\n", entry_index);
    state.append("  readiness: ", entry_index);
    append_mapped_value(
            state, readiness_display(entry.readiness), entry_index);
    state.append("\n", entry_index);
    render_readiness_diagnostic(state, entry.readiness, entry_index);
    if(state.failed()) return;
    render_representation(state, entry, view, entry_index);
}

void render_review_body(
        RenderState& state,
        const ReviewedSourceReviewBody& body) {
    state.append("overall readiness: ");
    append_mapped_value(state, readiness_display(body.readiness), 0);
    state.append("\nentry count: ");
    state.append_number(body.entries.size());
    state.append("\nentries:\n");
    if(body.entries.empty()) {
        state.append("  (none)\n");
        return;
    }
    for(std::size_t index = 0;
        index < body.entries.size() && !state.failed(); ++index) {
        render_entry(state, body.entries[index], index);
    }
}

std::optional<std::string_view> history_relation_display(
        ReviewedSourceHistoryRelation relation) noexcept {
    switch(relation) {
    case ReviewedSourceHistoryRelation::Ancestor:
        return "Ancestor";
    case ReviewedSourceHistoryRelation::NonAncestor:
        return "NonAncestor";
    }
    return std::nullopt;
}

std::optional<std::string_view> baseline_reason_display(
        ReviewedSourceBaselineUnavailableReason reason) noexcept {
    switch(reason) {
    case ReviewedSourceBaselineUnavailableReason::MissingOrNotCommit:
        return "MissingOrNotCommit";
    }
    return std::nullopt;
}

ReviewedSourcePresentationResult render_with_limit(
        const ReviewedSourceMaterializedReview& review,
        std::uintmax_t limit) {
    RenderState state(limit);
    std::visit(
            [&state](const auto& value) {
                using Review = std::decay_t<decltype(value)>;
                if constexpr(std::is_same_v<
                                     Review,
                                     ReviewedSourceMaterializedInitialFullReview>) {
                    state.append("review kind: InitialFullReview\n");
                    render_revision(state, "target revision: ", value.target);
                    render_review_body(state, value.review);
                } else if constexpr(std::is_same_v<
                                            Review,
                                            ReviewedSourceMaterializedAlreadyReviewed>) {
                    state.append("review kind: AlreadyReviewed\n");
                    render_revision(
                            state, "reviewed revision: ", value.revision);
                    state.append("result: already reviewed\n");
                } else if constexpr(std::is_same_v<
                                            Review,
                                            ReviewedSourceMaterializedUpdateReview>) {
                    state.append("review kind: UpdateReview\n");
                    render_revision(
                            state, "baseline revision: ", value.baseline);
                    render_revision(state, "target revision: ", value.target);
                    state.append("history relation: ");
                    append_mapped_value(
                            state,
                            history_relation_display(value.relation), 0);
                    state.append("\n");
                    render_review_body(state, value.review);
                } else {
                    state.append("review kind: RebaselineFullReview\n");
                    render_revision(
                            state, "unavailable baseline: ",
                            value.unavailable_baseline);
                    render_revision(state, "target revision: ", value.target);
                    state.append("baseline unavailable reason: ");
                    append_mapped_value(
                            state,
                            baseline_reason_display(value.reason), 0);
                    state.append("\n");
                    render_review_body(state, value.review);
                }
            },
            review);
    return state.finish();
}

} // namespace

ReviewedSourcePresentationResult render_reviewed_source_presentation(
        const ReviewedSourceMaterializedReview& review) {
    return render_with_limit(review, REVIEWED_SOURCE_RENDERED_OUTPUT_LIMIT);
}

#ifdef MOGUET_ENABLE_REVIEWED_SOURCE_PRESENTATION_TEST_HOOKS
ReviewedSourcePresentationSizeCheckResult
checked_reviewed_source_presentation_size_for_test(
        std::uintmax_t current,
        std::uintmax_t additional,
        std::uintmax_t limit) {
    return checked_rendered_output_size(current, additional, limit, 0);
}

ReviewedSourcePresentationResult
render_reviewed_source_presentation_with_limit_for_test(
        const ReviewedSourceMaterializedReview& review,
        std::uintmax_t limit) {
    return render_with_limit(review, limit);
}
#endif
