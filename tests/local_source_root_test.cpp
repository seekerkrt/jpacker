#include "local_source_root.hpp"

#include <cerrno>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

static_assert(!std::is_copy_constructible_v<LocalSourceRoot>);
static_assert(!std::is_copy_assignable_v<LocalSourceRoot>);
static_assert(std::is_nothrow_move_constructible_v<LocalSourceRoot>);
static_assert(!std::is_move_assignable_v<LocalSourceRoot>);

namespace {

namespace fs = std::filesystem;

constexpr std::string_view PKGBUILD_CONTENT =
    "pkgname=local-root-test\npkgver=1\npkgrel=1\narch=('any')\n";

void expect(bool condition, const std::string& message) {
    if(!condition) throw std::runtime_error(message);
}

class TemporaryTree final {
    fs::path path_;

public:
    TemporaryTree() {
        const std::string template_text =
            (fs::temp_directory_path() /
             "moguet-local-source-root-test-XXXXXX")
                .string();
        std::vector<char> path_template(
            template_text.begin(), template_text.end());
        path_template.push_back('\0');
        char* created_path = ::mkdtemp(path_template.data());
        if(created_path == nullptr) {
            throw std::runtime_error(
                "failed to create local source root test tree");
        }
        path_ = created_path;
    }

    TemporaryTree(const TemporaryTree&) = delete;
    TemporaryTree& operator=(const TemporaryTree&) = delete;

    ~TemporaryTree() noexcept {
        std::error_code remove_error;
        fs::remove_all(path_, remove_error);
    }

    const fs::path& path() const noexcept {
        return path_;
    }
};

class ScopedCurrentDirectory final {
    fs::path previous_;

public:
    explicit ScopedCurrentDirectory(const fs::path& path)
        : previous_(fs::current_path()) {
        fs::current_path(path);
    }

    ScopedCurrentDirectory(const ScopedCurrentDirectory&) = delete;
    ScopedCurrentDirectory& operator=(const ScopedCurrentDirectory&) = delete;

    ~ScopedCurrentDirectory() noexcept {
        std::error_code restore_error;
        fs::current_path(previous_, restore_error);
    }
};

void write_file(const fs::path& path, std::string_view contents) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if(!output) {
        throw std::runtime_error("failed to open test file: " + path.string());
    }
    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    output.close();
    if(!output) {
        throw std::runtime_error("failed to write test file: " + path.string());
    }
    if(::chmod(path.c_str(), 0644) != 0) {
        throw std::runtime_error("failed to set test file mode");
    }
}

fs::path make_root(
    const TemporaryTree& tree, std::string_view name = "root") {
    const fs::path root = tree.path() / std::string(name);
    fs::create_directory(root);
    if(::chmod(root.c_str(), 0755) != 0) {
        throw std::runtime_error("failed to set test root mode");
    }
    write_file(root / "PKGBUILD", PKGBUILD_CONTENT);
    return root;
}

std::string valid_srcinfo(std::string_view identity = "local-root-test") {
    const std::string name(identity);
    return "pkgbase = " + name + "\n" +
           "pkgver = 1\n" +
           "pkgrel = 1\n" +
           "arch = any\n" +
           "pkgname = " + name + "\n";
}

void set_mtime(
    const fs::path& path, std::int64_t seconds,
    std::int64_t nanoseconds) {
    struct stat status{};
    if(::stat(path.c_str(), &status) != 0) {
        throw std::runtime_error("failed to stat test mtime path");
    }
    const struct timespec times[2] = {
        status.st_atim,
        {static_cast<time_t>(seconds),
         static_cast<long>(nanoseconds)}};
    if(::utimensat(AT_FDCWD, path.c_str(), times, 0) != 0) {
        throw std::runtime_error("failed to set test mtime");
    }
}

template <typename Operation>
LocalSourceRootFailure require_failure(
    Operation operation, LocalSourceRootErrorCode expected_code,
    const std::string& context) {
    try {
        operation();
    } catch(const LocalSourceRootError& error) {
        expect(
            error.failure().code == expected_code,
            context + ": failure code differs (expected " +
                std::to_string(static_cast<int>(expected_code)) +
                ", observed " +
                std::to_string(
                    static_cast<int>(error.failure().code)) +
                ")");
        return error.failure();
    }
    throw std::runtime_error(context + ": operation unexpectedly succeeded");
}

