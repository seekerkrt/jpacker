#include "trusted_alpm_receipt_protocol.hpp"

#include <algorithm>
#include <optional>
#include <set>
#include <stdexcept>
#include <utility>

namespace {

constexpr std::string_view OWNER = "selected-repository-provider";
constexpr std::string_view PREPARED_HEADER = "MOGUET-ALPM-PREPARED\t1";
constexpr std::string_view PREPARE_RESPONSE_HEADER =
        "MOGUET-ALPM-PREPARE-RESPONSE\t1";
constexpr std::string_view RECEIPT_HEADER = "MOGUET-ALPM-RECEIPT\t1";
constexpr std::string_view TOKEN_PREFIX = "TOKEN\t";
constexpr std::string_view OWNER_PREFIX = "OWNER\t";
constexpr std::string_view REQUEST_PREFIX = "REQUEST\t";
constexpr std::string_view HOOK_DIRECTORY_PREFIX = "HOOKDIR\t";
constexpr std::string_view STATE_PREFIX = "STATE\t";
constexpr std::string_view INSTALL_PREFIX = "INSTALL\t";
constexpr std::string_view END_RECORD = "END";

template<typename Success>
std::variant<Success, TrustedAlpmReceiptProtocolFailure> fail(
        TrustedAlpmReceiptProtocolIssueKind issue) {
    return TrustedAlpmReceiptProtocolFailure{issue};
}

bool is_ascii_alphanumeric(unsigned char character) noexcept {
    return (character >= 'a' && character <= 'z') ||
           (character >= 'A' && character <= 'Z') ||
           (character >= '0' && character <= '9');
}

std::variant<std::vector<std::string_view>, TrustedAlpmReceiptProtocolFailure>
split_protocol_lines(std::string_view protocol) {
    if(protocol.size() > TRUSTED_ALPM_RECEIPT_MAXIMUM_BYTES) {
        return TrustedAlpmReceiptProtocolFailure{
                TrustedAlpmReceiptProtocolIssueKind::InputTooLarge};
    }
    if(protocol.empty() || protocol.back() != '\n') {
        return TrustedAlpmReceiptProtocolFailure{
                TrustedAlpmReceiptProtocolIssueKind::MissingFinalNewline};
    }

    std::vector<std::string_view> lines;
    std::size_t                   offset = 0;
    while(offset < protocol.size()) {
        const std::size_t newline = protocol.find('\n', offset);
        if(newline == std::string_view::npos) {
            return TrustedAlpmReceiptProtocolFailure{
                    TrustedAlpmReceiptProtocolIssueKind::TruncatedProtocol};
        }
        lines.push_back(protocol.substr(offset, newline - offset));
        if(lines.size() > TRUSTED_ALPM_RECEIPT_MAXIMUM_PACKAGES + 8) {
            return TrustedAlpmReceiptProtocolFailure{
                    TrustedAlpmReceiptProtocolIssueKind::TooManyPackages};
        }
        offset = newline + 1;
    }
    return lines;
}

std::optional<std::string_view> record_value(
        std::string_view record, std::string_view prefix) noexcept {
    if(!record.starts_with(prefix)) return std::nullopt;
    return record.substr(prefix.size());
}

bool retain_unique_package(
        std::string_view package_name,
        std::set<std::string>& unique_packages,
        std::vector<std::string>& packages,
        TrustedAlpmReceiptProtocolIssueKind& issue) {
    if(package_name.size() >
               TRUSTED_ALPM_RECEIPT_MAXIMUM_PACKAGE_NAME_BYTES ||
       !is_valid_trusted_alpm_receipt_package_name(package_name)) {
        issue = TrustedAlpmReceiptProtocolIssueKind::InvalidPackageName;
        return false;
    }
    if(packages.size() >= TRUSTED_ALPM_RECEIPT_MAXIMUM_PACKAGES) {
        issue = TrustedAlpmReceiptProtocolIssueKind::TooManyPackages;
        return false;
    }
    std::string owned_name(package_name);
    if(!unique_packages.insert(owned_name).second) {
        issue = TrustedAlpmReceiptProtocolIssueKind::DuplicatePackageName;
        return false;
    }
    packages.push_back(std::move(owned_name));
    return true;
}

void require_serializable_token(std::string_view transaction_token) {
    if(!is_valid_trusted_alpm_receipt_token(transaction_token)) {
        throw std::invalid_argument("invalid trusted ALPM receipt token");
    }
}

void require_serializable_packages(
        const std::vector<std::string>& packages, bool allow_empty) {
    if(!allow_empty && packages.empty()) {
        throw std::invalid_argument(
                "empty trusted ALPM receipt package set");
    }
    if(packages.size() > TRUSTED_ALPM_RECEIPT_MAXIMUM_PACKAGES) {
        throw std::invalid_argument(
                "oversized trusted ALPM receipt package set");
    }
    std::set<std::string> unique_packages;
    for(const std::string& package_name : packages) {
        if(package_name.size() >
                   TRUSTED_ALPM_RECEIPT_MAXIMUM_PACKAGE_NAME_BYTES ||
           !is_valid_trusted_alpm_receipt_package_name(package_name) ||
           !unique_packages.insert(package_name).second) {
            throw std::invalid_argument(
                    "invalid trusted ALPM receipt package set");
        }
    }
}

} // namespace

