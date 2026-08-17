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

ConfirmationInputParseResult parse_confirmation_input(
        std::string_view input, ConfirmationDefault default_answer);

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
