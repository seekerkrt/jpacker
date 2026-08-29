#include "artifact_identity.hpp"

#include "artifact_archive_metadata.hpp"
#include "artifact_workspace.hpp"
#include "stubs/artifact-identity/process_stub.hpp"
#include "trusted_cache.hpp"
#include "trusted_cache_test_support.hpp"

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

using ArtifactIdentityQuery = ArtifactPackageIdentity (*)(
    const ValidatedPackageArtifactPath&);
static_assert(!std::is_invocable_v<ArtifactIdentityQuery, const std::filesystem::path&>);
using ArchiveMetadataQuery = ArtifactPackageIdentity (*)(
    const artifact_archive_metadata::QueryAuthority&);
static_assert(
    !std::is_default_constructible_v<
        artifact_archive_metadata::QueryAuthority>);
static_assert(
    !std::is_constructible_v<
        artifact_archive_metadata::QueryAuthority,
        const std::filesystem::path&>);
static_assert(
    !std::is_invocable_v<
        ArchiveMetadataQuery,
        const std::filesystem::path&>);

namespace {

namespace fs = std::filesystem;
namespace stub = artifact_identity_test_stub;

void expect(bool condition, const std::string& message) {
    if(!condition) throw std::runtime_error(message);
}

template <typename Callable>
void expect_runtime_error(Callable callable, const std::string& context) {
    try {
        callable();
    } catch(const std::runtime_error&) {
        return;
    } catch(const std::exception& error) {
        throw std::runtime_error(
            context + ": unexpected exception category: " + error.what());
    }
    throw std::runtime_error(context + ": expected runtime_error");
}

class TemporaryCacheHome final {
    fs::path path_;
    std::optional<std::string> previous_xdg_cache_home_;

public:
    TemporaryCacheHome() {
        std::vector<char> path_template;
        const std::string template_text =
            (fs::temp_directory_path() /
             "moguet-artifact-identity-test-XXXXXX")
                .string();
        path_template.assign(template_text.begin(), template_text.end());
        path_template.push_back('\0');

        char* created_path = mkdtemp(path_template.data());
        if(created_path == nullptr) {
            throw std::runtime_error("Failed to create artifact identity test directory.");
        }
        path_ = created_path;

        const char* previous = std::getenv("XDG_CACHE_HOME");
        if(previous != nullptr) previous_xdg_cache_home_ = previous;
        if(setenv("XDG_CACHE_HOME", path_.c_str(), 1) != 0) {
            std::error_code error;
            fs::remove_all(path_, error);
            throw std::runtime_error("Failed to set artifact identity test cache home.");
        }
    }

    TemporaryCacheHome(const TemporaryCacheHome&) = delete;
    TemporaryCacheHome& operator=(const TemporaryCacheHome&) = delete;

    ~TemporaryCacheHome() {
        if(previous_xdg_cache_home_.has_value())
            static_cast<void>(setenv(
                "XDG_CACHE_HOME", previous_xdg_cache_home_->c_str(), 1));
        else
            static_cast<void>(unsetenv("XDG_CACHE_HOME"));

        std::error_code error;
        fs::remove_all(path_, error);
    }
};

class ArtifactFixture final {
    TemporaryCacheHome cache_home_;
    std::unique_ptr<ValidatedPackageArtifactPath> artifact_;

public:
    explicit ArtifactFixture(const std::string& artifact_leaf_name) {
        ArtifactWorkspace workspace = create_artifact_workspace(
            prepare_private_trusted_cache_root(
                prepare_test_trusted_cache_root()));
        fs::path artifact_path = workspace.path() / artifact_leaf_name;

        ExpectedPackageArtifactPath expected =
            validate_makepkg_packagelist_output(
                workspace, artifact_path.string() + "\n");
        std::ofstream artifact_file(artifact_path, std::ios::binary);
        if(!artifact_file) {
            throw std::runtime_error("Failed to create artifact identity fixture.");
        }
        artifact_file << "fixture";
        artifact_file.close();
        if(!artifact_file) {
            throw std::runtime_error("Failed to finish artifact identity fixture.");
        }

        ValidatedPackageArtifactPath artifact =
            validate_post_build_package_artifact(
                std::move(workspace), expected);
        artifact_ = std::make_unique<ValidatedPackageArtifactPath>(
            std::move(artifact));
    }

