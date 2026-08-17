#include "interactive_confirmation.hpp"

#include "localization.hpp"
#include "logging.hpp"

#include <iostream>
#include <optional>
#include <stdexcept>

#include <unistd.h>

namespace {

bool is_ascii_whitespace(char ch) noexcept {
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' ||
           ch == '\f' || ch == '\v';
}

char ascii_lower(char ch) noexcept {
    if(ch >= 'A' && ch <= 'Z') {
        return static_cast<char>(ch - 'A' + 'a');
    }
    return ch;
}

std::string normalize_confirmation_input(std::string_view input) {
    std::size_t first = 0;
    while(first < input.size() && is_ascii_whitespace(input[first])) {
        ++first;
    }
    std::size_t last = input.size();
    while(last > first && is_ascii_whitespace(input[last - 1])) {
        --last;
    }

    std::string normalized;
    normalized.reserve(last - first);
    for(std::size_t index = first; index < last; ++index) {
        normalized.push_back(ascii_lower(input[index]));
    }
    return normalized;
}

std::string prompt_suffix(ConfirmationDefault default_answer) {
    // NO_TRANSLATE: These tokens define the accepted/default prompt input.
    switch(default_answer) {
    case ConfirmationDefault::Yes:
        return "[Y/n]";
    case ConfirmationDefault::No:
        return "[y/N]";
    case ConfirmationDefault::None:
        return "[y/n]";
    }
    throw std::logic_error(localization::translate_message(
            "Unknown interactive confirmation default."));
}

std::optional<bool> default_value(ConfirmationDefault default_answer) {
    switch(default_answer) {
    case ConfirmationDefault::Yes:
        return true;
    case ConfirmationDefault::No:
        return false;
    case ConfirmationDefault::None:
        return std::nullopt;
    }
    return std::nullopt;
}

ConfirmationResult default_result(
        bool value, ConfirmationDecisionOrigin origin) {
    if(value) return ConfirmationAccepted{origin};
    return ConfirmationDeclined{origin};
}

} // namespace

ConfirmationInputParseResult parse_confirmation_input(
        std::string_view input, ConfirmationDefault default_answer) {
    const std::string normalized = normalize_confirmation_input(input);
    if(normalized.empty()) {
        const std::optional<bool> value = default_value(default_answer);
        if(!value.has_value()) return InvalidConfirmationInput{};
        if(value.value()) {
            return ConfirmationAccepted{
                    ConfirmationDecisionOrigin::Default};
        }
        return ConfirmationDeclined{
                ConfirmationDecisionOrigin::Default};
    }
    if(normalized == "y" || normalized == "yes") {
        return ConfirmationAccepted{
                ConfirmationDecisionOrigin::ExplicitToken};
    }
    if(normalized == "n" || normalized == "no") {
        return ConfirmationDeclined{
                ConfirmationDecisionOrigin::ExplicitToken};
    }
    if(normalized == "q" || normalized == "quit" ||
       normalized == "cancel") {
        return ConfirmationCancelled{
                ConfirmationCancellationReason::ExplicitToken};
    }
    return InvalidConfirmationInput{};
}

ConfirmationResult request_confirmation(
        const std::string& question, ConfirmationDefault default_answer,
        bool no_confirm) {
    return request_confirmation(
            question, default_answer, no_confirm,
            isatty(STDIN_FILENO) != 0, std::cin, std::cout);
}

