#include "makepkg_syncdeps_adapter_protocol.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <limits>
#include <set>
#include <stdexcept>
#include <system_error>
#include <utility>

#include <sys/random.h>

namespace {

constexpr std::string_view OWNER = "makepkg-sync-dependencies";
constexpr std::string_view REQUEST_HEADER =
    "MOGUET-MAKEPKG-SYNCDEPS-REQUEST\t1";
constexpr std::string_view PREPARED_SESSION_HEADER =
    "MOGUET-MAKEPKG-SYNCDEPS-PREPARED-SESSION\t1";
constexpr std::string_view BOUND_CHILD_HEADER =
    "MOGUET-MAKEPKG-SYNCDEPS-BOUND-CHILD\t1";
constexpr std::string_view PREPARED_TRANSACTION_HEADER =
    "MOGUET-MAKEPKG-SYNCDEPS-PREPARED-TRANSACTION\t1";
constexpr std::string_view TRANSACTION_OBSERVATION_HEADER =
    "MOGUET-MAKEPKG-SYNCDEPS-TRANSACTION-OBSERVATION\t1";
constexpr std::string_view TRANSACTION_OUTCOME_HEADER =
    "MOGUET-MAKEPKG-SYNCDEPS-TRANSACTION-OUTCOME\t1";
constexpr std::string_view TERMINAL_SESSION_HEADER =
    "MOGUET-MAKEPKG-SYNCDEPS-TERMINAL-SESSION\t1";
constexpr std::string_view SESSION_PREPARE_RESPONSE_HEADER =
    "MOGUET-MAKEPKG-SYNCDEPS-SESSION-PREPARED\t1";
constexpr std::string_view TRANSACTION_PREPARE_RESPONSE_HEADER =
    "MOGUET-MAKEPKG-SYNCDEPS-TRANSACTION-PREPARED\t1";
constexpr std::string_view SESSION_MANIFEST_HEADER =
    "MOGUET-MAKEPKG-SYNCDEPS-SESSION\t1";
constexpr std::string_view END_RECORD = "END";
constexpr std::string_view TRANSACTION_BEGIN_RECORD = "TRANSACTION-BEGIN";
constexpr std::string_view TRANSACTION_END_RECORD = "TRANSACTION-END";

template <typename Success>
std::variant<Success, MakepkgSyncdepsAdapterProtocolFailure> fail(
    MakepkgSyncdepsAdapterProtocolIssueKind issue) {
    return MakepkgSyncdepsAdapterProtocolFailure{issue};
}

using ProtocolLinesResult = std::variant<
    std::vector<std::string_view>,
    MakepkgSyncdepsAdapterProtocolFailure>;

ProtocolLinesResult split_protocol_lines(std::string_view protocol) {
    if(protocol.size() > MAKEPKG_SYNCDEPS_ADAPTER_MAXIMUM_BYTES) {
        return MakepkgSyncdepsAdapterProtocolFailure{
            MakepkgSyncdepsAdapterProtocolIssueKind::InputTooLarge};
    }
    if(protocol.empty() || protocol.back() != '\n') {
        return MakepkgSyncdepsAdapterProtocolFailure{
            MakepkgSyncdepsAdapterProtocolIssueKind::MissingFinalNewline};
    }

    std::vector<std::string_view> lines;
    std::size_t offset = 0;
    while(offset < protocol.size()) {
        const std::size_t newline = protocol.find('\n', offset);
        if(newline == std::string_view::npos) {
            return MakepkgSyncdepsAdapterProtocolFailure{
                MakepkgSyncdepsAdapterProtocolIssueKind::TruncatedProtocol};
        }
        lines.push_back(protocol.substr(offset, newline - offset));
        if(lines.size() >
           MAKEPKG_SYNCDEPS_ADAPTER_MAXIMUM_DEPENDENCY_SPECIFICATIONS +
               128U) {
            return MakepkgSyncdepsAdapterProtocolFailure{
                MakepkgSyncdepsAdapterProtocolIssueKind::TooManyRecords};
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

template <typename Integer>
std::optional<Integer> parse_unsigned_integer(std::string_view value) {
    if(value.empty() || value.front() == '+' || value.front() == '-') {
        return std::nullopt;
    }
    Integer parsed{};
    const auto result = std::from_chars(
        value.data(), value.data() + value.size(), parsed);
    if(result.ec != std::errc() ||
       result.ptr != value.data() + value.size()) {
        return std::nullopt;
    }
    return parsed;
}

std::optional<pid_t> parse_process_identifier(std::string_view value) {
    const auto parsed = parse_unsigned_integer<std::uint64_t>(value);
    if(!parsed.has_value() || *parsed == 0 ||
       *parsed > static_cast<std::uint64_t>(
                     std::numeric_limits<pid_t>::max())) {
        return std::nullopt;
    }
    return static_cast<pid_t>(*parsed);
}

std::optional<std::size_t> parse_ordinal(std::string_view value) {
    const auto parsed = parse_unsigned_integer<std::size_t>(value);
    if(!parsed.has_value() || *parsed == 0 || *parsed > 3) {
        return std::nullopt;
    }
    return parsed;
}

std::optional<int> parse_exit_code(std::string_view value) {
    const auto parsed = parse_unsigned_integer<unsigned int>(value);
    if(!parsed.has_value() || *parsed > 255U) return std::nullopt;
    return static_cast<int>(*parsed);
}

std::string hex_encode(std::string_view bytes) {
    constexpr char HEX_DIGITS[] = "0123456789abcdef";
    std::string encoded;
    encoded.resize(bytes.size() * 2U);
    for(std::size_t index = 0; index < bytes.size(); ++index) {
        const unsigned char byte =
            static_cast<unsigned char>(bytes[index]);
        encoded[index * 2U] = HEX_DIGITS[byte >> 4U];
        encoded[index * 2U + 1U] = HEX_DIGITS[byte & 0x0fU];
    }
    return encoded;
}

std::optional<unsigned char> hex_nibble(char character) noexcept {
    if(character >= '0' && character <= '9') {
        return static_cast<unsigned char>(character - '0');
    }
    if(character >= 'a' && character <= 'f') {
        return static_cast<unsigned char>(character - 'a' + 10);
    }
    return std::nullopt;
}

std::optional<std::string> hex_decode(std::string_view encoded) {
    if(encoded.size() % 2U != 0) return std::nullopt;
    std::string decoded;
    decoded.resize(encoded.size() / 2U);
    for(std::size_t index = 0; index < decoded.size(); ++index) {
        const auto high = hex_nibble(encoded[index * 2U]);
        const auto low = hex_nibble(encoded[index * 2U + 1U]);
        if(!high.has_value() || !low.has_value()) return std::nullopt;
        decoded[index] = static_cast<char>((*high << 4U) | *low);
    }
    return decoded;
}

std::string_view command_name(MakepkgSyncdepsAdapterCommand command) {
    switch(command) {
        case MakepkgSyncdepsAdapterCommand::SessionPrepare:
            return "session-prepare";
        case MakepkgSyncdepsAdapterCommand::SessionBind:
            return "session-bind";
        case MakepkgSyncdepsAdapterCommand::TransactionPrepare:
            return "transaction-prepare";
        case MakepkgSyncdepsAdapterCommand::TransactionRecord:
            return "transaction-record";
        case MakepkgSyncdepsAdapterCommand::TransactionFinalize:
            return "transaction-finalize";
        case MakepkgSyncdepsAdapterCommand::TransactionConsume:
            return "transaction-consume";
        case MakepkgSyncdepsAdapterCommand::TransactionAbort:
            return "transaction-abort";
        case MakepkgSyncdepsAdapterCommand::SessionFinalize:
            return "session-finalize";
        case MakepkgSyncdepsAdapterCommand::SessionConsume:
            return "session-consume";
        case MakepkgSyncdepsAdapterCommand::SessionAbort:
            return "session-abort";
        case MakepkgSyncdepsAdapterCommand::SyntheticSession:
            return "synthetic-session";
        case MakepkgSyncdepsAdapterCommand::RootSyntheticSession:
            return "root-synthetic-session";
        case MakepkgSyncdepsAdapterCommand::SyntheticSecurity:
            return "synthetic-security";
        case MakepkgSyncdepsAdapterCommand::RootSyntheticSecurity:
            return "root-synthetic-security";
    }
    throw std::invalid_argument("invalid makepkg syncdeps adapter command");
}

std::optional<MakepkgSyncdepsAdapterCommand> parse_command_name(
    std::string_view name) noexcept {
    if(name == "session-prepare") {
        return MakepkgSyncdepsAdapterCommand::SessionPrepare;
    }
    if(name == "session-bind") {
        return MakepkgSyncdepsAdapterCommand::SessionBind;
    }
    if(name == "transaction-prepare") {
        return MakepkgSyncdepsAdapterCommand::TransactionPrepare;
    }
    if(name == "transaction-record") {
        return MakepkgSyncdepsAdapterCommand::TransactionRecord;
    }
    if(name == "transaction-finalize") {
        return MakepkgSyncdepsAdapterCommand::TransactionFinalize;
    }
    if(name == "transaction-consume") {
        return MakepkgSyncdepsAdapterCommand::TransactionConsume;
    }
    if(name == "transaction-abort") {
        return MakepkgSyncdepsAdapterCommand::TransactionAbort;
    }
    if(name == "session-finalize") {
        return MakepkgSyncdepsAdapterCommand::SessionFinalize;
    }
    if(name == "session-consume") {
        return MakepkgSyncdepsAdapterCommand::SessionConsume;
    }
    if(name == "session-abort") {
        return MakepkgSyncdepsAdapterCommand::SessionAbort;
    }
    if(name == "synthetic-session") {
        return MakepkgSyncdepsAdapterCommand::SyntheticSession;
    }
    if(name == "root-synthetic-session") {
        return MakepkgSyncdepsAdapterCommand::RootSyntheticSession;
    }
    if(name == "synthetic-security") {
        return MakepkgSyncdepsAdapterCommand::SyntheticSecurity;
    }
    if(name == "root-synthetic-security") {
        return MakepkgSyncdepsAdapterCommand::RootSyntheticSecurity;
    }
    return std::nullopt;
}

std::string_view observation_name(
    MakepkgSyncdepsSyntheticObservation observation) noexcept {
    return observation == MakepkgSyncdepsSyntheticObservation::Observed
               ? "Observed"
               : "Missing";
}

std::optional<MakepkgSyncdepsSyntheticObservation> parse_observation_name(
    std::string_view value) noexcept {
    if(value == "Observed") {
        return MakepkgSyncdepsSyntheticObservation::Observed;
    }
    if(value == "Missing") {
        return MakepkgSyncdepsSyntheticObservation::Missing;
    }
    return std::nullopt;
}

std::string_view outcome_name(MakepkgSyncdepsCommandOutcome outcome) noexcept {
    switch(outcome) {
        case MakepkgSyncdepsCommandOutcome::NotAttempted:
            return "NotAttempted";
        case MakepkgSyncdepsCommandOutcome::Succeeded:
            return "Succeeded";
        case MakepkgSyncdepsCommandOutcome::Failed:
            return "Failed";
    }
    return "NotAttempted";
}

std::optional<MakepkgSyncdepsCommandOutcome> parse_outcome_name(
    std::string_view value) noexcept {
    if(value == "NotAttempted") {
        return MakepkgSyncdepsCommandOutcome::NotAttempted;
    }
    if(value == "Succeeded") {
        return MakepkgSyncdepsCommandOutcome::Succeeded;
    }
    if(value == "Failed") return MakepkgSyncdepsCommandOutcome::Failed;
    return std::nullopt;
}

bool has_valid_outcome_shape(
    MakepkgSyncdepsCommandOutcome outcome,
    const std::optional<int>& exit_code) noexcept {
    switch(outcome) {
        case MakepkgSyncdepsCommandOutcome::NotAttempted:
            return !exit_code.has_value();
        case MakepkgSyncdepsCommandOutcome::Succeeded:
            return exit_code == 0;
        case MakepkgSyncdepsCommandOutcome::Failed:
            return exit_code.has_value() && *exit_code > 0 &&
                   *exit_code <= 255;
    }
    return false;
}

std::string_view terminal_name(MakepkgSyncdepsTerminalState state) noexcept {
    return state == MakepkgSyncdepsTerminalState::Complete ? "Complete"
                                                           : "Unsupported";
}

std::optional<MakepkgSyncdepsTerminalState> parse_terminal_name(
    std::string_view value) noexcept {
    if(value == "Complete") return MakepkgSyncdepsTerminalState::Complete;
    if(value == "Unsupported") {
        return MakepkgSyncdepsTerminalState::Unsupported;
    }
    return std::nullopt;
}

std::string_view coverage_name(
    MakepkgSyncdepsAdapterCoverage coverage) noexcept {
    return coverage == MakepkgSyncdepsAdapterCoverage::Complete
               ? "Complete"
               : "Unsupported";
}

std::optional<MakepkgSyncdepsAdapterCoverage> parse_coverage_name(
    std::string_view value) noexcept {
    if(value == "Complete") return MakepkgSyncdepsAdapterCoverage::Complete;
    if(value == "Unsupported") {
        return MakepkgSyncdepsAdapterCoverage::Unsupported;
    }
    return std::nullopt;
}

void require_token(std::string_view token, const char* description) {
    if(!is_valid_makepkg_syncdeps_adapter_token(token)) {
        throw std::invalid_argument(description);
    }
}

void require_ordinal(std::size_t ordinal) {
    if(ordinal == 0 || ordinal > 2) {
        throw std::invalid_argument("invalid makepkg syncdeps ordinal");
    }
}

void require_dependency_specifications(
    const std::vector<std::string>& specifications) {
    if(specifications.empty() ||
       specifications.size() >
           MAKEPKG_SYNCDEPS_ADAPTER_MAXIMUM_DEPENDENCY_SPECIFICATIONS) {
        throw std::invalid_argument(
            "invalid makepkg syncdeps dependency specification count");
    }
    for(const std::string& specification : specifications) {
        if(!is_valid_makepkg_syncdeps_dependency_specification(
               specification)) {
            throw std::invalid_argument(
                "invalid makepkg syncdeps dependency specification");
        }
    }
}

void append_process_identity(
    std::string& protocol, std::string_view prefix,
    const MakepkgSyncdepsPidfdIdentity& identity) {
    if(identity.pid <= 0 || identity.device == 0 || identity.inode == 0) {
        throw std::invalid_argument("invalid makepkg syncdeps pidfd identity");
    }
    protocol.append(prefix).append("_PID\t").append(std::to_string(identity.pid)).push_back('\n');
    protocol.append(prefix).append("_PIDFD_DEVICE\t").append(std::to_string(identity.device)).push_back('\n');
    protocol.append(prefix).append("_PIDFD_INODE\t").append(std::to_string(identity.inode)).push_back('\n');
    protocol.append(prefix).append("_UID\t").append(std::to_string(identity.uid)).push_back('\n');
}

std::optional<MakepkgSyncdepsPidfdIdentity> parse_process_identity(
    const std::vector<std::string_view>& lines, std::size_t& index,
    std::string_view prefix) {
    if(index + 4U > lines.size()) return std::nullopt;
    const auto pid = record_value(
        lines[index++], std::string(prefix).append("_PID\t"));
    const auto device = record_value(
        lines[index++], std::string(prefix).append("_PIDFD_DEVICE\t"));
    const auto inode = record_value(
        lines[index++], std::string(prefix).append("_PIDFD_INODE\t"));
    const auto uid = record_value(
        lines[index++], std::string(prefix).append("_UID\t"));
    if(!pid.has_value() || !device.has_value() || !inode.has_value() ||
       !uid.has_value()) {
        return std::nullopt;
    }
    const auto parsed_pid = parse_process_identifier(*pid);
    const auto parsed_device = parse_unsigned_integer<std::uint64_t>(*device);
    const auto parsed_inode = parse_unsigned_integer<std::uint64_t>(*inode);
    const auto parsed_uid = parse_unsigned_integer<std::uint32_t>(*uid);
    if(!parsed_pid.has_value() || !parsed_device.has_value() ||
       !parsed_inode.has_value() || !parsed_uid.has_value() ||
       *parsed_device == 0 || *parsed_inode == 0) {
        return std::nullopt;
    }
    return MakepkgSyncdepsPidfdIdentity{
        *parsed_pid, *parsed_device, *parsed_inode, *parsed_uid};
}

std::vector<std::string> invocation_arguments_for_wire(
    const MakepkgSyncdepsAdapterInvocation& invocation) {
    std::vector<std::string> arguments{std::string(command_name(invocation.command))};
    switch(invocation.command) {
        case MakepkgSyncdepsAdapterCommand::SessionPrepare:
            arguments.push_back(std::to_string(invocation.launcher_pid));
            break;
        case MakepkgSyncdepsAdapterCommand::SessionBind:
            arguments.insert(
                arguments.end(),
                {invocation.session_token,
                 std::to_string(invocation.launcher_pid),
                 std::to_string(invocation.child_pid),
                 std::to_string(invocation.transaction_adapter_pid)});
            break;
        case MakepkgSyncdepsAdapterCommand::TransactionPrepare:
            arguments.insert(
                arguments.end(),
                {invocation.session_token,
                 std::to_string(invocation.ordinal), "--"});
            arguments.insert(
                arguments.end(), invocation.dependency_specifications.begin(),
                invocation.dependency_specifications.end());
            break;
        case MakepkgSyncdepsAdapterCommand::TransactionRecord:
            arguments.insert(
                arguments.end(),
                {invocation.session_token, std::to_string(invocation.ordinal),
                 invocation.transaction_token,
                 std::string(observation_name(
                     invocation.synthetic_observation))});
            break;
        case MakepkgSyncdepsAdapterCommand::TransactionFinalize:
            if(!invocation.exit_code.has_value()) {
                throw std::invalid_argument(
                    "transaction finalize requires an exit code");
            }
            arguments.insert(
                arguments.end(),
                {invocation.session_token, std::to_string(invocation.ordinal),
                 invocation.transaction_token,
                 std::string(outcome_name(invocation.command_outcome)),
                 std::to_string(*invocation.exit_code)});
            break;
        case MakepkgSyncdepsAdapterCommand::TransactionConsume:
        case MakepkgSyncdepsAdapterCommand::TransactionAbort:
            arguments.insert(
                arguments.end(),
                {invocation.session_token, std::to_string(invocation.ordinal),
                 invocation.transaction_token});
            break;
        case MakepkgSyncdepsAdapterCommand::SessionFinalize:
            if(!invocation.exit_code.has_value()) {
                throw std::invalid_argument(
                    "session finalize requires an exit code");
            }
            arguments.insert(
                arguments.end(),
                {invocation.session_token,
                 std::to_string(invocation.launcher_pid),
                 std::string(outcome_name(invocation.command_outcome)),
                 std::to_string(*invocation.exit_code)});
            break;
        case MakepkgSyncdepsAdapterCommand::SessionConsume:
        case MakepkgSyncdepsAdapterCommand::SessionAbort:
            arguments.insert(
                arguments.end(),
                {invocation.session_token,
                 std::to_string(invocation.launcher_pid)});
            break;
        case MakepkgSyncdepsAdapterCommand::SyntheticSession:
        case MakepkgSyncdepsAdapterCommand::RootSyntheticSession:
            arguments.push_back(
                std::to_string(invocation.synthetic_transaction_count));
            if(invocation.synthetic_hold) arguments.emplace_back("hold");
            break;
        case MakepkgSyncdepsAdapterCommand::SyntheticSecurity:
        case MakepkgSyncdepsAdapterCommand::RootSyntheticSecurity:
            arguments.push_back(invocation.synthetic_security_scenario);
            break;
    }
    return arguments;
}

std::string serialize_argument_vector(
    std::string_view header, const std::vector<std::string>& arguments) {
    std::string protocol;
    protocol.append(header).push_back('\n');
    for(const std::string& argument : arguments) {
        protocol.append("ARG_HEX\t").append(hex_encode(argument)).push_back('\n');
    }
    protocol.append(END_RECORD).push_back('\n');
    if(protocol.size() > MAKEPKG_SYNCDEPS_ADAPTER_MAXIMUM_BYTES) {
        throw std::invalid_argument("oversized makepkg syncdeps protocol");
    }
    return protocol;
}

std::variant<std::vector<std::string>, MakepkgSyncdepsAdapterProtocolFailure>
parse_argument_vector(std::string_view protocol, std::string_view header) {
    const ProtocolLinesResult split = split_protocol_lines(protocol);
    if(const auto* failure =
           std::get_if<MakepkgSyncdepsAdapterProtocolFailure>(&split);
       failure != nullptr) {
        return *failure;
    }
    const auto& lines = std::get<std::vector<std::string_view>>(split);
    if(lines.size() < 3) {
        return MakepkgSyncdepsAdapterProtocolFailure{
            MakepkgSyncdepsAdapterProtocolIssueKind::TruncatedProtocol};
    }
    if(lines.front() != header) {
        return MakepkgSyncdepsAdapterProtocolFailure{
            MakepkgSyncdepsAdapterProtocolIssueKind::InvalidHeader};
    }
    if(lines.back() != END_RECORD) {
        return MakepkgSyncdepsAdapterProtocolFailure{
            MakepkgSyncdepsAdapterProtocolIssueKind::TruncatedProtocol};
    }
    std::vector<std::string> arguments;
    for(std::size_t index = 1; index + 1 < lines.size(); ++index) {
        const auto encoded = record_value(lines[index], "ARG_HEX\t");
        if(!encoded.has_value()) {
            return MakepkgSyncdepsAdapterProtocolFailure{
                MakepkgSyncdepsAdapterProtocolIssueKind::UnexpectedRecord};
        }
        const auto decoded = hex_decode(*encoded);
        if(!decoded.has_value()) {
            return MakepkgSyncdepsAdapterProtocolFailure{
                MakepkgSyncdepsAdapterProtocolIssueKind::UnexpectedRecord};
        }
        arguments.push_back(*decoded);
    }
    return arguments;
}

void append_transaction_records(
    std::string& protocol,
    const MakepkgSyncdepsTransactionManifestEntry& entry) {
    const auto& prepared = entry.prepared;
    const auto& observation = entry.observation;
    const auto& outcome = entry.outcome;
    require_ordinal(prepared.ordinal);
    require_token(prepared.transaction_token, "invalid transaction token");
    require_dependency_specifications(prepared.dependency_specifications);
    if(observation.session_token != prepared.session_token ||
       outcome.session_token != prepared.session_token ||
       observation.ordinal != prepared.ordinal ||
       outcome.ordinal != prepared.ordinal ||
       observation.transaction_token != prepared.transaction_token ||
       outcome.transaction_token != prepared.transaction_token ||
       !has_valid_outcome_shape(outcome.outcome, outcome.exit_code)) {
        throw std::invalid_argument(
            "inconsistent makepkg syncdeps transaction manifest entry");
    }
    protocol.append(TRANSACTION_BEGIN_RECORD).push_back('\n');
    protocol.append("ORDINAL\t").append(std::to_string(prepared.ordinal)).push_back('\n');
    protocol.append("TRANSACTION_TOKEN\t")
        .append(prepared.transaction_token)
        .push_back('\n');
    for(const std::string& specification : prepared.dependency_specifications) {
        protocol.append("SPEC_HEX\t").append(hex_encode(specification)).push_back('\n');
    }
    protocol.append("OBSERVATION\t")
        .append(observation_name(observation.observation))
        .push_back('\n');
    protocol.append("COMMAND_OUTCOME\t")
        .append(outcome_name(outcome.outcome))
        .push_back('\n');
    protocol.append("COMMAND_EXIT\t")
        .append(outcome.exit_code.has_value()
                    ? std::to_string(*outcome.exit_code)
                    : "None")
        .push_back('\n');
    protocol.append(TRANSACTION_END_RECORD).push_back('\n');
}

} // namespace

std::string_view makepkg_syncdeps_adapter_owner() noexcept {
    return OWNER;
}

bool is_valid_makepkg_syncdeps_adapter_token(std::string_view token) noexcept {
    return token.size() == MAKEPKG_SYNCDEPS_ADAPTER_TOKEN_HEX_LENGTH &&
           std::all_of(
               token.begin(), token.end(), [](unsigned char character) {
                   return (character >= '0' && character <= '9') ||
                          (character >= 'a' && character <= 'f');
               });
}

bool is_valid_makepkg_syncdeps_dependency_specification(
    std::string_view specification) noexcept {
    return !specification.empty() && specification.front() != '-' &&
           specification.size() <=
               MAKEPKG_SYNCDEPS_ADAPTER_MAXIMUM_DEPENDENCY_SPECIFICATION_BYTES &&
           specification.find('\0') == std::string_view::npos;
}

bool is_valid_makepkg_syncdeps_security_scenario(
    std::string_view scenario) noexcept {
    constexpr std::array<std::string_view, 10> SCENARIOS{
        "launcher-transaction",
        "child-session",
        "descendant-transaction",
        "post-exec",
        "peer-exit",
        "intermediate-parent-exit",
        "old-packet-new-peer",
        "cross-session-replacement",
        "bind-failure",
        "barrier-failure",
    };
    return std::find(SCENARIOS.begin(), SCENARIOS.end(), scenario) !=
               SCENARIOS.end() ||
           scenario == "launcher-failure";
}

std::optional<std::string> generate_makepkg_syncdeps_adapter_token() noexcept {
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
        std::string random_bytes_string;
        random_bytes_string.reserve(random_bytes.size());
        for(const unsigned char byte : random_bytes) {
            random_bytes_string.push_back(static_cast<char>(byte));
        }
        return hex_encode(random_bytes_string);
    } catch(...) {
        return std::nullopt;
    }
}

MakepkgSyncdepsAdapterInvocationResult
parse_makepkg_syncdeps_adapter_arguments(
    const std::vector<std::string>& arguments) {
    if(arguments.empty()) {
        return fail<MakepkgSyncdepsAdapterInvocation>(
            MakepkgSyncdepsAdapterProtocolIssueKind::InvalidArgumentCount);
    }
    const auto command = parse_command_name(arguments[0]);
    if(!command.has_value()) {
        return fail<MakepkgSyncdepsAdapterInvocation>(
            MakepkgSyncdepsAdapterProtocolIssueKind::InvalidCommand);
    }
    MakepkgSyncdepsAdapterInvocation invocation;
    invocation.command = *command;

    const auto require_session_token = [&](std::size_t index) {
        if(index >= arguments.size() ||
           !is_valid_makepkg_syncdeps_adapter_token(arguments[index])) {
            return false;
        }
        invocation.session_token = arguments[index];
        return true;
    };
    const auto require_transaction_token = [&](std::size_t index) {
        if(index >= arguments.size() ||
           !is_valid_makepkg_syncdeps_adapter_token(arguments[index])) {
            return false;
        }
        invocation.transaction_token = arguments[index];
        return true;
    };

    switch(*command) {
        case MakepkgSyncdepsAdapterCommand::SessionPrepare: {
            if(arguments.size() != 2) {
                return fail<MakepkgSyncdepsAdapterInvocation>(
                    MakepkgSyncdepsAdapterProtocolIssueKind::InvalidArgumentCount);
            }
            const auto launcher_pid = parse_process_identifier(arguments[1]);
            if(!launcher_pid.has_value()) {
                return fail<MakepkgSyncdepsAdapterInvocation>(
                    MakepkgSyncdepsAdapterProtocolIssueKind::InvalidProcessIdentifier);
            }
            invocation.launcher_pid = *launcher_pid;
            break;
        }
        case MakepkgSyncdepsAdapterCommand::SessionBind: {
            if(arguments.size() != 5 || !require_session_token(1)) {
                return fail<MakepkgSyncdepsAdapterInvocation>(
                    arguments.size() != 5
                        ? MakepkgSyncdepsAdapterProtocolIssueKind::InvalidArgumentCount
                        : MakepkgSyncdepsAdapterProtocolIssueKind::InvalidSessionToken);
            }
            const auto launcher_pid = parse_process_identifier(arguments[2]);
            const auto child_pid = parse_process_identifier(arguments[3]);
            const auto transaction_adapter_pid =
                parse_process_identifier(arguments[4]);
            if(!launcher_pid.has_value() || !child_pid.has_value() ||
               !transaction_adapter_pid.has_value()) {
                return fail<MakepkgSyncdepsAdapterInvocation>(
                    MakepkgSyncdepsAdapterProtocolIssueKind::InvalidProcessIdentifier);
            }
            invocation.launcher_pid = *launcher_pid;
            invocation.child_pid = *child_pid;
            invocation.transaction_adapter_pid = *transaction_adapter_pid;
            break;
        }
        case MakepkgSyncdepsAdapterCommand::TransactionPrepare: {
            if(arguments.size() < 5 || !require_session_token(1)) {
                return fail<MakepkgSyncdepsAdapterInvocation>(
                    arguments.size() < 5
                        ? MakepkgSyncdepsAdapterProtocolIssueKind::InvalidArgumentCount
                        : MakepkgSyncdepsAdapterProtocolIssueKind::InvalidSessionToken);
            }
            const auto ordinal = parse_ordinal(arguments[2]);
            if(!ordinal.has_value()) {
                return fail<MakepkgSyncdepsAdapterInvocation>(
                    MakepkgSyncdepsAdapterProtocolIssueKind::InvalidOrdinal);
            }
            if(arguments[3] != "--") {
                return fail<MakepkgSyncdepsAdapterInvocation>(
                    MakepkgSyncdepsAdapterProtocolIssueKind::MissingArgumentSeparator);
            }
            invocation.ordinal = *ordinal;
            invocation.dependency_specifications.assign(
                arguments.begin() + 4, arguments.end());
            if(invocation.dependency_specifications.empty()) {
                return fail<MakepkgSyncdepsAdapterInvocation>(
                    MakepkgSyncdepsAdapterProtocolIssueKind::EmptyDependencySpecificationSet);
            }
            if(invocation.dependency_specifications.size() >
               MAKEPKG_SYNCDEPS_ADAPTER_MAXIMUM_DEPENDENCY_SPECIFICATIONS) {
                return fail<MakepkgSyncdepsAdapterInvocation>(
                    MakepkgSyncdepsAdapterProtocolIssueKind::TooManyRecords);
            }
            if(std::any_of(
                   invocation.dependency_specifications.begin(),
                   invocation.dependency_specifications.end(),
                   [](const std::string& specification) {
                       return !is_valid_makepkg_syncdeps_dependency_specification(
                           specification);
                   })) {
                return fail<MakepkgSyncdepsAdapterInvocation>(
                    MakepkgSyncdepsAdapterProtocolIssueKind::InvalidDependencySpecification);
            }
            break;
        }
        case MakepkgSyncdepsAdapterCommand::TransactionRecord: {
            if(arguments.size() != 5 || !require_session_token(1) ||
               !require_transaction_token(3)) {
                return fail<MakepkgSyncdepsAdapterInvocation>(
                    arguments.size() != 5
                        ? MakepkgSyncdepsAdapterProtocolIssueKind::InvalidArgumentCount
                        : (!is_valid_makepkg_syncdeps_adapter_token(arguments[1])
                               ? MakepkgSyncdepsAdapterProtocolIssueKind::InvalidSessionToken
                               : MakepkgSyncdepsAdapterProtocolIssueKind::InvalidTransactionToken));
            }
            const auto ordinal = parse_ordinal(arguments[2]);
            const auto observation = parse_observation_name(arguments[4]);
            if(!ordinal.has_value()) {
                return fail<MakepkgSyncdepsAdapterInvocation>(
                    MakepkgSyncdepsAdapterProtocolIssueKind::InvalidOrdinal);
            }
            if(!observation.has_value()) {
                return fail<MakepkgSyncdepsAdapterInvocation>(
                    MakepkgSyncdepsAdapterProtocolIssueKind::InvalidSyntheticObservation);
            }
            invocation.ordinal = *ordinal;
            invocation.synthetic_observation = *observation;
            break;
        }
        case MakepkgSyncdepsAdapterCommand::TransactionFinalize: {
            if(arguments.size() != 6 || !require_session_token(1) ||
               !require_transaction_token(3)) {
                return fail<MakepkgSyncdepsAdapterInvocation>(
                    arguments.size() != 6
                        ? MakepkgSyncdepsAdapterProtocolIssueKind::InvalidArgumentCount
                        : (!is_valid_makepkg_syncdeps_adapter_token(arguments[1])
                               ? MakepkgSyncdepsAdapterProtocolIssueKind::InvalidSessionToken
                               : MakepkgSyncdepsAdapterProtocolIssueKind::InvalidTransactionToken));
            }
            const auto ordinal = parse_ordinal(arguments[2]);
            const auto outcome = parse_outcome_name(arguments[4]);
            const auto exit_code = parse_exit_code(arguments[5]);
            if(!ordinal.has_value()) {
                return fail<MakepkgSyncdepsAdapterInvocation>(
                    MakepkgSyncdepsAdapterProtocolIssueKind::InvalidOrdinal);
            }
            if(!outcome.has_value() ||
               *outcome == MakepkgSyncdepsCommandOutcome::NotAttempted) {
                return fail<MakepkgSyncdepsAdapterInvocation>(
                    MakepkgSyncdepsAdapterProtocolIssueKind::InvalidOutcome);
            }
            if(!exit_code.has_value() ||
               !has_valid_outcome_shape(*outcome, exit_code)) {
                return fail<MakepkgSyncdepsAdapterInvocation>(
                    MakepkgSyncdepsAdapterProtocolIssueKind::InvalidExitCode);
            }
            invocation.ordinal = *ordinal;
            invocation.command_outcome = *outcome;
            invocation.exit_code = *exit_code;
            break;
        }
        case MakepkgSyncdepsAdapterCommand::TransactionConsume:
        case MakepkgSyncdepsAdapterCommand::TransactionAbort: {
            if(arguments.size() != 4 || !require_session_token(1) ||
               !require_transaction_token(3)) {
                return fail<MakepkgSyncdepsAdapterInvocation>(
                    arguments.size() != 4
                        ? MakepkgSyncdepsAdapterProtocolIssueKind::InvalidArgumentCount
                        : (!is_valid_makepkg_syncdeps_adapter_token(arguments[1])
                               ? MakepkgSyncdepsAdapterProtocolIssueKind::InvalidSessionToken
                               : MakepkgSyncdepsAdapterProtocolIssueKind::InvalidTransactionToken));
            }
            const auto ordinal = parse_ordinal(arguments[2]);
            if(!ordinal.has_value()) {
                return fail<MakepkgSyncdepsAdapterInvocation>(
                    MakepkgSyncdepsAdapterProtocolIssueKind::InvalidOrdinal);
            }
            invocation.ordinal = *ordinal;
            break;
        }
        case MakepkgSyncdepsAdapterCommand::SessionFinalize: {
            if(arguments.size() != 5 || !require_session_token(1)) {
                return fail<MakepkgSyncdepsAdapterInvocation>(
                    arguments.size() != 5
                        ? MakepkgSyncdepsAdapterProtocolIssueKind::InvalidArgumentCount
                        : MakepkgSyncdepsAdapterProtocolIssueKind::InvalidSessionToken);
            }
            const auto launcher_pid = parse_process_identifier(arguments[2]);
            const auto outcome = parse_outcome_name(arguments[3]);
            const auto exit_code = parse_exit_code(arguments[4]);
            if(!launcher_pid.has_value()) {
                return fail<MakepkgSyncdepsAdapterInvocation>(
                    MakepkgSyncdepsAdapterProtocolIssueKind::InvalidProcessIdentifier);
            }
            if(!outcome.has_value() ||
               *outcome == MakepkgSyncdepsCommandOutcome::NotAttempted) {
                return fail<MakepkgSyncdepsAdapterInvocation>(
                    MakepkgSyncdepsAdapterProtocolIssueKind::InvalidOutcome);
            }
            if(!exit_code.has_value() ||
               !has_valid_outcome_shape(*outcome, exit_code)) {
                return fail<MakepkgSyncdepsAdapterInvocation>(
                    MakepkgSyncdepsAdapterProtocolIssueKind::InvalidExitCode);
            }
            invocation.launcher_pid = *launcher_pid;
            invocation.command_outcome = *outcome;
            invocation.exit_code = *exit_code;
            break;
        }
        case MakepkgSyncdepsAdapterCommand::SessionConsume:
        case MakepkgSyncdepsAdapterCommand::SessionAbort: {
            if(arguments.size() != 3 || !require_session_token(1)) {
                return fail<MakepkgSyncdepsAdapterInvocation>(
                    arguments.size() != 3
                        ? MakepkgSyncdepsAdapterProtocolIssueKind::InvalidArgumentCount
                        : MakepkgSyncdepsAdapterProtocolIssueKind::InvalidSessionToken);
            }
            const auto launcher_pid = parse_process_identifier(arguments[2]);
            if(!launcher_pid.has_value()) {
                return fail<MakepkgSyncdepsAdapterInvocation>(
                    MakepkgSyncdepsAdapterProtocolIssueKind::InvalidProcessIdentifier);
            }
            invocation.launcher_pid = *launcher_pid;
            break;
        }
        case MakepkgSyncdepsAdapterCommand::SyntheticSession:
        case MakepkgSyncdepsAdapterCommand::RootSyntheticSession: {
            if(arguments.size() < 2 || arguments.size() > 3 ||
               (arguments.size() == 3 && arguments[2] != "hold")) {
                return fail<MakepkgSyncdepsAdapterInvocation>(
                    MakepkgSyncdepsAdapterProtocolIssueKind::InvalidArgumentCount);
            }
            const auto count = parse_unsigned_integer<std::size_t>(arguments[1]);
            if(!count.has_value() || *count > 3) {
                return fail<MakepkgSyncdepsAdapterInvocation>(
                    MakepkgSyncdepsAdapterProtocolIssueKind::InvalidOrdinal);
            }
            invocation.synthetic_transaction_count = *count;
            invocation.synthetic_hold = arguments.size() == 3;
            break;
        }
        case MakepkgSyncdepsAdapterCommand::SyntheticSecurity:
        case MakepkgSyncdepsAdapterCommand::RootSyntheticSecurity: {
            if(arguments.size() != 2 ||
               !is_valid_makepkg_syncdeps_security_scenario(arguments[1])) {
                return fail<MakepkgSyncdepsAdapterInvocation>(
                    arguments.size() != 2
                        ? MakepkgSyncdepsAdapterProtocolIssueKind::InvalidArgumentCount
                        : MakepkgSyncdepsAdapterProtocolIssueKind::InvalidCommand);
            }
            invocation.synthetic_security_scenario = arguments[1];
            break;
        }
    }
    return invocation;
}

std::string serialize_makepkg_syncdeps_adapter_request(
    const MakepkgSyncdepsAdapterInvocation& invocation) {
    return serialize_argument_vector(
        REQUEST_HEADER, invocation_arguments_for_wire(invocation));
}

MakepkgSyncdepsAdapterInvocationResult
parse_makepkg_syncdeps_adapter_request(std::string_view protocol) {
    const auto parsed = parse_argument_vector(protocol, REQUEST_HEADER);
    if(const auto* failure =
           std::get_if<MakepkgSyncdepsAdapterProtocolFailure>(&parsed);
       failure != nullptr) {
        return *failure;
    }
    return parse_makepkg_syncdeps_adapter_arguments(
        std::get<std::vector<std::string>>(parsed));
}

std::string serialize_makepkg_syncdeps_prepared_session_state(
    const MakepkgSyncdepsPreparedSessionState& state) {
    require_token(state.session_token, "invalid session token");
    if(state.invoking_uid == 0 || state.installed_executable.device == 0 ||
       state.installed_executable.inode == 0 ||
       state.launcher.uid != 0 ||
       state.supervisor.uid != 0) {
        throw std::invalid_argument("invalid prepared session state");
    }
    std::string protocol;
    protocol.append(PREPARED_SESSION_HEADER).push_back('\n');
    protocol.append("SESSION\t").append(state.session_token).push_back('\n');
    protocol.append("OWNER\t").append(OWNER).push_back('\n');
    protocol.append("INVOKING_UID\t")
        .append(std::to_string(state.invoking_uid))
        .push_back('\n');
    protocol.append("INSTALLED_DEVICE\t")
        .append(std::to_string(state.installed_executable.device))
        .push_back('\n');
    protocol.append("INSTALLED_INODE\t")
        .append(std::to_string(state.installed_executable.inode))
        .push_back('\n');
    append_process_identity(protocol, "LAUNCHER", state.launcher);
    append_process_identity(protocol, "SUPERVISOR", state.supervisor);
    protocol.append(END_RECORD).push_back('\n');
    return protocol;
}

MakepkgSyncdepsPreparedSessionStateResult
parse_makepkg_syncdeps_prepared_session_state(std::string_view protocol) {
    const ProtocolLinesResult split = split_protocol_lines(protocol);
    if(const auto* failure =
           std::get_if<MakepkgSyncdepsAdapterProtocolFailure>(&split);
       failure != nullptr) {
        return *failure;
    }
    const auto& lines = std::get<std::vector<std::string_view>>(split);
    if(lines.size() != 15 || lines[0] != PREPARED_SESSION_HEADER ||
       lines.back() != END_RECORD) {
        return fail<MakepkgSyncdepsPreparedSessionState>(
            lines.empty() || lines[0] != PREPARED_SESSION_HEADER
                ? MakepkgSyncdepsAdapterProtocolIssueKind::InvalidHeader
                : MakepkgSyncdepsAdapterProtocolIssueKind::TruncatedProtocol);
    }
    const auto token = record_value(lines[1], "SESSION\t");
    const auto owner = record_value(lines[2], "OWNER\t");
    const auto uid = record_value(lines[3], "INVOKING_UID\t");
    const auto installed_device =
        record_value(lines[4], "INSTALLED_DEVICE\t");
    const auto installed_inode =
        record_value(lines[5], "INSTALLED_INODE\t");
    if(!token.has_value() || !is_valid_makepkg_syncdeps_adapter_token(*token) ||
       !owner.has_value() || *owner != OWNER || !uid.has_value() ||
       !installed_device.has_value() || !installed_inode.has_value()) {
        return fail<MakepkgSyncdepsPreparedSessionState>(
            !owner.has_value() || *owner != OWNER
                ? MakepkgSyncdepsAdapterProtocolIssueKind::InvalidOwner
                : MakepkgSyncdepsAdapterProtocolIssueKind::UnexpectedRecord);
    }
    const auto parsed_uid = parse_unsigned_integer<std::uint32_t>(*uid);
    const auto parsed_device =
        parse_unsigned_integer<std::uint64_t>(*installed_device);
    const auto parsed_inode =
        parse_unsigned_integer<std::uint64_t>(*installed_inode);
    std::size_t index = 6;
    const auto launcher = parse_process_identity(lines, index, "LAUNCHER");
    const auto supervisor = parse_process_identity(lines, index, "SUPERVISOR");
    if(!parsed_uid.has_value() || *parsed_uid == 0 ||
       !parsed_device.has_value() || *parsed_device == 0 ||
       !parsed_inode.has_value() || *parsed_inode == 0 ||
       !launcher.has_value() || !supervisor.has_value() ||
       launcher->uid != 0 || supervisor->uid != 0 ||
       index != lines.size() - 1U) {
        return fail<MakepkgSyncdepsPreparedSessionState>(
            MakepkgSyncdepsAdapterProtocolIssueKind::InvalidProcessIdentity);
    }
    return MakepkgSyncdepsPreparedSessionState{
        std::string(*token), *parsed_uid, {*parsed_device, *parsed_inode}, *launcher, *supervisor};
}

std::string serialize_makepkg_syncdeps_bound_child_state(
    const MakepkgSyncdepsBoundChildState& state) {
    require_token(state.session_token, "invalid session token");
    std::string protocol;
    protocol.append(BOUND_CHILD_HEADER).push_back('\n');
    protocol.append("SESSION\t").append(state.session_token).push_back('\n');
    protocol.append("OWNER\t").append(OWNER).push_back('\n');
    append_process_identity(protocol, "CHILD", state.child);
    append_process_identity(
        protocol, "TRANSACTION_ADAPTER", state.transaction_adapter);
    protocol.append(END_RECORD).push_back('\n');
    return protocol;
}

MakepkgSyncdepsBoundChildStateResult
parse_makepkg_syncdeps_bound_child_state(std::string_view protocol) {
    const ProtocolLinesResult split = split_protocol_lines(protocol);
    if(const auto* failure =
           std::get_if<MakepkgSyncdepsAdapterProtocolFailure>(&split);
       failure != nullptr) {
        return *failure;
    }
    const auto& lines = std::get<std::vector<std::string_view>>(split);
    if(lines.size() != 12 || lines[0] != BOUND_CHILD_HEADER ||
       lines.back() != END_RECORD) {
        return fail<MakepkgSyncdepsBoundChildState>(
            lines.empty() || lines[0] != BOUND_CHILD_HEADER
                ? MakepkgSyncdepsAdapterProtocolIssueKind::InvalidHeader
                : MakepkgSyncdepsAdapterProtocolIssueKind::TruncatedProtocol);
    }
    const auto token = record_value(lines[1], "SESSION\t");
    const auto owner = record_value(lines[2], "OWNER\t");
    std::size_t index = 3;
    const auto child = parse_process_identity(lines, index, "CHILD");
    const auto transaction_adapter =
        parse_process_identity(lines, index, "TRANSACTION_ADAPTER");
    if(!token.has_value() || !is_valid_makepkg_syncdeps_adapter_token(*token) ||
       !owner.has_value() || *owner != OWNER || !child.has_value() ||
       !transaction_adapter.has_value() ||
       index != lines.size() - 1U) {
        return fail<MakepkgSyncdepsBoundChildState>(
            !owner.has_value() || *owner != OWNER
                ? MakepkgSyncdepsAdapterProtocolIssueKind::InvalidOwner
                : MakepkgSyncdepsAdapterProtocolIssueKind::InvalidProcessIdentity);
    }
    return MakepkgSyncdepsBoundChildState{
        std::string(*token), *child, *transaction_adapter};
}

std::string serialize_makepkg_syncdeps_prepared_transaction_state(
    const MakepkgSyncdepsPreparedTransactionState& state) {
    require_token(state.session_token, "invalid session token");
    require_token(state.transaction_token, "invalid transaction token");
    require_ordinal(state.ordinal);
    require_dependency_specifications(state.dependency_specifications);
    std::string protocol;
    protocol.append(PREPARED_TRANSACTION_HEADER).push_back('\n');
    protocol.append("SESSION\t").append(state.session_token).push_back('\n');
    protocol.append("OWNER\t").append(OWNER).push_back('\n');
    protocol.append("ORDINAL\t").append(std::to_string(state.ordinal)).push_back('\n');
    protocol.append("TRANSACTION_TOKEN\t")
        .append(state.transaction_token)
        .push_back('\n');
    for(const std::string& specification : state.dependency_specifications) {
        protocol.append("SPEC_HEX\t").append(hex_encode(specification)).push_back('\n');
    }
    protocol.append(END_RECORD).push_back('\n');
    if(protocol.size() > MAKEPKG_SYNCDEPS_ADAPTER_MAXIMUM_BYTES) {
        throw std::invalid_argument("oversized prepared transaction state");
    }
    return protocol;
}

MakepkgSyncdepsPreparedTransactionStateResult
parse_makepkg_syncdeps_prepared_transaction_state(std::string_view protocol) {
    const ProtocolLinesResult split = split_protocol_lines(protocol);
    if(const auto* failure =
           std::get_if<MakepkgSyncdepsAdapterProtocolFailure>(&split);
       failure != nullptr) {
        return *failure;
    }
    const auto& lines = std::get<std::vector<std::string_view>>(split);
    if(lines.size() < 7 || lines[0] != PREPARED_TRANSACTION_HEADER ||
       lines.back() != END_RECORD) {
        return fail<MakepkgSyncdepsPreparedTransactionState>(
            lines.empty() || lines[0] != PREPARED_TRANSACTION_HEADER
                ? MakepkgSyncdepsAdapterProtocolIssueKind::InvalidHeader
                : MakepkgSyncdepsAdapterProtocolIssueKind::TruncatedProtocol);
    }
    const auto token = record_value(lines[1], "SESSION\t");
    const auto owner = record_value(lines[2], "OWNER\t");
    const auto ordinal = record_value(lines[3], "ORDINAL\t");
    const auto transaction_token =
        record_value(lines[4], "TRANSACTION_TOKEN\t");
    if(!token.has_value() || !is_valid_makepkg_syncdeps_adapter_token(*token) ||
       !owner.has_value() || *owner != OWNER || !ordinal.has_value() ||
       !transaction_token.has_value() ||
       !is_valid_makepkg_syncdeps_adapter_token(*transaction_token)) {
        return fail<MakepkgSyncdepsPreparedTransactionState>(
            !owner.has_value() || *owner != OWNER
                ? MakepkgSyncdepsAdapterProtocolIssueKind::InvalidOwner
                : MakepkgSyncdepsAdapterProtocolIssueKind::UnexpectedRecord);
    }
    const auto parsed_ordinal = parse_unsigned_integer<std::size_t>(*ordinal);
    if(!parsed_ordinal.has_value() || *parsed_ordinal == 0 ||
       *parsed_ordinal > 2) {
        return fail<MakepkgSyncdepsPreparedTransactionState>(
            MakepkgSyncdepsAdapterProtocolIssueKind::InvalidOrdinal);
    }
    std::vector<std::string> specifications;
    for(std::size_t index = 5; index + 1 < lines.size(); ++index) {
        const auto encoded = record_value(lines[index], "SPEC_HEX\t");
        if(!encoded.has_value()) {
            return fail<MakepkgSyncdepsPreparedTransactionState>(
                MakepkgSyncdepsAdapterProtocolIssueKind::UnexpectedRecord);
        }
        const auto decoded = hex_decode(*encoded);
        if(!decoded.has_value() ||
           !is_valid_makepkg_syncdeps_dependency_specification(*decoded)) {
            return fail<MakepkgSyncdepsPreparedTransactionState>(
                MakepkgSyncdepsAdapterProtocolIssueKind::InvalidDependencySpecification);
        }
        specifications.push_back(*decoded);
    }
    if(specifications.empty()) {
        return fail<MakepkgSyncdepsPreparedTransactionState>(
            MakepkgSyncdepsAdapterProtocolIssueKind::EmptyDependencySpecificationSet);
    }
    if(specifications.size() >
       MAKEPKG_SYNCDEPS_ADAPTER_MAXIMUM_DEPENDENCY_SPECIFICATIONS) {
        return fail<MakepkgSyncdepsPreparedTransactionState>(
            MakepkgSyncdepsAdapterProtocolIssueKind::TooManyRecords);
    }
    return MakepkgSyncdepsPreparedTransactionState{
        std::string(*token), *parsed_ordinal,
        std::string(*transaction_token), std::move(specifications)};
}

std::string serialize_makepkg_syncdeps_transaction_observation_state(
    const MakepkgSyncdepsTransactionObservationState& state) {
    require_token(state.session_token, "invalid session token");
    require_token(state.transaction_token, "invalid transaction token");
    require_ordinal(state.ordinal);
    std::string protocol;
    protocol.append(TRANSACTION_OBSERVATION_HEADER).push_back('\n');
    protocol.append("SESSION\t").append(state.session_token).push_back('\n');
    protocol.append("OWNER\t").append(OWNER).push_back('\n');
    protocol.append("ORDINAL\t").append(std::to_string(state.ordinal)).push_back('\n');
    protocol.append("TRANSACTION_TOKEN\t")
        .append(state.transaction_token)
        .push_back('\n');
    protocol.append("OBSERVATION\t")
        .append(observation_name(state.observation))
        .push_back('\n');
    protocol.append(END_RECORD).push_back('\n');
    return protocol;
}

MakepkgSyncdepsTransactionObservationStateResult
parse_makepkg_syncdeps_transaction_observation_state(
    std::string_view protocol) {
    const ProtocolLinesResult split = split_protocol_lines(protocol);
    if(const auto* failure =
           std::get_if<MakepkgSyncdepsAdapterProtocolFailure>(&split);
       failure != nullptr) {
        return *failure;
    }
    const auto& lines = std::get<std::vector<std::string_view>>(split);
    if(lines.size() != 7 || lines[0] != TRANSACTION_OBSERVATION_HEADER ||
       lines.back() != END_RECORD) {
        return fail<MakepkgSyncdepsTransactionObservationState>(
            lines.empty() || lines[0] != TRANSACTION_OBSERVATION_HEADER
                ? MakepkgSyncdepsAdapterProtocolIssueKind::InvalidHeader
                : MakepkgSyncdepsAdapterProtocolIssueKind::TruncatedProtocol);
    }
    const auto token = record_value(lines[1], "SESSION\t");
    const auto owner = record_value(lines[2], "OWNER\t");
    const auto ordinal = record_value(lines[3], "ORDINAL\t");
    const auto transaction_token =
        record_value(lines[4], "TRANSACTION_TOKEN\t");
    const auto observation = record_value(lines[5], "OBSERVATION\t");
    const auto parsed_ordinal =
        ordinal.has_value()
            ? parse_unsigned_integer<std::size_t>(*ordinal)
            : std::nullopt;
    const auto parsed_observation =
        observation.has_value() ? parse_observation_name(*observation)
                                : std::nullopt;
    if(!token.has_value() || !is_valid_makepkg_syncdeps_adapter_token(*token) ||
       !owner.has_value() || *owner != OWNER ||
       !parsed_ordinal.has_value() || *parsed_ordinal == 0 ||
       *parsed_ordinal > 2 || !transaction_token.has_value() ||
       !is_valid_makepkg_syncdeps_adapter_token(*transaction_token) ||
       !parsed_observation.has_value()) {
        return fail<MakepkgSyncdepsTransactionObservationState>(
            !owner.has_value() || *owner != OWNER
                ? MakepkgSyncdepsAdapterProtocolIssueKind::InvalidOwner
                : MakepkgSyncdepsAdapterProtocolIssueKind::UnexpectedRecord);
    }
    return MakepkgSyncdepsTransactionObservationState{
        std::string(*token), *parsed_ordinal,
        std::string(*transaction_token), *parsed_observation};
}

std::string serialize_makepkg_syncdeps_transaction_outcome_state(
    const MakepkgSyncdepsTransactionOutcomeState& state) {
    require_token(state.session_token, "invalid session token");
    require_token(state.transaction_token, "invalid transaction token");
    require_ordinal(state.ordinal);
    if(!has_valid_outcome_shape(state.outcome, state.exit_code)) {
        throw std::invalid_argument("invalid transaction outcome state");
    }
    std::string protocol;
    protocol.append(TRANSACTION_OUTCOME_HEADER).push_back('\n');
    protocol.append("SESSION\t").append(state.session_token).push_back('\n');
    protocol.append("OWNER\t").append(OWNER).push_back('\n');
    protocol.append("ORDINAL\t").append(std::to_string(state.ordinal)).push_back('\n');
    protocol.append("TRANSACTION_TOKEN\t")
        .append(state.transaction_token)
        .push_back('\n');
    protocol.append("OUTCOME\t").append(outcome_name(state.outcome)).push_back('\n');
    protocol.append("EXIT\t")
        .append(state.exit_code.has_value() ? std::to_string(*state.exit_code)
                                            : "None")
        .push_back('\n');
    protocol.append(END_RECORD).push_back('\n');
    return protocol;
}

MakepkgSyncdepsTransactionOutcomeStateResult
parse_makepkg_syncdeps_transaction_outcome_state(std::string_view protocol) {
    const ProtocolLinesResult split = split_protocol_lines(protocol);
    if(const auto* failure =
           std::get_if<MakepkgSyncdepsAdapterProtocolFailure>(&split);
       failure != nullptr) {
        return *failure;
    }
    const auto& lines = std::get<std::vector<std::string_view>>(split);
    if(lines.size() != 8 || lines[0] != TRANSACTION_OUTCOME_HEADER ||
       lines.back() != END_RECORD) {
        return fail<MakepkgSyncdepsTransactionOutcomeState>(
            lines.empty() || lines[0] != TRANSACTION_OUTCOME_HEADER
                ? MakepkgSyncdepsAdapterProtocolIssueKind::InvalidHeader
                : MakepkgSyncdepsAdapterProtocolIssueKind::TruncatedProtocol);
    }
    const auto token = record_value(lines[1], "SESSION\t");
    const auto owner = record_value(lines[2], "OWNER\t");
    const auto ordinal = record_value(lines[3], "ORDINAL\t");
    const auto transaction_token =
        record_value(lines[4], "TRANSACTION_TOKEN\t");
    const auto outcome = record_value(lines[5], "OUTCOME\t");
    const auto exit = record_value(lines[6], "EXIT\t");
    const auto parsed_ordinal =
        ordinal.has_value()
            ? parse_unsigned_integer<std::size_t>(*ordinal)
            : std::nullopt;
    const auto parsed_outcome =
        outcome.has_value() ? parse_outcome_name(*outcome) : std::nullopt;
    std::optional<int> parsed_exit;
    bool exit_valid = false;
    if(exit.has_value() && *exit == "None") {
        exit_valid = true;
    } else if(exit.has_value()) {
        parsed_exit = parse_exit_code(*exit);
        exit_valid = parsed_exit.has_value();
    }
    if(!token.has_value() || !is_valid_makepkg_syncdeps_adapter_token(*token) ||
       !owner.has_value() || *owner != OWNER ||
       !parsed_ordinal.has_value() || *parsed_ordinal == 0 ||
       *parsed_ordinal > 2 || !transaction_token.has_value() ||
       !is_valid_makepkg_syncdeps_adapter_token(*transaction_token) ||
       !parsed_outcome.has_value() || !exit_valid ||
       !has_valid_outcome_shape(*parsed_outcome, parsed_exit)) {
        return fail<MakepkgSyncdepsTransactionOutcomeState>(
            !owner.has_value() || *owner != OWNER
                ? MakepkgSyncdepsAdapterProtocolIssueKind::InvalidOwner
                : MakepkgSyncdepsAdapterProtocolIssueKind::InvalidOutcome);
    }
    return MakepkgSyncdepsTransactionOutcomeState{
        std::string(*token), *parsed_ordinal,
        std::string(*transaction_token), *parsed_outcome, parsed_exit};
}

std::string serialize_makepkg_syncdeps_terminal_session_state(
    const MakepkgSyncdepsTerminalSessionState& state) {
    require_token(state.session_token, "invalid session token");
    if(state.transaction_count > 2 ||
       !has_valid_outcome_shape(
           state.makepkg_outcome, state.makepkg_exit_code) ||
       (state.terminal_state == MakepkgSyncdepsTerminalState::Complete) !=
           (state.coverage == MakepkgSyncdepsAdapterCoverage::Complete)) {
        throw std::invalid_argument("invalid terminal session state");
    }
    std::string protocol;
    protocol.append(TERMINAL_SESSION_HEADER).push_back('\n');
    protocol.append("SESSION\t").append(state.session_token).push_back('\n');
    protocol.append("OWNER\t").append(OWNER).push_back('\n');
    protocol.append("TERMINAL\t")
        .append(terminal_name(state.terminal_state))
        .push_back('\n');
    protocol.append("COVERAGE\t")
        .append(coverage_name(state.coverage))
        .push_back('\n');
    protocol.append("MAKEPKG_OUTCOME\t")
        .append(outcome_name(state.makepkg_outcome))
        .push_back('\n');
    protocol.append("MAKEPKG_EXIT\t")
        .append(state.makepkg_exit_code.has_value()
                    ? std::to_string(*state.makepkg_exit_code)
                    : "None")
        .push_back('\n');
    protocol.append("TRANSACTION_COUNT\t")
        .append(std::to_string(state.transaction_count))
        .push_back('\n');
    protocol.append(END_RECORD).push_back('\n');
    return protocol;
}

MakepkgSyncdepsTerminalSessionStateResult
parse_makepkg_syncdeps_terminal_session_state(std::string_view protocol) {
    const ProtocolLinesResult split = split_protocol_lines(protocol);
    if(const auto* failure =
           std::get_if<MakepkgSyncdepsAdapterProtocolFailure>(&split);
       failure != nullptr) {
        return *failure;
    }
    const auto& lines = std::get<std::vector<std::string_view>>(split);
    if(lines.size() != 9 || lines[0] != TERMINAL_SESSION_HEADER ||
       lines.back() != END_RECORD) {
        return fail<MakepkgSyncdepsTerminalSessionState>(
            lines.empty() || lines[0] != TERMINAL_SESSION_HEADER
                ? MakepkgSyncdepsAdapterProtocolIssueKind::InvalidHeader
                : MakepkgSyncdepsAdapterProtocolIssueKind::TruncatedProtocol);
    }
    const auto token = record_value(lines[1], "SESSION\t");
    const auto owner = record_value(lines[2], "OWNER\t");
    const auto terminal = record_value(lines[3], "TERMINAL\t");
    const auto coverage = record_value(lines[4], "COVERAGE\t");
    const auto outcome = record_value(lines[5], "MAKEPKG_OUTCOME\t");
    const auto exit = record_value(lines[6], "MAKEPKG_EXIT\t");
    const auto count = record_value(lines[7], "TRANSACTION_COUNT\t");
    const auto parsed_terminal =
        terminal.has_value() ? parse_terminal_name(*terminal) : std::nullopt;
    const auto parsed_coverage =
        coverage.has_value() ? parse_coverage_name(*coverage) : std::nullopt;
    const auto parsed_outcome =
        outcome.has_value() ? parse_outcome_name(*outcome) : std::nullopt;
    std::optional<int> parsed_exit;
    bool exit_valid = false;
    if(exit.has_value() && *exit == "None") {
        exit_valid = true;
    } else if(exit.has_value()) {
        parsed_exit = parse_exit_code(*exit);
        exit_valid = parsed_exit.has_value();
    }
    const auto parsed_count =
        count.has_value() ? parse_unsigned_integer<std::size_t>(*count)
                          : std::nullopt;
    if(!token.has_value() || !is_valid_makepkg_syncdeps_adapter_token(*token) ||
       !owner.has_value() || *owner != OWNER ||
       !parsed_terminal.has_value() || !parsed_coverage.has_value() ||
       !parsed_outcome.has_value() || !exit_valid ||
       !parsed_count.has_value() || *parsed_count > 2 ||
       !has_valid_outcome_shape(*parsed_outcome, parsed_exit) ||
       ((*parsed_terminal == MakepkgSyncdepsTerminalState::Complete) !=
        (*parsed_coverage == MakepkgSyncdepsAdapterCoverage::Complete))) {
        return fail<MakepkgSyncdepsTerminalSessionState>(
            !owner.has_value() || *owner != OWNER
                ? MakepkgSyncdepsAdapterProtocolIssueKind::InvalidOwner
                : MakepkgSyncdepsAdapterProtocolIssueKind::InvalidTerminalState);
    }
    return MakepkgSyncdepsTerminalSessionState{
        std::string(*token), *parsed_terminal, *parsed_coverage,
        *parsed_outcome, parsed_exit, *parsed_count};
}

std::string serialize_makepkg_syncdeps_session_prepare_response(
    const MakepkgSyncdepsSessionPrepareResponse& response) {
    require_token(response.session_token, "invalid session token");
    if(response.invoking_uid == 0) {
        throw std::invalid_argument("invalid invoking uid");
    }
    std::string protocol;
    protocol.append(SESSION_PREPARE_RESPONSE_HEADER).push_back('\n');
    protocol.append("SESSION\t").append(response.session_token).push_back('\n');
    protocol.append("OWNER\t").append(OWNER).push_back('\n');
    protocol.append("INVOKING_UID\t")
        .append(std::to_string(response.invoking_uid))
        .push_back('\n');
    protocol.append(END_RECORD).push_back('\n');
    return protocol;
}

MakepkgSyncdepsSessionPrepareResponseResult
parse_makepkg_syncdeps_session_prepare_response(std::string_view protocol) {
    const ProtocolLinesResult split = split_protocol_lines(protocol);
    if(const auto* failure =
           std::get_if<MakepkgSyncdepsAdapterProtocolFailure>(&split);
       failure != nullptr) {
        return *failure;
    }
    const auto& lines = std::get<std::vector<std::string_view>>(split);
    if(lines.size() != 5 || lines[0] != SESSION_PREPARE_RESPONSE_HEADER ||
       lines.back() != END_RECORD) {
        return fail<MakepkgSyncdepsSessionPrepareResponse>(
            lines.empty() || lines[0] != SESSION_PREPARE_RESPONSE_HEADER
                ? MakepkgSyncdepsAdapterProtocolIssueKind::InvalidHeader
                : MakepkgSyncdepsAdapterProtocolIssueKind::TruncatedProtocol);
    }
    const auto token = record_value(lines[1], "SESSION\t");
    const auto owner = record_value(lines[2], "OWNER\t");
    const auto uid = record_value(lines[3], "INVOKING_UID\t");
    const auto parsed_uid =
        uid.has_value() ? parse_unsigned_integer<std::uint32_t>(*uid)
                        : std::nullopt;
    if(!token.has_value() || !is_valid_makepkg_syncdeps_adapter_token(*token) ||
       !owner.has_value() || *owner != OWNER || !parsed_uid.has_value() ||
       *parsed_uid == 0) {
        return fail<MakepkgSyncdepsSessionPrepareResponse>(
            !owner.has_value() || *owner != OWNER
                ? MakepkgSyncdepsAdapterProtocolIssueKind::InvalidOwner
                : MakepkgSyncdepsAdapterProtocolIssueKind::UnexpectedRecord);
    }
    return MakepkgSyncdepsSessionPrepareResponse{
        std::string(*token), *parsed_uid};
}

std::string serialize_makepkg_syncdeps_transaction_prepare_response(
    const MakepkgSyncdepsTransactionPrepareResponse& response) {
    require_token(response.session_token, "invalid session token");
    require_token(response.transaction_token, "invalid transaction token");
    require_ordinal(response.ordinal);
    std::string protocol;
    protocol.append(TRANSACTION_PREPARE_RESPONSE_HEADER).push_back('\n');
    protocol.append("SESSION\t").append(response.session_token).push_back('\n');
    protocol.append("OWNER\t").append(OWNER).push_back('\n');
    protocol.append("ORDINAL\t").append(std::to_string(response.ordinal)).push_back('\n');
    protocol.append("TRANSACTION_TOKEN\t")
        .append(response.transaction_token)
        .push_back('\n');
    protocol.append(END_RECORD).push_back('\n');
    return protocol;
}

MakepkgSyncdepsTransactionPrepareResponseResult
parse_makepkg_syncdeps_transaction_prepare_response(
    std::string_view protocol) {
    const ProtocolLinesResult split = split_protocol_lines(protocol);
    if(const auto* failure =
           std::get_if<MakepkgSyncdepsAdapterProtocolFailure>(&split);
       failure != nullptr) {
        return *failure;
    }
    const auto& lines = std::get<std::vector<std::string_view>>(split);
    if(lines.size() != 6 ||
       lines[0] != TRANSACTION_PREPARE_RESPONSE_HEADER ||
       lines.back() != END_RECORD) {
        return fail<MakepkgSyncdepsTransactionPrepareResponse>(
            lines.empty() || lines[0] != TRANSACTION_PREPARE_RESPONSE_HEADER
                ? MakepkgSyncdepsAdapterProtocolIssueKind::InvalidHeader
                : MakepkgSyncdepsAdapterProtocolIssueKind::TruncatedProtocol);
    }
    const auto token = record_value(lines[1], "SESSION\t");
    const auto owner = record_value(lines[2], "OWNER\t");
    const auto ordinal = record_value(lines[3], "ORDINAL\t");
    const auto transaction_token =
        record_value(lines[4], "TRANSACTION_TOKEN\t");
    const auto parsed_ordinal =
        ordinal.has_value()
            ? parse_unsigned_integer<std::size_t>(*ordinal)
            : std::nullopt;
    if(!token.has_value() || !is_valid_makepkg_syncdeps_adapter_token(*token) ||
       !owner.has_value() || *owner != OWNER ||
       !parsed_ordinal.has_value() || *parsed_ordinal == 0 ||
       *parsed_ordinal > 2 || !transaction_token.has_value() ||
       !is_valid_makepkg_syncdeps_adapter_token(*transaction_token)) {
        return fail<MakepkgSyncdepsTransactionPrepareResponse>(
            !owner.has_value() || *owner != OWNER
                ? MakepkgSyncdepsAdapterProtocolIssueKind::InvalidOwner
                : MakepkgSyncdepsAdapterProtocolIssueKind::UnexpectedRecord);
    }
    return MakepkgSyncdepsTransactionPrepareResponse{
        std::string(*token), *parsed_ordinal,
        std::string(*transaction_token)};
}

std::string serialize_makepkg_syncdeps_session_manifest(
    const MakepkgSyncdepsSessionManifest& manifest) {
    const auto& prepared = manifest.prepared;
    const auto& binding = manifest.binding;
    const auto& terminal = manifest.terminal;
    require_token(prepared.session_token, "invalid session token");
    if(binding.session_token != prepared.session_token ||
       terminal.session_token != prepared.session_token ||
       terminal.transaction_count != manifest.transactions.size() ||
       manifest.transactions.size() > 2 || prepared.invoking_uid == 0 ||
       prepared.launcher.uid != 0 ||
       prepared.supervisor.uid != 0 ||
       binding.child.uid != prepared.invoking_uid ||
       binding.transaction_adapter.uid != prepared.invoking_uid ||
       !has_valid_outcome_shape(
           terminal.makepkg_outcome, terminal.makepkg_exit_code)) {
        throw std::invalid_argument("invalid makepkg syncdeps session manifest");
    }
    std::string protocol;
    protocol.append(SESSION_MANIFEST_HEADER).push_back('\n');
    protocol.append("SESSION\t").append(prepared.session_token).push_back('\n');
    protocol.append("OWNER\t").append(OWNER).push_back('\n');
    protocol.append("INVOKING_UID\t")
        .append(std::to_string(prepared.invoking_uid))
        .push_back('\n');
    protocol.append("INSTALLED_DEVICE\t")
        .append(std::to_string(prepared.installed_executable.device))
        .push_back('\n');
    protocol.append("INSTALLED_INODE\t")
        .append(std::to_string(prepared.installed_executable.inode))
        .push_back('\n');
    append_process_identity(protocol, "LAUNCHER", prepared.launcher);
    append_process_identity(protocol, "SUPERVISOR", prepared.supervisor);
    append_process_identity(protocol, "MAKEPKG_CHILD", binding.child);
    append_process_identity(
        protocol, "TRANSACTION_ADAPTER", binding.transaction_adapter);
    protocol.append(
        "PROCESS_BINDING\tRootOwnedLauncherAndExactRoleChannels\n");
    protocol.append("TERMINAL\t")
        .append(terminal_name(terminal.terminal_state))
        .push_back('\n');
    protocol.append("COVERAGE\t")
        .append(coverage_name(terminal.coverage))
        .push_back('\n');
    protocol.append("MAKEPKG_OUTCOME\t")
        .append(outcome_name(terminal.makepkg_outcome))
        .push_back('\n');
    protocol.append("MAKEPKG_EXIT\t")
        .append(terminal.makepkg_exit_code.has_value()
                    ? std::to_string(*terminal.makepkg_exit_code)
                    : "None")
        .push_back('\n');
    protocol.append("EVIDENCE\tSynthetic\n");
    protocol.append("TRANSACTION_COUNT\t")
        .append(std::to_string(terminal.transaction_count))
        .push_back('\n');
    for(const auto& transaction : manifest.transactions) {
        append_transaction_records(protocol, transaction);
    }
    protocol.append(END_RECORD).push_back('\n');
    if(protocol.size() > MAKEPKG_SYNCDEPS_ADAPTER_MAXIMUM_BYTES) {
        throw std::invalid_argument("oversized makepkg syncdeps manifest");
    }
    return protocol;
}

MakepkgSyncdepsSessionManifestResult
parse_makepkg_syncdeps_session_manifest(std::string_view protocol) {
    const ProtocolLinesResult split = split_protocol_lines(protocol);
    if(const auto* failure =
           std::get_if<MakepkgSyncdepsAdapterProtocolFailure>(&split);
       failure != nullptr) {
        return *failure;
    }
    const auto& lines = std::get<std::vector<std::string_view>>(split);
    if(lines.size() < 30 || lines[0] != SESSION_MANIFEST_HEADER ||
       lines.back() != END_RECORD) {
        return fail<MakepkgSyncdepsSessionManifest>(
            lines.empty() || lines[0] != SESSION_MANIFEST_HEADER
                ? MakepkgSyncdepsAdapterProtocolIssueKind::InvalidHeader
                : MakepkgSyncdepsAdapterProtocolIssueKind::TruncatedProtocol);
    }
    std::size_t index = 1;
    const auto session = record_value(lines[index++], "SESSION\t");
    const auto owner = record_value(lines[index++], "OWNER\t");
    const auto invoking_uid =
        record_value(lines[index++], "INVOKING_UID\t");
    const auto installed_device =
        record_value(lines[index++], "INSTALLED_DEVICE\t");
    const auto installed_inode =
        record_value(lines[index++], "INSTALLED_INODE\t");
    const auto launcher = parse_process_identity(lines, index, "LAUNCHER");
    const auto supervisor =
        parse_process_identity(lines, index, "SUPERVISOR");
    const auto child =
        parse_process_identity(lines, index, "MAKEPKG_CHILD");
    const auto transaction_adapter =
        parse_process_identity(lines, index, "TRANSACTION_ADAPTER");
    const auto process_binding =
        index < lines.size()
            ? record_value(lines[index++], "PROCESS_BINDING\t")
            : std::nullopt;
    const auto terminal = index < lines.size()
                              ? record_value(lines[index++], "TERMINAL\t")
                              : std::nullopt;
    const auto coverage = index < lines.size()
                              ? record_value(lines[index++], "COVERAGE\t")
                              : std::nullopt;
    const auto makepkg_outcome =
        index < lines.size()
            ? record_value(lines[index++], "MAKEPKG_OUTCOME\t")
            : std::nullopt;
    const auto makepkg_exit =
        index < lines.size()
            ? record_value(lines[index++], "MAKEPKG_EXIT\t")
            : std::nullopt;
    const auto evidence = index < lines.size()
                              ? record_value(lines[index++], "EVIDENCE\t")
                              : std::nullopt;
    const auto transaction_count =
        index < lines.size()
            ? record_value(lines[index++], "TRANSACTION_COUNT\t")
            : std::nullopt;

    const auto parsed_uid =
        invoking_uid.has_value()
            ? parse_unsigned_integer<std::uint32_t>(*invoking_uid)
            : std::nullopt;
    const auto parsed_installed_device =
        installed_device.has_value()
            ? parse_unsigned_integer<std::uint64_t>(*installed_device)
            : std::nullopt;
    const auto parsed_installed_inode =
        installed_inode.has_value()
            ? parse_unsigned_integer<std::uint64_t>(*installed_inode)
            : std::nullopt;
    const auto parsed_terminal =
        terminal.has_value() ? parse_terminal_name(*terminal) : std::nullopt;
    const auto parsed_coverage =
        coverage.has_value() ? parse_coverage_name(*coverage) : std::nullopt;
    const auto parsed_outcome =
        makepkg_outcome.has_value()
            ? parse_outcome_name(*makepkg_outcome)
            : std::nullopt;
    std::optional<int> parsed_exit;
    bool exit_valid = false;
    if(makepkg_exit.has_value() && *makepkg_exit == "None") {
        exit_valid = true;
    } else if(makepkg_exit.has_value()) {
        parsed_exit = parse_exit_code(*makepkg_exit);
        exit_valid = parsed_exit.has_value();
    }
    const auto parsed_count =
        transaction_count.has_value()
            ? parse_unsigned_integer<std::size_t>(*transaction_count)
            : std::nullopt;
    if(!session.has_value() ||
       !is_valid_makepkg_syncdeps_adapter_token(*session) ||
       !owner.has_value() || *owner != OWNER || !parsed_uid.has_value() ||
       *parsed_uid == 0 || !parsed_installed_device.has_value() ||
       *parsed_installed_device == 0 || !parsed_installed_inode.has_value() ||
       *parsed_installed_inode == 0 || !launcher.has_value() ||
       !supervisor.has_value() || !child.has_value() ||
       !transaction_adapter.has_value() || launcher->uid != 0 ||
       supervisor->uid != 0 || child->uid != *parsed_uid ||
       transaction_adapter->uid != *parsed_uid ||
       !process_binding.has_value() ||
       *process_binding !=
           "RootOwnedLauncherAndExactRoleChannels" ||
       !parsed_terminal.has_value() || !parsed_coverage.has_value() ||
       !parsed_outcome.has_value() || !exit_valid ||
       !has_valid_outcome_shape(*parsed_outcome, parsed_exit) ||
       !evidence.has_value() || *evidence != "Synthetic" ||
       !parsed_count.has_value() || *parsed_count > 2 ||
       ((*parsed_terminal == MakepkgSyncdepsTerminalState::Complete) !=
        (*parsed_coverage == MakepkgSyncdepsAdapterCoverage::Complete))) {
        return fail<MakepkgSyncdepsSessionManifest>(
            !owner.has_value() || *owner != OWNER
                ? MakepkgSyncdepsAdapterProtocolIssueKind::InvalidOwner
                : MakepkgSyncdepsAdapterProtocolIssueKind::UnexpectedRecord);
    }

    std::vector<MakepkgSyncdepsTransactionManifestEntry> transactions;
    std::set<std::size_t> ordinals;
    std::set<std::string> transaction_tokens;
    while(index + 1U < lines.size()) {
        if(lines[index++] != TRANSACTION_BEGIN_RECORD ||
           index + 6U >= lines.size()) {
            return fail<MakepkgSyncdepsSessionManifest>(
                MakepkgSyncdepsAdapterProtocolIssueKind::UnexpectedRecord);
        }
        const auto ordinal = record_value(lines[index++], "ORDINAL\t");
        const auto transaction_token =
            record_value(lines[index++], "TRANSACTION_TOKEN\t");
        std::vector<std::string> specifications;
        while(index < lines.size() && lines[index].starts_with("SPEC_HEX\t")) {
            const auto encoded = record_value(lines[index++], "SPEC_HEX\t");
            const auto decoded =
                encoded.has_value() ? hex_decode(*encoded) : std::nullopt;
            if(!decoded.has_value() ||
               !is_valid_makepkg_syncdeps_dependency_specification(*decoded)) {
                return fail<MakepkgSyncdepsSessionManifest>(
                    MakepkgSyncdepsAdapterProtocolIssueKind::InvalidDependencySpecification);
            }
            specifications.push_back(*decoded);
        }
        if(index + 3U >= lines.size()) {
            return fail<MakepkgSyncdepsSessionManifest>(
                MakepkgSyncdepsAdapterProtocolIssueKind::TruncatedProtocol);
        }
        const auto observation =
            record_value(lines[index++], "OBSERVATION\t");
        const auto command_outcome =
            record_value(lines[index++], "COMMAND_OUTCOME\t");
        const auto command_exit =
            record_value(lines[index++], "COMMAND_EXIT\t");
        if(lines[index++] != TRANSACTION_END_RECORD) {
            return fail<MakepkgSyncdepsSessionManifest>(
                MakepkgSyncdepsAdapterProtocolIssueKind::UnexpectedRecord);
        }
        const auto parsed_ordinal =
            ordinal.has_value()
                ? parse_unsigned_integer<std::size_t>(*ordinal)
                : std::nullopt;
        const auto parsed_observation =
            observation.has_value() ? parse_observation_name(*observation)
                                    : std::nullopt;
        const auto parsed_command_outcome =
            command_outcome.has_value()
                ? parse_outcome_name(*command_outcome)
                : std::nullopt;
        std::optional<int> parsed_command_exit;
        bool command_exit_valid = false;
        if(command_exit.has_value() && *command_exit == "None") {
            command_exit_valid = true;
        } else if(command_exit.has_value()) {
            parsed_command_exit = parse_exit_code(*command_exit);
            command_exit_valid = parsed_command_exit.has_value();
        }
        if(!parsed_ordinal.has_value() || *parsed_ordinal == 0 ||
           *parsed_ordinal > 2 || !transaction_token.has_value() ||
           !is_valid_makepkg_syncdeps_adapter_token(*transaction_token) ||
           specifications.empty() || !parsed_observation.has_value() ||
           !parsed_command_outcome.has_value() || !command_exit_valid ||
           !has_valid_outcome_shape(
               *parsed_command_outcome, parsed_command_exit)) {
            return fail<MakepkgSyncdepsSessionManifest>(
                MakepkgSyncdepsAdapterProtocolIssueKind::UnexpectedRecord);
        }
        if(!ordinals.insert(*parsed_ordinal).second) {
            return fail<MakepkgSyncdepsSessionManifest>(
                MakepkgSyncdepsAdapterProtocolIssueKind::DuplicateOrdinal);
        }
        if(!transaction_tokens.insert(std::string(*transaction_token)).second) {
            return fail<MakepkgSyncdepsSessionManifest>(
                MakepkgSyncdepsAdapterProtocolIssueKind::DuplicateTransactionToken);
        }
        if(*parsed_ordinal != transactions.size() + 1U) {
            return fail<MakepkgSyncdepsSessionManifest>(
                MakepkgSyncdepsAdapterProtocolIssueKind::InvalidOrdinal);
        }
        MakepkgSyncdepsPreparedTransactionState prepared_transaction{
            std::string(*session), *parsed_ordinal,
            std::string(*transaction_token), std::move(specifications)};
        transactions.push_back(MakepkgSyncdepsTransactionManifestEntry{
            prepared_transaction,
            MakepkgSyncdepsTransactionObservationState{
                std::string(*session), *parsed_ordinal,
                std::string(*transaction_token), *parsed_observation},
            MakepkgSyncdepsTransactionOutcomeState{
                std::string(*session), *parsed_ordinal,
                std::string(*transaction_token), *parsed_command_outcome,
                parsed_command_exit}});
    }
    if(index != lines.size() - 1U || transactions.size() != *parsed_count) {
        return fail<MakepkgSyncdepsSessionManifest>(
            MakepkgSyncdepsAdapterProtocolIssueKind::UnexpectedRecord);
    }
    MakepkgSyncdepsPreparedSessionState prepared_state{
        std::string(*session), *parsed_uid, {*parsed_installed_device, *parsed_installed_inode}, *launcher, *supervisor};
    return MakepkgSyncdepsSessionManifest{
        std::move(prepared_state),
        MakepkgSyncdepsBoundChildState{
            std::string(*session), *child, *transaction_adapter},
        MakepkgSyncdepsTerminalSessionState{
            std::string(*session), *parsed_terminal, *parsed_coverage,
            *parsed_outcome, parsed_exit, *parsed_count},
        MakepkgSyncdepsEvidenceKind::Synthetic,
        std::move(transactions)};
}
