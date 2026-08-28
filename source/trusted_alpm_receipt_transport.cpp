#include "trusted_alpm_receipt_transport.hpp"

#include "logging.hpp"
#include "package_identifier.hpp"
#include "process.hpp"
#include "shell_words.hpp"
#include "trusted_alpm_receipt_protocol.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <exception>
#include <fcntl.h>
#include <set>
#include <string_view>
#include <sys/random.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>
#include <variant>

#ifndef MOGUET_ALPM_RECEIPT_HELPER_PATH
#error "MOGUET_ALPM_RECEIPT_HELPER_PATH is required"
#endif

namespace {

constexpr std::string_view SUDO_PATH = "/usr/bin/sudo";
constexpr std::string_view PACMAN_PATH = "/usr/bin/pacman";
constexpr std::size_t PREPARE_RESPONSE_LIMIT = 4096;

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

bool same_identity(const struct stat& lhs, const struct stat& rhs) noexcept {
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
        const std::size_t component_end = is_final
                                              ? executable_path.size()
                                              : separator;
        const std::string component(
            executable_path.substr(
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
               !same_identity(descriptor_metadata, named_metadata) ||
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
           !same_identity(descriptor_metadata, named_metadata) ||
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
               MOGUET_ALPM_RECEIPT_HELPER_PATH, true);
}

bool is_valid_repository_name(std::string_view repository_name) noexcept {
    if(repository_name.empty() || repository_name == "." ||
       repository_name == ".." || repository_name.front() == '-') {
        return false;
    }
    return std::all_of(
        repository_name.begin(), repository_name.end(),
        [](unsigned char character) {
            return (character >= 'a' && character <= 'z') ||
                   (character >= 'A' && character <= 'Z') ||
                   (character >= '0' && character <= '9') ||
                   character == '.' || character == '_' ||
                   character == '+' || character == '-';
        });
}

bool request_is_valid(
    const TrustedAlpmReceiptSelectedProviderRequest& request) {
    if(request.targets.empty()) return false;
    switch(request.install_directive) {
        case TrustedAlpmReceiptRepositoryInstallDirective::
            PreserveExistingReason:
        case TrustedAlpmReceiptRepositoryInstallDirective::AsDependency:
            break;
        default:
            return false;
    }

    std::set<std::string> unique_packages;
    for(const TrustedAlpmReceiptRepositoryTarget& target : request.targets) {
        if(!is_valid_repository_name(target.repository_name) ||
           !is_valid_trusted_alpm_receipt_package_name(
               target.package_name) ||
           !is_valid_package_name(target.package_name) ||
           !unique_packages.insert(target.package_name).second) {
            return false;
        }
    }
    return true;
}

std::vector<std::string> requested_package_names(
    const TrustedAlpmReceiptSelectedProviderRequest& request) {
    std::vector<std::string> names;
    names.reserve(request.targets.size());
    for(const TrustedAlpmReceiptRepositoryTarget& target : request.targets) {
        names.push_back(target.package_name);
    }
    return names;
}

std::vector<std::string> minimal_root_command_environment() {
    // No caller-controlled environment or PATH lookup crosses this boundary.
    return {"PATH=/usr/bin", "LC_ALL=C"};
}

void log_explicit_invocation(const ExplicitProcessInvocation& invocation) {
    std::vector<std::string> display;
    display.reserve(invocation.arguments.size() + 1);
    display.push_back(invocation.executable);
    display.insert(
        display.end(), invocation.arguments.begin(),
        invocation.arguments.end());
    // This quoting is presentation only. Execution goes directly to execve.
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

ExplicitProcessInvocation helper_invocation(
    const std::string& command, const std::string& transaction_token,
    const std::vector<std::string>& trailing_arguments = {}) {
    ExplicitProcessInvocation invocation;
    invocation.executable = std::string(SUDO_PATH);
    invocation.arguments = {
        "--", MOGUET_ALPM_RECEIPT_HELPER_PATH, command,
        transaction_token,
        std::string(
            trusted_alpm_receipt_selected_repository_provider_owner())};
    invocation.arguments.insert(
        invocation.arguments.end(), trailing_arguments.begin(),
        trailing_arguments.end());
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
            // Cleanup must still be attempted when diagnostic logging fails.
            return run_explicit_process(abort);
        }
    } catch(...) {
        return 127;
    }
}

ExplicitProcessInvocation pacman_invocation(
    const TrustedAlpmReceiptSelectedProviderRequest& request,
    const std::string& hook_directory) {
    ExplicitProcessInvocation invocation;
    invocation.executable = std::string(SUDO_PATH);
    invocation.arguments = {"--", std::string(PACMAN_PATH), "-S"};
    if(request.install_directive ==
       TrustedAlpmReceiptRepositoryInstallDirective::AsDependency) {
        invocation.arguments.push_back("--asdeps");
    }
    invocation.arguments.push_back("--needed");
    if(request.no_confirm) invocation.arguments.push_back("--noconfirm");
    invocation.arguments.push_back("--hookdir");
    invocation.arguments.push_back(hook_directory);
    invocation.arguments.push_back("--");
    for(const TrustedAlpmReceiptRepositoryTarget& target : request.targets) {
        invocation.arguments.push_back(
            target.repository_name + "/" + target.package_name);
    }
    invocation.environment = minimal_root_command_environment();
    return invocation;
}

InvocationDependencyTransaction make_transaction(
    const std::string& transaction_token,
    std::vector<std::string> requested_packages,
    InvocationDependencyTransactionCommandOutcome command_outcome,
    PacmanTransactionReceiptObservation observation) {
    const auto owner = InvocationDependencyTransactionOwner::
        SelectedRepositoryProvider;
    PacmanTransactionReceipt receipt = validate_pacman_transaction_receipt(
        transaction_token, owner, observation);
    return InvocationDependencyTransaction{
        transaction_token, owner, std::move(requested_packages),
        command_outcome, std::move(receipt)};
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
        InvocationDependencyTransactionOwner::
            SelectedRepositoryProvider,
        {}};
}

