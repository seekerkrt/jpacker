#include "app_config.hpp"
#include "aur_update_execution_preparation.hpp"
#include "source_preference.hpp"

#include "stubs/aur-update-execution-preparation-integration/preparation_stub.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

#include <sys/stat.h>

namespace {

namespace fs = std::filesystem;
namespace stub = aur_update_execution_preparation_integration_stub;

void expect(bool condition, const std::string& diagnostic) {
    if(!condition) throw std::runtime_error(diagnostic);
}

void expect_assignment(
        const SourceBuildEnvironment& environment,
        std::size_t index,
        const std::string& expected_key,
        const std::string& expected_value,
        const std::string& context) {
    expect(
            index < environment.ordered_assignments.size(),
            context + ": missing assignment " + std::to_string(index));
    const SourceEnvironmentAssignment& assignment =
            environment.ordered_assignments[index];
    expect(
            assignment.key == expected_key &&
                    assignment.value == expected_value,
            context + ": unexpected assignment at " +
                    std::to_string(index));
}

class ScopedUnsetEnvironmentVariable final {
    std::string                key_;
    std::optional<std::string> previous_value_;

public:
    explicit ScopedUnsetEnvironmentVariable(std::string key)
        : key_(std::move(key)) {
        const char* previous_value = std::getenv(key_.c_str());
        if(previous_value != nullptr) previous_value_ = previous_value;
        if(::unsetenv(key_.c_str()) != 0) {
            throw std::runtime_error(
                    "Failed to unset test environment variable " + key_ + ".");
        }
    }

    ScopedUnsetEnvironmentVariable(const ScopedUnsetEnvironmentVariable&) = delete;
    ScopedUnsetEnvironmentVariable& operator=(
            const ScopedUnsetEnvironmentVariable&) = delete;

    ~ScopedUnsetEnvironmentVariable() noexcept {
        if(previous_value_.has_value()) {
            static_cast<void>(
                    ::setenv(key_.c_str(), previous_value_->c_str(), 1));
        } else {
            static_cast<void>(::unsetenv(key_.c_str()));
        }
    }
};

class PreferenceFixture final {
    std::vector<std::string> package_names_;

    void retain_package_name(const std::string& package_name) {
        if(std::find(
                   package_names_.begin(), package_names_.end(),
                   package_name) == package_names_.end()) {
            package_names_.push_back(package_name);
        }
    }

public:
    PreferenceFixture() = default;
    PreferenceFixture(const PreferenceFixture&) = delete;
    PreferenceFixture& operator=(const PreferenceFixture&) = delete;

    ~PreferenceFixture() noexcept {
        reset_source_preference_test_hooks();
        for(auto entry = package_names_.rbegin();
            entry != package_names_.rend(); ++entry) {
            std::error_code remove_error;
            fs::remove_all(source_preference_entry_path(*entry), remove_error);
        }
    }

    fs::path entry_path(const std::string& package_name) {
        retain_package_name(package_name);
        return source_preference_entry_path(package_name);
    }

    void remove_entry(const std::string& package_name) {
        const fs::path path = entry_path(package_name);
        std::error_code remove_error;
        fs::remove_all(path, remove_error);
        if(remove_error) {
            throw std::runtime_error(
                    "Failed to remove source preference fixture " +
                    path.string() + ": " + remove_error.message());
        }
    }

    void write_entry(
            const std::string& package_name,
            const std::string& contents) {
        remove_entry(package_name);
        const fs::path path = entry_path(package_name);
        std::ofstream output(path, std::ios::binary);
        if(!output) {
            throw std::runtime_error(
                    "Failed to create source preference fixture " +
                    path.string() + ".");
        }
        output.write(
                contents.data(),
                static_cast<std::streamsize>(contents.size()));
        if(!output) {
            throw std::runtime_error(
                    "Failed to write source preference fixture " +
                    path.string() + ".");
        }
    }

    void create_directory_entry(const std::string& package_name) {
        remove_entry(package_name);
        const fs::path path = entry_path(package_name);
        if(!fs::create_directory(path)) {
            throw std::runtime_error(
                    "Failed to create source preference directory fixture " +
                    path.string() + ".");
        }
    }