std::string_view
trusted_alpm_receipt_selected_repository_provider_owner() noexcept {
    return OWNER;
}

bool is_valid_trusted_alpm_receipt_token(
        std::string_view transaction_token) noexcept {
    return transaction_token.size() ==
                   TRUSTED_ALPM_RECEIPT_TOKEN_HEX_LENGTH &&
           std::all_of(
                   transaction_token.begin(), transaction_token.end(),
                   [](unsigned char character) {
                       return (character >= '0' && character <= '9') ||
                              (character >= 'a' && character <= 'f');
                   });
}

bool is_valid_trusted_alpm_receipt_package_name(
        std::string_view package_name) noexcept {
    if(package_name.empty() || package_name == "." || package_name == ".." ||
       package_name.front() == '-') {
        return false;
    }
    return std::all_of(
            package_name.begin(), package_name.end(),
            [](unsigned char character) {
                return is_ascii_alphanumeric(character) || character == '@' ||
                       character == '.' || character == '_' ||
                       character == '+' || character == '-';
            });
}

TrustedAlpmReceiptHelperInvocationResult
parse_trusted_alpm_receipt_helper_arguments(
        const std::vector<std::string>& arguments) {
    if(arguments.empty()) {
        return fail<TrustedAlpmReceiptHelperInvocation>(
                TrustedAlpmReceiptProtocolIssueKind::InvalidArgumentCount);
    }

    TrustedAlpmReceiptHelperCommand command;
    if(arguments[0] == "prepare") {
        command = TrustedAlpmReceiptHelperCommand::Prepare;
    } else if(arguments[0] == "record") {
        command = TrustedAlpmReceiptHelperCommand::Record;
    } else if(arguments[0] == "consume") {
        command = TrustedAlpmReceiptHelperCommand::Consume;
    } else if(arguments[0] == "abort") {
        command = TrustedAlpmReceiptHelperCommand::Abort;
    } else {
        return fail<TrustedAlpmReceiptHelperInvocation>(
                TrustedAlpmReceiptProtocolIssueKind::InvalidCommand);
    }

    if(arguments.size() < 3) {
        return fail<TrustedAlpmReceiptHelperInvocation>(
                TrustedAlpmReceiptProtocolIssueKind::InvalidArgumentCount);
    }
    if(!is_valid_trusted_alpm_receipt_token(arguments[1])) {
        return fail<TrustedAlpmReceiptHelperInvocation>(
                TrustedAlpmReceiptProtocolIssueKind::
                        InvalidTransactionToken);
    }
    if(arguments[2] != OWNER) {
        return fail<TrustedAlpmReceiptHelperInvocation>(
                TrustedAlpmReceiptProtocolIssueKind::
                        InvalidTransactionOwner);
    }

    if(command != TrustedAlpmReceiptHelperCommand::Prepare) {
        if(arguments.size() != 3) {
            return fail<TrustedAlpmReceiptHelperInvocation>(
                    TrustedAlpmReceiptProtocolIssueKind::
                            InvalidArgumentCount);
        }
        return TrustedAlpmReceiptHelperInvocation{
                command, arguments[1], {}};
    }

    if(arguments.size() < 4) {
        return fail<TrustedAlpmReceiptHelperInvocation>(
                TrustedAlpmReceiptProtocolIssueKind::
                        EmptyRequestedPackageSet);
    }
    if(arguments[3] != "--") {
        return fail<TrustedAlpmReceiptHelperInvocation>(
                TrustedAlpmReceiptProtocolIssueKind::
                        MissingArgumentSeparator);
    }
    if(arguments.size() == 4) {
        return fail<TrustedAlpmReceiptHelperInvocation>(
                TrustedAlpmReceiptProtocolIssueKind::
                        EmptyRequestedPackageSet);
    }

    std::vector<std::string> packages;
    std::set<std::string>    unique_packages;
    for(std::size_t index = 4; index < arguments.size(); ++index) {
        TrustedAlpmReceiptProtocolIssueKind issue =
                TrustedAlpmReceiptProtocolIssueKind::InvalidPackageName;
        if(!retain_unique_package(
                   arguments[index], unique_packages, packages, issue)) {
            return fail<TrustedAlpmReceiptHelperInvocation>(issue);
        }
    }
    if(packages.empty()) {
        return fail<TrustedAlpmReceiptHelperInvocation>(
                TrustedAlpmReceiptProtocolIssueKind::
                        EmptyRequestedPackageSet);
    }
    return TrustedAlpmReceiptHelperInvocation{
            command, arguments[1], std::move(packages)};
}