bool has_stale_reason(
    const LocalSourceMetadataSnapshot& metadata,
    LocalSourceMetadataStaleReason reason) {
    for(const LocalSourceMetadataStaleReason observed :
        metadata.stale_reasons()) {
        if(observed == reason) return true;
    }
    return false;
}

void test_safe_paths_and_invocation_anchor() {
    TemporaryTree tree;
    const fs::path root_path = make_root(tree);
    expect(!fs::exists(root_path / ".git"), "test root unexpectedly has .git");

    LocalSourceRoot absolute = open_local_source_root(root_path);
    expect(
        absolute.canonical_path() == fs::canonical(root_path),
        "absolute root canonical path differs");
    expect(
        absolute.pkgbuild().contents == PKGBUILD_CONTENT,
        "PKGBUILD exact content snapshot differs");
    expect(
        absolute.metadata().state() == LocalSourceMetadataState::Missing,
        "missing .SRCINFO was not classified Missing");
    expect(
        !absolute.metadata().provenance().has_value(),
        "Missing metadata claims existing .SRCINFO provenance");
    expect(
        absolute.directory_identity().owner ==
            static_cast<std::uintmax_t>(::geteuid()),
        "directory owner identity differs");
    expect(
        absolute.directory_identity().mode == 0755,
        "directory permission mode identity differs");
    expect(
        absolute.pkgbuild().identity.mode == 0644,
        "PKGBUILD permission mode identity differs");
    absolute.require_unchanged_identity();

    const fs::path trailing_separator(root_path.string() + "/");
    LocalSourceRoot with_trailing_separator =
        open_local_source_root(trailing_separator);
    expect(
        with_trailing_separator.canonical_path() ==
            absolute.canonical_path(),
        "safe root with trailing separator was not accepted");
    with_trailing_separator.require_unchanged_identity();

    ScopedCurrentDirectory at_tree(tree.path());
    LocalSourceRoot relative = open_local_source_root("root");
    fs::current_path(fs::temp_directory_path());
    relative.require_unchanged_identity();
    expect(
        relative.canonical_path() == absolute.canonical_path(),
        "relative path did not resolve from invocation anchor");

    fs::current_path(root_path);
    LocalSourceRoot dot = open_local_source_root(".");
    expect(
        dot.canonical_path() == fs::canonical(root_path),
        "dot root canonical path differs");
    expect(!fs::exists(root_path / ".git"), "root inspection created .git");
}

void test_intermediate_symlink_is_not_blanket_rejected() {
    TemporaryTree tree;
    const fs::path parent = tree.path() / "actual-parent";
    fs::create_directory(parent);
    const fs::path root = parent / "root";
    fs::create_directory(root);
    write_file(root / "PKGBUILD", PKGBUILD_CONTENT);
    fs::create_directory_symlink(parent, tree.path() / "parent-link");

    ScopedCurrentDirectory at_tree(tree.path());
    LocalSourceRoot opened = open_local_source_root("parent-link/root");
    expect(
        opened.canonical_path() == fs::canonical(root),
        "allowed intermediate symlink did not reach the root");
    opened.require_unchanged_identity();
}

void test_execute_only_root_does_not_require_directory_read_permission() {
    TemporaryTree tree;
    const fs::path root = make_root(tree);
    if(::chmod(root.c_str(), 0111) != 0) {
        throw std::runtime_error("failed to set execute-only root mode");
    }

    {
        LocalSourceRoot opened = open_local_source_root(root);
        expect(
            opened.metadata().state() == LocalSourceMetadataState::Missing,
            "execute-only safe root metadata state differs");
        opened.require_unchanged_identity();
    }

    if(::chmod(root.c_str(), 0755) != 0) {
        throw std::runtime_error("failed to restore test root mode");
    }
}

