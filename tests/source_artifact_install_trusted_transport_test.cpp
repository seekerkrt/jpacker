#include "artifact_workspace.hpp"
#include "package_base_artifact_install_executor.hpp"
#include "process.hpp"
#include "source_artifact_install_trusted_helper_state.hpp"
#include "source_artifact_install_trusted_protocol.hpp"
#include "source_artifact_install_trusted_transport.hpp"
#include "source_package_identity.hpp"
#include "trusted_alpm_receipt_helper_state.hpp"
#include "trusted_alpm_receipt_protocol.hpp"
#include "trusted_alpm_receipt_transport.hpp"
#include "trusted_cache_test_support.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <type_traits>
#include <unistd.h>
#include <utility>
#include <variant>
#include <vector>

#include <fcntl.h>
#include <linux/memfd.h>

static_assert(
    !std::is_convertible_v<
        SourceArtifactInstallTrustedExecutionResult,
        SourceArtifactInstallReceiptObservation>);
static_assert(
    !std::is_convertible_v<
        SourceArtifactInstallTrustedExecutionResult,
        SourceArtifactInstallCausalEvidence>);
static_assert(
    !std::is_convertible_v<
        TrustedAlpmReceiptCaptureResult,
        SourceArtifactInstallTrustedExecutionResult>);

namespace fs = std::filesystem;

namespace {

void expect(bool condition, const std::string& message) {
    if(!condition) throw std::runtime_error(message);
}

template <typename Function>
void expect_failure(Function&& function, const std::string& message) {
    bool failed = false;
    try {
        function();
    } catch(const std::exception&) {
        failed = true;
    }
    expect(failed, message);
}

std::string transaction_token(char character) {
    return std::string(64, character);
}

class TemporaryDirectory final {
public:
    explicit TemporaryDirectory(std::string prefix) {
        std::vector<char> path_template;
        const std::string template_text =
            (fs::temp_directory_path() / (prefix + "-XXXXXX")).string();
        path_template.assign(template_text.begin(), template_text.end());
        path_template.push_back('\0');
        char* created = mkdtemp(path_template.data());
        if(created == nullptr) {
            throw std::runtime_error("failed to create temporary directory");
        }
        path_ = created;
        fs::permissions(
            path_, fs::perms::owner_all, fs::perm_options::replace);
    }

    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

    ~TemporaryDirectory() noexcept {
        std::error_code error;
        fs::remove_all(path_, error);
    }

    [[nodiscard]] const fs::path& path() const noexcept {
        return path_;
    }

private:
    fs::path path_;
};

class ActualArchiveFixture final {
public:
    ActualArchiveFixture() : directory_("moguet-source-receipt-archive") {
    }