ConfirmationResult request_confirmation(
        const std::string& question, ConfirmationDefault default_answer,
        bool no_confirm, bool is_interactive_input, std::istream& input,
        std::ostream& output) {
    const std::optional<bool> value = default_value(default_answer);
    if(no_confirm) {
        // POLICY: --noconfirm does not authorize a prompt with no default.
        if(!value.has_value()) {
            return ConfirmationUnavailable{
                    ConfirmationUnavailableReason::NoConfirm};
        }
        if(value.value()) {
            // TRANSLATORS: The placeholders are the literal --noconfirm option and a complete prompt question.
            Logger::info(localization::format_translated_message(
                    "Skipping prompt ({}): {} -> yes",
                    "--noconfirm", question));
        } else {
            // TRANSLATORS: The placeholders are the literal --noconfirm option and a complete prompt question.
            Logger::info(localization::format_translated_message(
                    "Skipping prompt ({}): {} -> no",
                    "--noconfirm", question));
        }
        return default_result(
                value.value(), ConfirmationDecisionOrigin::NoConfirm);
    }

    if(!is_interactive_input) {
        // LANDMINE: A non-interactive stream may use only the safe No default.
        if(value.has_value() && !value.value()) {
            // TRANSLATORS: The placeholders are the literal stdin identity and a complete prompt question.
            Logger::info(localization::format_translated_message(
                    "Skipping prompt (non-interactive {}): {} -> no",
                    "stdin", question));
            return ConfirmationDeclined{
                    ConfirmationDecisionOrigin::NonInteractiveDefault};
        }
        return ConfirmationUnavailable{
                ConfirmationUnavailableReason::NonInteractiveInput};
    }

    for(;;) {
        // NO_TRANSLATE: Prompt framing, suffix, and response tokens are fixed
        // UI syntax; question and the cancel instruction are translated.
        output << ":: " << question << " " << prompt_suffix(default_answer)
               << " "
               << localization::format_translated_message(
                          "(type {} to cancel)", "q/quit/cancel")
               << " ";
        std::string line;
        if(!std::getline(input, line)) {
            if(input.eof() && !input.bad()) {
                return ConfirmationCancelled{
                        ConfirmationCancellationReason::EndOfInput};
            }
            return ConfirmationInputFailure{};
        }

        ConfirmationInputParseResult parsed =
                parse_confirmation_input(line, default_answer);
        if(const auto* accepted =
                   std::get_if<ConfirmationAccepted>(&parsed)) {
            return *accepted;
        }
        if(const auto* declined =
                   std::get_if<ConfirmationDeclined>(&parsed)) {
            return *declined;
        }
        if(const auto* cancelled =
                   std::get_if<ConfirmationCancelled>(&parsed)) {
            return *cancelled;
        }

        Logger::warn(localization::translate_message(
                "Please answer yes, no, or cancel."));
    }
}

std::string confirmation_stop_diagnostic(const ConfirmationResult& result) {
    if(std::holds_alternative<ConfirmationDeclined>(result)) {
        return localization::translate_message(
                "The required confirmation was declined. Any earlier completed phases remain unchanged.");
    }
    if(const auto* cancelled =
               std::get_if<ConfirmationCancelled>(&result)) {
        if(cancelled->reason ==
           ConfirmationCancellationReason::EndOfInput) {
            return localization::translate_message(
                    "The current operation was cancelled because interactive input ended. Any earlier completed phases remain unchanged.");
        }
        return localization::translate_message(
                "The current operation was cancelled at an interactive confirmation. Any earlier completed phases remain unchanged.");
    }
    if(const auto* unavailable =
               std::get_if<ConfirmationUnavailable>(&result)) {
        if(unavailable->reason ==
           ConfirmationUnavailableReason::NoConfirm) {
            // TRANSLATORS: The placeholder is the literal --noconfirm option.
            return localization::format_translated_message(
                    "The required confirmation is unavailable while {} is set. Any earlier completed phases remain unchanged.",
                    "--noconfirm");
        }
        return localization::translate_message(
                "The required confirmation is unavailable because standard input is non-interactive. Any earlier completed phases remain unchanged.");
    }
    if(std::holds_alternative<ConfirmationInputFailure>(result)) {
        return localization::translate_message(
                "Failed to read the interactive confirmation input.");
    }
    throw std::logic_error(localization::translate_message(
            "An accepted confirmation cannot stop an operation."));
}
