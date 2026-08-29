#pragma once

#include "source_artifact_install_trusted_protocol.hpp"

#include <memory>
#include <stdexcept>
#include <string>

#include <sys/types.h>

class SourceArtifactInstallTrustedStateError final
    : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// The installed helper supplies a descriptor for fixed /run and uid 0.
// Tests may supply an isolated descriptor, but no helper CLI can select a
// state root. Source-artifact state never shares the selected-provider
// alpm-receipts subtree.
class SourceArtifactInstallTrustedStateStore final {
public:
    [[nodiscard]] static SourceArtifactInstallTrustedStateStore
    open_below_runtime_parent(
        int runtime_parent_fd, uid_t expected_owner);

    SourceArtifactInstallTrustedStateStore(
        const SourceArtifactInstallTrustedStateStore&) = delete;
    SourceArtifactInstallTrustedStateStore(
        SourceArtifactInstallTrustedStateStore&&) noexcept;
    SourceArtifactInstallTrustedStateStore& operator=(
        const SourceArtifactInstallTrustedStateStore&) = delete;
    SourceArtifactInstallTrustedStateStore& operator=(
        SourceArtifactInstallTrustedStateStore&&) noexcept;
    ~SourceArtifactInstallTrustedStateStore();

    [[nodiscard]] SourceArtifactInstallRootPrepareResponse prepare(
        const SourceArtifactInstallRootPrepareRequest& request,
        int sealed_artifact_input_fd);
    void record(
        const std::string& transaction_token,
        int needs_targets_input_fd);
    [[nodiscard]] std::string consume(
        const std::string& transaction_token);
    void abort(const std::string& transaction_token);

private:
    struct Implementation;

    explicit SourceArtifactInstallTrustedStateStore(
        std::unique_ptr<Implementation> implementation) noexcept;

    std::unique_ptr<Implementation> implementation_;
};
