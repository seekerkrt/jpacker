#include "interactive_confirmation.hpp"

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

namespace {

static_assert(!std::is_default_constructible_v<
              ExplicitConfirmationAcceptance>);
static_assert(!std::is_copy_constructible_v<
              ExplicitConfirmationAcceptance>);
static_assert(std::is_move_constructible_v<
              ExplicitConfirmationAcceptance>);
static_assert(!std::is_constructible_v<
              ExplicitConfirmationAcceptance,
              ConfirmationDecisionOrigin>);
static_assert(!std::is_constructible_v<
              ExplicitConfirmationAcceptance,
              ConfirmationAccepted>);

void expect(bool condition, const std::string& message) {
    if(!condition) throw std::runtime_error(message);
}

template<typename Expected>
Expected expect_result(
        const ConfirmationResult& result, std::string_view context) {
    const Expected* value = std::get_if<Expected>(&result);
    expect(
            value != nullptr,
            std::string(context) + ": unexpected confirmation result");
    return *value;
}

template<typename Expected>
Expected expect_parse_result(
        const ConfirmationInputParseResult& result,
        std::string_view context) {
    const Expected* value = std::get_if<Expected>(&result);
    expect(
            value != nullptr,
            std::string(context) + ": unexpected parser result");
    return *value;
}

std::size_t count_occurrences(
        const std::string& text, std::string_view needle) {
    std::size_t count = 0;
    std::size_t position = 0;
    while((position = text.find(needle, position)) != std::string::npos) {
        ++count;
        position += needle.size();
    }
    return count;
}

ConfirmationResult run_session(
        ConfirmationDefault default_answer, const std::string& input_text,
        std::string* output_text = nullptr) {
    std::istringstream input(input_text);
    std::ostringstream output;
    ConfirmationResult result = request_confirmation(
            "Continue?", default_answer, false, true, input, output);
    if(output_text != nullptr) *output_text = output.str();
    return result;
}

void expect_explicit_tokens(ConfirmationDefault default_answer) {
    for(const char* token : {"y", "yes", " Y ", "YES"}) {
        const ConfirmationAccepted& accepted = expect_parse_result<
                ConfirmationAccepted>(
                parse_confirmation_input(token, default_answer), token);
        expect(
                accepted.origin ==
                        ConfirmationDecisionOrigin::ExplicitToken,
                std::string(token) + ": yes origin differs");
    }
    for(const char* token : {"n", "no", " N ", "NO"}) {
        const ConfirmationDeclined& declined = expect_parse_result<
                ConfirmationDeclined>(
                parse_confirmation_input(token, default_answer), token);
        expect(
                declined.origin ==
                        ConfirmationDecisionOrigin::ExplicitToken,
                std::string(token) + ": no origin differs");
    }
    for(const char* token : {
                "q", "quit", "cancel", " Q ", "QUIT", "CANCEL"}) {
        const ConfirmationCancelled& cancelled = expect_parse_result<
                ConfirmationCancelled>(
                parse_confirmation_input(token, default_answer), token);
        expect(
                cancelled.reason ==
                        ConfirmationCancellationReason::ExplicitToken,
                std::string(token) + ": cancel reason differs");
    }
}

void test_default_yes_matrix() {
    const ConfirmationAccepted& default_accepted = expect_parse_result<
            ConfirmationAccepted>(
            parse_confirmation_input("", ConfirmationDefault::Yes),
            "[Y/n] empty");
    expect(
            default_accepted.origin == ConfirmationDecisionOrigin::Default,
            "[Y/n] empty did not use the Yes default");
    expect_explicit_tokens(ConfirmationDefault::Yes);

    std::string retry_output;
    const ConfirmationAccepted& retry = expect_result<ConfirmationAccepted>(
            run_session(
                    ConfirmationDefault::Yes, "maybe\nyes\n",
                    &retry_output),
            "[Y/n] invalid then valid");
    expect(
            retry.origin == ConfirmationDecisionOrigin::ExplicitToken,
            "[Y/n] retry did not retain explicit origin");
    expect(
            count_occurrences(retry_output, "[Y/n]") == 2,
            "[Y/n] invalid input did not re-prompt");
    expect(
            retry_output.find("q/quit/cancel") != std::string::npos,
            "[Y/n] prompt does not expose cancel tokens");

    std::istringstream eof_input;
    std::ostringstream eof_output;
    const ConfirmationCancelled& eof = expect_result<
            ConfirmationCancelled>(
            request_confirmation(
                    "Continue?", ConfirmationDefault::Yes, false, true,
                    eof_input, eof_output),
            "[Y/n] EOF");
    expect(
            eof.reason == ConfirmationCancellationReason::EndOfInput,
            "[Y/n] EOF was not typed as EndOfInput cancellation");

    std::istringstream failed_input;
    failed_input.setstate(std::ios::badbit);
    std::ostringstream failed_output;
    expect_result<ConfirmationInputFailure>(
            request_confirmation(
                    "Continue?", ConfirmationDefault::Yes, false, true,
                    failed_input, failed_output),
            "[Y/n] stream failure");

    std::istringstream unused_input;
    std::ostringstream unused_output;
    const ConfirmationUnavailable& non_interactive = expect_result<
            ConfirmationUnavailable>(
            request_confirmation(
                    "Continue?", ConfirmationDefault::Yes, false, false,
                    unused_input, unused_output),
            "[Y/n] non-TTY");
    expect(
            non_interactive.reason ==
                    ConfirmationUnavailableReason::NonInteractiveInput,
            "[Y/n] non-TTY reason differs");

    const ConfirmationAccepted& no_confirm = expect_result<
            ConfirmationAccepted>(
            request_confirmation(
                    "Continue?", ConfirmationDefault::Yes, true, false,
                    unused_input, unused_output),
            "[Y/n] --noconfirm");
    expect(
            no_confirm.origin == ConfirmationDecisionOrigin::NoConfirm,
            "[Y/n] --noconfirm did not retain its origin");
}

void test_default_no_matrix() {
    const ConfirmationDeclined& default_declined = expect_parse_result<
            ConfirmationDeclined>(
            parse_confirmation_input("", ConfirmationDefault::No),
            "[y/N] empty");
    expect(
            default_declined.origin == ConfirmationDecisionOrigin::Default,
            "[y/N] empty did not use the No default");
    expect_explicit_tokens(ConfirmationDefault::No);

    std::string retry_output;
    expect_result<ConfirmationDeclined>(
            run_session(
                    ConfirmationDefault::No, "invalid\nno\n",
                    &retry_output),
            "[y/N] invalid then valid");
    expect(
            count_occurrences(retry_output, "[y/N]") == 2,
            "[y/N] invalid input did not re-prompt");

    std::istringstream unused_input;
    std::ostringstream unused_output;
    const ConfirmationDeclined& non_interactive = expect_result<
            ConfirmationDeclined>(
            request_confirmation(
                    "Continue?", ConfirmationDefault::No, false, false,
                    unused_input, unused_output),
            "[y/N] non-TTY");
    expect(
            non_interactive.origin ==
                    ConfirmationDecisionOrigin::NonInteractiveDefault,
            "[y/N] non-TTY did not retain safe-default origin");

    const ConfirmationDeclined& no_confirm = expect_result<
            ConfirmationDeclined>(
            request_confirmation(
                    "Continue?", ConfirmationDefault::No, true, true,
                    unused_input, unused_output),
            "[y/N] --noconfirm");
    expect(
            no_confirm.origin == ConfirmationDecisionOrigin::NoConfirm,
            "[y/N] --noconfirm did not retain its origin");
}

void test_no_default_matrix() {
    expect_parse_result<InvalidConfirmationInput>(
            parse_confirmation_input("", ConfirmationDefault::None),
            "[y/n] empty parser result");
    expect_explicit_tokens(ConfirmationDefault::None);

    std::string empty_retry_output;
    const ConfirmationDeclined& empty_retry = expect_result<
            ConfirmationDeclined>(
            run_session(
                    ConfirmationDefault::None, "\nn\n",
                    &empty_retry_output),
            "[y/n] empty then n");
    expect(
            empty_retry.origin ==
                    ConfirmationDecisionOrigin::ExplicitToken,
            "[y/n] empty retry did not require an explicit answer");
    expect(
            count_occurrences(empty_retry_output, "[y/n]") == 2,
            "[y/n] empty input did not re-prompt");

    std::string invalid_retry_output;
    expect_result<ConfirmationAccepted>(
            run_session(
                    ConfirmationDefault::None, "later\ny\n",
                    &invalid_retry_output),
            "[y/n] invalid then y");
    expect(
            count_occurrences(invalid_retry_output, "[y/n]") == 2,
            "[y/n] invalid input did not re-prompt");

    std::istringstream eof_input;
    std::ostringstream eof_output;
    const ConfirmationCancelled& eof = expect_result<
            ConfirmationCancelled>(
            request_confirmation(
                    "Continue?", ConfirmationDefault::None, false, true,
                    eof_input, eof_output),
            "[y/n] EOF");
    expect(
            eof.reason == ConfirmationCancellationReason::EndOfInput,
            "[y/n] EOF reason differs");

    std::istringstream failed_input;
    failed_input.setstate(std::ios::badbit);
    std::ostringstream failed_output;
    expect_result<ConfirmationInputFailure>(
            request_confirmation(
                    "Continue?", ConfirmationDefault::None, false, true,
                    failed_input, failed_output),
            "[y/n] stream failure");

    std::istringstream unused_input;
    std::ostringstream unused_output;
    const ConfirmationUnavailable& non_interactive = expect_result<
            ConfirmationUnavailable>(
            request_confirmation(
                    "Continue?", ConfirmationDefault::None, false, false,
                    unused_input, unused_output),
            "[y/n] non-TTY");
    expect(
            non_interactive.reason ==
                    ConfirmationUnavailableReason::NonInteractiveInput,
            "[y/n] non-TTY reason differs");

    const ConfirmationUnavailable& no_confirm = expect_result<
            ConfirmationUnavailable>(
            request_confirmation(
                    "Continue?", ConfirmationDefault::None, true, true,
                    unused_input, unused_output),
            "[y/n] --noconfirm");
    expect(
            no_confirm.reason == ConfirmationUnavailableReason::NoConfirm,
            "[y/n] --noconfirm reason differs");
}

void test_sealed_explicit_acceptance() {
    for(const std::string_view token : {
                std::string_view("y"), std::string_view("yes")}) {
        ExplicitConfirmationInputParseResult parsed =
                parse_explicit_confirmation_input(token);
        const auto* accepted =
                std::get_if<ExplicitConfirmationAcceptance>(&parsed);
        expect(accepted != nullptr && accepted->valid(),
                std::string(token) +
                        ": sealed explicit acceptance was not produced");
    }

    ExplicitConfirmationInputParseResult empty =
            parse_explicit_confirmation_input("");
    expect(std::holds_alternative<InvalidConfirmationInput>(empty),
            "No-default empty input produced sealed acceptance");

    for(const std::string_view token : {
                std::string_view("y"), std::string_view("yes")}) {
        std::istringstream input(std::string(token) + "\n");
        std::ostringstream output;
        ExplicitConfirmationResult result = request_explicit_confirmation(
                "Continue?", false, true, input, output);
        const auto* accepted =
                std::get_if<ExplicitConfirmationAcceptance>(&result);
        expect(accepted != nullptr && accepted->valid(),
                std::string(token) +
                        ": interactive session lost sealed acceptance");
    }

    std::istringstream unused_input;
    std::ostringstream unused_output;
    ExplicitConfirmationResult no_confirm = request_explicit_confirmation(
            "Continue?", true, true, unused_input, unused_output);
    const auto* no_confirm_unavailable =
            std::get_if<ConfirmationUnavailable>(&no_confirm);
    expect(no_confirm_unavailable != nullptr &&
                    no_confirm_unavailable->reason ==
                            ConfirmationUnavailableReason::NoConfirm,
            "--noconfirm produced sealed explicit acceptance");

    ExplicitConfirmationResult non_interactive =
            request_explicit_confirmation(
                    "Continue?", false, false, unused_input, unused_output);
    const auto* non_interactive_unavailable =
            std::get_if<ConfirmationUnavailable>(&non_interactive);
    expect(non_interactive_unavailable != nullptr &&
                    non_interactive_unavailable->reason ==
                            ConfirmationUnavailableReason::NonInteractiveInput,
            "Non-interactive input produced sealed explicit acceptance");

    ExplicitConfirmationInputParseResult movable =
            parse_explicit_confirmation_input("yes");
    auto* source = std::get_if<ExplicitConfirmationAcceptance>(&movable);
    expect(source != nullptr && source->valid(),
            "Move fixture did not contain sealed acceptance");
    ExplicitConfirmationAcceptance moved_to(std::move(*source));
    expect(!source->valid() && moved_to.valid(),
            "Moving sealed acceptance did not invalidate its source");
    ExplicitConfirmationAcceptance second_move(std::move(*source));
    expect(!second_move.valid(),
            "A moved-from sealed acceptance regained authority");
}

} // namespace

int main() {
    try {
        test_default_yes_matrix();
        test_default_no_matrix();
        test_no_default_matrix();
        test_sealed_explicit_acceptance();
        std::cout << "interactive confirmation tests: all checks passed\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr << "interactive confirmation test failure: "
                  << error.what() << '\n';
        return 1;
    }
}