TrustedAlpmReceiptCaptureResult result_with_transaction(
    TrustedAlpmReceiptCaptureStatus status,
    std::optional<int> pacman_exit_status,
    InvocationDependencyTransaction transaction,
    std::optional<std::string> diagnostic = std::nullopt) {
    InvocationDependencyTransactionLedger ledger;
    ledger.transactions.push_back(std::move(transaction));
    return TrustedAlpmReceiptCaptureResult{
        status, pacman_exit_status, std::move(ledger),
        std::move(diagnostic)};
}

TrustedAlpmReceiptCaptureResult execute_with_token(
    const TrustedAlpmReceiptSelectedProviderRequest& request,
    const std::string& transaction_token) {
    std::vector<std::string> package_names = requested_package_names(request);

    std::vector<std::string> prepare_trailing{"--"};
    prepare_trailing.insert(
        prepare_trailing.end(), package_names.begin(),
        package_names.end());
    ExplicitProcessInvocation prepare = helper_invocation(
        "prepare", transaction_token, prepare_trailing);
    prepare.stdout_capture_limit = PREPARE_RESPONSE_LIMIT;
    CapturedCommandResult prepare_result;
    try {
        prepare_result = capture_explicit(prepare);
    } catch(const std::exception&) {
        const int cleanup_status =
            abort_prepared_state_noexcept(transaction_token);
        return result_with_transaction(
            cleanup_status == 0
                ? TrustedAlpmReceiptCaptureStatus::PrepareFailed
                : TrustedAlpmReceiptCaptureStatus::AbortFailed,
            std::nullopt,
            make_transaction(
                transaction_token, std::move(package_names),
                InvocationDependencyTransactionCommandOutcome::
                    NotAttempted,
                missing_observation()),
            cleanup_status == 0
                ? "trusted ALPM receipt preparation observation failed"
                : "trusted ALPM receipt preparation observation and exact abort failed");
    }
    const TrustedAlpmReceiptPrepareResponseResult prepare_response =
        prepare_result.exit_code == 0 &&
                !prepare_result.stdout_capture_limit_exceeded
            ? parse_trusted_alpm_receipt_prepare_response(
                  prepare_result.output, transaction_token)
            : TrustedAlpmReceiptPrepareResponseResult{
                  TrustedAlpmReceiptProtocolFailure{
                      TrustedAlpmReceiptProtocolIssueKind::
                          TruncatedProtocol}};
    const auto* prepared =
        std::get_if<TrustedAlpmReceiptPrepareResponse>(&prepare_response);
    if(prepared == nullptr) {
        int cleanup_status = 0;
        if(prepare_result.exit_code == 0) {
            cleanup_status =
                abort_prepared_state_noexcept(transaction_token);
        }
        return result_with_transaction(
            cleanup_status == 0
                ? TrustedAlpmReceiptCaptureStatus::PrepareFailed
                : TrustedAlpmReceiptCaptureStatus::AbortFailed,
            std::nullopt,
            make_transaction(
                transaction_token, std::move(package_names),
                InvocationDependencyTransactionCommandOutcome::
                    NotAttempted,
                missing_observation()),
            cleanup_status == 0
                ? "trusted ALPM receipt preparation failed"
                : "trusted ALPM receipt preparation and exact abort failed");
    }

    int pacman_status;
    try {
        pacman_status = run_explicit(
            pacman_invocation(request, prepared->hook_directory));
    } catch(const std::exception&) {
        const int abort_status =
            abort_prepared_state_noexcept(transaction_token);
        return result_with_transaction(
            abort_status == 0
                ? TrustedAlpmReceiptCaptureStatus::PrepareFailed
                : TrustedAlpmReceiptCaptureStatus::AbortFailed,
            std::nullopt,
            make_transaction(
                transaction_token, std::move(package_names),
                InvocationDependencyTransactionCommandOutcome::
                    NotAttempted,
                missing_observation()),
            abort_status == 0
                ? "selected-provider pacman invocation failed before execution"
                : "selected-provider pacman invocation and exact receipt abort failed");
    }
    if(pacman_status != 0) {
        const int abort_status =
            abort_prepared_state_noexcept(transaction_token);
        return result_with_transaction(
            abort_status == 0
                ? TrustedAlpmReceiptCaptureStatus::PacmanFailed
                : TrustedAlpmReceiptCaptureStatus::AbortFailed,
            pacman_status,
            make_transaction(
                transaction_token, std::move(package_names),
                InvocationDependencyTransactionCommandOutcome::Failed,
                missing_observation()),
            abort_status == 0
                ? "selected-provider pacman transaction failed"
                : "selected-provider pacman transaction and exact receipt abort failed");
    }

    ExplicitProcessInvocation consume =
        helper_invocation("consume", transaction_token);
    consume.stdout_capture_limit = TRUSTED_ALPM_RECEIPT_MAXIMUM_BYTES;
    CapturedCommandResult consume_result;
    try {
        consume_result = capture_explicit(consume);
    } catch(const std::exception&) {
        const int abort_status =
            abort_prepared_state_noexcept(transaction_token);
        return result_with_transaction(
            abort_status == 0
                ? TrustedAlpmReceiptCaptureStatus::ConsumeFailed
                : TrustedAlpmReceiptCaptureStatus::AbortFailed,
            0,
            make_transaction(
                transaction_token, std::move(package_names),
                InvocationDependencyTransactionCommandOutcome::
                    Succeeded,
                incomplete_observation(transaction_token)),
            abort_status == 0
                ? "trusted ALPM receipt consume observation failed"
                : "trusted ALPM receipt consume observation and exact abort failed");
    }
    if(consume_result.exit_code != 0) {
        const int abort_status =
            abort_prepared_state_noexcept(transaction_token);
        return result_with_transaction(
            abort_status == 0
                ? TrustedAlpmReceiptCaptureStatus::ConsumeFailed
                : TrustedAlpmReceiptCaptureStatus::AbortFailed,
            0,
            make_transaction(
                transaction_token, std::move(package_names),
                InvocationDependencyTransactionCommandOutcome::
                    Succeeded,
                incomplete_observation(transaction_token)),
            abort_status == 0
                ? "trusted ALPM receipt consume failed"
                : "trusted ALPM receipt consume and exact abort failed");
    }
    if(consume_result.stdout_capture_limit_exceeded) {
        return result_with_transaction(
            TrustedAlpmReceiptCaptureStatus::MalformedReceipt, 0,
            make_transaction(
                transaction_token, std::move(package_names),
                InvocationDependencyTransactionCommandOutcome::
                    Succeeded,
                incomplete_observation(transaction_token)),
            "trusted ALPM receipt exceeded its capture limit");
    }

    const TrustedAlpmReceiptMachineReceiptResult parsed_receipt =
        parse_trusted_alpm_receipt_machine_receipt(
            consume_result.output);
    const auto* receipt =
        std::get_if<TrustedAlpmReceiptMachineReceipt>(&parsed_receipt);
    if(receipt == nullptr ||
       receipt->transaction_token != transaction_token) {
        return result_with_transaction(
            TrustedAlpmReceiptCaptureStatus::MalformedReceipt, 0,
            make_transaction(
                transaction_token, std::move(package_names),
                InvocationDependencyTransactionCommandOutcome::
                    Succeeded,
                incomplete_observation(transaction_token)),
            "trusted ALPM receipt machine protocol was malformed or mismatched");
    }

    if(receipt->state == TrustedAlpmReceiptMachineState::Missing) {
        return result_with_transaction(
            TrustedAlpmReceiptCaptureStatus::Missing, 0,
            make_transaction(
                transaction_token, std::move(package_names),
                InvocationDependencyTransactionCommandOutcome::
                    Succeeded,
                missing_observation()));
    }

    std::vector<PacmanTransactionPackageObservation> observations;
    observations.reserve(receipt->installed_package_names.size());
    for(const std::string& package_name :
        receipt->installed_package_names) {
        observations.push_back(PacmanTransactionPackageObservation{
            PacmanTransactionPackageOperation::Install, package_name});
    }
    const auto owner = InvocationDependencyTransactionOwner::
        SelectedRepositoryProvider;
    return result_with_transaction(
        TrustedAlpmReceiptCaptureStatus::Complete, 0,
        make_transaction(
            transaction_token, std::move(package_names),
            InvocationDependencyTransactionCommandOutcome::Succeeded,
            PacmanTransactionReceiptObservation{
                PacmanTransactionReceiptObservationState::Complete,
                transaction_token, owner,
                std::move(observations)}));
}

} // namespace

