#include "artifact_identity.hpp"

#include "artifact_workspace.hpp"
#include "stubs/artifact-identity/process_stub.hpp"
#include "trusted_cache.hpp"

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

#include <unistd.h>

using ArtifactIdentityQuery = ArtifactPackageIdentity (*)(
        const ValidatedPackageArtifactPath&);
static_assert(!std::is_invocable_v<ArtifactIdentityQuery, const std::filesystem::path&>);

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
    fs::path                   path_;
    std::optional<std::string> previous_xdg_cache_home_;

public:
    TemporaryCacheHome() {
        std::vector<char> path_template;
        const std::string template_text =
                (fs::temp_directory_path() /
                 "jpacker-artifact-identity-test-XXXXXX")
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
    TemporaryCacheHome                              cache_home_;
    std::unique_ptr<ValidatedPackageArtifactPath> artifact_;

public:
    explicit ArtifactFixture(const std::string& artifact_leaf_name) {
        ArtifactWorkspace workspace = create_artifact_workspace(
                prepare_private_trusted_cache_root());
        fs::path           artifact_path = workspace.path() / artifact_leaf_name;

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
    return "LC_ALL=C 'pacman' '-U' '--print' '--print-format' '%n\t%v' '--' " +
           expected_shell_quote(artifact_path.string());
}

void test_name_and_epoch_full_version_success(
        const ValidatedPackageArtifactPath& artifact) {
    ArtifactPackageIdentity identity =
            query_with_result(artifact, "sample-package\t2:1.4.0-3\n");

    expect(identity.package_name == "sample-package", "Package name differs");
    expect(identity.full_version == "2:1.4.0-3", "Epoch full version differs");
    expect(stub::capture_command_call_count() == 1, "pacman was not called once");

    identity = query_with_result(artifact, "sample-package\t1.4.0-3");
    expect(
            identity.full_version == "1.4.0-3",
            "Single record without line terminator was not preserved");
}

void test_command_failure(const ValidatedPackageArtifactPath& artifact) {
    expect_runtime_error(
            [&artifact]() {
                static_cast<void>(query_with_result(
                        artifact, "sample-package\t1-1\n", 127));
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
            {"extra output line", "sample-package\t1-1\nother-package\t2-1\n"},
            {"trailing blank line", "sample-package\t1-1\n\n"},
            {"missing tab", "sample-package 1-1\n"},
            {"multiple tabs", "sample-package\t1-1\textra\n"},
            {"empty name", "\t1-1\n"},
            {"empty version", "sample-package\t\n"},
            {"invalid package name", "-sample-package\t1-1\n"},
            {"carriage return", "sample-package\t1-1\r\n"},
            {"vertical tab", "sample-package\t1-1\v\n"},
    };

    for(const auto& [context, output] : cases) {
        expect_runtime_error(
                [&artifact, &output]() {
                    static_cast<void>(query_with_result(artifact, output));
                },
                context);
    }

    std::string null_output = "sample-package\t1";
    null_output.push_back('\0');
    null_output += "-1\n";
    expect_runtime_error(
            [&artifact, &null_output]() {
                static_cast<void>(query_with_result(artifact, null_output));
            },
            "null control character");

    std::string delete_output = "sample-package\t1";
    delete_output.push_back(static_cast<char>(0x7f));
    delete_output += "-1\n";
    expect_runtime_error(
            [&artifact, &delete_output]() {
                static_cast<void>(query_with_result(artifact, delete_output));
            },
            "delete control character");
}

void test_exact_safe_command(const ValidatedPackageArtifactPath& artifact) {
    static_cast<void>(query_with_result(artifact, "sample-package\t1-1\n"));

    const std::string command = stub::last_captured_command();
    expect(
            command == expected_identity_command(artifact.path()),
            "Artifact identity command differs");
    expect(command.find("'%n\t%v'") != std::string::npos, "Format lacks actual tab");
    expect(command.find("'--print'") != std::string::npos, "--print is missing");
    expect(
            command.find("'--print-format'") != std::string::npos,
            "--print-format is missing");
    expect(
            command.find("'--' " + expected_shell_quote(artifact.path().string())) !=
                    std::string::npos,
            "Semantic -- is not immediately before artifact path");
    expect(command.find("sudo") == std::string::npos, "Identity command contains sudo");
    expect(
            command.find("'-U' '--print'") != std::string::npos,
            "-U is not guarded by --print");
}

void test_special_artifact_path_command(const std::string& artifact_leaf_name) {
    ArtifactFixture fixture(artifact_leaf_name);
    static_cast<void>(query_with_result(
            fixture.artifact(), "sample-package\t1-1\n"));
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
            CapturedCommandResult{"sample-package\t1-1\n", 0});
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
            CapturedCommandResult{"sample-package\t1-1\n", 0});
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
    } catch(const std::exception& error) {
        std::cerr << "artifact identity test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "artifact identity tests passed\n";
    return 0;
}