TrustedAlpmReceiptNeedsTargetsResult parse_trusted_alpm_receipt_needs_targets(
        std::string_view input) {
    const auto split = split_protocol_lines(input);
    const auto* failure =
            std::get_if<TrustedAlpmReceiptProtocolFailure>(&split);
    if(failure != nullptr) return *failure;

    std::vector<std::string> packages;
    std::set<std::string>    unique_packages;
    for(const std::string_view line :
        std::get<std::vector<std::string_view>>(split)) {
        TrustedAlpmReceiptProtocolIssueKind issue =
                TrustedAlpmReceiptProtocolIssueKind::InvalidPackageName;
        if(!retain_unique_package(
                   line, unique_packages, packages, issue)) {
            return TrustedAlpmReceiptProtocolFailure{issue};
        }
    }
    if(packages.empty()) {
        return TrustedAlpmReceiptProtocolFailure{
                TrustedAlpmReceiptProtocolIssueKind::
                        EmptyRequestedPackageSet};
    }
    return packages;
}

std::string serialize_trusted_alpm_receipt_prepared_state(
        const TrustedAlpmReceiptPreparedState& state) {
    require_serializable_token(state.transaction_token);
    require_serializable_packages(state.requested_package_names, false);
    std::string protocol;
    protocol.reserve(160 + state.requested_package_names.size() * 32);
    protocol.append(PREPARED_HEADER).push_back('\n');
    protocol.append(TOKEN_PREFIX).append(state.transaction_token).push_back(
            '\n');
    protocol.append(OWNER_PREFIX).append(OWNER).push_back('\n');
    for(const std::string& package_name : state.requested_package_names) {
        protocol.append(REQUEST_PREFIX).append(package_name).push_back('\n');
    }
    protocol.append(END_RECORD).push_back('\n');
    if(protocol.size() > TRUSTED_ALPM_RECEIPT_MAXIMUM_BYTES) {
        throw std::invalid_argument(
                "oversized trusted ALPM prepared protocol");
    }
    return protocol;
}

