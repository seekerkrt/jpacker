#include "artifact_identity.hpp"

#include "artifact_workspace.hpp"
#include "stubs/artifact-identity/process_stub.hpp"
#include "trusted_cache.hpp"
#include "trusted_cache_test_support.hpp"

#include <cstddef>
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

using MultipleArtifactIdentityQuery = ArtifactPackageIdentitySet (*)(
        const ValidatedPackageArtifactSet&);
static_assert(!std::is_invocable_v<
              MultipleArtifactIdentityQuery,
              const std::filesystem::path&>);
static_assert(!std::is_invocable_v<
              MultipleArtifactIdentityQuery,
              const std::vector<std::filesystem::path>&>);

static_assert(!std::is_default_constructible_v<ArtifactPackageIdentitySet>);
static_assert(!std::is_copy_constructible_v<ArtifactPackageIdentitySet>);
static_assert(!std::is_copy_assignable_v<ArtifactPackageIdentitySet>);
static_assert(std::is_move_constructible_v<ArtifactPackageIdentitySet>);
static_assert(std::is_nothrow_move_constructible_v<ArtifactPackageIdentitySet>);
static_assert(!std::is_move_assignable_v<ArtifactPackageIdentitySet>);
static_assert(!std::is_constructible_v<
              ArtifactPackageIdentitySet,
              std::vector<ArtifactPackageIdentity>>);
static_assert(!std::is_default_constructible_v<
              IndexedArtifactPackageIdentity>);
static_assert(!std::is_constructible_v<
              IndexedArtifactPackageIdentity,
              std::size_t,
              ArtifactPackageIdentity>);
static_assert(std::is_same_v<
              decltype(std::declval<const IndexedArtifactPackageIdentity&>()
                               .identity()),
              const ArtifactPackageIdentity&>);

namespace {

namespace fs = std::filesystem;
namespace stub = artifact_identity_test_stub;

void expect(bool condition, const std::string& message) {
    if(!condition) throw std::runtime_error(message);
}

template <typename Callable>
void expect_runtime_error(Callable&& callable, const std::string& context) {
    try {
        std::forward<Callable>(callable)();
    } catch(const std::runtime_error&) {
        return;
    } catch(const std::exception& error) {
        throw std::runtime_error(
                context + ": unexpected exception category: " + error.what());
    }
    throw std::runtime_error(context + ": expected runtime_error");
}

void write_file(const fs::path& path, const std::string& contents = "fixture") {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if(!file) {
        throw std::runtime_error(
                "Failed to create multiple identity fixture: " +
                path.string());
    }
    file.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    file.close();
    if(!file) {
        throw std::runtime_error(
                "Failed to finish multiple identity fixture: " +
                path.string());
    }
}

fs::path signature_path(const fs::path& artifact_path) {
    return fs::path(artifact_path.string() + ".sig");
}

class TemporaryCacheHome final {
    fs::path                   path_;
    std::optional<std::string> previous_xdg_cache_home_;

public:
    TemporaryCacheHome() {
        const std::string template_text =
                (fs::temp_directory_path() /
                 "moguet-multiple-artifact-identity-test-XXXXXX")
                        .string();
        std::vector<char> path_template(
                template_text.begin(), template_text.end());
        path_template.push_back('\0');

        char* created_path = mkdtemp(path_template.data());
        if(created_path == nullptr) {
            throw std::runtime_error(
                    "Failed to create multiple artifact identity test directory.");
        }
        path_ = created_path;

        const char* previous = std::getenv("XDG_CACHE_HOME");
        if(previous != nullptr) previous_xdg_cache_home_ = previous;
        if(setenv("XDG_CACHE_HOME", path_.c_str(), 1) != 0) {
            std::error_code error;
            fs::remove_all(path_, error);
            throw std::runtime_error(
                    "Failed to set multiple identity test cache home.");
        }
    }

    TemporaryCacheHome(const TemporaryCacheHome&) = delete;
    TemporaryCacheHome& operator=(const TemporaryCacheHome&) = delete;

