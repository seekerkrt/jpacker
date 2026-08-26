#pragma once

#include "source_package_identity.hpp"

#include <optional>
#include <string>

enum class VcsKind {
    Git,
    Svn,
    Hg,
    Bzr,
    Cvs,
    Darcs,
};

enum class VcsSelectorKind {
    DefaultHead,
    Branch,
    FixedRevision,
    Tag,
    Unsupported,
    Unrecognized,
};

enum class VcsSelectorTrackingBehavior {
    Floating,
    Fixed,
    Indeterminate,
};

// Selector syntax remains parser-owned. This value only preserves the parsed
// role without pretending that every VCS gives branch/tag/revision the same
// remote-observation semantics.
class VcsSelector final {
public:
    VcsSelector() = delete;
    VcsSelector(const VcsSelector&) = default;
    VcsSelector(VcsSelector&&) noexcept = default;
    VcsSelector& operator=(const VcsSelector&) = default;
    VcsSelector& operator=(VcsSelector&&) noexcept = default;
    ~VcsSelector() = default;

    [[nodiscard]] static VcsSelector default_head() noexcept;
    [[nodiscard]] static VcsSelector branch(std::string branch_name);
    [[nodiscard]] static VcsSelector fixed_revision(std::string revision);
    [[nodiscard]] static VcsSelector tag(std::string tag_name);
    [[nodiscard]] static VcsSelector unsupported(std::string selector);
    [[nodiscard]] static VcsSelector unrecognized(std::string selector);

    [[nodiscard]] VcsSelectorKind kind() const noexcept;
    [[nodiscard]] VcsSelectorTrackingBehavior tracking_behavior()
            const noexcept;
    [[nodiscard]] const std::string* value() const noexcept;

    bool operator==(const VcsSelector&) const = default;

private:
    VcsSelector(
            VcsSelectorKind kind,
            VcsSelectorTrackingBehavior tracking_behavior,
            std::optional<std::string> value) noexcept;

    VcsSelectorKind             kind_;
    VcsSelectorTrackingBehavior tracking_behavior_;
    std::optional<std::string>   value_;
};

class VcsSourceIdentity final {
public:
    VcsSourceIdentity() = delete;
    VcsSourceIdentity(const VcsSourceIdentity&) = default;
    VcsSourceIdentity(VcsSourceIdentity&&) noexcept = default;
    VcsSourceIdentity& operator=(const VcsSourceIdentity&) = default;
    VcsSourceIdentity& operator=(VcsSourceIdentity&&) noexcept = default;
    ~VcsSourceIdentity() = default;

    [[nodiscard]] static VcsSourceIdentity make(
            VcsKind kind,
            std::string source_location,
            VcsSelector selector,
            std::optional<std::string> architecture = std::nullopt);

    [[nodiscard]] VcsKind kind() const noexcept;
    [[nodiscard]] const std::string& source_location() const noexcept;
    [[nodiscard]] const VcsSelector& selector() const noexcept;
    [[nodiscard]] const std::string* architecture() const noexcept;

    bool operator==(const VcsSourceIdentity&) const = default;

private:
    VcsSourceIdentity(
            VcsKind kind,
            std::string source_location,
            VcsSelector selector,
            std::optional<std::string> architecture) noexcept;

    VcsKind                    kind_;
    std::string                source_location_;
    VcsSelector                selector_;
    std::optional<std::string> architecture_;
};

// SourceRevisionIdentity is reused only as a validated low-level Git OID.
// These distinct owners prevent an AUR recipe commit from entering an
// upstream-source revision API merely because the hexadecimal text matches.
class AurRecipeRevision final {
public:
    AurRecipeRevision() = delete;
    AurRecipeRevision(const AurRecipeRevision&) = default;
    AurRecipeRevision(AurRecipeRevision&&) noexcept = default;
    AurRecipeRevision& operator=(const AurRecipeRevision&) = default;
    AurRecipeRevision& operator=(AurRecipeRevision&&) noexcept = default;
    ~AurRecipeRevision() = default;

    [[nodiscard]] static AurRecipeRevision git_commit(std::string object_id);

    [[nodiscard]] const SourceRevisionIdentity& value() const noexcept;

    bool operator==(const AurRecipeRevision&) const = default;

private:
    explicit AurRecipeRevision(SourceRevisionIdentity value) noexcept;

    SourceRevisionIdentity value_;
};

class UpstreamGitRevision final {
public:
    UpstreamGitRevision() = delete;
    UpstreamGitRevision(const UpstreamGitRevision&) = default;
    UpstreamGitRevision(UpstreamGitRevision&&) noexcept = default;
    UpstreamGitRevision& operator=(const UpstreamGitRevision&) = default;
    UpstreamGitRevision& operator=(UpstreamGitRevision&&) noexcept = default;
    ~UpstreamGitRevision() = default;

    [[nodiscard]] static UpstreamGitRevision git_commit(
            VcsSourceIdentity source, std::string object_id);

    [[nodiscard]] const VcsSourceIdentity& source() const noexcept;
    [[nodiscard]] const SourceRevisionIdentity& value() const noexcept;

    bool operator==(const UpstreamGitRevision&) const = default;

private:
    UpstreamGitRevision(
            VcsSourceIdentity source,
            SourceRevisionIdentity value) noexcept;

    VcsSourceIdentity       source_;
    SourceRevisionIdentity value_;
};
