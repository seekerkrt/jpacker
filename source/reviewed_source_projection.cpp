#include "reviewed_source_projection.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace {

bool is_lower_hex(char character) noexcept {
    return (character >= '0' && character <= '9') ||
           (character >= 'a' && character <= 'f');
}

GitObjectFormat detect_object_format(std::string_view object_id) {
    if(object_id.size() == 40) return GitObjectFormat::Sha1;
    if(object_id.size() == 64) return GitObjectFormat::Sha256;
    throw std::invalid_argument(
            "Reviewed source object ID must be a complete SHA-1 or SHA-256 value.");
}

ReviewedSourceFileClassification classify_path(
        const ReviewedSourcePath& path) noexcept {
    return path.raw_bytes() == ".SRCINFO"
            ? ReviewedSourceFileClassification::GeneratedMetadata
            : ReviewedSourceFileClassification::TrackedSource;
}

} // namespace

ReviewedSourcePath::ReviewedSourcePath(std::string raw_bytes) noexcept
    : raw_bytes_(std::move(raw_bytes)) {}

ReviewedSourcePath ReviewedSourcePath::make(std::string raw_bytes) {
    if(raw_bytes.empty() || raw_bytes.find('\0') != std::string::npos) {
        throw std::invalid_argument(
                "Reviewed source path must be nonempty opaque Git path bytes.");
    }
    return ReviewedSourcePath(std::move(raw_bytes));
}

const std::string& ReviewedSourcePath::raw_bytes() const noexcept {
    return raw_bytes_;
}

std::string ReviewedSourcePath::escaped_display() const {
    constexpr std::array<char, 16> HEX{
            '0', '1', '2', '3', '4', '5', '6', '7',
            '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};
    std::string display;
    display.reserve(raw_bytes_.size() + 2);
    display.push_back('"');
    for(unsigned char byte : raw_bytes_) {
        switch(byte) {
        case '"':
            display += "\\\"";
            break;
        case '\\':
            display += "\\\\";
            break;
        case '\t':
            display += "\\t";
            break;
        case '\n':
            display += "\\n";
            break;
        case '\r':
            display += "\\r";
            break;
        default:
            if(byte >= 0x20 && byte <= 0x7e) {
                display.push_back(static_cast<char>(byte));
            } else {
                display += "\\x";
                display.push_back(HEX[(byte >> 4) & 0x0f]);
                display.push_back(HEX[byte & 0x0f]);
            }
            break;
        }
    }
    display.push_back('"');
    return display;
}

ReviewedSourceObjectId::ReviewedSourceObjectId(
        GitObjectFormat format, std::string object_id) noexcept
    : format_(format), object_id_(std::move(object_id)) {}

ReviewedSourceObjectId ReviewedSourceObjectId::make(std::string object_id) {
    const GitObjectFormat format = detect_object_format(object_id);
    if(!std::all_of(object_id.begin(), object_id.end(), is_lower_hex)) {
        throw std::invalid_argument(
                "Reviewed source object ID must use lowercase hexadecimal.");
    }
    return ReviewedSourceObjectId(format, std::move(object_id));
}

GitObjectFormat ReviewedSourceObjectId::format() const noexcept {
    return format_;
}

const std::string& ReviewedSourceObjectId::value() const noexcept {
    return object_id_;
}

ReviewedSourceFileType reviewed_source_file_type(
        ReviewedSourceFileMode mode) noexcept {
    switch(mode) {
    case ReviewedSourceFileMode::Regular:
    case ReviewedSourceFileMode::Executable:
        return ReviewedSourceFileType::Regular;
    case ReviewedSourceFileMode::SymbolicLink:
        return ReviewedSourceFileType::SymbolicLink;
    case ReviewedSourceFileMode::Gitlink:
        return ReviewedSourceFileType::Gitlink;
    }
    return ReviewedSourceFileType::Regular;
}

ReviewedSourceFileVersion::ReviewedSourceFileVersion(
        ReviewedSourcePath path,
        ReviewedSourceFileClassification classification,
        ReviewedSourceFileMode mode,
        ReviewedSourceObjectId object_id,
        std::optional<std::uintmax_t> blob_size) noexcept
    : path_(std::move(path)), classification_(classification), mode_(mode),
      object_id_(std::move(object_id)), blob_size_(blob_size) {}

ReviewedSourceFileVersion ReviewedSourceFileVersion::make(
        ReviewedSourcePath path,
        ReviewedSourceFileMode mode,
        ReviewedSourceObjectId object_id,
        std::optional<std::uintmax_t> blob_size) {
    const bool is_gitlink = mode == ReviewedSourceFileMode::Gitlink;
    if(is_gitlink == blob_size.has_value()) {
        throw std::invalid_argument(
                "Reviewed source blob size and Git file mode are inconsistent.");
    }
    const ReviewedSourceFileClassification classification = classify_path(path);
    return ReviewedSourceFileVersion(
            std::move(path), classification, mode, std::move(object_id),
            blob_size);
}

const ReviewedSourcePath& ReviewedSourceFileVersion::path() const noexcept {
    return path_;
}

ReviewedSourceFileClassification
ReviewedSourceFileVersion::classification() const noexcept {
    return classification_;
}

ReviewedSourceFileMode ReviewedSourceFileVersion::mode() const noexcept {
    return mode_;
}

const ReviewedSourceObjectId&
ReviewedSourceFileVersion::object_id() const noexcept {
    return object_id_;
}

const std::optional<std::uintmax_t>&
ReviewedSourceFileVersion::blob_size() const noexcept {
    return blob_size_;
}

ReviewedSourceObjectId reviewed_source_empty_tree_object_id(
        GitObjectFormat format) {
    switch(format) {
    case GitObjectFormat::Sha1:
        return ReviewedSourceObjectId::make(
                "4b825dc642cb6eb9a060e54bf8d69288fbee4904");
    case GitObjectFormat::Sha256:
        return ReviewedSourceObjectId::make(
                "6ef19b41225c5369f1c104d45d8d85efa9b057b53b14b4b9b939dd74decc5321");
    }
    throw std::invalid_argument("Unsupported reviewed source Git object format.");
}