    ArtifactFixture(const ArtifactFixture&) = delete;
    ArtifactFixture& operator=(const ArtifactFixture&) = delete;

    const ValidatedPackageArtifactPath& artifact() const {
        return *artifact_;
    }

    const fs::path& artifact_path() const {
        return artifact_->path();
    }
};

class ActualArchiveFixture final {
    fs::path root_;
    std::size_t next_archive_index_ = 0;

    static void run_bsdtar(
        const fs::path& package_root,
        const fs::path& archive_path) {
        const pid_t child = fork();
        if(child < 0) {
            throw std::runtime_error(
                "Failed to fork deterministic package archive fixture.");
        }
        if(child == 0) {
            execl(
                "/usr/bin/bsdtar", "bsdtar", "--zstd", "-cf",
                archive_path.c_str(), "-C", package_root.c_str(),
                ".PKGINFO", static_cast<char*>(nullptr));
            _exit(127);
        }

        int status = 0;
        if(waitpid(child, &status, 0) != child ||
           !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            throw std::runtime_error(
                "Failed to create deterministic package archive fixture.");
        }
    }

public:
    ActualArchiveFixture() {
        std::vector<char> path_template;
        const std::string template_text =
            (fs::temp_directory_path() /
             "moguet-actual-archive-identity-XXXXXX")
                .string();
        path_template.assign(template_text.begin(), template_text.end());
        path_template.push_back('\0');
        char* created_path = mkdtemp(path_template.data());
        if(created_path == nullptr) {
            throw std::runtime_error(
                "Failed to create actual archive identity fixture root.");
        }
        root_ = created_path;
    }

    ActualArchiveFixture(const ActualArchiveFixture&) = delete;
    ActualArchiveFixture& operator=(const ActualArchiveFixture&) = delete;

    ~ActualArchiveFixture() noexcept {
        std::error_code error;
        fs::remove_all(root_, error);
    }

    fs::path create_archive(
        const std::string& package_name,
        const std::string& full_version,
        std::optional<std::string> package_base,
        std::optional<std::string> architecture) {
        const std::size_t index = next_archive_index_++;
        const fs::path package_root =
            root_ / ("package-" + std::to_string(index));
        fs::create_directory(package_root);

        std::ofstream pkginfo(package_root / ".PKGINFO");
        if(!pkginfo) {
            throw std::runtime_error(
                "Failed to create deterministic .PKGINFO fixture.");
        }
        pkginfo << "pkgname = " << package_name << '\n';
        if(package_base.has_value()) {
            pkginfo << "pkgbase = " << package_base.value() << '\n';
        }
        pkginfo << "pkgver = " << full_version << '\n';
        pkginfo << "pkgdesc = Moguet Issue 485 archive fixture\n";
        pkginfo << "url = https://example.invalid/moguet-issue-485\n";
        pkginfo << "builddate = 1\n";
        pkginfo << "packager = Moguet tests\n";
        pkginfo << "size = 0\n";
        if(architecture.has_value()) {
            pkginfo << "arch = " << architecture.value() << '\n';
        }
        pkginfo << "license = MIT\n";
        pkginfo.close();
        if(!pkginfo) {
            throw std::runtime_error(
                "Failed to finish deterministic .PKGINFO fixture.");
        }

        const fs::path archive_path =
            root_ /
            (package_name + "-" + std::to_string(index) +
             ".pkg.tar.zst");
        run_bsdtar(package_root, archive_path);
        return archive_path;
    }