TrustedAlpmReceiptPreparedStateResult parse_trusted_alpm_receipt_prepared_state(
        std::string_view protocol) {
    const auto split = split_protocol_lines(protocol);
    if(const auto* failure =
               std::get_if<TrustedAlpmReceiptProtocolFailure>(&split);
       failure != nullptr) {
        return *failure;
    }
    const auto& lines = std::get<std::vector<std::string_view>>(split);
    if(lines.size() < 5) {
        return fail<TrustedAlpmReceiptPreparedState>(
                TrustedAlpmReceiptProtocolIssueKind::TruncatedProtocol);
    }
    if(lines[0] != PREPARED_HEADER) {
        return fail<TrustedAlpmReceiptPreparedState>(
                TrustedAlpmReceiptProtocolIssueKind::InvalidHeader);
    }
    const auto token = record_value(lines[1], TOKEN_PREFIX);
    if(!token.has_value() ||
       !is_valid_trusted_alpm_receipt_token(*token)) {
        return fail<TrustedAlpmReceiptPreparedState>(
                TrustedAlpmReceiptProtocolIssueKind::
                        InvalidTransactionToken);
    }
    const auto owner = record_value(lines[2], OWNER_PREFIX);
    if(!owner.has_value() || *owner != OWNER) {
        return fail<TrustedAlpmReceiptPreparedState>(
                TrustedAlpmReceiptProtocolIssueKind::
                        InvalidTransactionOwner);
    }
    if(lines.back() != END_RECORD) {
        return fail<TrustedAlpmReceiptPreparedState>(
                TrustedAlpmReceiptProtocolIssueKind::TruncatedProtocol);
    }

    std::vector<std::string> packages;
    std::set<std::string>    unique_packages;
    for(std::size_t index = 3; index + 1 < lines.size(); ++index) {
        const auto package = record_value(lines[index], REQUEST_PREFIX);
        if(!package.has_value()) {
            return fail<TrustedAlpmReceiptPreparedState>(
                    TrustedAlpmReceiptProtocolIssueKind::UnexpectedRecord);
        }
        TrustedAlpmReceiptProtocolIssueKind issue =
                TrustedAlpmReceiptProtocolIssueKind::InvalidPackageName;
        if(!retain_unique_package(
                   *package, unique_packages, packages, issue)) {
            return fail<TrustedAlpmReceiptPreparedState>(issue);
        }
    }
    if(packages.empty()) {
        return fail<TrustedAlpmReceiptPreparedState>(
                TrustedAlpmReceiptProtocolIssueKind::
                        EmptyRequestedPackageSet);
    }
    return TrustedAlpmReceiptPreparedState{
            std::string(*token), std::move(packages)};
}

std::string trusted_alpm_receipt_hook_directory(
        std::string_view transaction_token) {
    require_serializable_token(transaction_token);
    return "/run/moguet/alpm-receipts/active/" +
           std::string(transaction_token) + "/hooks";
}

std::string trusted_alpm_receipt_hook_filename(
        std::string_view transaction_token) {
    require_serializable_token(transaction_token);
    return "moguet-install-" + std::string(transaction_token) + ".hook";
}

std::string serialize_trusted_alpm_receipt_prepare_response(
        const TrustedAlpmReceiptPrepareResponse& response) {
    require_serializable_token(response.transaction_token);
    if(response.hook_directory !=
       trusted_alpm_receipt_hook_directory(response.transaction_token)) {
        throw std::invalid_argument(
                "invalid trusted ALPM hook directory");
    }
    std::string protocol;
    protocol.append(PREPARE_RESPONSE_HEADER).push_back('\n');
    protocol.append(TOKEN_PREFIX)
            .append(response.transaction_token)
            .push_back('\n');
    protocol.append(OWNER_PREFIX).append(OWNER).push_back('\n');
    protocol.append(HOOK_DIRECTORY_PREFIX)
            .append(response.hook_directory)
            .push_back('\n');
    protocol.append(END_RECORD).push_back('\n');
    return protocol;
}