std::optional<std::string>
generate_trusted_alpm_receipt_transaction_token() noexcept {
    std::array<unsigned char, 32> random_bytes{};
    std::size_t offset = 0;
    while(offset < random_bytes.size()) {
        const ssize_t count = getrandom(
            random_bytes.data() + offset,
            random_bytes.size() - offset, 0);
        if(count > 0) {
            offset += static_cast<std::size_t>(count);
            continue;
        }
        if(count == -1 && errno == EINTR) continue;
        return std::nullopt;
    }

    try {
        constexpr char HEX_DIGITS[] = "0123456789abcdef";
        std::string token;
        token.resize(TRUSTED_ALPM_RECEIPT_TOKEN_HEX_LENGTH);
        for(std::size_t index = 0; index < random_bytes.size(); ++index) {
            token[index * 2] = HEX_DIGITS[random_bytes[index] >> 4U];
            token[index * 2 + 1] =
                HEX_DIGITS[random_bytes[index] & 0x0fU];
        }
        return token;
    } catch(...) {
        return std::nullopt;
    }
}

TrustedAlpmReceiptCaptureResult
execute_trusted_alpm_receipt_selected_provider_transaction(
    const TrustedAlpmReceiptSelectedProviderRequest& request) {
    if(!request_is_valid(request)) {
        return TrustedAlpmReceiptCaptureResult{
            TrustedAlpmReceiptCaptureStatus::InvalidRequest,
            std::nullopt,
            {},
            "trusted ALPM receipt request is invalid"};
    }
    if(!fixed_executables_are_trusted()) {
        return TrustedAlpmReceiptCaptureResult{
            TrustedAlpmReceiptCaptureStatus::
                TrustedExecutableUnavailable,
            std::nullopt,
            {},
            "installed trusted ALPM receipt executables are unavailable"};
    }
    const std::optional<std::string> transaction_token =
        generate_trusted_alpm_receipt_transaction_token();
    if(!transaction_token.has_value()) {
        return TrustedAlpmReceiptCaptureResult{
            TrustedAlpmReceiptCaptureStatus::TokenGenerationFailed,
            std::nullopt,
            {},
            "cryptographic transaction token generation failed"};
    }
    return execute_with_token(request, transaction_token.value());
}

#ifdef MOGUET_ENABLE_TRUSTED_ALPM_RECEIPT_TEST_HOOKS
TrustedAlpmReceiptCaptureResult
execute_trusted_alpm_receipt_selected_provider_transaction_for_test(
    const TrustedAlpmReceiptSelectedProviderRequest& request,
    const std::string& transaction_token) {
    if(!request_is_valid(request) ||
       !is_valid_trusted_alpm_receipt_token(transaction_token)) {
        return TrustedAlpmReceiptCaptureResult{
            TrustedAlpmReceiptCaptureStatus::InvalidRequest,
            std::nullopt,
            {},
            "trusted ALPM receipt request is invalid"};
    }
    return execute_with_token(request, transaction_token);
}
#endif
