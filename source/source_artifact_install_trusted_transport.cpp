#include "source_artifact_install_trusted_transport.hpp"

#include "logging.hpp"
#include "package_identifier.hpp"
#include "process.hpp"
#include "shell_words.hpp"
#include "source_artifact_install_trusted_protocol.hpp"
#include "source_package_identity_projection.hpp"
#include "trusted_alpm_receipt_protocol.hpp"
#include "trusted_alpm_receipt_transport.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <exception>
#include <fcntl.h>
#include <optional>
#include <set>
#include <string_view>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <utility>
#include <variant>

#include <linux/memfd.h>

#ifndef MOGUET_SOURCE_ARTIFACT_INSTALL_HELPER_PATH
#error "MOGUET_SOURCE_ARTIFACT_INSTALL_HELPER_PATH is required"
#endif

namespace {

constexpr std::string_view SUDO_PATH = "/usr/bin/sudo";
constexpr std::string_view PACMAN_PATH = "/usr/bin/pacman";

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
    OwnedDescriptor& operator=(OwnedDescriptor&& other) noexcept {
        if(this == &other) return *this;
        if(descriptor_ >= 0) static_cast<void>(close(descriptor_));
        descriptor_ = std::exchange(other.descriptor_, -1);
        return *this;
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

bool same_executable_identity(
    const struct stat& lhs, const struct stat& rhs) noexcept {
    return lhs.st_dev == rhs.st_dev && lhs.st_ino == rhs.st_ino &&
           lhs.st_mode == rhs.st_mode && lhs.st_uid == rhs.st_uid;
}

bool trusted_directory_metadata(const struct stat& metadata) noexcept {
    return S_ISDIR(metadata.st_mode) && metadata.st_uid == 0 &&
           (metadata.st_mode & (S_IWGRP | S_IWOTH)) == 0;
}

bool validate_fixed_executable(
    std::string_view executable_path, bool require_helper_mode) noexcept {
    if(executable_path.empty() || executable_path.front() != '/' ||
       executable_path.find('\0') != std::string_view::npos) {
        return false;
    }

    const int root_fd = open(
        "/", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if(root_fd == -1) return false;
    OwnedDescriptor current(root_fd);
    struct stat root_metadata{};
    if(fstat(current.get(), &root_metadata) == -1 ||
       !trusted_directory_metadata(root_metadata)) {
        return false;
    }

    std::size_t component_begin = 1;
    while(component_begin < executable_path.size()) {
        const std::size_t separator =
            executable_path.find('/', component_begin);
        const bool is_final = separator == std::string_view::npos;
        const std::size_t component_end =
            is_final ? executable_path.size() : separator;
        const std::string component(executable_path.substr(
            component_begin, component_end - component_begin));
        if(component.empty() || component == "." || component == "..") {
            return false;
        }

        if(is_final) {
            const int file_fd = openat(
                current.get(), component.c_str(),
                O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
            if(file_fd == -1) return false;
            OwnedDescriptor file(file_fd);
            struct stat descriptor_metadata{};
            struct stat named_metadata{};
            if(fstat(file.get(), &descriptor_metadata) == -1 ||
               fstatat(
                   current.get(), component.c_str(), &named_metadata,
                   AT_SYMLINK_NOFOLLOW) == -1 ||
               !same_executable_identity(
                   descriptor_metadata, named_metadata) ||
               !S_ISREG(descriptor_metadata.st_mode) ||
               descriptor_metadata.st_uid != 0 ||
               (descriptor_metadata.st_mode & (S_IWGRP | S_IWOTH)) != 0 ||
               (descriptor_metadata.st_mode & S_IXUSR) == 0) {
                return false;
            }
            if(require_helper_mode &&
               (descriptor_metadata.st_mode & 07777) != 0755) {
                return false;
            }
            return true;
        }

        const int next_fd = openat(
            current.get(), component.c_str(),
            O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        if(next_fd == -1) return false;
        OwnedDescriptor next(next_fd);
        struct stat descriptor_metadata{};
        struct stat named_metadata{};
        if(fstat(next.get(), &descriptor_metadata) == -1 ||
           fstatat(
               current.get(), component.c_str(), &named_metadata,
               AT_SYMLINK_NOFOLLOW) == -1 ||
           !same_executable_identity(
               descriptor_metadata, named_metadata) ||
           !trusted_directory_metadata(descriptor_metadata)) {
            return false;
        }
        current = std::move(next);
        component_begin = separator + 1;
    }
    return false;
}

bool fixed_executables_are_trusted() noexcept {
    return validate_fixed_executable(SUDO_PATH, false) &&
           validate_fixed_executable(PACMAN_PATH, false) &&
           validate_fixed_executable(
               MOGUET_SOURCE_ARTIFACT_INSTALL_HELPER_PATH, true);
}

std::vector<std::string> minimal_root_command_environment() {
    return {"PATH=/usr/bin", "LC_ALL=C"};
}

void log_explicit_invocation(const ExplicitProcessInvocation& invocation) {
    std::vector<std::string> display;
    display.reserve(invocation.arguments.size() + 1);
    display.push_back(invocation.executable);
    display.insert(
        display.end(), invocation.arguments.begin(),
        invocation.arguments.end());
    Logger::raw_cmd(shell_words::join(display));
}

CapturedCommandResult capture_explicit(
    const ExplicitProcessInvocation& invocation) {
    log_explicit_invocation(invocation);
    return capture_explicit_process_output_raw(invocation);
}

int run_explicit(const ExplicitProcessInvocation& invocation) {
    log_explicit_invocation(invocation);
    return run_explicit_process(invocation);
}

bool roots_are_valid_and_unique(
    const std::vector<RootTargetIdentity>& roots) {
    if(roots.empty()) return false;
    std::set<std::pair<std::size_t, std::string>> unique;
    for(const RootTargetIdentity& root : roots) {
        if(!is_valid_package_name(root.requested_name) ||
           !unique.emplace(root.invocation_index, root.requested_name)
                .second) {
            return false;
        }
    }
    return true;
}

bool same_root_set(
    const std::vector<RootTargetIdentity>& lhs,
    const std::vector<RootTargetIdentity>& rhs) {
    if(lhs.size() != rhs.size()) return false;
    return std::all_of(lhs.begin(), lhs.end(), [&rhs](const auto& root) {
        return std::find(rhs.begin(), rhs.end(), root) != rhs.end();
    });
}

bool root_set_is_subset(
    const std::vector<RootTargetIdentity>& subset,
    const std::vector<RootTargetIdentity>& superset) {
    return std::all_of(
        subset.begin(), subset.end(), [&superset](const auto& root) {
            return std::find(superset.begin(), superset.end(), root) !=
                   superset.end();
        });
}

bool roles_are_valid_and_unique(const std::vector<PackageRole>& roles) {
    if(roles.empty()) return false;
    std::set<int> unique;
    for(PackageRole role : roles) {
        switch(role) {
            case PackageRole::BuildDependency:
            case PackageRole::CheckDependency:
                break;
            case PackageRole::Root:
            case PackageRole::RuntimeDependency:
            default:
                return false;
        }
        if(!unique.insert(static_cast<int>(role)).second) return false;
    }
    return true;
}

bool edge_indices_are_unique(
    const std::vector<std::size_t>& edge_indices) {
    std::set<std::size_t> unique;
    return std::all_of(
        edge_indices.begin(), edge_indices.end(),
        [&unique](std::size_t edge_index) {
            return unique.insert(edge_index).second;
        });
}

bool binding_is_coherent(
    const PreparedPackageBaseArtifactInstall& install,
    const SourceArtifactInstallTrustedBinding& binding) {
    if(!is_valid_package_name(binding.work_item.package_base) ||
       binding.work_item.package_base != install.package_base() ||
       !roots_are_valid_and_unique(binding.work_item.requested_roots) ||
       binding.selected_artifacts.size() !=
           install.selected_artifacts().size() ||
       binding.selected_artifacts.empty()) {
        return false;
    }
    if(install.transaction_directive() != InstallReasonDirective::Default &&
       install.transaction_directive() !=
           InstallReasonDirective::AsDependency) {
        return false;
    }

    std::vector<RootTargetIdentity> attributed_roots;
    std::set<std::size_t> artifact_indices;
    std::set<std::string> package_names;
    for(std::size_t index = 0;
        index < binding.selected_artifacts.size(); ++index) {
        const auto& expected = binding.selected_artifacts[index];
        const auto& prepared = install.selected_artifacts()[index];
        if(expected.artifact_index != prepared.artifact_index ||
           expected.desired_reason != DesiredInstallReason::Dependency ||
           prepared.desired_reason != DesiredInstallReason::Dependency ||
           !roles_are_valid_and_unique(expected.dependency_roles) ||
           !edge_indices_are_unique(
               expected.build_plan_dependency_edge_indices) ||
           !roots_are_valid_and_unique(expected.requested_roots) ||
           !root_set_is_subset(
               expected.requested_roots,
               binding.work_item.requested_roots) ||
           !artifact_indices.insert(expected.artifact_index).second ||
           !package_names.insert(prepared.identity.package_name).second ||
           expected.expected_identity.package()
                   .package_base()
                   .package_base() != binding.work_item.package_base ||
           !project_artifact_source_package_identity(
                expected.expected_identity, prepared.identity)
                .is_success()) {
            return false;
        }
        for(const RootTargetIdentity& root : expected.requested_roots) {
            if(std::find(
                   attributed_roots.begin(), attributed_roots.end(), root) ==
               attributed_roots.end()) {
                attributed_roots.push_back(root);
            }
        }
    }
    return same_root_set(
        attributed_roots, binding.work_item.requested_roots);
}

bool same_timespec(const timespec& lhs, const timespec& rhs) noexcept {
    return lhs.tv_sec == rhs.tv_sec && lhs.tv_nsec == rhs.tv_nsec;
}

bool same_snapshot_metadata(
    const struct stat& lhs, const struct stat& rhs) noexcept {
    return lhs.st_dev == rhs.st_dev && lhs.st_ino == rhs.st_ino &&
           lhs.st_mode == rhs.st_mode && lhs.st_uid == rhs.st_uid &&
           lhs.st_gid == rhs.st_gid && lhs.st_nlink == rhs.st_nlink &&
           lhs.st_size == rhs.st_size &&
           same_timespec(lhs.st_mtim, rhs.st_mtim) &&
           same_timespec(lhs.st_ctim, rhs.st_ctim);
}

struct stat require_snapshot_source(
    int descriptor, std::uintmax_t expected_device,
    std::uintmax_t expected_inode, std::uintmax_t expected_owner,
    std::uint64_t maximum_size) {
    struct stat metadata{};
    if(descriptor < 0 || fstat(descriptor, &metadata) == -1 ||
       !S_ISREG(metadata.st_mode) || metadata.st_size <= 0 ||
       static_cast<std::uintmax_t>(metadata.st_dev) != expected_device ||
       static_cast<std::uintmax_t>(metadata.st_ino) != expected_inode ||
       static_cast<std::uintmax_t>(metadata.st_uid) != expected_owner ||
       static_cast<std::uint64_t>(metadata.st_size) > maximum_size) {
        throw std::runtime_error(
            "validated artifact descriptor cannot be snapshotted");
    }
    return metadata;
}

void append_descriptor_bytes(
    int source_fd, const struct stat& expected_source,
    int destination_fd) {
    std::array<char, 64U * 1024U> buffer{};
    std::uint64_t offset = 0;
    const std::uint64_t byte_count =
        static_cast<std::uint64_t>(expected_source.st_size);
    while(offset < byte_count) {
        const std::size_t request_size = static_cast<std::size_t>(
            std::min<std::uint64_t>(byte_count - offset, buffer.size()));
        const ssize_t count = pread(
            source_fd, buffer.data(), request_size,
            static_cast<off_t>(offset));
        if(count > 0) {
            std::size_t written_offset = 0;
            const std::size_t count_size = static_cast<std::size_t>(count);
            while(written_offset < count_size) {
                const ssize_t written = write(
                    destination_fd, buffer.data() + written_offset,
                    count_size - written_offset);
                if(written > 0) {
                    written_offset += static_cast<std::size_t>(written);
                    continue;
                }
                if(written == -1 && errno == EINTR) continue;
                throw std::runtime_error(
                    "unable to write sealed source-artifact snapshot");
            }
            offset += count_size;
            continue;
        }
        if(count == -1 && errno == EINTR) continue;
        throw std::runtime_error(
            "unable to read validated artifact descriptor");
    }
    struct stat after{};
    if(fstat(source_fd, &after) == -1 ||
       !same_snapshot_metadata(expected_source, after)) {
        throw std::runtime_error(
            "validated artifact changed while being snapshotted");
    }
}

OwnedDescriptor create_sealable_memfd() {
#ifdef SYS_memfd_create
    const long descriptor = syscall(
        SYS_memfd_create, "moguet-source-artifact-snapshot",
        MFD_CLOEXEC | MFD_ALLOW_SEALING);
    if(descriptor < 0) {
        throw std::runtime_error(
            "unable to create source-artifact snapshot");
    }
    return OwnedDescriptor(static_cast<int>(descriptor));
#else
    throw std::runtime_error(
        "memfd_create is unavailable for source-artifact staging");
#endif
}

struct PreparedTransportInput {
    OwnedDescriptor sealed_input;
    SourceArtifactInstallRootPrepareRequest root_request;
    std::vector<SourceArtifactInstallObservedSelectedArtifact>
        observed_artifacts;
    std::vector<std::string> requested_package_names;
};

std::vector<std::string> prepare_arguments(
    const SourceArtifactInstallRootPrepareRequest& request) {
    std::vector<std::string> arguments{
        "--",
        MOGUET_SOURCE_ARTIFACT_INSTALL_HELPER_PATH,
        "prepare",
        request.transaction_token,
        request.package_base,
        request.directive == SourceArtifactInstallTrustedDirective::
                                 AsDependency
            ? "AsDependency"
            : "PreserveExistingReason",
        request.needed ? "1" : "0",
        request.no_confirm ? "1" : "0",
        "--"};
    arguments.reserve(arguments.size() + request.artifacts.size() * 7);
    for(const auto& artifact : request.artifacts) {
        arguments.push_back(std::to_string(artifact.artifact_index));
        arguments.push_back(artifact.package_name);
        arguments.push_back(artifact.full_version);
        arguments.push_back(artifact.package_base);
        arguments.push_back(artifact.architecture);
        arguments.push_back(std::to_string(artifact.artifact_size));
        arguments.push_back(std::to_string(artifact.signature_size));
    }
    return arguments;
}

ExplicitProcessInvocation prepare_invocation(
    const SourceArtifactInstallRootPrepareRequest& request,
    int sealed_input_fd) {
    ExplicitProcessInvocation invocation;
    invocation.executable = std::string(SUDO_PATH);
    invocation.arguments = prepare_arguments(request);
    invocation.environment = minimal_root_command_environment();
    invocation.stdout_capture_limit =
        SOURCE_ARTIFACT_INSTALL_MAXIMUM_PROTOCOL_BYTES;
    invocation.standard_input_fd = sealed_input_fd;
    return invocation;
}

ExplicitProcessInvocation helper_invocation(
    const std::string& command,
    const std::string& transaction_token) {
    ExplicitProcessInvocation invocation;
    invocation.executable = std::string(SUDO_PATH);
    invocation.arguments = {
        "--", MOGUET_SOURCE_ARTIFACT_INSTALL_HELPER_PATH, command,
        transaction_token};
    invocation.environment = minimal_root_command_environment();
    return invocation;
}

ExplicitProcessInvocation pacman_invocation(
    const SourceArtifactInstallRootPrepareRequest& request,
    const SourceArtifactInstallRootPrepareResponse& response) {
    ExplicitProcessInvocation invocation;
    invocation.executable = std::string(SUDO_PATH);
    invocation.arguments = {"--", std::string(PACMAN_PATH), "-U"};
    if(request.needed) invocation.arguments.push_back("--needed");
    if(request.directive ==
       SourceArtifactInstallTrustedDirective::AsDependency) {
        invocation.arguments.push_back("--asdeps");
    }
    if(request.no_confirm) invocation.arguments.push_back("--noconfirm");
    invocation.arguments.push_back("--hookdir");
    invocation.arguments.push_back(response.hook_directory);
    invocation.arguments.push_back("--");
    for(const auto& artifact : response.artifacts) {
        invocation.arguments.push_back(artifact.path);
    }
    invocation.environment = minimal_root_command_environment();
    return invocation;
}

int abort_prepared_state_noexcept(
    const std::string& transaction_token) noexcept {
    try {
        ExplicitProcessInvocation abort =
            helper_invocation("abort", transaction_token);
        try {
            return run_explicit(abort);
        } catch(...) {
            return run_explicit_process(abort);
        }
    } catch(...) {
        return 127;
    }
}

PacmanTransactionReceiptObservation missing_observation() {
    return PacmanTransactionReceiptObservation{
        PacmanTransactionReceiptObservationState::Missing,
        std::nullopt,
        std::nullopt,
        {}};
}

PacmanTransactionReceiptObservation incomplete_observation(
    const std::string& transaction_token) {
    return PacmanTransactionReceiptObservation{
        PacmanTransactionReceiptObservationState::Incomplete,
        transaction_token,
        InvocationDependencyTransactionOwner::SourceArtifactInstall,
        {}};
}

PacmanTransactionReceiptObservation invalid_observation(
    const std::string& transaction_token) {
    return PacmanTransactionReceiptObservation{
        PacmanTransactionReceiptObservationState::Complete,
        transaction_token,
        InvocationDependencyTransactionOwner::Unknown,
        {}};
}

InvocationDependencyTransaction make_transaction(
    const std::string& transaction_token,
    std::vector<std::string> requested_packages,
    InvocationDependencyTransactionCommandOutcome command_outcome,
    PacmanTransactionReceiptObservation observation) {
    constexpr auto OWNER =
        InvocationDependencyTransactionOwner::SourceArtifactInstall;
    return InvocationDependencyTransaction{
        transaction_token,
        OWNER,
        std::move(requested_packages),
        command_outcome,
        validate_pacman_transaction_receipt(
            transaction_token, OWNER, observation)};
}

} // namespace

class SourceArtifactInstallTrustedTransport final {
    static PreparedTransportInput snapshot_selected_artifacts(
        PreparedPackageBaseArtifactInstall& install,
        const SourceArtifactInstallTrustedBinding& binding,
        const std::string& transaction_token,
        const ArtifactInstallExecutionOptions& options) {
        install.require_execution_coherence();
        if(!binding_is_coherent(install, binding)) {
            throw std::runtime_error(
                "source-artifact trusted binding is incoherent");
        }

        OwnedDescriptor snapshot = create_sealable_memfd();
        SourceArtifactInstallRootPrepareRequest root_request{
            transaction_token,
            install.package_base_,
            install.transaction_directive_ ==
                    InstallReasonDirective::AsDependency
                ? SourceArtifactInstallTrustedDirective::AsDependency
                : SourceArtifactInstallTrustedDirective::
                      PreserveExistingReason,
            install.needed_,
            options.no_confirm,
            {}};
        std::vector<SourceArtifactInstallObservedSelectedArtifact>
            observed_artifacts;
        std::vector<std::string> requested_names;
        root_request.artifacts.reserve(install.selected_artifacts_.size());
        observed_artifacts.reserve(install.selected_artifacts_.size());
        requested_names.reserve(install.selected_artifacts_.size());

        for(std::size_t position = 0;
            position < install.selected_artifacts_.size(); ++position) {
            const auto& selected = install.selected_artifacts_[position];
            const auto& expected = binding.selected_artifacts[position];
            const auto& record =
                install.artifacts_.records_[selected.artifact_index];
            const struct stat artifact_metadata = require_snapshot_source(
                record.artifact_descriptor, record.artifact_device,
                record.artifact_inode, record.artifact_owner,
                SOURCE_ARTIFACT_INSTALL_MAXIMUM_ARTIFACT_BYTES);
            append_descriptor_bytes(
                record.artifact_descriptor, artifact_metadata,
                snapshot.get());

            std::uint64_t signature_size = 0;
            if(record.has_signature) {
                const struct stat signature_metadata =
                    require_snapshot_source(
                        record.signature_descriptor,
                        record.signature_device,
                        record.signature_inode,
                        record.signature_owner,
                        SOURCE_ARTIFACT_INSTALL_MAXIMUM_SIGNATURE_BYTES);
                append_descriptor_bytes(
                    record.signature_descriptor, signature_metadata,
                    snapshot.get());
                signature_size =
                    static_cast<std::uint64_t>(signature_metadata.st_size);
            } else if(record.signature_descriptor >= 0) {
                throw std::runtime_error(
                    "source-artifact signature descriptor is incoherent");
            }

            const std::string* package_base =
                selected.identity.package_base.value();
            const std::string* architecture =
                selected.identity.architecture.value();
            if(selected.identity.package_base.state() !=
                   ArtifactMetadataValueState::Known ||
               selected.identity.architecture.state() !=
                   ArtifactMetadataValueState::Known ||
               package_base == nullptr || architecture == nullptr) {
                throw std::runtime_error(
                    "source-artifact archive identity is incomplete");
            }
            root_request.artifacts.push_back(
                SourceArtifactInstallRootArtifactExpectation{
                    selected.artifact_index,
                    selected.identity.package_name,
                    selected.identity.full_version,
                    *package_base,
                    *architecture,
                    static_cast<std::uint64_t>(
                        artifact_metadata.st_size),
                    signature_size});
            observed_artifacts.push_back(
                SourceArtifactInstallObservedSelectedArtifact{
                    selected.artifact_index,
                    selected.identity,
                    selected.desired_reason,
                    expected.dependency_roles,
                    expected.requested_roots,
                    expected.build_plan_dependency_edge_indices});
            requested_names.push_back(selected.identity.package_name);
        }

        install.require_execution_coherence();
        if(!is_valid_source_artifact_install_root_request(root_request) ||
           fsync(snapshot.get()) == -1) {
            throw std::runtime_error(
                "source-artifact snapshot is invalid");
        }
        constexpr int SEALS =
            F_SEAL_SEAL | F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE;
        if(fcntl(snapshot.get(), F_ADD_SEALS, SEALS) == -1 ||
           (fcntl(snapshot.get(), F_GET_SEALS) & SEALS) != SEALS ||
           lseek(snapshot.get(), 0, SEEK_SET) == -1) {
            throw std::runtime_error(
                "source-artifact snapshot could not be sealed");
        }
        return PreparedTransportInput{
            std::move(snapshot), std::move(root_request),
            std::move(observed_artifacts), std::move(requested_names)};
    }

    static SourceArtifactInstallReceiptObservation make_observation(
        const SourceArtifactInstallTrustedBinding& binding,
        std::vector<SourceArtifactInstallObservedSelectedArtifact>
            observed_artifacts,
        InvocationDependencyTransaction transaction) {
        InvocationDependencyTransactionLedger ledger;
        ledger.transactions.push_back(std::move(transaction));
        return SourceArtifactInstallReceiptObservation(
            binding.work_item, std::move(observed_artifacts),
            std::move(ledger));
    }

public:
    static bool capability_is_active(
        const PreparedPackageBaseArtifactInstall& install) noexcept {
        return install.state_ ==
               PreparedPackageBaseArtifactInstall::State::Active;
    }

    static SourceArtifactInstallTrustedExecutionResult
    unknown_after_consumption(
        const SourceArtifactInstallTrustedBinding& binding,
        const std::string& transaction_token) {
        static_cast<void>(abort_prepared_state_noexcept(transaction_token));
        return SourceArtifactInstallTrustedExecutionResult(
            SourceArtifactInstallTrustedExecutionStatus::OutcomeUnknown,
            std::nullopt,
            SourceArtifactInstallReceiptExpectation{
                binding.work_item, binding.selected_artifacts,
                transaction_token},
            std::nullopt,
            "source-artifact execution outcome is unknown after the one-shot capability was consumed");
    }

    static bool request_is_valid(
        PreparedPackageBaseArtifactInstall& install,
        const SourceArtifactInstallTrustedBinding& binding) noexcept {
        try {
            install.require_execution_coherence();
            return binding_is_coherent(install, binding);
        } catch(...) {
            return false;
        }
    }

    static SourceArtifactInstallTrustedExecutionResult invalid_result(
        SourceArtifactInstallTrustedExecutionStatus status,
        std::string diagnostic) {
        return SourceArtifactInstallTrustedExecutionResult(
            status, std::nullopt, std::nullopt, std::nullopt,
            std::move(diagnostic));
    }

    static SourceArtifactInstallTrustedExecutionResult execute(
        PreparedPackageBaseArtifactInstall& install,
        const SourceArtifactInstallTrustedBinding& binding,
        const ArtifactInstallExecutionOptions& options,
        const std::string& transaction_token) {
        SourceArtifactInstallReceiptExpectation expectation{
            binding.work_item, binding.selected_artifacts,
            transaction_token};

        PreparedTransportInput input = snapshot_selected_artifacts(
            install, binding, transaction_token, options);

        // The immutable stream is complete and all local identities have
        // been revalidated. Any root helper attempt consumes this one-shot
        // PackageBase install capability, even if execution later fails.
        install.state_ = PreparedPackageBaseArtifactInstall::State::Consumed;

        CapturedCommandResult prepare_result;
        try {
            prepare_result = capture_explicit(prepare_invocation(
                input.root_request, input.sealed_input.get()));
        } catch(...) {
            const int abort_status =
                abort_prepared_state_noexcept(transaction_token);
            auto observation = make_observation(
                binding, std::move(input.observed_artifacts),
                make_transaction(
                    transaction_token,
                    std::move(input.requested_package_names),
                    InvocationDependencyTransactionCommandOutcome::
                        NotAttempted,
                    missing_observation()));
            return SourceArtifactInstallTrustedExecutionResult(
                abort_status == 0
                    ? SourceArtifactInstallTrustedExecutionStatus::
                          PrepareFailed
                    : SourceArtifactInstallTrustedExecutionStatus::
                          AbortFailed,
                std::nullopt, std::move(expectation),
                std::move(observation),
                abort_status == 0
                    ? "source-artifact preparation observation failed"
                    : "source-artifact preparation observation and exact abort failed");
        }
        const SourceArtifactInstallRootPrepareResponseResult parsed_prepare =
            prepare_result.exit_code == 0 &&
                    !prepare_result.stdout_capture_limit_exceeded
                ? parse_source_artifact_install_root_prepare_response(
                      prepare_result.output, input.root_request)
                : SourceArtifactInstallRootPrepareResponseResult{
                      SourceArtifactInstallTrustedProtocolFailure{
                          SourceArtifactInstallTrustedProtocolIssueKind::
                              TruncatedProtocol}};
        const auto* prepared =
            std::get_if<SourceArtifactInstallRootPrepareResponse>(
                &parsed_prepare);
        if(prepared == nullptr) {
            int abort_status = 0;
            if(prepare_result.exit_code == 0) {
                abort_status =
                    abort_prepared_state_noexcept(transaction_token);
            }
            auto observation = make_observation(
                binding, std::move(input.observed_artifacts),
                make_transaction(
                    transaction_token,
                    std::move(input.requested_package_names),
                    InvocationDependencyTransactionCommandOutcome::
                        NotAttempted,
                    missing_observation()));
            return SourceArtifactInstallTrustedExecutionResult(
                abort_status == 0
                    ? SourceArtifactInstallTrustedExecutionStatus::
                          PrepareFailed
                    : SourceArtifactInstallTrustedExecutionStatus::
                          AbortFailed,
                std::nullopt, std::move(expectation),
                std::move(observation),
                abort_status == 0
                    ? "source-artifact preparation failed"
                    : "source-artifact preparation and exact abort failed");
        }

        int pacman_status = 127;
        try {
            pacman_status = run_explicit(
                pacman_invocation(input.root_request, *prepared));
        } catch(...) {
            const int abort_status =
                abort_prepared_state_noexcept(transaction_token);
            auto observation = make_observation(
                binding, std::move(input.observed_artifacts),
                make_transaction(
                    transaction_token,
                    std::move(input.requested_package_names),
                    InvocationDependencyTransactionCommandOutcome::
                        NotAttempted,
                    missing_observation()));
            return SourceArtifactInstallTrustedExecutionResult(
                abort_status == 0
                    ? SourceArtifactInstallTrustedExecutionStatus::
                          PrepareFailed
                    : SourceArtifactInstallTrustedExecutionStatus::
                          AbortFailed,
                std::nullopt, std::move(expectation),
                std::move(observation),
                abort_status == 0
                    ? "source-artifact pacman invocation failed before execution"
                    : "source-artifact pacman invocation and exact abort failed");
        }
        if(pacman_status != 0) {
            const int abort_status =
                abort_prepared_state_noexcept(transaction_token);
            auto observation = make_observation(
                binding, std::move(input.observed_artifacts),
                make_transaction(
                    transaction_token,
                    std::move(input.requested_package_names),
                    InvocationDependencyTransactionCommandOutcome::Failed,
                    missing_observation()));
            return SourceArtifactInstallTrustedExecutionResult(
                abort_status == 0
                    ? SourceArtifactInstallTrustedExecutionStatus::
                          PacmanFailed
                    : SourceArtifactInstallTrustedExecutionStatus::
                          AbortFailed,
                pacman_status, std::move(expectation),
                std::move(observation),
                abort_status == 0
                    ? "source-artifact pacman transaction failed"
                    : "source-artifact pacman transaction and exact abort failed");
        }

        ExplicitProcessInvocation consume =
            helper_invocation("consume", transaction_token);
        consume.stdout_capture_limit =
            SOURCE_ARTIFACT_INSTALL_MAXIMUM_PROTOCOL_BYTES;
        CapturedCommandResult consume_result;
        try {
            consume_result = capture_explicit(consume);
        } catch(...) {
            const int abort_status =
                abort_prepared_state_noexcept(transaction_token);
            auto observation = make_observation(
                binding, std::move(input.observed_artifacts),
                make_transaction(
                    transaction_token,
                    std::move(input.requested_package_names),
                    InvocationDependencyTransactionCommandOutcome::Succeeded,
                    incomplete_observation(transaction_token)));
            return SourceArtifactInstallTrustedExecutionResult(
                abort_status == 0
                    ? SourceArtifactInstallTrustedExecutionStatus::
                          ConsumeFailed
                    : SourceArtifactInstallTrustedExecutionStatus::
                          AbortFailed,
                0, std::move(expectation), std::move(observation),
                abort_status == 0
                    ? "source-artifact receipt consume observation failed"
                    : "source-artifact receipt consume observation and exact abort failed");
        }
        if(consume_result.exit_code != 0) {
            const int abort_status =
                abort_prepared_state_noexcept(transaction_token);
            auto observation = make_observation(
                binding, std::move(input.observed_artifacts),
                make_transaction(
                    transaction_token,
                    std::move(input.requested_package_names),
                    InvocationDependencyTransactionCommandOutcome::Succeeded,
                    incomplete_observation(transaction_token)));
            return SourceArtifactInstallTrustedExecutionResult(
                abort_status == 0
                    ? SourceArtifactInstallTrustedExecutionStatus::
                          ConsumeFailed
                    : SourceArtifactInstallTrustedExecutionStatus::
                          AbortFailed,
                0, std::move(expectation), std::move(observation),
                abort_status == 0
                    ? "source-artifact receipt consume failed"
                    : "source-artifact receipt consume and exact abort failed");
        }
        if(consume_result.stdout_capture_limit_exceeded) {
            auto observation = make_observation(
                binding, std::move(input.observed_artifacts),
                make_transaction(
                    transaction_token,
                    std::move(input.requested_package_names),
                    InvocationDependencyTransactionCommandOutcome::Succeeded,
                    invalid_observation(transaction_token)));
            return SourceArtifactInstallTrustedExecutionResult(
                SourceArtifactInstallTrustedExecutionStatus::MalformedReceipt,
                0, std::move(expectation), std::move(observation),
                "source-artifact receipt exceeded its capture limit");
        }

        const SourceArtifactInstallRootReceiptResult parsed_receipt =
            parse_source_artifact_install_root_receipt(
                consume_result.output);
        const auto* receipt =
            std::get_if<SourceArtifactInstallRootReceipt>(&parsed_receipt);
        if(receipt == nullptr ||
           receipt->transaction_token != transaction_token) {
            auto observation = make_observation(
                binding, std::move(input.observed_artifacts),
                make_transaction(
                    transaction_token,
                    std::move(input.requested_package_names),
                    InvocationDependencyTransactionCommandOutcome::Succeeded,
                    invalid_observation(transaction_token)));
            return SourceArtifactInstallTrustedExecutionResult(
                SourceArtifactInstallTrustedExecutionStatus::MalformedReceipt,
                0, std::move(expectation), std::move(observation),
                "source-artifact receipt protocol was malformed or mismatched");
        }
        if(receipt->state ==
           SourceArtifactInstallRootReceiptState::Missing) {
            auto observation = make_observation(
                binding, std::move(input.observed_artifacts),
                make_transaction(
                    transaction_token,
                    std::move(input.requested_package_names),
                    InvocationDependencyTransactionCommandOutcome::Succeeded,
                    missing_observation()));
            return SourceArtifactInstallTrustedExecutionResult(
                SourceArtifactInstallTrustedExecutionStatus::Missing, 0,
                std::move(expectation), std::move(observation),
                std::nullopt);
        }

        std::vector<PacmanTransactionPackageObservation> operations;
        operations.reserve(receipt->installed_package_names.size());
        for(const std::string& package_name :
            receipt->installed_package_names) {
            operations.push_back(PacmanTransactionPackageObservation{
                PacmanTransactionPackageOperation::Install,
                package_name});
        }
        auto observation = make_observation(
            binding, std::move(input.observed_artifacts),
            make_transaction(
                transaction_token,
                std::move(input.requested_package_names),
                InvocationDependencyTransactionCommandOutcome::Succeeded,
                PacmanTransactionReceiptObservation{
                    PacmanTransactionReceiptObservationState::Complete,
                    transaction_token,
                    InvocationDependencyTransactionOwner::
                        SourceArtifactInstall,
                    std::move(operations)}));
        return SourceArtifactInstallTrustedExecutionResult(
            SourceArtifactInstallTrustedExecutionStatus::Complete, 0,
            std::move(expectation), std::move(observation), std::nullopt);
    }
};

SourceArtifactInstallTrustedExecutionResult::
    SourceArtifactInstallTrustedExecutionResult(
        SourceArtifactInstallTrustedExecutionStatus status,
        std::optional<int> pacman_exit_status,
        std::optional<SourceArtifactInstallReceiptExpectation> expectation,
        std::optional<SourceArtifactInstallReceiptObservation> observation,
        std::optional<std::string> diagnostic) noexcept
    : status_(status), pacman_exit_status_(pacman_exit_status),
      expectation_(std::move(expectation)),
      observation_(std::move(observation)),
      diagnostic_(std::move(diagnostic)) {
}

SourceArtifactInstallTrustedExecutionStatus
SourceArtifactInstallTrustedExecutionResult::status() const noexcept {
    return status_;
}

const std::optional<int>&
SourceArtifactInstallTrustedExecutionResult::pacman_exit_status()
    const noexcept {
    return pacman_exit_status_;
}

const std::optional<SourceArtifactInstallReceiptExpectation>&
SourceArtifactInstallTrustedExecutionResult::expectation() const noexcept {
    return expectation_;
}

const std::optional<SourceArtifactInstallReceiptObservation>&
SourceArtifactInstallTrustedExecutionResult::observation() const noexcept {
    return observation_;
}

const std::optional<std::string>&
SourceArtifactInstallTrustedExecutionResult::diagnostic() const noexcept {
    return diagnostic_;
}

SourceArtifactInstallTrustedExecutionResult
execute_source_artifact_install_trusted_transaction(
    PreparedPackageBaseArtifactInstall& install,
    const SourceArtifactInstallTrustedBinding& binding,
    const ArtifactInstallExecutionOptions& options) {
    if(!SourceArtifactInstallTrustedTransport::request_is_valid(
           install, binding)) {
        return SourceArtifactInstallTrustedTransport::invalid_result(
            SourceArtifactInstallTrustedExecutionStatus::InvalidRequest,
            "source-artifact trusted request is invalid");
    }
    if(!fixed_executables_are_trusted()) {
        return SourceArtifactInstallTrustedTransport::invalid_result(
            SourceArtifactInstallTrustedExecutionStatus::
                TrustedExecutableUnavailable,
            "installed source-artifact trusted executables are unavailable");
    }
    const std::optional<std::string> transaction_token =
        generate_trusted_alpm_receipt_transaction_token();
    if(!transaction_token.has_value()) {
        return SourceArtifactInstallTrustedTransport::invalid_result(
            SourceArtifactInstallTrustedExecutionStatus::
                TokenGenerationFailed,
            "cryptographic source-artifact transaction token generation failed");
    }
    try {
        return SourceArtifactInstallTrustedTransport::execute(
            install, binding, options, *transaction_token);
    } catch(...) {
        if(!SourceArtifactInstallTrustedTransport::capability_is_active(
               install)) {
            return SourceArtifactInstallTrustedTransport::
                unknown_after_consumption(
                    binding, *transaction_token);
        }
        return SourceArtifactInstallTrustedTransport::invalid_result(
            SourceArtifactInstallTrustedExecutionStatus::
                ArtifactSnapshotFailed,
            "source-artifact immutable snapshot failed before root preparation");
    }
}

#ifdef MOGUET_ENABLE_SOURCE_ARTIFACT_INSTALL_TRUSTED_TRANSPORT_TEST_HOOKS
SourceArtifactInstallTrustedExecutionResult
execute_source_artifact_install_trusted_transaction_for_test(
    PreparedPackageBaseArtifactInstall& install,
    const SourceArtifactInstallTrustedBinding& binding,
    const ArtifactInstallExecutionOptions& options,
    const std::string& transaction_token) {
    if(!is_valid_trusted_alpm_receipt_token(transaction_token) ||
       !SourceArtifactInstallTrustedTransport::request_is_valid(
           install, binding)) {
        return SourceArtifactInstallTrustedTransport::invalid_result(
            SourceArtifactInstallTrustedExecutionStatus::InvalidRequest,
            "source-artifact trusted test token is invalid");
    }
    try {
        return SourceArtifactInstallTrustedTransport::execute(
            install, binding, options, transaction_token);
    } catch(...) {
        if(!SourceArtifactInstallTrustedTransport::capability_is_active(
               install)) {
            return SourceArtifactInstallTrustedTransport::
                unknown_after_consumption(binding, transaction_token);
        }
        return SourceArtifactInstallTrustedTransport::invalid_result(
            SourceArtifactInstallTrustedExecutionStatus::
                ArtifactSnapshotFailed,
            "source-artifact immutable snapshot failed before root preparation");
    }
}
#endif