TrustedAlpmReceiptPrepareResponseResult
parse_trusted_alpm_receipt_prepare_response(
        std::string_view protocol,
        std::string_view expected_transaction_token) {
    if(!is_valid_trusted_alpm_receipt_token(expected_transaction_token)) {
        return fail<TrustedAlpmReceiptPrepareResponse>(
                TrustedAlpmReceiptProtocolIssueKind::
                        InvalidTransactionToken);
    }
    const auto split = split_protocol_lines(protocol);
    if(const auto* failure =
               std::get_if<TrustedAlpmReceiptProtocolFailure>(&split);
       failure != nullptr) {
        return *failure;
    }
    const auto& lines = std::get<std::vector<std::string_view>>(split);
    if(lines.size() != 5) {
        return fail<TrustedAlpmReceiptPrepareResponse>(
                lines.size() < 5
                        ? TrustedAlpmReceiptProtocolIssueKind::
                                  TruncatedProtocol
                        : TrustedAlpmReceiptProtocolIssueKind::
                                  UnexpectedRecord);
    }
    if(lines[0] != PREPARE_RESPONSE_HEADER) {
        return fail<TrustedAlpmReceiptPrepareResponse>(
                TrustedAlpmReceiptProtocolIssueKind::InvalidHeader);
    }
    const auto token = record_value(lines[1], TOKEN_PREFIX);
    if(!token.has_value() || *token != expected_transaction_token) {
        return fail<TrustedAlpmReceiptPrepareResponse>(
                TrustedAlpmReceiptProtocolIssueKind::
                        InvalidTransactionToken);
    }
    const auto owner = record_value(lines[2], OWNER_PREFIX);
    if(!owner.has_value() || *owner != OWNER) {
        return fail<TrustedAlpmReceiptPrepareResponse>(
                TrustedAlpmReceiptProtocolIssueKind::
                        InvalidTransactionOwner);
    }
    const auto hook_directory =
            record_value(lines[3], HOOK_DIRECTORY_PREFIX);
    const std::string expected_hook_directory =
            trusted_alpm_receipt_hook_directory(expected_transaction_token);
    if(!hook_directory.has_value() ||
       *hook_directory != expected_hook_directory) {
        return fail<TrustedAlpmReceiptPrepareResponse>(
                TrustedAlpmReceiptProtocolIssueKind::InvalidHookDirectory);
    }
    if(lines[4] != END_RECORD) {
        return fail<TrustedAlpmReceiptPrepareResponse>(
                TrustedAlpmReceiptProtocolIssueKind::TruncatedProtocol);
    }
    return TrustedAlpmReceiptPrepareResponse{
            std::string(*token), std::string(*hook_directory)};
}

std::string serialize_trusted_alpm_receipt_machine_receipt(
        const TrustedAlpmReceiptMachineReceipt& receipt) {
    require_serializable_token(receipt.transaction_token);
    require_serializable_packages(
            receipt.installed_package_names,
            receipt.state == TrustedAlpmReceiptMachineState::Missing);
    if(receipt.state == TrustedAlpmReceiptMachineState::Missing &&
       !receipt.installed_package_names.empty()) {
        throw std::invalid_argument(
                "missing trusted ALPM receipt contains packages");
    }
    std::string protocol;
    protocol.reserve(160 + receipt.installed_package_names.size() * 32);
    protocol.append(RECEIPT_HEADER).push_back('\n');
    protocol.append(TOKEN_PREFIX).append(receipt.transaction_token).push_back(
            '\n');
    protocol.append(OWNER_PREFIX).append(OWNER).push_back('\n');
    protocol.append(STATE_PREFIX)
            .append(receipt.state == TrustedAlpmReceiptMachineState::Complete
                            ? "Complete"
                            : "Missing")
            .push_back('\n');
    for(const std::string& package_name : receipt.installed_package_names) {
        protocol.append(INSTALL_PREFIX).append(package_name).push_back('\n');
    }
    protocol.append(END_RECORD).push_back('\n');
    if(protocol.size() > TRUSTED_ALPM_RECEIPT_MAXIMUM_BYTES) {
        throw std::invalid_argument(
                "oversized trusted ALPM receipt protocol");
    }
    return protocol;
}