void test_usable_and_invalid_metadata() {
    {
        TemporaryTree tree;
        const fs::path root = make_root(tree);
        const std::string source = valid_srcinfo();
        write_file(root / ".SRCINFO", source);
        set_mtime(root / "PKGBUILD", 1'700'000'000, 100);
        set_mtime(root / ".SRCINFO", 1'700'000'000, 200);

        LocalSourceRoot opened = open_local_source_root(root);
        const LocalSourceMetadataSnapshot& metadata = opened.metadata();
        expect(
            metadata.state() ==
                LocalSourceMetadataState::UsableUnverified,
            "valid existing metadata was not UsableUnverified");
        expect(
            metadata.provenance() ==
                std::optional<LocalSourceMetadataProvenance>{
                    LocalSourceMetadataProvenance::
                        ExistingSrcinfo},
            "existing metadata provenance differs");
        expect(metadata.file() != nullptr, "usable metadata has no file snapshot");
        expect(metadata.file()->contents == source, "SRCINFO content differs");
        expect(
            metadata.parse_result() != nullptr &&
                metadata.parse_result()->is_success(),
            "usable metadata does not retain successful parse result");
        expect(
            metadata.parse_result()->metadata()->package_base ==
                "local-root-test",
            "typed PackageBase differs");
        opened.require_unchanged_identity();
    }

    {
        TemporaryTree tree;
        const fs::path root = make_root(tree);
        write_file(
            root / ".SRCINFO",
            "pkgbase = invalid\npkgver = 1\narch = any\n"
            "pkgname = invalid\n");

        LocalSourceRoot opened = open_local_source_root(root);
        const LocalSourceMetadataSnapshot& metadata = opened.metadata();
        expect(
            metadata.state() == LocalSourceMetadataState::Invalid,
            "invalid safe metadata was not Invalid");
        expect(
            metadata.parse_result() != nullptr &&
                !metadata.parse_result()->is_success(),
            "invalid metadata parse failure is absent");
        expect(
            metadata.parse_result()->failure() != nullptr &&
                metadata.parse_result()->failure()->code ==
                    LocalPackageMetadataParseErrorCode::
                        MissingPkgrel,
            "invalid metadata parse reason differs");
        opened.require_unchanged_identity();
    }
}

void test_known_stale_signals() {
    {
        TemporaryTree tree;
        const fs::path root = make_root(tree);
        write_file(root / ".SRCINFO", valid_srcinfo());
        set_mtime(root / "PKGBUILD", 1'700'000'000, 201);
        set_mtime(root / ".SRCINFO", 1'700'000'000, 200);

        LocalSourceRoot opened = open_local_source_root(root);
        expect(
            opened.metadata().state() ==
                LocalSourceMetadataState::KnownStale,
            "nanosecond-newer PKGBUILD was not KnownStale");
        expect(
            has_stale_reason(
                opened.metadata(),
                LocalSourceMetadataStaleReason::PkgbuildNewer),
            "PKGBUILD-newer stale reason is absent");
    }

    {
        TemporaryTree tree;
        const fs::path root = make_root(tree);
        write_file(root / ".SRCINFO", valid_srcinfo());
        set_mtime(root / "PKGBUILD", 1'700'000'000, 100);
        set_mtime(root / ".SRCINFO", 1'700'000'000, 200);

        // true represents any parsed one-off assignment, including V=.
        LocalSourceRoot opened = open_local_source_root(root, true);
        expect(
            opened.metadata().state() ==
                LocalSourceMetadataState::KnownStale,
            "one-off empty assignment was not KnownStale");
        expect(
            has_stale_reason(
                opened.metadata(),
                LocalSourceMetadataStaleReason::
                    OneOffEnvironmentAssignment),
            "one-off assignment stale reason is absent");
    }
}

void test_root_and_pkgbuild_safety() {
    {
        TemporaryTree tree;
        require_failure(
            [&] {
                static_cast<void>(open_local_source_root(
                    tree.path() / "missing-root"));
            },
            LocalSourceRootErrorCode::Missing,
            "missing root");
    }

    {
        TemporaryTree tree;
        const fs::path root = make_root(tree);
        const fs::path link = tree.path() / "root-link";
        fs::create_directory_symlink(root, link);
        require_failure(
            [&] { static_cast<void>(open_local_source_root(link)); },
            LocalSourceRootErrorCode::Symlink,
            "final root symlink");
        const fs::path trailing_separator(link.string() + "/");
        require_failure(
            [&] {
                static_cast<void>(
                    open_local_source_root(trailing_separator));
            },
            LocalSourceRootErrorCode::Symlink,
            "trailing separator final root symlink");
    }

    {
        TemporaryTree tree;
        const fs::path root = make_root(tree);
        LocalSourceRootTestOverrides overrides;
        overrides.root_observed_owner =
            static_cast<std::uintmax_t>(::geteuid()) + 1;
        require_failure(
            [&] {
                static_cast<void>(open_local_source_root_for_test(
                    root, false, overrides));
            },
            LocalSourceRootErrorCode::OwnershipMismatch,
            "wrong-owner root hook");
    }

    {
        TemporaryTree tree;
        const fs::path root = make_root(tree);
        if(::chmod(root.c_str(), 01777) != 0) {
            throw std::runtime_error("failed to set sticky unsafe root mode");
        }
        require_failure(
            [&] { static_cast<void>(open_local_source_root(root)); },
            LocalSourceRootErrorCode::UnsafePermissions,
            "sticky group/other-writable root");
    }

    {
        TemporaryTree tree;
        const fs::path direct_file = tree.path() / "PKGBUILD";
        write_file(direct_file, PKGBUILD_CONTENT);
        require_failure(
            [&] { static_cast<void>(open_local_source_root(direct_file)); },
            LocalSourceRootErrorCode::NotDirectory,
            "direct PKGBUILD operand");
    }

    {
        TemporaryTree tree;
        const fs::path root = tree.path() / "missing-pkgbuild";
        fs::create_directory(root);
        require_failure(
            [&] { static_cast<void>(open_local_source_root(root)); },
            LocalSourceRootErrorCode::Missing,
            "missing PKGBUILD");
    }

    {
        TemporaryTree tree;
        const fs::path root = tree.path() / "symlink-pkgbuild";
        fs::create_directory(root);
        write_file(tree.path() / "outside", PKGBUILD_CONTENT);
        fs::create_symlink(tree.path() / "outside", root / "PKGBUILD");
        require_failure(
            [&] { static_cast<void>(open_local_source_root(root)); },
            LocalSourceRootErrorCode::Symlink,
            "symlink PKGBUILD");
    }

    {
        TemporaryTree tree;
        const fs::path root = tree.path() / "directory-pkgbuild";
        fs::create_directory(root);
        fs::create_directory(root / "PKGBUILD");
        require_failure(
            [&] { static_cast<void>(open_local_source_root(root)); },
            LocalSourceRootErrorCode::NotRegularFile,
            "directory PKGBUILD");
    }

    {
        TemporaryTree tree;
        const fs::path root = make_root(tree);
        if(::chmod((root / "PKGBUILD").c_str(), 0664) != 0) {
            throw std::runtime_error("failed to set unsafe PKGBUILD mode");
        }
        require_failure(
            [&] { static_cast<void>(open_local_source_root(root)); },
            LocalSourceRootErrorCode::UnsafePermissions,
            "group-writable PKGBUILD");
    }

    {
        TemporaryTree tree;
        const fs::path root = make_root(tree);
        LocalSourceRootTestOverrides overrides;
        overrides.pkgbuild_observed_owner =
            static_cast<std::uintmax_t>(::geteuid()) + 1;
        require_failure(
            [&] {
                static_cast<void>(open_local_source_root_for_test(
                    root, false, overrides));
            },
            LocalSourceRootErrorCode::OwnershipMismatch,
            "wrong-owner PKGBUILD hook");
    }

    {
        TemporaryTree tree;
        const fs::path root = make_root(tree);
        LocalSourceRootTestOverrides overrides;
        overrides.injected_failure = LocalSourceRootInjectedFailure{
            LocalSourceRootTestFailurePoint::RootInspection, EIO};
        const LocalSourceRootFailure failure = require_failure(
            [&] {
                static_cast<void>(open_local_source_root_for_test(
                    root, false, overrides));
            },
            LocalSourceRootErrorCode::MetadataFailure,
            "root inspection EIO");
        expect(
            failure.system_error ==
                std::optional<std::error_code>{
                    std::error_code(EIO, std::generic_category())},
            "root inspection EIO system error differs");
    }
}

void test_unsafe_metadata_is_inspectable_but_not_usable() {
    {
        TemporaryTree tree;
        const fs::path root = make_root(tree);
        write_file(tree.path() / "outside-srcinfo", valid_srcinfo());
        fs::create_symlink(
            tree.path() / "outside-srcinfo", root / ".SRCINFO");

        LocalSourceRoot opened = open_local_source_root(root);
        expect(
            opened.metadata().state() == LocalSourceMetadataState::Unsafe,
            "symlink SRCINFO was not Unsafe");
        expect(
            opened.metadata().unsafe_failure() != nullptr &&
                opened.metadata().unsafe_failure()->code ==
                    LocalSourceRootErrorCode::Symlink,
            "unsafe SRCINFO reason differs");
        require_failure(
            [&] { opened.require_unchanged_identity(); },
            LocalSourceRootErrorCode::UnsafeMetadata,
            "Unsafe metadata operation revalidation");
    }

    {
        TemporaryTree tree;
        const fs::path root = make_root(tree);
        fs::create_directory(root / ".SRCINFO");

        LocalSourceRoot opened = open_local_source_root(root);
        expect(
            opened.metadata().state() == LocalSourceMetadataState::Unsafe,
            "directory SRCINFO was not Unsafe");
        expect(
            opened.metadata().unsafe_failure() != nullptr &&
                opened.metadata().unsafe_failure()->code ==
                    LocalSourceRootErrorCode::NotRegularFile,
            "directory SRCINFO reason differs");
    }

    {
        TemporaryTree tree;
        const fs::path root = make_root(tree);
        write_file(root / ".SRCINFO", valid_srcinfo());
        LocalSourceRootTestOverrides overrides;
        overrides.srcinfo_observed_owner =
            static_cast<std::uintmax_t>(::geteuid()) + 1;

        LocalSourceRoot opened =
            open_local_source_root_for_test(root, false, overrides);
        expect(
            opened.metadata().state() == LocalSourceMetadataState::Unsafe,
            "wrong-owner SRCINFO was not Unsafe");
        expect(
            opened.metadata().unsafe_failure() != nullptr &&
                opened.metadata().unsafe_failure()->code ==
                    LocalSourceRootErrorCode::OwnershipMismatch,
            "wrong-owner SRCINFO reason differs");
    }

    {
        TemporaryTree tree;
        const fs::path root = make_root(tree);
        write_file(root / ".SRCINFO", valid_srcinfo());
        LocalSourceRootTestOverrides overrides;
        overrides.injected_failure = LocalSourceRootInjectedFailure{
            LocalSourceRootTestFailurePoint::SrcinfoRead, EIO};
        LocalSourceRoot opened =
            open_local_source_root_for_test(root, false, overrides);
        expect(
            opened.metadata().state() == LocalSourceMetadataState::Unsafe,
            "SRCINFO EIO was not Unsafe");
        expect(
            opened.metadata().unsafe_failure() != nullptr &&
                opened.metadata().unsafe_failure()->code ==
                    LocalSourceRootErrorCode::ReadFailure,
            "SRCINFO EIO reason differs");
        expect(
            opened.metadata().unsafe_failure()->system_error ==
                std::optional<std::error_code>{
                    std::error_code(EIO, std::generic_category())},
            "SRCINFO EIO system error differs");
    }

    {
        TemporaryTree tree;
        const fs::path root = make_root(tree);
        write_file(root / ".SRCINFO", valid_srcinfo());
        if(::chmod((root / ".SRCINFO").c_str(), 0664) != 0) {
            throw std::runtime_error("failed to set unsafe SRCINFO mode");
        }
        LocalSourceRoot opened = open_local_source_root(root);
        expect(
            opened.metadata().state() == LocalSourceMetadataState::Unsafe,
            "group-writable SRCINFO was not Unsafe");
        expect(
            opened.metadata().unsafe_failure() != nullptr &&
                opened.metadata().unsafe_failure()->code ==
                    LocalSourceRootErrorCode::UnsafePermissions,
            "group-writable SRCINFO reason differs");
    }
}

void test_root_and_pkgbuild_revalidation() {
    {
        TemporaryTree tree;
        const fs::path root = make_root(tree);
        LocalSourceRoot opened = open_local_source_root(root);
        fs::rename(root, tree.path() / "original-root");
        static_cast<void>(make_root(tree));
        require_failure(
            [&] { opened.require_unchanged_identity(); },
            LocalSourceRootErrorCode::ConcurrentReplacement,
            "root pathname replacement");
    }

    {
        TemporaryTree tree;
        const fs::path root = make_root(tree);
        LocalSourceRoot opened = open_local_source_root(root);
        if(::chmod(root.c_str(), 0777) != 0) {
            throw std::runtime_error("failed to mutate root mode");
        }
        require_failure(
            [&] { opened.require_unchanged_identity(); },
            LocalSourceRootErrorCode::UnsafePermissions,
            "root unsafe mode change");
    }

    {
        TemporaryTree tree;
        const fs::path root = make_root(tree);
        LocalSourceRoot opened = open_local_source_root(root);
        fs::rename(root / "PKGBUILD", root / "PKGBUILD.old");
        write_file(root / "PKGBUILD", PKGBUILD_CONTENT);
        require_failure(
            [&] { opened.require_unchanged_identity(); },
            LocalSourceRootErrorCode::ConcurrentReplacement,
            "PKGBUILD pathname replacement");
    }

    {
        TemporaryTree tree;
        const fs::path root = make_root(tree);
        LocalSourceRoot opened = open_local_source_root(root);
        write_file(root / "PKGBUILD", "pkgname=changed-in-place\n");
        require_failure(
            [&] { opened.require_unchanged_identity(); },
            LocalSourceRootErrorCode::ContentChanged,
            "PKGBUILD in-place content change");
    }

    {
        TemporaryTree tree;
        const fs::path root = make_root(tree);
        LocalSourceRoot opened = open_local_source_root(root);
        if(::chmod((root / "PKGBUILD").c_str(), 0666) != 0) {
            throw std::runtime_error("failed to mutate PKGBUILD mode");
        }
        require_failure(
            [&] { opened.require_unchanged_identity(); },
            LocalSourceRootErrorCode::UnsafePermissions,
            "PKGBUILD unsafe mode change");
    }
}

void test_metadata_revalidation() {
    {
        TemporaryTree tree;
        const fs::path root = make_root(tree);
        write_file(root / ".SRCINFO", valid_srcinfo());
        LocalSourceRoot opened = open_local_source_root(root);
        fs::rename(root / ".SRCINFO", root / ".SRCINFO.old");
        write_file(root / ".SRCINFO", valid_srcinfo());
        require_failure(
            [&] { opened.require_unchanged_identity(); },
            LocalSourceRootErrorCode::ConcurrentReplacement,
            "SRCINFO pathname replacement");
    }

    {
        TemporaryTree tree;
        const fs::path root = make_root(tree);
        write_file(root / ".SRCINFO", valid_srcinfo());
        LocalSourceRoot opened = open_local_source_root(root);
        write_file(root / ".SRCINFO", valid_srcinfo("changed"));
        require_failure(
            [&] { opened.require_unchanged_identity(); },
            LocalSourceRootErrorCode::ContentChanged,
            "SRCINFO in-place content change");
    }

    {
        TemporaryTree tree;
        const fs::path root = make_root(tree);
        write_file(root / ".SRCINFO", valid_srcinfo());
        LocalSourceRoot opened = open_local_source_root(root);
        if(::chmod((root / ".SRCINFO").c_str(), 0666) != 0) {
            throw std::runtime_error("failed to mutate SRCINFO mode");
        }
        require_failure(
            [&] { opened.require_unchanged_identity(); },
            LocalSourceRootErrorCode::UnsafePermissions,
            "SRCINFO unsafe mode change");
    }

    {
        TemporaryTree tree;
        const fs::path root = make_root(tree);
        LocalSourceRoot opened = open_local_source_root(root);
        write_file(root / ".SRCINFO", valid_srcinfo());
        require_failure(
            [&] { opened.require_unchanged_identity(); },
            LocalSourceRootErrorCode::ConcurrentReplacement,
            "previously missing SRCINFO appeared");
    }
}

} // namespace

int main() {
    try {
        test_safe_paths_and_invocation_anchor();
        test_intermediate_symlink_is_not_blanket_rejected();
        test_execute_only_root_does_not_require_directory_read_permission();
        test_usable_and_invalid_metadata();
        test_known_stale_signals();
        test_root_and_pkgbuild_safety();
        test_unsafe_metadata_is_inspectable_but_not_usable();
        test_root_and_pkgbuild_revalidation();
        test_metadata_revalidation();
        std::cout << "local source root tests passed\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr << "local source root test failed: " << error.what() << '\n';
        return 1;
    }
}
