#pragma once

#include "process.hpp"
#include "source_environment.hpp"
#include "trusted_cache.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

class ExpectedPackageArtifactPath;
class ExpectedPackageArtifactSet;
class ValidatedPackageArtifactPath;
class ValidatedPackageArtifactSet;
class ArtifactMakepkgContext;
struct ArtifactMakepkgContextProvenance;

// 一回のsource-build invocationだけが所有するfresh PKGDEST。
// POLICY(#242): named pathだけでなくroot/workspace FDと作成時identityを保持し、
// identityを再証明できた場合だけscope cleanupする。
class ArtifactWorkspace final {
    ValidatedPrivateCacheRoot root_;
    std::filesystem::path path_;
    std::filesystem::path canonical_path_;
    std::string           leaf_name_;
    int                   directory_descriptor_ = -1;
    std::uintmax_t        device_ = 0;
    std::uintmax_t        inode_ = 0;
    std::uintmax_t        owner_ = 0;
    bool                  owns_path_ = false;
    bool                  cleanup_on_destruction_ = true;

    ArtifactWorkspace(
            ValidatedPrivateCacheRoot root, std::filesystem::path path,
            std::filesystem::path canonical_path, std::string leaf_name,
            int directory_descriptor, std::uintmax_t device,
            std::uintmax_t inode, std::uintmax_t owner) noexcept;

    void require_unchanged_identity_for_owner(
            std::uintmax_t expected_effective_user) const;

    friend ArtifactWorkspace create_artifact_workspace(
            ValidatedPrivateCacheRoot root);
    friend ArtifactMakepkgContext prepare_artifact_makepkg_context(
            const ValidatedCachePath& checkout,
            const ArtifactWorkspace& workspace,
            const SourceBuildEnvironment& environment,
            SourceEnvironmentEmptyValuePolicy empty_value_policy);
    friend class ArtifactMakepkgContext;
    friend class ExpectedPackageArtifactPath;
    friend class ExpectedPackageArtifactSet;
    friend class ValidatedPackageArtifactPath;
    friend class ValidatedPackageArtifactSet;
    friend ExpectedPackageArtifactPath validate_makepkg_packagelist_output(
            const ArtifactWorkspace& workspace, const std::string& raw_output);
    friend ExpectedPackageArtifactSet
    validate_makepkg_packagelist_output_set(
            const ArtifactWorkspace& workspace,
            const std::string& raw_output);
    friend ExpectedPackageArtifactPath query_makepkg_packagelist(
            const ArtifactWorkspace& workspace,
            const ArtifactMakepkgContext& context);
    friend ExpectedPackageArtifactSet query_makepkg_packagelist_set(
            const ArtifactWorkspace& workspace,
            const ArtifactMakepkgContext& context);
    friend ValidatedPackageArtifactPath validate_post_build_package_artifact(
            ArtifactWorkspace&& workspace,
            const ExpectedPackageArtifactPath& expected);
    friend ValidatedPackageArtifactSet validate_post_build_package_artifacts(
            ArtifactWorkspace&& workspace,
            const ExpectedPackageArtifactSet& expected);
#ifdef JPACKER_ENABLE_ARTIFACT_WORKSPACE_TEST_HOOKS
    friend void require_artifact_workspace_identity_for_test(
            const ArtifactWorkspace& workspace,
            std::uintmax_t expected_effective_user);
    friend ValidatedPackageArtifactPath
    validate_post_build_package_artifact_for_test(
            ArtifactWorkspace&& workspace,
            const ExpectedPackageArtifactPath& expected,
            std::uintmax_t expected_effective_user);
    friend ValidatedPackageArtifactSet
    validate_post_build_package_artifacts_for_test(
            ArtifactWorkspace&& workspace,
            const ExpectedPackageArtifactSet& expected,
            std::uintmax_t expected_effective_user);
    friend ValidatedPackageArtifactSet
    validate_post_build_package_artifacts_for_test(
            ArtifactWorkspace&& workspace,
            const ExpectedPackageArtifactSet& expected,
            std::uintmax_t expected_workspace_owner,
            std::uintmax_t expected_artifact_owner,
            std::uintmax_t expected_signature_owner);
#endif

public:
    ArtifactWorkspace(const ArtifactWorkspace&) = delete;
    ArtifactWorkspace& operator=(const ArtifactWorkspace&) = delete;
    ArtifactWorkspace(ArtifactWorkspace&& other) noexcept;
    ArtifactWorkspace& operator=(ArtifactWorkspace&&) = delete;
    ~ArtifactWorkspace() noexcept;