TrustedAlpmReceiptMachineReceiptResult
parse_trusted_alpm_receipt_machine_receipt(std::string_view protocol) {
    const auto split = split_protocol_lines(protocol);
    if(const auto* failure =
               std::get_if<TrustedAlpmReceiptProtocolFailure>(&split);
       failure != nullptr) {
        return *failure;
    }
    const auto& lines = std::get<std::vector<std::string_view>>(split);
    if(lines.size() < 5) {
        return fail<TrustedAlpmReceiptMachineReceipt>(
                TrustedAlpmReceiptProtocolIssueKind::TruncatedProtocol);
    }
    if(lines[0] != RECEIPT_HEADER) {
        return fail<TrustedAlpmReceiptMachineReceipt>(
                TrustedAlpmReceiptProtocolIssueKind::InvalidHeader);
    }
    const auto token = record_value(lines[1], TOKEN_PREFIX);
    if(!token.has_value() ||
       !is_valid_trusted_alpm_receipt_token(*token)) {
        return fail<TrustedAlpmReceiptMachineReceipt>(
                TrustedAlpmReceiptProtocolIssueKind::
                        InvalidTransactionToken);
    }
    const auto owner = record_value(lines[2], OWNER_PREFIX);
    if(!owner.has_value() || *owner != OWNER) {
        return fail<TrustedAlpmReceiptMachineReceipt>(
                TrustedAlpmReceiptProtocolIssueKind::
                        InvalidTransactionOwner);
    }
    const auto state_record = record_value(lines[3], STATE_PREFIX);
    if(!state_record.has_value()) {
        return fail<TrustedAlpmReceiptMachineReceipt>(
                TrustedAlpmReceiptProtocolIssueKind::InvalidReceiptState);
    }
    TrustedAlpmReceiptMachineState state;
    if(*state_record == "Complete") {
        state = TrustedAlpmReceiptMachineState::Complete;
    } else if(*state_record == "Missing") {
        state = TrustedAlpmReceiptMachineState::Missing;
    } else {
        return fail<TrustedAlpmReceiptMachineReceipt>(
                TrustedAlpmReceiptProtocolIssueKind::InvalidReceiptState);
    }
    if(lines.back() != END_RECORD) {
        return fail<TrustedAlpmReceiptMachineReceipt>(
                TrustedAlpmReceiptProtocolIssueKind::TruncatedProtocol);
    }

    std::vector<std::string> packages;
    std::set<std::string>    unique_packages;
    for(std::size_t index = 4; index + 1 < lines.size(); ++index) {
        const auto package = record_value(lines[index], INSTALL_PREFIX);
        if(!package.has_value()) {
            return fail<TrustedAlpmReceiptMachineReceipt>(
                    TrustedAlpmReceiptProtocolIssueKind::UnexpectedRecord);
        }
        TrustedAlpmReceiptProtocolIssueKind issue =
                TrustedAlpmReceiptProtocolIssueKind::InvalidPackageName;
        if(!retain_unique_package(
                   *package, unique_packages, packages, issue)) {
            return fail<TrustedAlpmReceiptMachineReceipt>(issue);
        }
    }
    if(state == TrustedAlpmReceiptMachineState::Complete && packages.empty()) {
        return fail<TrustedAlpmReceiptMachineReceipt>(
                TrustedAlpmReceiptProtocolIssueKind::
                        EmptyRequestedPackageSet);
    }
    if(state == TrustedAlpmReceiptMachineState::Missing && !packages.empty()) {
        return fail<TrustedAlpmReceiptMachineReceipt>(
                TrustedAlpmReceiptProtocolIssueKind::UnexpectedRecord);
    }
    return TrustedAlpmReceiptMachineReceipt{
            state, std::string(*token), std::move(packages)};
}