    fs::path create_archive(
        const std::string& package_name,
        const std::string& full_version,
        const std::string& package_base,
        const std::string& architecture) {
        const fs::path root = directory_.path() / package_name;
        fs::create_directory(root);
        std::ofstream pkginfo(root / ".PKGINFO");
        if(!pkginfo) throw std::runtime_error("failed to create .PKGINFO");
        pkginfo << "pkgname = " << package_name << '\n';
        pkginfo << "pkgbase = " << package_base << '\n';
        pkginfo << "pkgver = " << full_version << '\n';
        pkginfo << "pkgdesc = Moguet source artifact receipt test\n";
        pkginfo << "url = https://example.invalid/moguet\n";
        pkginfo << "builddate = 1\n";
        pkginfo << "packager = Moguet tests\n";
        pkginfo << "size = 0\n";
        pkginfo << "arch = " << architecture << '\n';
        pkginfo << "license = GPL\n";
        pkginfo.close();
        if(!pkginfo) throw std::runtime_error("failed to finish .PKGINFO");

        const fs::path archive =
            directory_.path() /
            (package_name + "-" + full_version + "-" + architecture +
             ".pkg.tar.zst");
        const pid_t child = fork();
        if(child < 0) throw std::runtime_error("failed to fork bsdtar");
        if(child == 0) {
            execl(
                "/usr/bin/bsdtar", "bsdtar", "--zstd", "-cf",
                archive.c_str(), "-C", root.c_str(), ".PKGINFO",
                static_cast<char*>(nullptr));
            _exit(127);
        }
        int status = 0;
        if(waitpid(child, &status, 0) != child ||
           !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            throw std::runtime_error("failed to create package archive");
        }
        return archive;
    }

private:
    TemporaryDirectory directory_;
};

class OwnedDescriptor final {
public:
    explicit OwnedDescriptor(int descriptor = -1) noexcept
        : descriptor_(descriptor) {
    }
    OwnedDescriptor(const OwnedDescriptor&) = delete;
    OwnedDescriptor& operator=(const OwnedDescriptor&) = delete;
    OwnedDescriptor(OwnedDescriptor&& other) noexcept
        : descriptor_(std::exchange(other.descriptor_, -1)) {
    }
    ~OwnedDescriptor() {
        if(descriptor_ >= 0) static_cast<void>(close(descriptor_));
    }
    [[nodiscard]] int get() const noexcept {
        return descriptor_;
    }

private:
    int descriptor_;
};

void write_all(int descriptor, const char* data, std::size_t size) {
    std::size_t offset = 0;
    while(offset < size) {
        const ssize_t written = write(descriptor, data + offset, size - offset);
        if(written > 0) {
            offset += static_cast<std::size_t>(written);
            continue;
        }
        if(written == -1 && errno == EINTR) continue;
        throw std::runtime_error("failed to write fixture descriptor");
    }
}

OwnedDescriptor sealed_input_from_files(
    const std::vector<fs::path>& files) {
#ifdef SYS_memfd_create
    const long raw_descriptor = syscall(
        SYS_memfd_create, "moguet-source-receipt-test",
        MFD_CLOEXEC | MFD_ALLOW_SEALING);
    if(raw_descriptor < 0) throw std::runtime_error("memfd_create failed");
    OwnedDescriptor descriptor(static_cast<int>(raw_descriptor));
    std::array<char, 64U * 1024U> buffer{};
    for(const fs::path& file : files) {
        std::ifstream input(file, std::ios::binary);
        if(!input) throw std::runtime_error("failed to read archive fixture");
        while(input) {
            input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            const std::streamsize count = input.gcount();
            if(count > 0) {
                write_all(
                    descriptor.get(), buffer.data(),
                    static_cast<std::size_t>(count));
            }
        }
        if(!input.eof()) throw std::runtime_error("archive fixture read failed");
    }
    constexpr int seals =
        F_SEAL_SEAL | F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE;
    if(fcntl(descriptor.get(), F_ADD_SEALS, seals) == -1 ||
       lseek(descriptor.get(), 0, SEEK_SET) == -1) {
        throw std::runtime_error("failed to seal archive fixture");
    }
    return descriptor;
#else
    static_cast<void>(files);
    throw std::runtime_error("memfd_create is unavailable");
#endif
}

std::uint64_t fixture_file_size(const fs::path& path) {
    return static_cast<std::uint64_t>(fs::file_size(path));
}

SourceArtifactInstallRootPrepareRequest root_request(
    const std::string& token,
    const fs::path& archive,
    const std::string& package_name = "moguet-source-transport-test",
    const std::string& full_version = "1-1",
    const std::string& package_base = "moguet-source-transport-base",
    const std::string& architecture = "any") {
    return SourceArtifactInstallRootPrepareRequest{
        token,
        package_base,
        SourceArtifactInstallTrustedDirective::AsDependency,
        false,
        true,
        {{0,
          package_name,
          full_version,
          package_base,
          architecture,
          fixture_file_size(archive),
          0}}};
}

SourceArtifactInstallTrustedStateStore open_source_store(
    const TemporaryDirectory& directory) {
    const int descriptor = open(
        directory.path().c_str(),
        O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if(descriptor < 0) throw std::runtime_error("failed to open test root");
    SourceArtifactInstallTrustedStateStore store =
        SourceArtifactInstallTrustedStateStore::open_below_runtime_parent(
            descriptor, geteuid());
    static_cast<void>(close(descriptor));
    return store;
}

TrustedAlpmReceiptStateStore open_selected_store(
    const TemporaryDirectory& directory) {
    const int descriptor = open(
        directory.path().c_str(),
        O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if(descriptor < 0) throw std::runtime_error("failed to open test root");
    TrustedAlpmReceiptStateStore store =
        TrustedAlpmReceiptStateStore::open_below_runtime_parent(
            descriptor, geteuid());
    static_cast<void>(close(descriptor));
    return store;
}

OwnedDescriptor input_text(const std::string& text) {
#ifdef SYS_memfd_create
    const long raw_descriptor = syscall(
        SYS_memfd_create, "moguet-source-receipt-targets", MFD_CLOEXEC);
    if(raw_descriptor < 0) throw std::runtime_error("memfd_create failed");
    OwnedDescriptor descriptor(static_cast<int>(raw_descriptor));
    write_all(descriptor.get(), text.data(), text.size());
    if(lseek(descriptor.get(), 0, SEEK_SET) == -1) {
        throw std::runtime_error("failed to rewind input text");
    }
    return descriptor;
#else
    static_cast<void>(text);
    throw std::runtime_error("memfd_create is unavailable");
#endif
}

void test_protocol_is_fixed_owner_and_closed() {
    const std::string token = transaction_token('a');
    const std::vector<std::string> valid{
        "prepare", token, "moguet-source-transport-base",
        "AsDependency", "0", "1", "--", "0",
        "moguet-source-transport-test", "1-1",
        "moguet-source-transport-base", "any", "64", "0"};
    const auto parsed =
        parse_source_artifact_install_trusted_helper_arguments(valid);
    expect(
        std::holds_alternative<
            SourceArtifactInstallTrustedHelperInvocation>(parsed),
        "valid source helper protocol was rejected");

    std::vector<std::string> owner_injection = valid;
    owner_injection.insert(
        owner_injection.begin() + 2, "selected-repository-provider");
    expect(
        std::holds_alternative<
            SourceArtifactInstallTrustedProtocolFailure>(
            parse_source_artifact_install_trusted_helper_arguments(
                owner_injection)),
        "source helper accepted a caller-selected owner");
    expect(
        std::holds_alternative<
            SourceArtifactInstallTrustedProtocolFailure>(
            parse_source_artifact_install_trusted_helper_arguments(
                {"consume", token, "/tmp/output"})),
        "source helper accepted an output path");
    expect(
        source_artifact_install_trusted_owner() ==
            "source-artifact-install",
        "source helper owner changed");

    std::vector<std::string> duplicate_child = valid;
    duplicate_child.insert(
        duplicate_child.end(),
        {"1", "moguet-source-transport-test", "1-1",
         "moguet-source-transport-base", "any", "64", "0"});
    expect(
        std::holds_alternative<
            SourceArtifactInstallTrustedProtocolFailure>(
            parse_source_artifact_install_trusted_helper_arguments(
                duplicate_child)),
        "source helper accepted a duplicate selected child");
    std::vector<std::string> wrong_package_base = valid;
    wrong_package_base[10] = "other-package-base";
    expect(
        std::holds_alternative<
            SourceArtifactInstallTrustedProtocolFailure>(
            parse_source_artifact_install_trusted_helper_arguments(
                wrong_package_base)),
        "source helper accepted a mismatched artifact PackageBase");
}

void test_state_staging_receipt_replay_and_owner_isolation() {
    ActualArchiveFixture archives;
    const fs::path archive = archives.create_archive(
        "moguet-source-transport-test", "1-1",
        "moguet-source-transport-base", "any");
    TemporaryDirectory runtime("moguet-source-receipt-state");
    SourceArtifactInstallTrustedStateStore source = open_source_store(runtime);
    TrustedAlpmReceiptStateStore selected = open_selected_store(runtime);

    const std::string token = transaction_token('b');
    const auto request = root_request(token, archive);
    OwnedDescriptor input = sealed_input_from_files({archive});
    const auto response = source.prepare(request, input.get());
    expect(
        response.hook_directory ==
                source_artifact_install_hook_directory(token) &&
            response.artifacts.size() == 1 &&
            response.artifacts[0].path ==
                source_artifact_install_staged_artifact_path(token, 0),
        "source state returned an unexpected fixed staging response");

    OwnedDescriptor targets = input_text(
        "moguet-source-transport-test\nsolver-introduced\n");
    source.record(token, targets.get());
    const auto receipt = parse_source_artifact_install_root_receipt(
        source.consume(token));
    const auto* complete = std::get_if<SourceArtifactInstallRootReceipt>(
        &receipt);
    expect(
        complete != nullptr &&
            complete->state ==
                SourceArtifactInstallRootReceiptState::Complete &&
            complete->installed_package_names ==
                std::vector<std::string>{
                    "moguet-source-transport-test",
                    "solver-introduced"},
        "source state did not retain the factual Install set");
    expect_failure(
        [&]() { static_cast<void>(source.consume(token)); },
        "second source consume succeeded");
    expect_failure(
        [&]() { source.abort(token); },
        "abort after source consume succeeded");

    const std::string selected_token = transaction_token('c');
    selected.prepare(selected_token, {"selected-package"});
    expect_failure(
        [&]() { static_cast<void>(source.consume(selected_token)); },
        "selected-provider token crossed into source state");

    const std::string source_token = transaction_token('d');
    auto source_request = root_request(source_token, archive);
    OwnedDescriptor source_input = sealed_input_from_files({archive});
    static_cast<void>(source.prepare(source_request, source_input.get()));
    expect_failure(
        [&]() { static_cast<void>(selected.consume(source_token)); },
        "source token crossed into selected-provider state");
    source.abort(source_token);
    selected.abort(selected_token);

    const std::string first = transaction_token('e');
    const std::string second = transaction_token('f');
    auto first_request = root_request(first, archive);
    auto second_request = root_request(second, archive);
    OwnedDescriptor first_input = sealed_input_from_files({archive});
    OwnedDescriptor second_input = sealed_input_from_files({archive});
    static_cast<void>(source.prepare(first_request, first_input.get()));
    static_cast<void>(source.prepare(second_request, second_input.get()));
    expect(
        fs::is_directory(
            runtime.path() / "moguet" / "source-artifact-installs" /
            "active" / first) &&
            fs::is_directory(
                runtime.path() / "moguet" /
                "source-artifact-installs" / "active" / second),
        "independent source transactions did not coexist");
    OwnedDescriptor duplicate_active_input = sealed_input_from_files({archive});
    expect_failure(
        [&]() {
            static_cast<void>(source.prepare(
                first_request, duplicate_active_input.get()));
        },
        "active source token was prepared twice");
    source.abort(first);
    source.abort(second);

    const std::string stale = transaction_token('9');
    auto stale_request = root_request(stale, archive);
    OwnedDescriptor stale_input = sealed_input_from_files({archive});
    static_cast<void>(source.prepare(stale_request, stale_input.get()));
    {
        std::ofstream unexpected(
            runtime.path() / "moguet" / "source-artifact-installs" /
            "active" / stale / "unexpected");
        unexpected << "unexpected\n";
    }
    expect_failure(
        [&]() { static_cast<void>(source.consume(stale)); },
        "source consume ignored stale extra state");
    expect_failure(
        [&]() { source.abort(stale); },
        "source abort deleted stale extra state");

    const std::string mismatch = transaction_token('1');
    auto mismatch_request = root_request(mismatch, archive);
    mismatch_request.artifacts[0].full_version = "2-1";
    OwnedDescriptor mismatch_input = sealed_input_from_files({archive});
    expect_failure(
        [&]() {
            static_cast<void>(source.prepare(
                mismatch_request, mismatch_input.get()));
        },
        "root staging accepted mismatched archive metadata");
    expect(
        !fs::exists(
            runtime.path() / "moguet" / "source-artifact-installs" /
            "active" / mismatch),
        "metadata mismatch left active source state");

#ifdef SYS_memfd_create
    const long unsealed_raw = syscall(
        SYS_memfd_create, "moguet-unsealed-source", MFD_CLOEXEC);
    expect(unsealed_raw >= 0, "failed to create unsealed input");
    OwnedDescriptor unsealed(static_cast<int>(unsealed_raw));
    std::ifstream archive_input(archive, std::ios::binary);
    std::vector<char> bytes{
        std::istreambuf_iterator<char>(archive_input),
        std::istreambuf_iterator<char>()};
    write_all(unsealed.get(), bytes.data(), bytes.size());
    const std::string unsealed_token = transaction_token('2');
    auto unsealed_request = root_request(unsealed_token, archive);
    expect_failure(
        [&]() {
            static_cast<void>(source.prepare(
                unsealed_request, unsealed.get()));
        },
        "root staging accepted mutable input bytes");
#endif
}

class TemporaryCacheHome final {
public:
    TemporaryCacheHome() : directory_("moguet-source-transport-cache") {
        const char* previous = std::getenv("XDG_CACHE_HOME");
        if(previous != nullptr) previous_ = previous;
        if(setenv("XDG_CACHE_HOME", directory_.path().c_str(), 1) != 0) {
            throw std::runtime_error("failed to set XDG_CACHE_HOME");
        }
    }

    ~TemporaryCacheHome() noexcept {
        if(previous_.has_value()) {
            static_cast<void>(setenv(
                "XDG_CACHE_HOME", previous_->c_str(), 1));
        } else {
            static_cast<void>(unsetenv("XDG_CACHE_HOME"));
        }
    }

private:
    TemporaryDirectory directory_;
    std::optional<std::string> previous_;
};

SourceAwarePackageIdentity expected_identity(
    const ArtifactPackageIdentity& actual,
    const std::string& package_base) {
    const std::string* architecture = actual.architecture.value();
    expect(architecture != nullptr, "actual architecture is unavailable");
    return SourceAwarePackageIdentity::make(
        PackageChildIdentity::make(
            PackageBaseIdentity::make(
                PackageSourceIdentity::aur(
                    SourceLocationIdentity::known_git_remote(
                        "https://aur.archlinux.org/" + package_base +
                        ".git")),
                package_base),
            actual.package_name),
        SourceRevisionIdentity::unknown(),
        PackageVersionIdentity::composite(actual.full_version),
        PackageArchitectureIdentity::known({*architecture}));
}

class PreparedInstallFixture final {
public:
    PreparedInstallFixture(
        const fs::path& archive,
        bool needed = false) {
        workspace_ = std::make_unique<ArtifactWorkspace>(
            create_artifact_workspace(prepare_private_trusted_cache_root(
                prepare_test_trusted_cache_root())));
        const fs::path target = workspace_->path() / archive.filename();
        const std::string output = target.string() + "\n";
        ExpectedPackageArtifactSet expected =
            validate_makepkg_packagelist_output_set(*workspace_, output);
        fs::copy_file(archive, target);
        ValidatedPackageArtifactSet artifacts =
            validate_post_build_package_artifacts(
                std::move(*workspace_), expected);
        workspace_.reset();

        PackageBaseArtifactInstallPreparationResult prepared =
            prepare_package_base_artifact_install(
                artifacts,
                "moguet-source-transport-base",
                {{"moguet-source-transport-base",
                  "moguet-source-transport-test",
                  DesiredInstallReason::Dependency}},
                ArtifactInstallPreparationOptions{needed, false},
                PacmanDatabasePaths{"/", "/var/lib/pacman"});
        if(!prepared.is_prepared() || prepared.prepared() == nullptr) {
            throw std::runtime_error("failed to prepare transport fixture");
        }
        preparation_ = std::make_unique<
            PackageBaseArtifactInstallPreparationResult>(
            std::move(prepared));

        PreparedPackageBaseArtifactInstall* install =
            preparation_->prepared();
        const auto& selected = install->selected_artifacts();
        expect(selected.size() == 1, "fixture selected set is not singular");
        const RootTargetIdentity root{0, "fixture-root"};
        binding_ = std::make_unique<SourceArtifactInstallTrustedBinding>(
            SourceArtifactInstallTrustedBinding{
                {SourceArtifactInstallInvocationIdentity::from_local_value(
                     "fixture-invocation"),
                 0,
                 "moguet-source-transport-base",
                 {root}},
                {{selected[0].artifact_index,
                  expected_identity(
                      selected[0].identity,
                      "moguet-source-transport-base"),
                  DesiredInstallReason::Dependency,
                  {PackageRole::BuildDependency},
                  {root}}}});
    }

    PreparedPackageBaseArtifactInstall& install() {
        return *preparation_->prepared();
    }

    const SourceArtifactInstallTrustedBinding& binding() const {
        return *binding_;
    }

private:
    std::unique_ptr<ArtifactWorkspace> workspace_;
    std::unique_ptr<PackageBaseArtifactInstallPreparationResult>
        preparation_;
    std::unique_ptr<SourceArtifactInstallTrustedBinding> binding_;
};

enum class ProcessScenario {
    Complete,
    Missing,
    PacmanFailure,
    MalformedReceipt,
};

struct ProcessStubState {
    ProcessScenario scenario = ProcessScenario::Complete;
    std::optional<SourceArtifactInstallRootPrepareRequest> request;
    std::size_t capture_count = 0;
    std::size_t run_count = 0;
    std::size_t abort_count = 0;
};

ProcessStubState process_state;

std::vector<std::string> helper_arguments(
    const ExplicitProcessInvocation& invocation) {
    expect(
        invocation.executable == "/usr/bin/sudo" &&
            invocation.arguments.size() >= 3 &&
            invocation.arguments[0] == "--" &&
            invocation.arguments[1] ==
                MOGUET_SOURCE_ARTIFACT_INSTALL_HELPER_PATH &&
            invocation.environment ==
                std::vector<std::string>{"PATH=/usr/bin", "LC_ALL=C"},
        "source transport helper provenance changed");
    return std::vector<std::string>(
        invocation.arguments.begin() + 2, invocation.arguments.end());
}

void verify_pacman_invocation(
    const ExplicitProcessInvocation& invocation) {
    expect(
        process_state.request.has_value(),
        "pacman ran before source preparation");
    const auto& request = *process_state.request;
    std::vector<std::string> expected{"--", "/usr/bin/pacman", "-U"};
    if(request.needed) expected.push_back("--needed");
    expected.insert(
        expected.end(),
        {"--asdeps", "--noconfirm", "--hookdir",
         source_artifact_install_hook_directory(request.transaction_token),
         "--"});
    expected.push_back(source_artifact_install_staged_artifact_path(
        request.transaction_token, 0));
    expect(
        invocation.executable == "/usr/bin/sudo" &&
            invocation.arguments == expected &&
            invocation.environment ==
                std::vector<std::string>{"PATH=/usr/bin", "LC_ALL=C"} &&
            !invocation.standard_input_fd.has_value(),
        "source transport pacman argv was not closed");
}

SourceArtifactInstallTrustedExecutionResult execute_scenario(
    PreparedInstallFixture& fixture,
    ProcessScenario scenario,
    char token_character) {
    process_state = ProcessStubState{scenario, std::nullopt, 0, 0, 0};
    return execute_source_artifact_install_trusted_transaction_for_test(
        fixture.install(), fixture.binding(),
        ArtifactInstallExecutionOptions{true},
        transaction_token(token_character));
}

void test_transport_observation_and_failure_matrix(
    const fs::path& archive) {
    PreparedInstallFixture complete_fixture(archive);
    const auto complete = execute_scenario(
        complete_fixture, ProcessScenario::Complete, '3');
    expect(
        complete.status() ==
                SourceArtifactInstallTrustedExecutionStatus::Complete &&
            complete.expectation().has_value() &&
            complete.observation().has_value() &&
            process_state.capture_count == 2 &&
            process_state.run_count == 1,
        "complete trusted transport did not close the process flow");
    const auto evidence = establish_source_artifact_install_receipt_evidence(
        *complete.expectation(), *complete.observation());
    const auto causal = project_source_artifact_install_causal_evidence(
        evidence);
    expect(
        evidence.completeness() ==
                SourceArtifactInstallReceiptEvidenceCompleteness::Complete &&
            causal.has_value() &&
            evidence.actual_install_set() ==
                std::vector<std::string>{
                    "moguet-source-transport-test", "solver-added"},
        "trusted transport did not produce closed causal evidence");

    SourceArtifactInstallReceiptExpectation wrong_invocation =
        *complete.expectation();
    wrong_invocation.work_item.invocation =
        SourceArtifactInstallInvocationIdentity::from_local_value(
            "other-invocation");
    const auto cross_evidence =
        establish_source_artifact_install_receipt_evidence(
            wrong_invocation, *complete.observation());
    expect(
        !project_source_artifact_install_causal_evidence(cross_evidence)
             .has_value(),
        "transport observation crossed invocation identity");
    SourceArtifactInstallReceiptExpectation wrong_work_item =
        *complete.expectation();
    ++wrong_work_item.work_item.work_item_index;
    const auto cross_work_item_evidence =
        establish_source_artifact_install_receipt_evidence(
            wrong_work_item, *complete.observation());
    expect(
        !project_source_artifact_install_causal_evidence(
             cross_work_item_evidence)
             .has_value(),
        "transport observation crossed work-item identity");

    process_state = {};
    const auto replay =
        execute_source_artifact_install_trusted_transaction_for_test(
            complete_fixture.install(), complete_fixture.binding(),
            ArtifactInstallExecutionOptions{true}, transaction_token('4'));
    expect(
        replay.status() ==
                SourceArtifactInstallTrustedExecutionStatus::InvalidRequest &&
            process_state.capture_count == 0 && process_state.run_count == 0,
        "consumed prepared transport capability was replayed");

    PreparedInstallFixture missing_fixture(archive, true);
    const auto missing = execute_scenario(
        missing_fixture, ProcessScenario::Missing, '5');
    const auto missing_evidence =
        establish_source_artifact_install_receipt_evidence(
            *missing.expectation(), *missing.observation());
    expect(
        missing.status() ==
                SourceArtifactInstallTrustedExecutionStatus::Missing &&
            missing_evidence.completeness() ==
                SourceArtifactInstallReceiptEvidenceCompleteness::Missing &&
            !project_source_artifact_install_causal_evidence(
                 missing_evidence)
                 .has_value(),
        "missing receipt became positive");

    PreparedInstallFixture failed_fixture(archive);
    const auto failed = execute_scenario(
        failed_fixture, ProcessScenario::PacmanFailure, '6');
    expect(
        failed.status() ==
                SourceArtifactInstallTrustedExecutionStatus::PacmanFailed &&
            failed.pacman_exit_status() == std::optional<int>{42} &&
            failed.observation()->transaction_ledger().transactions.front().command_outcome ==
                InvocationDependencyTransactionCommandOutcome::Failed &&
            !project_source_artifact_install_causal_evidence(
                 establish_source_artifact_install_receipt_evidence(
                     *failed.expectation(), *failed.observation()))
                 .has_value(),
        "failed transaction became positive");

    PreparedInstallFixture malformed_fixture(archive);
    const auto malformed = execute_scenario(
        malformed_fixture, ProcessScenario::MalformedReceipt, '7');
    expect(
        malformed.status() ==
                SourceArtifactInstallTrustedExecutionStatus::
                    MalformedReceipt &&
            malformed.observation()->transaction_ledger().transactions.front().receipt.state() ==
                PacmanTransactionReceiptState::Invalid,
        "malformed receipt was flattened to an empty receipt");

    PreparedInstallFixture invalid_fixture(archive);
    SourceArtifactInstallTrustedBinding invalid = invalid_fixture.binding();
    invalid.selected_artifacts[0].dependency_roles = {PackageRole::Root};
    process_state = {};
    const auto invalid_result =
        execute_source_artifact_install_trusted_transaction_for_test(
            invalid_fixture.install(), invalid,
            ArtifactInstallExecutionOptions{true}, transaction_token('8'));
    expect(
        invalid_result.status() ==
                SourceArtifactInstallTrustedExecutionStatus::InvalidRequest &&
            !invalid_result.expectation().has_value() &&
            process_state.capture_count == 0 && process_state.run_count == 0,
        "invalid role reached root preparation");
}

} // namespace

CapturedCommandResult capture_explicit_process_output_raw(
    const ExplicitProcessInvocation& invocation,
    bool suppress_standard_error) {
    expect(!suppress_standard_error, "source transport suppressed helper errors");
    ++process_state.capture_count;
    const std::vector<std::string> arguments = helper_arguments(invocation);
    expect(!arguments.empty(), "source helper invocation is empty");

    if(arguments[0] == "prepare") {
        expect(
            invocation.standard_input_fd.has_value() &&
                invocation.stdout_capture_limit ==
                    SOURCE_ARTIFACT_INSTALL_MAXIMUM_PROTOCOL_BYTES,
            "source prepare did not carry bounded sealed input");
        constexpr int seals =
            F_SEAL_SEAL | F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE;
        expect(
            (fcntl(*invocation.standard_input_fd, F_GET_SEALS) & seals) ==
                seals,
            "source prepare input was mutable");
        const auto parsed =
            parse_source_artifact_install_trusted_helper_arguments(arguments);
        const auto* helper =
            std::get_if<SourceArtifactInstallTrustedHelperInvocation>(&parsed);
        expect(helper != nullptr, "transport emitted invalid source prepare argv");
        SourceArtifactInstallRootPrepareRequest request{
            helper->transaction_token,
            helper->package_base,
            helper->directive,
            helper->needed,
            helper->no_confirm,
            helper->artifacts};
        process_state.request = request;
        SourceArtifactInstallRootPrepareResponse response{
            request.transaction_token,
            source_artifact_install_hook_directory(request.transaction_token),
            {{request.artifacts[0].artifact_index,
              source_artifact_install_staged_artifact_path(
                  request.transaction_token, 0)}}};
        return CapturedCommandResult{
            serialize_source_artifact_install_root_prepare_response(
                response, request),
            0,
            false};
    }

    expect(arguments[0] == "consume", "unexpected source helper capture verb");
    expect(
        invocation.stdout_capture_limit ==
            SOURCE_ARTIFACT_INSTALL_MAXIMUM_PROTOCOL_BYTES,
        "source consume was not bounded");
    const std::string& token = arguments[1];
    switch(process_state.scenario) {
        case ProcessScenario::Complete:
            return CapturedCommandResult{
                serialize_source_artifact_install_root_receipt(
                    SourceArtifactInstallRootReceipt{
                        SourceArtifactInstallRootReceiptState::Complete,
                        token,
                        {"moguet-source-transport-test", "solver-added"}}),
                0,
                false};
        case ProcessScenario::Missing:
            return CapturedCommandResult{
                serialize_source_artifact_install_root_receipt(
                    SourceArtifactInstallRootReceipt{
                        SourceArtifactInstallRootReceiptState::Missing,
                        token,
                        {}}),
                0,
                false};
        case ProcessScenario::MalformedReceipt:
            return CapturedCommandResult{"malformed\n", 0, false};
        case ProcessScenario::PacmanFailure:
            throw std::logic_error("consume ran after pacman failure");
    }
    throw std::logic_error("unknown process scenario");
}

int run_explicit_process(
    const ExplicitProcessInvocation& invocation,
    bool suppress_standard_output,
    bool suppress_standard_error) {
    expect(
        !suppress_standard_output && !suppress_standard_error,
        "source transport suppressed process output");
    const bool is_helper = invocation.arguments.size() >= 3 &&
                           invocation.arguments[1] ==
                               MOGUET_SOURCE_ARTIFACT_INSTALL_HELPER_PATH;
    if(is_helper) {
        const auto arguments = helper_arguments(invocation);
        expect(arguments[0] == "abort", "unexpected source helper run verb");
        ++process_state.abort_count;
        return 0;
    }
    ++process_state.run_count;
    verify_pacman_invocation(invocation);
    return process_state.scenario == ProcessScenario::PacmanFailure ? 42 : 0;
}

int main() {
    try {
        TemporaryCacheHome cache_home;
        test_protocol_is_fixed_owner_and_closed();
        test_state_staging_receipt_replay_and_owner_isolation();
        ActualArchiveFixture archives;
        const fs::path archive = archives.create_archive(
            "moguet-source-transport-test", "1-1",
            "moguet-source-transport-base", "any");
        test_transport_observation_and_failure_matrix(archive);
    } catch(const std::exception& error) {
        std::cerr << "source artifact trusted transport test failed: "
                  << error.what() << '\n';
        return 1;
    }
    return 0;
}
