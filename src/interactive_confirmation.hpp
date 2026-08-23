#pragma once

#include <exception>
#include <iosfwd>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

enum class ConfirmationDefault {
    Yes,
    No,
    None,
};

enum class ConfirmationDecisionOrigin {
    ExplicitToken,
    Default,
    NoConfirm,
    NonInteractiveDefault,
};

enum class ConfirmationCancellationReason {
    ExplicitToken,
    EndOfInput,
};

enum class ConfirmationUnavailableReason {
    NonInteractiveInput,
    NoConfirm,
};

// A positive no-default confirmation recognized by the parser/session. Unlike
// ConfirmationAccepted, this is a sealed capability: callers cannot construct
// it from a public origin enum or relabel an automatic/default answer.
class ExplicitConfirmationAcceptance final {
public:
    ExplicitConfirmationAcceptance() = delete;
    ExplicitConfirmationAcceptance(
            const ExplicitConfirmationAcceptance&) = delete;
    ExplicitConfirmationAcceptance(
            ExplicitConfirmationAcceptance&& other) noexcept;
    ExplicitConfirmationAcceptance& operator=(
            const ExplicitConfirmationAcceptance&) = delete;
    ExplicitConfirmationAcceptance& operator=(
            ExplicitConfirmationAcceptance&& other) noexcept;
    ~ExplicitConfirmationAcceptance() = default;

    [[nodiscard]] bool valid() const noexcept;

private:
    // Exact non-inline parser boundary. Its definition revalidates raw y/yes
    // input before construction; there is no nameable authority class or
    // generic mint operation to complete in another translation unit.
    friend ExplicitConfirmationAcceptance
    parse_explicit_confirmation_acceptance(std::string_view input);

    explicit ExplicitConfirmationAcceptance(bool valid) noexcept;

    bool valid_ = false;
};

struct ConfirmationAccepted {
    ConfirmationDecisionOrigin origin;

    bool operator==(const ConfirmationAccepted&) const = default;
};

struct ConfirmationDeclined {
    ConfirmationDecisionOrigin origin;

    bool operator==(const ConfirmationDeclined&) const = default;
};

struct ConfirmationCancelled {
    ConfirmationCancellationReason reason;

    bool operator==(const ConfirmationCancelled&) const = default;
};

struct ConfirmationUnavailable {
    ConfirmationUnavailableReason reason;

    bool operator==(const ConfirmationUnavailable&) const = default;
};

struct ConfirmationInputFailure {
    bool operator==(const ConfirmationInputFailure&) const = default;
};

struct InvalidConfirmationInput {
    bool operator==(const InvalidConfirmationInput&) const = default;
};

using ConfirmationResult = std::variant<
        ConfirmationAccepted,
        ConfirmationDeclined,
        ConfirmationCancelled,
        ConfirmationUnavailable,
        ConfirmationInputFailure>;

using ConfirmationInputParseResult = std::variant<
        ConfirmationAccepted,
        ConfirmationDeclined,
        ConfirmationCancelled,
        InvalidConfirmationInput>;

using ExplicitConfirmationInputParseResult = std::variant<
        ExplicitConfirmationAcceptance,
        ConfirmationDeclined,
        ConfirmationCancelled,
        InvalidConfirmationInput>;

using ExplicitConfirmationResult = std::variant<
        ExplicitConfirmationAcceptance,
        ConfirmationDeclined,
        ConfirmationCancelled,
        ConfirmationUnavailable,
        ConfirmationInputFailure>;

ConfirmationInputParseResult parse_confirmation_input(
        std::string_view input, ConfirmationDefault default_answer);

// Fixed no-default parser. Only actual y/yes bytes can produce the sealed
// positive capability; empty/default/automatic decisions have no conversion.
ExplicitConfirmationInputParseResult parse_explicit_confirmation_input(
        std::string_view input);

ConfirmationResult request_confirmation(
        const std::string& question, ConfirmationDefault default_answer,
        bool no_confirm);

// Injected session for focused tests and non-production adapters. The caller
// supplies the already-observed TTY state so parser/default semantics do not
// depend on process-global stdin.
ConfirmationResult request_confirmation(
        const std::string& question, ConfirmationDefault default_answer,
        bool no_confirm, bool is_interactive_input, std::istream& input,
        std::ostream& output);

ExplicitConfirmationResult request_explicit_confirmation(
        const std::string& question, bool no_confirm);

ExplicitConfirmationResult request_explicit_confirmation(
        const std::string& question, bool no_confirm,
        bool is_interactive_input, std::istream& input, std::ostream& output);

std::string confirmation_stop_diagnostic(const ConfirmationResult& result);

// Typed propagation signal after a caller has projected and reported a
// non-accepted confirmation result. It is not a production/build failure and
// must be rethrown by generic exception adapters.
class ConfirmationOperationStopped final : public std::exception {
    ConfirmationResult result_;

public:
    explicit ConfirmationOperationStopped(
            ConfirmationResult result) noexcept
        : result_(std::move(result)) {
    }

    const ConfirmationResult& result() const noexcept {
        return result_;
    }

    const char* what() const noexcept override {
        return "Interactive confirmation stopped the current operation.";
    }
};