    const std::filesystem::path& path() const noexcept {
        return path_;
    }

    const std::filesystem::path& canonical_path() const noexcept {
        return canonical_path_;
    }

    void require_unchanged_identity() const;

    // diagnostic retention後も明示cleanupは可能。次回invocationは常に別workspaceを作る。
    void retain_for_diagnostics() noexcept;
    void cleanup();
};

ArtifactWorkspace create_artifact_workspace(ValidatedPrivateCacheRoot root);

// source/ambient PKGDESTはvalueのempty/nonemptyに関係なくownership conflictとする。
void require_unclaimed_artifact_pkgdest(
        const SourceBuildEnvironment& environment);

// build-only makepkgへ渡せるoptionを、install/remove系optionから型で分離する。
struct ArtifactMakepkgBuildOptions {
    bool no_confirm = false;
    bool rebuild = false;
    bool clean_build = false;
};

// packagelistと後続build-only makepkgが共有するcheckout/environment境界。
// owned PKGDESTはsource assignmentの順序を保った末尾へ一度だけ追加する。
class ArtifactMakepkgContext final {
    ValidatedCachePath                   checkout_;
    SourceBuildEnvironment               command_environment_;
    SourceEnvironmentEmptyValuePolicy    empty_value_policy_;
    int                                  checkout_descriptor_ = -1;
    std::uintmax_t                       checkout_device_ = 0;
    std::uintmax_t                       checkout_inode_ = 0;
    std::shared_ptr<const ArtifactMakepkgContextProvenance> provenance_;
    std::filesystem::path                workspace_path_;
    std::uintmax_t                       workspace_device_ = 0;
    std::uintmax_t                       workspace_inode_ = 0;

    ArtifactMakepkgContext(
            ValidatedCachePath checkout,
            SourceBuildEnvironment command_environment,
            SourceEnvironmentEmptyValuePolicy empty_value_policy,
            int checkout_descriptor,
            std::uintmax_t checkout_device,
            std::uintmax_t checkout_inode,
            std::filesystem::path workspace_path,
            std::uintmax_t workspace_device,
            std::uintmax_t workspace_inode);

    void require_unchanged_checkout() const;
    void require_matching_workspace(const ArtifactWorkspace& workspace) const;
    CapturedCommandResult capture_makepkg_output(
            const ArtifactWorkspace& workspace,
            const std::vector<std::string>& makepkg_arguments) const;
    std::string makepkg_command(
            const std::vector<std::string>& makepkg_arguments) const;

    friend ArtifactMakepkgContext prepare_artifact_makepkg_context(
            const ValidatedCachePath& checkout,
            const ArtifactWorkspace& workspace,
            const SourceBuildEnvironment& environment,
            SourceEnvironmentEmptyValuePolicy empty_value_policy);
    friend ExpectedPackageArtifactPath query_makepkg_packagelist(
            const ArtifactWorkspace& workspace,
            const ArtifactMakepkgContext& context);
    friend ExpectedPackageArtifactSet query_makepkg_packagelist_set(
            const ArtifactWorkspace& workspace,
            const ArtifactMakepkgContext& context);
    friend class ExpectedPackageArtifactPath;
    friend class ExpectedPackageArtifactSet;

public:
    ArtifactMakepkgContext(const ArtifactMakepkgContext&) = delete;
    ArtifactMakepkgContext& operator=(const ArtifactMakepkgContext&) = delete;
    ArtifactMakepkgContext(ArtifactMakepkgContext&& other) noexcept;
    ArtifactMakepkgContext& operator=(ArtifactMakepkgContext&&) = delete;
    ~ArtifactMakepkgContext() noexcept;

    const std::filesystem::path& checkout_path() const noexcept {
        return checkout_.canonical_path();
    }

    const std::filesystem::path& workspace_path() const noexcept {
        return workspace_path_;
    }

    const SourceBuildEnvironment& command_environment() const noexcept {
        return command_environment_;
    }

    std::string command_environment_prefix() const;

    // query由来expectedとのprovenance照合を必須にし、exact build-only argvだけを
    // 公開する。--needed/-r/-iを受けるarbitrary argv境界にはしない。
    int run_makepkg_build_only(
            const ArtifactWorkspace& workspace,
            const ExpectedPackageArtifactPath& expected,
            const ArtifactMakepkgBuildOptions& options) const;

    int run_makepkg_build_only(
            const ArtifactWorkspace& workspace,
            const ExpectedPackageArtifactSet& expected,
            const ArtifactMakepkgBuildOptions& options) const;
};