    fs::path create_invalid_archive() {
        const fs::path archive_path = root_ / "invalid.pkg.tar.zst";
        std::ofstream archive(archive_path, std::ios::binary);
        archive << "not a package archive";
        archive.close();
        if(!archive) {
            throw std::runtime_error(
                "Failed to create invalid archive fixture.");
        }
        return archive_path;
    }
};

ArtifactPackageIdentity query_with_result(
    const ValidatedPackageArtifactPath& artifact,
    const std::string& output,
    int exit_code = 0) {
    stub::reset_process_stub();
    stub::set_captured_command_result(
        CapturedCommandResult{output, exit_code});
    return query_artifact_package_identity(artifact);
}

std::string expected_shell_quote(const std::string& value) {
    std::string quoted = "'";
    for(char character : value) {
        if(character == '\'')
            quoted += "'\\''";
        else
            quoted += character;
    }
    quoted += "'";
    return quoted;
}

std::string expected_identity_command(const fs::path& artifact_path) {
    return "LC_ALL=C 'pacman' '-Qp' '--color' 'never' '--' " +
           expected_shell_quote(artifact_path.string());
}

void test_name_and_epoch_full_version_success(
    const ValidatedPackageArtifactPath& artifact) {
    ArtifactPackageIdentity identity =
        query_with_result(artifact, "sample-package 2:1.4.0-3\n");

    expect(identity.package_name == "sample-package", "Package name differs");
    expect(identity.full_version == "2:1.4.0-3", "Epoch full version differs");
    expect(stub::capture_command_call_count() == 1, "pacman was not called once");

    identity = query_with_result(artifact, "sample-package 1.4.0-3");
    expect(
        identity.full_version == "1.4.0-3",
        "Single record without line terminator was not preserved");
}

void test_command_failure(const ValidatedPackageArtifactPath& artifact) {
    expect_runtime_error(
        [&artifact]() {
            static_cast<void>(query_with_result(
                artifact, "sample-package 1-1\n", 127));
        },
        "command failure");
}

void test_invalid_archive_result(const ValidatedPackageArtifactPath& artifact) {
    expect_runtime_error(
        [&artifact]() {
            static_cast<void>(query_with_result(
                artifact, "Unrecognized archive format\n", 1));
        },
        "invalid archive result");
}

void test_malformed_outputs(const ValidatedPackageArtifactPath& artifact) {
    const std::vector<std::pair<std::string, std::string>> cases = {
        {"empty output", ""},
        {"blank-only output", "\n"},
        {"space-only output", "   \n"},
        {"extra output line", "sample-package 1-1\nother-package 2-1\n"},
        {"trailing blank line", "sample-package 1-1\n\n"},
        {"missing space", "sample-package-1-1\n"},
        {"tab separator", "sample-package\t1-1\n"},
        {"multiple spaces", "sample-package  1-1\n"},
        {"empty name", " 1-1\n"},
        {"empty version", "sample-package \n"},
        {"invalid package name", "-sample-package 1-1\n"},
        {"carriage return", "sample-package 1-1\r\n"},
        {"vertical tab", "sample-package 1-1\v\n"},
    };

    for(const auto& [context, output] : cases) {
        expect_runtime_error(
            [&artifact, &output]() {
                static_cast<void>(query_with_result(artifact, output));
            },
            context);
    }

    std::string null_output = "sample-package 1";
    null_output.push_back('\0');
    null_output += "-1\n";
    expect_runtime_error(
        [&artifact, &null_output]() {
            static_cast<void>(query_with_result(artifact, null_output));
        },
        "null control character");

    std::string delete_output = "sample-package 1";
    delete_output.push_back(static_cast<char>(0x7f));
    delete_output += "-1\n";
    expect_runtime_error(
        [&artifact, &delete_output]() {
            static_cast<void>(query_with_result(artifact, delete_output));
        },
        "delete control character");
}

void test_exact_safe_command(const ValidatedPackageArtifactPath& artifact) {
    static_cast<void>(query_with_result(artifact, "sample-package 1-1\n"));

    const std::string command = stub::last_captured_command();
    expect(
        command == expected_identity_command(artifact.path()),
        "Artifact identity command differs");
    expect(
        command.find("'-Qp' '--color' 'never'") != std::string::npos,
        "Archive-only query or deterministic color option is missing");
    expect(
        command.find("'--' " + expected_shell_quote(artifact.path().string())) !=
            std::string::npos,
        "Semantic -- is not immediately before artifact path");
    expect(command.find("sudo") == std::string::npos, "Identity command contains sudo");
    expect(
        command.find("'-U'") == std::string::npos &&
            command.find("'--print'") == std::string::npos &&
            command.find("'--print-format'") == std::string::npos,
        "Identity command contains transaction projection options");
}

void test_special_artifact_path_command(const std::string& artifact_leaf_name) {
    ArtifactFixture fixture(artifact_leaf_name);
    static_cast<void>(query_with_result(
        fixture.artifact(), "sample-package 1-1\n"));
    expect(
        stub::last_captured_command() ==
            expected_identity_command(fixture.artifact_path()),
        "Special artifact path command differs for " + artifact_leaf_name);
}

fs::path g_artifact_path_to_replace;

void replace_artifact_during_capture() {
    fs::path original_path = g_artifact_path_to_replace;
    original_path += ".original";
    fs::rename(g_artifact_path_to_replace, original_path);

    std::ofstream replacement(g_artifact_path_to_replace, std::ios::binary);
    if(!replacement) {
        throw std::runtime_error("Failed to replace artifact during capture.");
    }
    replacement << "replacement";
}

void test_pre_command_artifact_revalidation() {
    ArtifactFixture fixture("pre-validation.pkg.tar.zst");
    fs::path original_path = fixture.artifact_path();
    original_path += ".original";
    fs::rename(fixture.artifact_path(), original_path);
    std::ofstream(fixture.artifact_path(), std::ios::binary) << "replacement";

    stub::reset_process_stub();
    stub::set_captured_command_result(
        CapturedCommandResult{"sample-package 1-1\n", 0});
    expect_runtime_error(
        [&fixture]() {
            static_cast<void>(query_artifact_package_identity(
                fixture.artifact()));
        },
        "pre-command artifact revalidation");
    expect(
        stub::capture_command_call_count() == 0,
        "pacman ran after pre-command artifact identity mismatch");
}

void test_post_command_artifact_revalidation() {
    ArtifactFixture fixture("post-validation.pkg.tar.zst");
    g_artifact_path_to_replace = fixture.artifact_path();

    stub::reset_process_stub();
    stub::set_captured_command_result(
        CapturedCommandResult{"sample-package 1-1\n", 0});
    stub::set_capture_hook(replace_artifact_during_capture);
    expect_runtime_error(
        [&fixture]() {
            static_cast<void>(query_artifact_package_identity(
                fixture.artifact()));
        },
        "post-command artifact revalidation");
    expect(
        stub::capture_command_call_count() == 1,
        "pacman call count differs for post-command replacement");

    g_artifact_path_to_replace.clear();
    stub::reset_process_stub();
}

void test_actual_archive_metadata_is_lossless_and_read_only() {
    ActualArchiveFixture fixture;
    const fs::path single = fixture.create_archive(
        "single-child", "2:1.4.0-3", "single-base", "x86_64");
    const fs::path split_child = fixture.create_archive(
        "foo-libs", "1.0-1", "foo", "any");
    const fs::path split_sibling = fixture.create_archive(
        "foo", "1.0-1", "foo", "armv7h");
    const fs::path missing_base = fixture.create_archive(
        "missing-base", "1.0-1", std::nullopt, "x86_64");
    const fs::path missing_arch = fixture.create_archive(
        "missing-arch", "1.0-1", "missing-arch", std::nullopt);
    const fs::path malformed_base = fixture.create_archive(
        "malformed-base", "1.0-1", "-invalid-base", "x86_64");
    const fs::path malformed_arch = fixture.create_archive(
        "malformed-arch", "1.0-1", "malformed-arch", "invalid arch");

    struct stat before{};
    struct stat after{};
    expect(stat(single.c_str(), &before) == 0, "actual archive stat failed");
    const ArtifactPackageIdentity single_identity =
        artifact_archive_metadata::query_with_libalpm_for_test(single);
    expect(stat(single.c_str(), &after) == 0, "actual archive restat failed");
    expect(
        single_identity.package_name == "single-child" &&
            single_identity.full_version == "2:1.4.0-3" &&
            single_identity.package_base.state() ==
                ArtifactMetadataValueState::Known &&
            single_identity.package_base.value() != nullptr &&
            *single_identity.package_base.value() == "single-base" &&
            single_identity.architecture.state() ==
                ArtifactMetadataValueState::Known &&
            single_identity.architecture.value() != nullptr &&
            *single_identity.architecture.value() == "x86_64",
        "single actual archive identity was not retained losslessly");
    expect(
        before.st_dev == after.st_dev && before.st_ino == after.st_ino &&
            before.st_size == after.st_size &&
            before.st_mtim.tv_sec == after.st_mtim.tv_sec &&
            before.st_mtim.tv_nsec == after.st_mtim.tv_nsec,
        "read-only archive metadata query mutated the archive");

    const ArtifactPackageIdentity child_identity =
        artifact_archive_metadata::query_with_libalpm_for_test(split_child);
    const ArtifactPackageIdentity sibling_identity =
        artifact_archive_metadata::query_with_libalpm_for_test(split_sibling);
    expect(
        child_identity.package_name == "foo-libs" &&
            sibling_identity.package_name == "foo" &&
            child_identity.package_base.value() != nullptr &&
            sibling_identity.package_base.value() != nullptr &&
            *child_identity.package_base.value() == "foo" &&
            *sibling_identity.package_base.value() == "foo" &&
            child_identity.architecture.value() != nullptr &&
            *child_identity.architecture.value() == "any" &&
            sibling_identity.architecture.value() != nullptr &&
            *sibling_identity.architecture.value() == "armv7h",
        "split PackageBase child/sibling or architecture identity collapsed");

    const ArtifactPackageIdentity missing_base_identity =
        artifact_archive_metadata::query_with_libalpm_for_test(missing_base);
    expect(
        missing_base_identity.package_base.state() ==
                ArtifactMetadataValueState::Missing &&
            missing_base_identity.package_base.value() == nullptr,
        "missing actual PackageBase was filled from package name");

    const ArtifactPackageIdentity missing_arch_identity =
        artifact_archive_metadata::query_with_libalpm_for_test(missing_arch);
    expect(
        missing_arch_identity.architecture.state() ==
                ArtifactMetadataValueState::Missing &&
            missing_arch_identity.architecture.value() == nullptr,
        "missing actual architecture was presented as known");

    const ArtifactPackageIdentity malformed_base_identity =
        artifact_archive_metadata::query_with_libalpm_for_test(malformed_base);
    expect(
        malformed_base_identity.package_base.state() ==
                ArtifactMetadataValueState::Malformed &&
            malformed_base_identity.package_base.value() == nullptr,
        "malformed actual PackageBase was presented as known");

    const ArtifactPackageIdentity malformed_arch_identity =
        artifact_archive_metadata::query_with_libalpm_for_test(malformed_arch);
    expect(
        malformed_arch_identity.architecture.state() ==
                ArtifactMetadataValueState::Malformed &&
            malformed_arch_identity.architecture.value() == nullptr,
        "malformed actual architecture was presented as known");

    const fs::path invalid_archive = fixture.create_invalid_archive();
    expect_runtime_error(
        [&invalid_archive]() {
            static_cast<void>(
                artifact_archive_metadata::query_with_libalpm_for_test(
                    invalid_archive));
        },
        "unavailable actual archive metadata");
}

} // namespace

int main() {
    try {
        {
            ArtifactFixture fixture("sample-package-1-1-x86_64.pkg.tar.zst");
            test_name_and_epoch_full_version_success(fixture.artifact());
            test_command_failure(fixture.artifact());
            test_invalid_archive_result(fixture.artifact());
            test_malformed_outputs(fixture.artifact());
            test_exact_safe_command(fixture.artifact());
        }

        test_special_artifact_path_command(
            "sample package-1-1-x86_64.pkg.tar.zst");
        test_special_artifact_path_command(
            "sample'package-1-1-x86_64.pkg.tar.zst");
        test_special_artifact_path_command(
            "-sample-package-1-1-x86_64.pkg.tar.zst");
        test_pre_command_artifact_revalidation();
        test_post_command_artifact_revalidation();
        test_actual_archive_metadata_is_lossless_and_read_only();
    } catch(const std::exception& error) {
        std::cerr << "artifact identity test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "artifact identity tests passed\n";
    return 0;
}