    void create_symlink_entry(
            const std::string& package_name,
            const std::string& target_package_name) {
        remove_entry(package_name);
        retain_package_name(target_package_name);
        fs::create_symlink(
                fs::path(target_package_name), entry_path(package_name));
    }

    void create_fifo_entry(const std::string& package_name) {
        remove_entry(package_name);
        const fs::path path = entry_path(package_name);
        if(::mkfifo(path.c_str(), 0600) != 0) {
            const int fixture_errno = errno;
            throw std::runtime_error(
                    "Failed to create source preference FIFO fixture " +
                    path.string() + ": " +
                    std::string(std::strerror(fixture_errno)));
        }
    }
};

AurUpdateExecutionPreflight executable_preflight(
        const std::string& package_name,
        const std::string& package_base = "") {
    const std::string selected_package_base =
            package_base.empty() ? package_name : package_base;
    const RootTargetIdentity root{0, package_name};

    AurUpdatePlanEntry update;
    update.installed_name = package_name;
    update.installed_version = "1.0-1";
    update.install_reason = InstalledPackageReason::Explicit;
    update.aur_package = AurUpdateRemotePackage{
            package_name,
            selected_package_base,
            "2.0-1",
            AurVersionRelation::NewerThanInstalled};
    update.classification = AurUpdateClassification::UpdateAvailable;

    AurUpdateExecutionTarget target;
    target.update_plan_index = 0;
    target.build_plan_root_index = 0;
    target.update = std::move(update);
    target.status = AurUpdateExecutionTargetStatus::Executable;
    target.desired_install_reason = DesiredInstallReason::Explicit;

    BuildPlan plan;
    plan.root_targets.push_back(root);
    plan.package_targets.push_back(PlannedPackageTarget{
            package_name,
            selected_package_base,
            {PackageRole::Root},
            {root}});
    plan.order.push_back(BuildPlanEntry{
            selected_package_base,
            {package_name}});

    AurUpdateExecutionPreflight preflight;
    preflight.targets.push_back(std::move(target));
    preflight.build_plan = std::move(plan);
    return preflight;
}

AurUpdateSourceBuildPreparation prepare(
        const AurUpdateExecutionPreflight& preflight,
        bool needed = false) {
    AppConfig config;
    return prepare_aur_update_source_build_invocation(
            preflight, needed, config);
}

void reset_case() {
    reset_source_preference_test_hooks();
    stub::reset();
}

const AurUpdatePreparationIssue& require_single_issue(
        const AurUpdateSourceBuildPreparation& preparation,
        AurUpdatePreparationReason expected_reason,
        const char* context) {
    const std::string context_text(context);
    expect(preparation.is_blocked(), context_text + ": result is not blocked");
    expect(
            !preparation.invocation.has_value(),
            context_text + ": blocked result retained a partial invocation");
    expect(
            preparation.issues.size() == 1,
            context_text + ": unexpected issue count");
    expect(
            preparation.issues.front().reason == expected_reason,
            context_text + ": unexpected issue reason");
    return preparation.issues.front();
}

const ProductionSourceBuildWorkItem& require_single_work_item(
        const AurUpdateSourceBuildPreparation& preparation,
        const char* context) {
    const std::string context_text(context);
    expect(preparation.is_prepared(), context_text + ": result is not prepared");
    expect(
            preparation.issues.empty(),
            context_text + ": prepared result has issues");
    expect(
            preparation.invocation.has_value(),
            context_text + ": prepared result has no invocation");
    expect(
            preparation.invocation->work_items.size() == 1,
            context_text + ": unexpected work item count");
    return preparation.invocation->work_items.front();
}

void expect_successful_generic_preparation(
        const AurUpdateSourceBuildPreparation& preparation,
        const char* context) {
    const std::string context_text(context);
    expect(
            stub::database_resolution_call_count() == 1,
            context_text + ": database resolver was not called exactly once");
    expect(
            stub::separated_option_check_call_count() == 1,
            context_text + ": generic option preflight call count changed");
    expect(
            stub::artifact_pkgdest_check_call_count() == 2,
            context_text +
                    ": generic PKGDEST preflight did not scan ambient and item environments");
    expect(
            preparation.invocation->database_paths.root_dir == fs::path("/") &&
                    preparation.invocation->database_paths.db_path ==
                            fs::path("/var/lib/pacman"),
            context_text + ": database snapshot differs from resolver-owned value");
}

void expect_no_generic_preparation(const char* context) {
    const std::string context_text(context);
    expect(
            stub::database_resolution_call_count() == 0,
            context_text +
                    ": database resolver ran before strict preference completion");
    expect(
            stub::separated_option_check_call_count() == 0 &&
                    stub::artifact_pkgdest_check_call_count() == 0,
            context_text +
                    ": generic preparation ran after an update-specific blocker");
}

void test_absent_preference(PreferenceFixture& fixture) {
    const std::string package_name = "preparation-absent";
    fixture.remove_entry(package_name);
    reset_case();

    const AurUpdateSourceBuildPreparation preparation =
            prepare(executable_preflight(package_name));
    const ProductionSourceBuildWorkItem& work_item =
            require_single_work_item(preparation, "absent preference");
    expect(
            work_item.request.custom_environment.ordered_assignments.empty(),
            "absent preference did not select an empty environment");
    expect(preparation.warnings.empty(), "absent preference emitted warnings");
    expect_successful_generic_preparation(preparation, "absent preference");
}

void test_empty_preference(PreferenceFixture& fixture) {
    const std::string package_name = "preparation-empty";
    fixture.write_entry(package_name, "");
    reset_case();

    const AurUpdateSourceBuildPreparation preparation =
            prepare(executable_preflight(package_name));
    const ProductionSourceBuildWorkItem& work_item =
            require_single_work_item(preparation, "empty preference");
    expect(
            work_item.request.custom_environment.ordered_assignments.empty(),
            "empty loaded preference did not select an empty environment");
    expect(preparation.warnings.empty(), "empty loaded preference emitted warnings");
    expect(
            fs::is_regular_file(fixture.entry_path(package_name)) &&
                    fs::file_size(fixture.entry_path(package_name)) == 0,
            "empty preference fixture was not preserved as a regular file");
    expect_successful_generic_preparation(preparation, "empty preference");
}

void test_valid_preference(PreferenceFixture& fixture) {
    const std::string package_name = "preparation-valid";
    fixture.write_entry(
            package_name,
            "FIRST=alpha\n"
            "EMPTY=\n"
            "SECOND=\"two words\"\n");
    reset_case();

    const AurUpdateSourceBuildPreparation preparation =
            prepare(executable_preflight(package_name), true);
    const ProductionSourceBuildWorkItem& work_item =
            require_single_work_item(preparation, "valid preference");
    const SourceBuildEnvironment& environment =
            work_item.request.custom_environment;
    expect(
            environment.ordered_assignments.size() == 3,
            "valid preference assignment count changed");
    expect_assignment(environment, 0, "FIRST", "alpha", "valid preference");
    expect_assignment(environment, 1, "EMPTY", "", "valid preference");
    expect_assignment(environment, 2, "SECOND", "two words", "valid preference");
    expect(
            work_item.request.empty_value_policy ==
                    SourceEnvironmentEmptyValuePolicy::Omit &&
                    work_item.request.needed,
            "valid preference changed update work item options");
    expect_successful_generic_preparation(preparation, "valid preference");
}

void test_warning_retention(PreferenceFixture& fixture) {
    const std::string package_name = "preparation-warning";
    fixture.write_entry(
            package_name,
            "9FIRST_INVALID=value\n"
            "VALID=kept\n"
            "=second-invalid\n");
    reset_case();

    const AurUpdateSourceBuildPreparation preparation =
            prepare(executable_preflight(package_name));
    const ProductionSourceBuildWorkItem& work_item =
            require_single_work_item(preparation, "warning retention");
    expect_assignment(
            work_item.request.custom_environment,
            0, "VALID", "kept", "warning retention");
    expect(
            preparation.warnings.size() == 2,
            "strict preference warnings were not retained");
    expect(
            preparation.warnings[0].preference_name == package_name &&
                    preparation.warnings[0].entry_path ==
                            fixture.entry_path(package_name) &&
                    preparation.warnings[0].diagnostic ==
                            "Ignoring invalid environment assignment: 9FIRST_INVALID=value" &&
                    preparation.warnings[1].diagnostic ==
                            "Ignoring invalid environment assignment: =second-invalid",
            "strict preference warning attribution or read order changed");
    expect_successful_generic_preparation(preparation, "warning retention");
}

void test_injected_preference_failure(
        PreferenceFixture& fixture,
        const std::string& package_name,
        SourcePreferenceTestFailurePoint failure_point,
        SourcePreferenceFailureKind expected_kind,
        const std::error_code& expected_error,
        const char* context) {
    const std::string context_text(context);
    fixture.write_entry(package_name, "VALUE=available\n");
    reset_case();
    fail_next_source_preference_operation_for_test(
            package_name, failure_point);

    const AurUpdateSourceBuildPreparation preparation =
            prepare(executable_preflight(package_name));
    const AurUpdatePreparationIssue& issue = require_single_issue(
            preparation,
            AurUpdatePreparationReason::SourcePreferenceUnavailable,
            context);
    expect(
            issue.package_name == package_name &&
                    issue.source_preference_failure.has_value(),
            context_text + ": typed source preference failure is missing");
    const SourcePreferenceFailure& failure =
            *issue.source_preference_failure;
    expect(
            failure.kind == expected_kind &&
                    failure.entry_path == fixture.entry_path(package_name) &&
                    failure.system_error == expected_error &&
                    !failure.observed_file_type.has_value(),
            context_text + ": typed source preference failure fields changed");
    expect_no_generic_preparation(context);

    // One-shot hookが対象readで消費され、partial resultを後続へ残さないことも確認する。
    StrictSourcePreferenceResult retry =
            read_source_preference_strict(package_name);
    expect(
            std::get_if<SourcePreferenceLoaded>(&retry) != nullptr,
            context_text + ": one-shot strict reader failure was not consumed");
}

void test_status_open_and_read_failures(PreferenceFixture& fixture) {
    test_injected_preference_failure(
            fixture,
            "preparation-status-failure",
            SourcePreferenceTestFailurePoint::Status,
            SourcePreferenceFailureKind::StatusUnavailable,
            std::make_error_code(std::errc::permission_denied),
            "strict status failure");
    test_injected_preference_failure(
            fixture,
            "preparation-open-failure",
            SourcePreferenceTestFailurePoint::Open,
            SourcePreferenceFailureKind::OpenFailed,
            std::make_error_code(std::errc::permission_denied),
            "strict open failure");
    test_injected_preference_failure(
            fixture,
            "preparation-read-failure",
            SourcePreferenceTestFailurePoint::Read,
            SourcePreferenceFailureKind::ReadFailed,
            std::make_error_code(std::errc::io_error),
            "strict read failure");
}

void expect_nonregular_preference_failure(
        const std::string& package_name,
        fs::file_type expected_file_type,
        const char* context) {
    const std::string context_text(context);
    reset_case();
    const AurUpdateSourceBuildPreparation preparation =
            prepare(executable_preflight(package_name));
    const AurUpdatePreparationIssue& issue = require_single_issue(
            preparation,
            AurUpdatePreparationReason::SourcePreferenceUnavailable,
            context);
    expect(
            issue.source_preference_failure.has_value(),
            context_text + ": typed source preference failure is missing");
    const SourcePreferenceFailure& failure =
            *issue.source_preference_failure;
    expect(
            failure.kind == SourcePreferenceFailureKind::UnsupportedFileType &&
                    failure.observed_file_type == expected_file_type &&
                    !failure.system_error.has_value(),
            context_text + ": observed source preference file type changed");
    expect_no_generic_preparation(context);
}

void test_nonregular_preferences(PreferenceFixture& fixture) {
    const std::string symlink_source = "preparation-symlink-source";
    const std::string symlink_package = "preparation-symlink";
    fixture.write_entry(symlink_source, "VALUE=must-not-follow\n");
    fixture.create_symlink_entry(symlink_package, symlink_source);
    expect_nonregular_preference_failure(
            symlink_package, fs::file_type::symlink,
            "strict symlink preference");

    const std::string directory_package = "preparation-directory";
    fixture.create_directory_entry(directory_package);
    expect_nonregular_preference_failure(
            directory_package, fs::file_type::directory,
            "strict directory preference");

    const std::string fifo_package = "preparation-fifo";
    fixture.create_fifo_entry(fifo_package);
    expect_nonregular_preference_failure(
            fifo_package, fs::file_type::fifo,
            "strict FIFO preference");
}

void test_package_to_base_fallback(PreferenceFixture& fixture) {
    const std::string package_name = "preparation-fallback-package";
    const std::string package_base = "preparation-fallback-base";
    fixture.write_entry(package_name, "9PACKAGE_INVALID=value\n");
    fixture.write_entry(
            package_base,
            "9BASE_INVALID=value\n"
            "BASE_VALUE=selected\n");
    reset_case();

    const AurUpdateSourceBuildPreparation preparation = prepare(
            executable_preflight(package_name, package_base));
    static_cast<void>(require_single_issue(
            preparation,
            AurUpdatePreparationReason::StaticWorkItemInvalid,
            "package to PackageBase fallback"));
    expect(
            preparation.warnings.size() == 2 &&
                    preparation.warnings[0].preference_name == package_name &&
                    preparation.warnings[0].entry_path ==
                            fixture.entry_path(package_name) &&
                    preparation.warnings[1].preference_name == package_base &&
                    preparation.warnings[1].entry_path ==
                            fixture.entry_path(package_base),
            "package to PackageBase fallback did not preserve both reads in order");
    expect_no_generic_preparation("package to PackageBase fallback");
}

void test_package_failure_does_not_read_base(PreferenceFixture& fixture) {
    const std::string package_name = "preparation-package-failure";
    const std::string package_base = "preparation-package-failure-base";
    fixture.write_entry(package_name, "VALUE=available\n");
    // Baseをdirectory trapにし、誤ってfallback readすると2件目のtyped issueになる。
    fixture.create_directory_entry(package_base);
    reset_case();
    fail_next_source_preference_operation_for_test(
            package_name, SourcePreferenceTestFailurePoint::Status);

    const AurUpdateSourceBuildPreparation preparation = prepare(
            executable_preflight(package_name, package_base));
    const AurUpdatePreparationIssue& issue = require_single_issue(
            preparation,
            AurUpdatePreparationReason::SourcePreferenceUnavailable,
            "package failure short-circuit");
    expect(
            issue.package_name == package_name &&
                    issue.source_preference_failure.has_value() &&
                    issue.source_preference_failure->kind ==
                            SourcePreferenceFailureKind::StatusUnavailable,
            "package failure was replaced by a PackageBase result");
    expect(
            preparation.warnings.empty() &&
                    fs::is_directory(fixture.entry_path(package_base)),
            "package failure path consumed or changed the PackageBase trap");
    expect_no_generic_preparation("package failure short-circuit");

    StrictSourcePreferenceResult retry =
            read_source_preference_strict(package_name);
    expect(
            std::get_if<SourcePreferenceLoaded>(&retry) != nullptr,
            "package failure short-circuit did not consume the package hook");
}

void test_base_failure_is_typed(PreferenceFixture& fixture) {
    const std::string package_name = "preparation-base-failure-package";
    const std::string package_base = "preparation-base-failure-base";
    fixture.remove_entry(package_name);
    fixture.write_entry(package_base, "BASE_VALUE=available\n");
    reset_case();
    fail_next_source_preference_operation_for_test(
            package_base, SourcePreferenceTestFailurePoint::Open);

    const AurUpdateSourceBuildPreparation preparation = prepare(
            executable_preflight(package_name, package_base));
    const AurUpdatePreparationIssue& issue = require_single_issue(
            preparation,
            AurUpdatePreparationReason::SourcePreferenceUnavailable,
            "PackageBase strict failure");
    expect(
            issue.package_name == package_base &&
                    issue.source_preference_failure.has_value() &&
                    issue.source_preference_failure->kind ==
                            SourcePreferenceFailureKind::OpenFailed &&
                    issue.source_preference_failure->entry_path ==
                            fixture.entry_path(package_base),
            "PackageBase strict failure lost its typed identity");
    expect_no_generic_preparation("PackageBase strict failure");
}

void test_pkgdest_conflict(
        PreferenceFixture& fixture,
        const std::string& package_name,
        const std::string& pkgdest_value,
        const char* context) {
    const std::string context_text(context);
    fixture.write_entry(package_name, "PKGDEST=" + pkgdest_value + "\n");
    reset_case();

    const AurUpdateSourceBuildPreparation preparation =
            prepare(executable_preflight(package_name));
    const AurUpdatePreparationIssue& issue = require_single_issue(
            preparation,
            AurUpdatePreparationReason::SourcePreferencePkgdestConflict,
            context);
    expect(
            issue.package_name == package_name &&
                    issue.package_base == package_name &&
                    !issue.source_preference_failure.has_value(),
            context_text + ": PKGDEST conflict attribution changed");
    expect_no_generic_preparation(context);
}

void test_empty_and_nonempty_pkgdest(PreferenceFixture& fixture) {
    test_pkgdest_conflict(
            fixture,
            "preparation-empty-pkgdest",
            "",
            "empty PKGDEST conflict");
    test_pkgdest_conflict(
            fixture,
            "preparation-nonempty-pkgdest",
            "/owned-elsewhere",
            "nonempty PKGDEST conflict");
}

void test_database_failure_is_typed(PreferenceFixture& fixture) {
    const std::string package_name = "preparation-database-failure";
    fixture.write_entry(
            package_name,
            "9INVALID=value\n"
            "VALID=read-before-database\n");
    reset_case();
    stub::set_database_failure(PackageMetadataFailure{
            PackageMetadataErrorCode::ConfigurationUnavailable,
            "injected database path failure"});

    const AurUpdateSourceBuildPreparation preparation =
            prepare(executable_preflight(package_name));
    const AurUpdatePreparationIssue& issue = require_single_issue(
            preparation,
            AurUpdatePreparationReason::PacmanDatabaseUnavailable,
            "database resolver failure");
    expect(
            issue.package_metadata_failure.has_value() &&
                    issue.package_metadata_failure->code ==
                            PackageMetadataErrorCode::ConfigurationUnavailable &&
                    issue.package_metadata_failure->diagnostic ==
                            "injected database path failure" &&
                    issue.diagnostic == "injected database path failure",
            "database resolver failure lost PackageMetadataFailure fields");
    expect(
            preparation.warnings.size() == 1 &&
                    preparation.warnings.front().preference_name == package_name,
            "database resolver ran before strict preference warnings were retained");
    expect(
            stub::database_resolution_call_count() == 1 &&
                    stub::separated_option_check_call_count() == 1 &&
                    stub::artifact_pkgdest_check_call_count() == 2,
            "database failure did not occur after one complete generic preflight");
}

} // namespace

int main(int argc, char* argv[]) {
    try {
        if(argc != 2) {
            throw std::runtime_error(
                    "Usage: aur-update-execution-preparation-integration-test "
                    "<preference-fixture-root>");
        }

        const fs::path expected_root = fs::path(argv[1]).lexically_normal();
        expect(
                source_preference_root().lexically_normal() == expected_root,
                "Source preference test override was not captured before main");
        fs::create_directories(expected_root);

        ScopedUnsetEnvironmentVariable inherited_pkgdest("PKGDEST");
        PreferenceFixture fixture;

        test_absent_preference(fixture);
        test_empty_preference(fixture);
        test_valid_preference(fixture);
        test_warning_retention(fixture);
        test_status_open_and_read_failures(fixture);
        test_nonregular_preferences(fixture);
        test_package_to_base_fallback(fixture);
        test_package_failure_does_not_read_base(fixture);
        test_base_failure_is_typed(fixture);
        test_empty_and_nonempty_pkgdest(fixture);
        test_database_failure_is_typed(fixture);
        reset_source_preference_test_hooks();
    } catch(const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    std::cout << "AUR update execution preparation integration tests: all checks passed\n";
    return 0;
}