ArtifactMakepkgContext prepare_artifact_makepkg_context(
        const ValidatedCachePath& checkout,
        const ArtifactWorkspace& workspace,
        const SourceBuildEnvironment& environment,
        SourceEnvironmentEmptyValuePolicy empty_value_policy);

// makepkg --packagelistで宣言されたbuild前のexpected path。
// filesystem artifactの検証済みcapabilityではない。
class ExpectedPackageArtifactPath final {
    std::filesystem::path path_;
    std::string           leaf_name_;
    std::uintmax_t        workspace_device_ = 0;
    std::uintmax_t        workspace_inode_ = 0;
    bool                  makepkg_context_bound_ = false;
    std::shared_ptr<const ArtifactMakepkgContextProvenance>
            makepkg_context_provenance_;

    ExpectedPackageArtifactPath(
            std::filesystem::path path, std::string leaf_name,
            std::uintmax_t workspace_device,
            std::uintmax_t workspace_inode);

    void bind_makepkg_context(const ArtifactMakepkgContext& context);
    void require_matching_makepkg_context(
            const ArtifactMakepkgContext& context) const;

    friend ExpectedPackageArtifactPath validate_makepkg_packagelist_output(
            const ArtifactWorkspace& workspace, const std::string& raw_output);
    friend ExpectedPackageArtifactPath query_makepkg_packagelist(
            const ArtifactWorkspace& workspace,
            const ArtifactMakepkgContext& context);
    friend class ValidatedPackageArtifactPath;
    friend class ArtifactMakepkgContext;
    friend ValidatedPackageArtifactPath validate_post_build_package_artifact(
            ArtifactWorkspace&& workspace,
            const ExpectedPackageArtifactPath& expected);
#ifdef JPACKER_ENABLE_ARTIFACT_WORKSPACE_TEST_HOOKS
    friend ValidatedPackageArtifactPath
    validate_post_build_package_artifact_for_test(
            ArtifactWorkspace&& workspace,
            const ExpectedPackageArtifactPath& expected,
            std::uintmax_t expected_effective_user);
#endif

public:
    const std::filesystem::path& path() const noexcept {
        return path_;
    }
};

ExpectedPackageArtifactPath validate_makepkg_packagelist_output(
        const ArtifactWorkspace& workspace, const std::string& raw_output);

ExpectedPackageArtifactPath query_makepkg_packagelist(
        const ArtifactWorkspace& workspace,
        const ArtifactMakepkgContext& context);

// makepkg --packagelistのrecord順と、一つのworkspace/context lineageを
// set全体で保持するmultiple expected capability。
class ExpectedPackageArtifactSet final {
    struct Entry {
        std::filesystem::path path;
        std::string           leaf_name;
    };

    std::vector<Entry> entries_;
    std::uintmax_t     workspace_device_ = 0;
    std::uintmax_t     workspace_inode_ = 0;
    bool               makepkg_context_bound_ = false;
    std::shared_ptr<const ArtifactMakepkgContextProvenance>
            makepkg_context_provenance_;

    ExpectedPackageArtifactSet(
            std::vector<Entry> entries,
            std::uintmax_t workspace_device,
            std::uintmax_t workspace_inode) noexcept;

    void bind_makepkg_context(const ArtifactMakepkgContext& context);
    void require_matching_makepkg_context(
            const ArtifactMakepkgContext& context) const;
    void require_matching_workspace(
            const ArtifactWorkspace& workspace) const;

    friend ExpectedPackageArtifactSet
    validate_makepkg_packagelist_output_set(
            const ArtifactWorkspace& workspace,
            const std::string& raw_output);
    friend ExpectedPackageArtifactSet query_makepkg_packagelist_set(
            const ArtifactWorkspace& workspace,
            const ArtifactMakepkgContext& context);
    friend class ArtifactMakepkgContext;
    friend class ValidatedPackageArtifactSet;

public:
    ExpectedPackageArtifactSet(const ExpectedPackageArtifactSet&) = default;
    ExpectedPackageArtifactSet& operator=(
            const ExpectedPackageArtifactSet&) = default;
    ExpectedPackageArtifactSet(
            ExpectedPackageArtifactSet&&) noexcept = default;
    ExpectedPackageArtifactSet& operator=(
            ExpectedPackageArtifactSet&&) noexcept = default;
    ~ExpectedPackageArtifactSet() = default;

    std::size_t size() const noexcept {
        return entries_.size();
    }