    ~TemporaryCacheHome() noexcept {
        if(previous_xdg_cache_home_.has_value()) {
            static_cast<void>(setenv(
                    "XDG_CACHE_HOME",
                    previous_xdg_cache_home_->c_str(), 1));
        } else {
            static_cast<void>(unsetenv("XDG_CACHE_HOME"));
        }

        std::error_code error;
        fs::remove_all(path_, error);
    }
};

class ArtifactSetFixture final {
    TemporaryCacheHome                            cache_home_;
    std::unique_ptr<ValidatedPackageArtifactSet> artifacts_;
    std::vector<fs::path>                        paths_;
    fs::path                                     workspace_path_;

public:
    explicit ArtifactSetFixture(
            const std::vector<std::string>& artifact_leaf_names,
            const std::vector<std::size_t>& signed_indices = {}) {
        ArtifactWorkspace workspace = create_artifact_workspace(
                prepare_private_trusted_cache_root(
                        prepare_test_trusted_cache_root()));
        workspace_path_ = workspace.path();

        std::string packagelist_output;
        paths_.reserve(artifact_leaf_names.size());
        for(const std::string& leaf_name : artifact_leaf_names) {
            fs::path artifact_path = workspace.path() / leaf_name;
            paths_.push_back(artifact_path);
            packagelist_output += artifact_path.string() + "\n";
        }
        ExpectedPackageArtifactSet expected =
                validate_makepkg_packagelist_output_set(
                        workspace, packagelist_output);
        for(std::size_t index = 0; index < paths_.size(); ++index) {
            write_file(paths_[index], "artifact-" + std::to_string(index));
        }
        for(const std::size_t index : signed_indices) {
            if(index >= paths_.size()) {
                throw std::logic_error("Signed fixture index is out of range.");
            }
            write_file(signature_path(paths_[index]), "signature");
        }

        ValidatedPackageArtifactSet artifacts =
                validate_post_build_package_artifacts(
                        std::move(workspace), expected);
        artifacts_ = std::make_unique<ValidatedPackageArtifactSet>(
                std::move(artifacts));
    }

    ArtifactSetFixture(const ArtifactSetFixture&) = delete;
    ArtifactSetFixture& operator=(const ArtifactSetFixture&) = delete;

    ValidatedPackageArtifactSet& artifacts() {
        return *artifacts_;
    }

    const ValidatedPackageArtifactSet& artifacts() const {
        return *artifacts_;
    }

    const fs::path& path_at(std::size_t index) const {
        return paths_.at(index);
    }

