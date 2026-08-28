#pragma once

#include "reviewed_source_projection.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <variant>
#include <vector>

enum class ReviewedSourceMachineStream {
    CommitResolution,
    BaselineTree,
    TargetTree,
    NameStatus,
    Numstat,
    CrossStream,
    ResourcePreflight,
};

enum class ReviewedSourceProjectionFailureReason {
    MalformedMachineOutput,
    InconsistentMachineOutput,
    RenameCandidateLimitExceeded,
    SingleBlobSizeLimitExceeded,
    AggregateBlobSizeLimitExceeded,
};

struct ReviewedSourceProjectionFailure {
    ReviewedSourceProjectionFailureReason reason;
    ReviewedSourceMachineStream stream;
    std::size_t record_index = 0;
    std::uintmax_t observed = 0;
    std::uintmax_t limit = 0;

    bool operator==(const ReviewedSourceProjectionFailure&) const = default;
};

struct ReviewedSourceTreeInventory {
    std::vector<ReviewedSourceFileVersion> entries;

    bool operator==(const ReviewedSourceTreeInventory&) const = default;
};

using ReviewedSourceCommitParseResult = std::variant<
    SourceRevisionIdentity,
    ReviewedSourceProjectionFailure>;

using ReviewedSourceTreeParseResult = std::variant<
    ReviewedSourceTreeInventory,
    ReviewedSourceProjectionFailure>;

using ReviewedSourceResourcePreflightResult = std::variant<
    std::monostate,
    ReviewedSourceProjectionFailure>;

using ReviewedSourceChangeAssemblyResult = std::variant<
    std::vector<ReviewedSourceFileChange>,
    ReviewedSourceProjectionFailure>;

[[nodiscard]] ReviewedSourceCommitParseResult
parse_reviewed_source_commit_output(std::string_view output);

[[nodiscard]] ReviewedSourceTreeParseResult
parse_reviewed_source_tree_output(
    std::string_view metadata_output,
    std::string_view path_output,
    GitObjectFormat object_format,
    ReviewedSourceMachineStream stream);

[[nodiscard]] ReviewedSourceResourcePreflightResult
preflight_reviewed_source_projection_resources(
    const ReviewedSourceTreeInventory& baseline,
    const ReviewedSourceTreeInventory& target,
    bool detect_renames);

[[nodiscard]] ReviewedSourceChangeAssemblyResult
assemble_reviewed_source_changes(
    const ReviewedSourceTreeInventory& baseline,
    const ReviewedSourceTreeInventory& target,
    std::string_view name_status_output,
    std::string_view numstat_output,
    bool detect_renames);