    const std::filesystem::path& path_at(std::size_t index) const;
};

ExpectedPackageArtifactSet validate_makepkg_packagelist_output_set(
        const ArtifactWorkspace& workspace,
        const std::string& raw_output);

ExpectedPackageArtifactSet query_makepkg_packagelist_set(
        const ArtifactWorkspace& workspace,
        const ArtifactMakepkgContext& context);

// post-build filesystem validationを通過し、workspace ownershipも保持するcapability。
// PR3のidentity/pacman executorはraw path overloadを持たず、この型だけを受け取る。
class ValidatedPackageArtifactPath final {
    ArtifactWorkspace     workspace_;
    std::filesystem::path path_;
    std::string           leaf_name_;
    int                   artifact_descriptor_ = -1;
    std::uintmax_t        device_ = 0;
    std::uintmax_t        inode_ = 0;
    std::uintmax_t        owner_ = 0;

    ValidatedPackageArtifactPath(
            ArtifactWorkspace&& workspace, std::filesystem::path path,
            std::string leaf_name, int artifact_descriptor,
            std::uintmax_t device, std::uintmax_t inode,
            std::uintmax_t owner) noexcept;

    void require_validity_for_owner(
            std::uintmax_t expected_effective_user) const;
    static ValidatedPackageArtifactPath validate_for_owners(
            ArtifactWorkspace&& workspace,
            const ExpectedPackageArtifactPath& expected,
            std::uintmax_t expected_workspace_owner,
            std::uintmax_t expected_artifact_owner);

    friend ValidatedPackageArtifactPath validate_post_build_package_artifact(
            ArtifactWorkspace&& workspace,
            const ExpectedPackageArtifactPath& expected);
#ifdef JPACKER_ENABLE_ARTIFACT_WORKSPACE_TEST_HOOKS
    friend ValidatedPackageArtifactPath
    validate_post_build_package_artifact_for_test(
            ArtifactWorkspace&& workspace,
            const ExpectedPackageArtifactPath& expected,
            std::uintmax_t expected_effective_user);
#endif

public:
    ValidatedPackageArtifactPath(const ValidatedPackageArtifactPath&) = delete;
    ValidatedPackageArtifactPath& operator=(
            const ValidatedPackageArtifactPath&) = delete;
    ValidatedPackageArtifactPath(
            ValidatedPackageArtifactPath&& other) noexcept;
    ValidatedPackageArtifactPath& operator=(
            ValidatedPackageArtifactPath&&) = delete;
    ~ValidatedPackageArtifactPath() noexcept;

    const std::filesystem::path& path() const noexcept {
        return path_;
    }

    const std::filesystem::path& workspace_path() const noexcept {
        return workspace_.path();
    }

    void require_validity() const;
    void retain_workspace_for_diagnostics() noexcept;
    void cleanup_workspace();
};

// validation failureではworkspaceを移動せず、callerがdiagnostic retentionへ遷移できる。
// success時だけreturned capabilityへcleanup ownershipを移す。
ValidatedPackageArtifactPath validate_post_build_package_artifact(
        ArtifactWorkspace&& workspace,
        const ExpectedPackageArtifactPath& expected);

// 複数のvalidated artifact recordとexactly one workspaceを一括所有する。
// 個別recordへcleanup/retention ownershipを分配しない。
class ValidatedPackageArtifactSet final {
    enum class OwnershipState {
        Active,
        WorkspaceCleanupPending,
        Inactive,
    };

    struct Record {
        std::filesystem::path path;
        std::string           leaf_name;
        int                   artifact_descriptor = -1;
        std::uintmax_t        artifact_device = 0;
        std::uintmax_t        artifact_inode = 0;
        std::uintmax_t        artifact_owner = 0;
        bool                  has_signature = false;
        int                   signature_descriptor = -1;
        std::uintmax_t        signature_device = 0;
        std::uintmax_t        signature_inode = 0;
        std::uintmax_t        signature_owner = 0;

        Record(
                std::filesystem::path artifact_path,
                std::string artifact_leaf,
                int retained_artifact_descriptor,
                std::uintmax_t retained_artifact_device,
                std::uintmax_t retained_artifact_inode,
                std::uintmax_t retained_artifact_owner,
                bool retained_signature,
                int retained_signature_descriptor,
                std::uintmax_t retained_signature_device,
                std::uintmax_t retained_signature_inode,
                std::uintmax_t retained_signature_owner) noexcept;
        Record(const Record&) = delete;
        Record& operator=(const Record&) = delete;
        Record(Record&& other) noexcept;
        Record& operator=(Record&&) = delete;
        ~Record() noexcept;
    };