    const fs::path& workspace_path() const noexcept {
        return workspace_path_;
    }
};

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

std::vector<stub::CapturedCommandStep> successful_steps(
        const std::vector<std::string>& package_names) {
    std::vector<stub::CapturedCommandStep> steps;
    steps.reserve(package_names.size());
    for(std::size_t index = 0; index < package_names.size(); ++index) {
        steps.push_back(stub::CapturedCommandStep{
                CapturedCommandResult{
                        package_names[index] + " " +
                                std::to_string(index + 1) + ".0-1\n",
                        0}});
    }
    return steps;
}

void expect_identity(
        const ArtifactPackageIdentitySet& identities,
        std::size_t position, std::size_t expected_index,
        const std::string& expected_name,
        const std::string& expected_version) {
    const IndexedArtifactPackageIdentity& entry =
            identities.entry_at(position);
    expect(
            entry.artifact_index() == expected_index,
            "Stable artifact index differs at position " +
                    std::to_string(position));
    expect(
            entry.identity().package_name == expected_name,
            "Package name differs at position " + std::to_string(position));
    expect(
            entry.identity().full_version == expected_version,
            "Full version differs at position " + std::to_string(position));
}

void test_ordinary_one_artifact_and_epoch() {
    ArtifactSetFixture fixture({"ordinary.pkg.tar.zst"});
    stub::reset_process_stub();
    stub::set_captured_command_results(
            {CapturedCommandResult{"ordinary 2:1.4.0-3\n", 0}});

    ArtifactPackageIdentitySet identities =
            query_artifact_package_identities(fixture.artifacts());
    expect(identities.size() == 1, "Single identity set size differs");
    expect_identity(identities, 0, 0, "ordinary", "2:1.4.0-3");
    expect(
            stub::capture_command_call_count() == 1,
            "Single aggregate artifact was not queried exactly once");
    fixture.artifacts().require_validity();
}

void test_multiple_order_full_versions_and_commands() {
    ArtifactSetFixture fixture(
            {"gamma package.pkg.tar.zst",
             "alpha'package.pkg.tar.zst",
             "-beta-package.pkg.tar.zst"});
    stub::reset_process_stub();
    stub::set_captured_command_results(
            {CapturedCommandResult{"gamma 1:9.0-2\n", 0},
             CapturedCommandResult{"alpha 3.4.5-6\n", 0},
             CapturedCommandResult{"beta 2:7.8-9", 0}});

    ArtifactPackageIdentitySet identities =
            query_artifact_package_identities(fixture.artifacts());
    expect(identities.size() == 3, "Multiple identity set size differs");
    expect_identity(identities, 0, 0, "gamma", "1:9.0-2");
    expect_identity(identities, 1, 1, "alpha", "3.4.5-6");
    expect_identity(identities, 2, 2, "beta", "2:7.8-9");

    const std::vector<std::string> commands = stub::captured_commands();
    expect(commands.size() == 3, "Captured command history size differs");
    for(std::size_t index = 0; index < commands.size(); ++index) {
        expect(
                commands[index] == expected_identity_command(fixture.path_at(index)),
                "Command order or quoting differs at index " +
                        std::to_string(index));
    }
}

void test_obs_shaped_base_plugin_debug_queries_all_artifacts() {
    ArtifactSetFixture fixture(
            {"obs-studio-32.2.1-7-x86_64.pkg.tar.zst",
             "obs-studio-plugin-browser-32.2.1-7-x86_64.pkg.tar.zst",
             "obs-studio-debug-32.2.1-7-x86_64.pkg.tar.zst"});
    stub::reset_process_stub();
    stub::set_captured_command_results(
            {CapturedCommandResult{"obs-studio 32.2.1-7\n", 0},
             CapturedCommandResult{
                     "obs-studio-plugin-browser 32.2.1-7\n", 0},
             CapturedCommandResult{"obs-studio-debug 32.2.1-7\n", 0}});

    ArtifactPackageIdentitySet identities =
            query_artifact_package_identities(fixture.artifacts());
    expect(identities.size() == 3, "OBS-shaped identity set size differs");
    expect_identity(identities, 0, 0, "obs-studio", "32.2.1-7");
    expect_identity(
            identities, 1, 1, "obs-studio-plugin-browser", "32.2.1-7");
    expect_identity(identities, 2, 2, "obs-studio-debug", "32.2.1-7");

    const std::vector<std::string> commands = stub::captured_commands();
    expect(commands.size() == 3, "OBS-shaped query count differs");
    for(std::size_t index = 0; index < commands.size(); ++index) {
        expect(
                commands[index] ==
                        expected_identity_command(fixture.path_at(index)),
                "OBS-shaped command order differs at index " +
                        std::to_string(index));
    }
    fixture.artifacts().require_validity();
}

void test_duplicate_package_names_are_preserved() {
    ArtifactSetFixture fixture(
            {"duplicate-one.pkg.tar.zst", "duplicate-two.pkg.tar.zst"});
    stub::reset_process_stub();
    stub::set_captured_command_results(
            {CapturedCommandResult{"duplicate 1.0-1\n", 0},
             CapturedCommandResult{"duplicate 2.0-1\n", 0}});

    ArtifactPackageIdentitySet identities =
            query_artifact_package_identities(fixture.artifacts());
    expect_identity(identities, 0, 0, "duplicate", "1.0-1");
    expect_identity(identities, 1, 1, "duplicate", "2.0-1");
}

void test_command_failure_at_each_position() {
    for(std::size_t failed_index = 0; failed_index < 3; ++failed_index) {
        ArtifactSetFixture fixture(
                {"failure-one.pkg.tar.zst",
                 "failure-two.pkg.tar.zst",
                 "failure-three.pkg.tar.zst"});
        std::vector<stub::CapturedCommandStep> steps = successful_steps(
                {"failure-one", "failure-two", "failure-three"});
        steps[failed_index].result.exit_code =
                static_cast<int>(40 + failed_index);

        stub::reset_process_stub();
        stub::set_captured_command_steps(std::move(steps));
        expect_runtime_error(
                [&fixture]() {
                    static_cast<void>(query_artifact_package_identities(
                            fixture.artifacts()));
                },
                "command failure at index " + std::to_string(failed_index));
        expect(
                stub::capture_command_call_count() == failed_index + 1,
                "Command failure did not stop the aggregate at index " +
                        std::to_string(failed_index));
        fixture.artifacts().require_validity();
    }
}

void test_malformed_output_at_each_position() {
    const std::vector<std::string> malformed_outputs = {
            "",
            "middle 1-1\nextra 2-1\n",
            "\n",
    };
    for(std::size_t malformed_index = 0;
        malformed_index < malformed_outputs.size();
        ++malformed_index) {
        ArtifactSetFixture fixture(
                {"malformed-one.pkg.tar.zst",
                 "malformed-two.pkg.tar.zst",
                 "malformed-three.pkg.tar.zst"});
        std::vector<stub::CapturedCommandStep> steps = successful_steps(
                {"malformed-one", "malformed-two", "malformed-three"});
        steps[malformed_index].result.output =
                malformed_outputs[malformed_index];

        stub::reset_process_stub();
        stub::set_captured_command_steps(std::move(steps));
        expect_runtime_error(
                [&fixture]() {
                    static_cast<void>(query_artifact_package_identities(
                            fixture.artifacts()));
                },
                "malformed output at index " +
                        std::to_string(malformed_index));
        expect(
                stub::capture_command_call_count() == malformed_index + 1,
                "Malformed output did not stop the aggregate at index " +
                        std::to_string(malformed_index));
        fixture.artifacts().require_validity();
    }
}

void test_invalid_identity_outputs_fail_closed() {
    ArtifactSetFixture fixture({"invalid-identity.pkg.tar.zst"});
    const std::vector<std::string> invalid_outputs = {
            "-invalid-name 1-1\n",
            "valid-name 1\v-1\n",
            "valid-name 1-1\n\n",
            "valid-name 1-1 unexpected\n",
    };
    for(const std::string& output : invalid_outputs) {
        stub::reset_process_stub();
        stub::set_captured_command_results(
                {CapturedCommandResult{output, 0}});
        expect_runtime_error(
                [&fixture]() {
                    static_cast<void>(query_artifact_package_identities(
                            fixture.artifacts()));
                },
                "invalid multiple identity output");
        expect(
                stub::capture_command_call_count() == 1,
                "Invalid identity output call count differs");
        fixture.artifacts().require_validity();
    }
}

fs::path g_replacement_target;
fs::path g_replacement_original;

void replace_regular_file() {
    g_replacement_original = g_replacement_target;
    g_replacement_original += ".identity-original";
    fs::rename(g_replacement_target, g_replacement_original);
    write_file(g_replacement_target, "replacement");
}

void restore_regular_file() {
    expect(
            fs::remove(g_replacement_target),
            "Replacement file was not removed");
    fs::rename(g_replacement_original, g_replacement_target);
    g_replacement_target.clear();
    g_replacement_original.clear();
}

fs::path g_workspace_replacement_target;
fs::path g_workspace_replacement_original;

void replace_workspace() {
    g_workspace_replacement_original = g_workspace_replacement_target;
    g_workspace_replacement_original += ".identity-original";
    fs::rename(
            g_workspace_replacement_target,
            g_workspace_replacement_original);
    fs::create_directory(g_workspace_replacement_target);
    fs::permissions(
            g_workspace_replacement_target,
            fs::perms::owner_all,
            fs::perm_options::replace);
}

void restore_workspace() {
    fs::remove_all(g_workspace_replacement_target);
    fs::rename(
            g_workspace_replacement_original,
            g_workspace_replacement_target);
    g_workspace_replacement_target.clear();
    g_workspace_replacement_original.clear();
}

void test_replacement_before_query_runs_no_command() {
    ArtifactSetFixture fixture(
            {"pre-query-one.pkg.tar.zst", "pre-query-two.pkg.tar.zst"});
    g_replacement_target = fixture.path_at(1);
    replace_regular_file();

    stub::reset_process_stub();
    stub::set_captured_command_results(
            {CapturedCommandResult{"pre-query-one 1-1\n", 0},
             CapturedCommandResult{"pre-query-two 1-1\n", 0}});
    expect_runtime_error(
            [&fixture]() {
                static_cast<void>(query_artifact_package_identities(
                        fixture.artifacts()));
            },
            "replacement before aggregate identity query");
    expect(
            stub::capture_command_call_count() == 0,
            "pacman ran after pre-query aggregate replacement");

    restore_regular_file();
    fixture.artifacts().require_validity();
}

void test_first_command_mutation_reproves_whole_aggregate() {
    ArtifactSetFixture fixture(
            {"first-command-one.pkg.tar.zst",
             "first-command-two.pkg.tar.zst",
             "first-command-three.pkg.tar.zst"});
    g_replacement_target = fixture.path_at(2);
    std::vector<stub::CapturedCommandStep> steps = successful_steps(
            {"first-command-one", "first-command-two", "first-command-three"});
    steps[0].before_hook = replace_regular_file;

    stub::reset_process_stub();
    stub::set_captured_command_steps(std::move(steps));
    expect_runtime_error(
            [&fixture]() {
                static_cast<void>(query_artifact_package_identities(
                        fixture.artifacts()));
            },
            "sibling replacement during first command");
    expect(
            stub::capture_command_call_count() == 1,
            "First-command replacement call count differs");

    restore_regular_file();
    fixture.artifacts().require_validity();
}

void test_middle_command_mutation() {
    ArtifactSetFixture fixture(
            {"middle-command-one.pkg.tar.zst",
             "middle-command-two.pkg.tar.zst",
             "middle-command-three.pkg.tar.zst"});
    g_replacement_target = fixture.path_at(0);
    std::vector<stub::CapturedCommandStep> steps = successful_steps(
            {"middle-command-one", "middle-command-two", "middle-command-three"});
    steps[1].before_hook = replace_regular_file;

    stub::reset_process_stub();
    stub::set_captured_command_steps(std::move(steps));
    expect_runtime_error(
            [&fixture]() {
                static_cast<void>(query_artifact_package_identities(
                        fixture.artifacts()));
            },
            "replacement during middle command");
    expect(
            stub::capture_command_call_count() == 2,
            "Middle-command replacement call count differs");

    restore_regular_file();
    fixture.artifacts().require_validity();
}

void test_between_commands_mutation_stops_before_next_command() {
    ArtifactSetFixture fixture(
            {"between-command-one.pkg.tar.zst",
             "between-command-two.pkg.tar.zst",
             "between-command-three.pkg.tar.zst"});
    g_replacement_target = fixture.path_at(1);
    std::vector<stub::CapturedCommandStep> steps = successful_steps(
            {"between-command-one", "between-command-two",
             "between-command-three"});
    steps[0].after_hook = replace_regular_file;

    stub::reset_process_stub();
    stub::set_captured_command_steps(std::move(steps));
    expect_runtime_error(
            [&fixture]() {
                static_cast<void>(query_artifact_package_identities(
                        fixture.artifacts()));
            },
            "replacement between aggregate identity commands");
    expect(
            stub::capture_command_call_count() == 1,
            "Between-command replacement did not stop before the next command");

    restore_regular_file();
    fixture.artifacts().require_validity();
}

void test_last_command_after_hook_mutation() {
    ArtifactSetFixture fixture(
            {"last-command-one.pkg.tar.zst",
             "last-command-two.pkg.tar.zst",
             "last-command-three.pkg.tar.zst"});
    g_replacement_target = fixture.path_at(1);
    std::vector<stub::CapturedCommandStep> steps = successful_steps(
            {"last-command-one", "last-command-two", "last-command-three"});
    steps[2].after_hook = replace_regular_file;

    stub::reset_process_stub();
    stub::set_captured_command_steps(std::move(steps));
    expect_runtime_error(
            [&fixture]() {
                static_cast<void>(query_artifact_package_identities(
                        fixture.artifacts()));
            },
            "replacement after last command");
    expect(
            stub::capture_command_call_count() == 3,
            "Last-command replacement call count differs");

    restore_regular_file();
    fixture.artifacts().require_validity();
}

void test_signature_replacement_during_query() {
    ArtifactSetFixture fixture(
            {"signed-one.pkg.tar.zst",
             "signed-two.pkg.tar.zst",
             "signed-three.pkg.tar.zst"},
            {1});
    g_replacement_target = signature_path(fixture.path_at(1));
    std::vector<stub::CapturedCommandStep> steps = successful_steps(
            {"signed-one", "signed-two", "signed-three"});
    steps[0].after_hook = replace_regular_file;

    stub::reset_process_stub();
    stub::set_captured_command_steps(std::move(steps));
    expect_runtime_error(
            [&fixture]() {
                static_cast<void>(query_artifact_package_identities(
                        fixture.artifacts()));
            },
            "signature replacement during aggregate identity query");
    expect(
            stub::capture_command_call_count() == 1,
            "Signature replacement call count differs");

    restore_regular_file();
    fixture.artifacts().require_validity();
}

void test_workspace_replacement_during_query() {
    ArtifactSetFixture fixture(
            {"workspace-one.pkg.tar.zst",
             "workspace-two.pkg.tar.zst",
             "workspace-three.pkg.tar.zst"});
    g_workspace_replacement_target = fixture.workspace_path();
    std::vector<stub::CapturedCommandStep> steps = successful_steps(
            {"workspace-one", "workspace-two", "workspace-three"});
    steps[1].after_hook = replace_workspace;

    stub::reset_process_stub();
    stub::set_captured_command_steps(std::move(steps));
    expect_runtime_error(
            [&fixture]() {
                static_cast<void>(query_artifact_package_identities(
                        fixture.artifacts()));
            },
            "workspace replacement during aggregate identity query");
    expect(
            stub::capture_command_call_count() == 2,
            "Workspace replacement call count differs");

    restore_workspace();
    fixture.artifacts().require_validity();
}

void test_moved_from_and_cleaned_aggregate_run_no_command() {
    {
        ArtifactSetFixture fixture(
                {"moved-one.pkg.tar.zst", "moved-two.pkg.tar.zst"});
        ValidatedPackageArtifactSet moved = std::move(fixture.artifacts());
        stub::reset_process_stub();
        stub::set_captured_command_results(
                {CapturedCommandResult{"moved-one 1-1\n", 0}});
        expect_runtime_error(
                [&fixture]() {
                    static_cast<void>(query_artifact_package_identities(
                            fixture.artifacts()));
                },
                "moved-from aggregate identity query");
        expect(
                stub::capture_command_call_count() == 0,
                "pacman ran for moved-from aggregate");
        moved.require_validity();
    }
    {
        ArtifactSetFixture fixture(
                {"cleaned-one.pkg.tar.zst", "cleaned-two.pkg.tar.zst"});
        fixture.artifacts().cleanup_workspace();
        stub::reset_process_stub();
        stub::set_captured_command_results(
                {CapturedCommandResult{"cleaned-one 1-1\n", 0}});
        expect_runtime_error(
                [&fixture]() {
                    static_cast<void>(query_artifact_package_identities(
                            fixture.artifacts()));
                },
                "cleaned aggregate identity query");
        expect(
                stub::capture_command_call_count() == 0,
                "pacman ran for cleaned aggregate");
    }
}

void test_failure_and_success_leave_aggregate_ownership_with_caller() {
    {
        ArtifactSetFixture fixture(
                {"owned-failure-one.pkg.tar.zst",
                 "owned-failure-two.pkg.tar.zst"});
        stub::reset_process_stub();
        stub::set_captured_command_results(
                {CapturedCommandResult{"owned-failure-one 1-1\n", 0},
                 CapturedCommandResult{"owned-failure-two 1-1\n", 17}});
        expect_runtime_error(
                [&fixture]() {
                    static_cast<void>(query_artifact_package_identities(
                            fixture.artifacts()));
                },
                "failure ownership retention");
        fixture.artifacts().require_validity();
        const fs::path workspace_path = fixture.workspace_path();
        fixture.artifacts().cleanup_workspace();
        expect(
                !fs::exists(workspace_path),
                "Caller could not cleanup aggregate after query failure");
    }
    {
        ArtifactSetFixture fixture(
                {"owned-success-one.pkg.tar.zst",
                 "owned-success-two.pkg.tar.zst"});
        stub::reset_process_stub();
        stub::set_captured_command_results(
                {CapturedCommandResult{"owned-success-one 1-1\n", 0},
                 CapturedCommandResult{"owned-success-two 2-1\n", 0}});
        ArtifactPackageIdentitySet identities =
                query_artifact_package_identities(fixture.artifacts());

        fixture.artifacts().retain_workspace_for_diagnostics();
        fixture.artifacts().require_validity();
        const fs::path workspace_path = fixture.workspace_path();
        fixture.artifacts().cleanup_workspace();
        expect(
                !fs::exists(workspace_path),
                "Caller could not cleanup aggregate after query success");
        expect_identity(
                identities, 1, 1, "owned-success-two", "2-1");
    }
}

void test_identity_set_move_closes_source() {
    ArtifactSetFixture fixture(
            {"move-result-one.pkg.tar.zst",
             "move-result-two.pkg.tar.zst"});
    stub::reset_process_stub();
    stub::set_captured_command_results(
            {CapturedCommandResult{"move-result-one 1-1\n", 0},
             CapturedCommandResult{"move-result-two 2-1\n", 0}});
    ArtifactPackageIdentitySet source =
            query_artifact_package_identities(fixture.artifacts());
    ArtifactPackageIdentitySet moved(std::move(source));

    expect_runtime_error(
            [&source]() { static_cast<void>(source.size()); },
            "moved-from identity set size");
    expect_runtime_error(
            [&source]() { static_cast<void>(source.entry_at(0)); },
            "moved-from identity set entry");
    expect_identity(moved, 0, 0, "move-result-one", "1-1");
    expect_identity(moved, 1, 1, "move-result-two", "2-1");
}

} // namespace

int main() {
    try {
        test_ordinary_one_artifact_and_epoch();
        test_multiple_order_full_versions_and_commands();
        test_obs_shaped_base_plugin_debug_queries_all_artifacts();
        test_duplicate_package_names_are_preserved();
        test_command_failure_at_each_position();
        test_malformed_output_at_each_position();
        test_invalid_identity_outputs_fail_closed();
        test_replacement_before_query_runs_no_command();
        test_first_command_mutation_reproves_whole_aggregate();
        test_middle_command_mutation();
        test_between_commands_mutation_stops_before_next_command();
        test_last_command_after_hook_mutation();
        test_signature_replacement_during_query();
        test_workspace_replacement_during_query();
        test_moved_from_and_cleaned_aggregate_run_no_command();
        test_failure_and_success_leave_aggregate_ownership_with_caller();
        test_identity_set_move_closes_source();
    } catch(const std::exception& error) {
        std::cerr << "multiple artifact identity test failed: "
                  << error.what() << '\n';
        return 1;
    }

    std::cout << "multiple artifact identity tests passed\n";
    return 0;
}