    // LANDMINE(#268): 通常のmember破棄でもrecord FDを閉じてからworkspace
    // cleanupへ進むため、records_をworkspace_より後ろへ保つ。
    ArtifactWorkspace   workspace_;
    std::vector<Record> records_;
    OwnershipState      ownership_state_ = OwnershipState::Active;

    ValidatedPackageArtifactSet(
            ArtifactWorkspace&& workspace,
            std::vector<Record>&& records) noexcept;

    void require_active() const;
    void require_workspace_ownership() const;
    void require_validity_for_owner(
            std::uintmax_t expected_effective_user) const;
    static ValidatedPackageArtifactSet validate_for_owners(
            ArtifactWorkspace&& workspace,
            const ExpectedPackageArtifactSet& expected,
            std::uintmax_t expected_workspace_owner,
            std::uintmax_t expected_artifact_owner,
            std::uintmax_t expected_signature_owner);

    friend ValidatedPackageArtifactSet validate_post_build_package_artifacts(
            ArtifactWorkspace&& workspace,
            const ExpectedPackageArtifactSet& expected);
#ifdef JPACKER_ENABLE_ARTIFACT_WORKSPACE_TEST_HOOKS
    friend ValidatedPackageArtifactSet
    validate_post_build_package_artifacts_for_test(
            ArtifactWorkspace&& workspace,
            const ExpectedPackageArtifactSet& expected,
            std::uintmax_t expected_effective_user);
    friend ValidatedPackageArtifactSet
    validate_post_build_package_artifacts_for_test(
            ArtifactWorkspace&& workspace,
            const ExpectedPackageArtifactSet& expected,
            std::uintmax_t expected_workspace_owner,
            std::uintmax_t expected_artifact_owner,
            std::uintmax_t expected_signature_owner);
#endif

public:
    ValidatedPackageArtifactSet(
            const ValidatedPackageArtifactSet&) = delete;
    ValidatedPackageArtifactSet& operator=(
            const ValidatedPackageArtifactSet&) = delete;
    ValidatedPackageArtifactSet(
            ValidatedPackageArtifactSet&& other) noexcept;
    ValidatedPackageArtifactSet& operator=(
            ValidatedPackageArtifactSet&&) = delete;
    ~ValidatedPackageArtifactSet() noexcept = default;

    std::size_t size() const;
    const std::filesystem::path& path_at(std::size_t index) const;
    const std::filesystem::path& workspace_path() const;

    void require_validity() const;
    void retain_workspace_for_diagnostics();
    void cleanup_workspace();
};

// validation failureではworkspaceをconsumeせず、全件成功後だけaggregateへmoveする。
ValidatedPackageArtifactSet validate_post_build_package_artifacts(
        ArtifactWorkspace&& workspace,
        const ExpectedPackageArtifactSet& expected);

#ifdef JPACKER_ENABLE_ARTIFACT_WORKSPACE_TEST_HOOKS
void require_artifact_workspace_identity_for_test(
        const ArtifactWorkspace& workspace,
        std::uintmax_t expected_effective_user);

ValidatedPackageArtifactPath validate_post_build_package_artifact_for_test(
        ArtifactWorkspace&& workspace,
        const ExpectedPackageArtifactPath& expected,
        std::uintmax_t expected_artifact_owner);

using MultipleArtifactValidationObserverForTest =
        void (*)(const std::filesystem::path& workspace_path);

void set_multiple_artifact_validation_observer_for_test(
        MultipleArtifactValidationObserverForTest observer);

using MultipleArtifactCleanupObserverForTest = void (*)(
        const std::filesystem::path& workspace_path,
        const std::vector<int>& retained_descriptors);

void set_multiple_artifact_cleanup_observer_for_test(
        MultipleArtifactCleanupObserverForTest observer);

ValidatedPackageArtifactSet validate_post_build_package_artifacts_for_test(
        ArtifactWorkspace&& workspace,
        const ExpectedPackageArtifactSet& expected,
        std::uintmax_t expected_artifact_owner);

ValidatedPackageArtifactSet validate_post_build_package_artifacts_for_test(
        ArtifactWorkspace&& workspace,
        const ExpectedPackageArtifactSet& expected,
        std::uintmax_t expected_workspace_owner,
        std::uintmax_t expected_artifact_owner,
        std::uintmax_t expected_signature_owner);
#endif
