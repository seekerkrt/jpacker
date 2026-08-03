#include "provider_selection.hpp"

#include <exception>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

void expect(bool condition, const std::string& message) {
    if(!condition) throw std::runtime_error(message);
}

ProvidedDependency repository_candidate(
        std::string package_version = "1.2.3-1") {
    return ProvidedDependency::from_repository(
            "extra", "shared-provider", "virtual-dependency",
            "virtual-dependency=1.2", std::move(package_version));
}

ProvidedDependency aur_candidate(
        std::string package_version = "2.4.0-1") {
    return ProvidedDependency::from_aur(
            "shared-provider", "shared-provider-base",
            "virtual-dependency", "virtual-dependency>=2.4",
            std::move(package_version));
}

std::vector<ProvidedDependency> candidates() {
    return {repository_candidate(), aur_candidate()};
}

ProvidedDependency repository_identity_candidate(
        const std::string& repository,
        const std::string& dependency,
        std::string version = "1.0-1") {
    return ProvidedDependency::from_repository(
            repository, "shared-provider", dependency,
            dependency + "=1", std::move(version));
}

ProvidedDependency aur_identity_candidate(
        const std::string& package_base,
        const std::string& dependency,
        std::string version = "1.0-1") {
    return ProvidedDependency::from_aur(
            "shared-provider", package_base, dependency,
            dependency + "=1", std::move(version));
}

ProvidedDependency decoy_candidate(const std::string& dependency) {
    return ProvidedDependency::from_repository(
            "extra", "decoy-provider", dependency,
            dependency + "=1", "1.0-1");
}

std::size_t occurrence_count(
        std::string_view text, std::string_view needle) {
    std::size_t count = 0;
    std::size_t offset = 0;
    while((offset = text.find(needle, offset)) != std::string_view::npos) {
        ++count;
        offset += needle.size();
    }
    return count;
}

void test_noninteractive_session_does_not_read_or_write() {
    std::istringstream input("2\n");
    std::ostringstream output;
    ProviderSelectionSession session(input, output, false);

    std::optional<ProvidedDependency> selected =
            session.select_provider("virtual-dependency>=1", candidates());

    expect(!selected.has_value(), "non-interactive session selected a provider");
    expect(input.tellg() == std::streampos(0), "non-interactive session read stdin");
    expect(output.str().empty(), "non-interactive session wrote a prompt");
}

void test_candidate_metadata_and_exact_number_selection() {
    std::istringstream input(" 2 \n");
    std::ostringstream output;
    ProviderSelectionSession session(input, output, true);

    std::optional<ProvidedDependency> selected =
            session.select_provider("virtual-dependency", candidates());

    expect(selected.has_value(), "numbered provider was not selected");
    expect(
            same_provider_identity(selected.value(), aur_candidate()),
            "numbered selection did not preserve source-aware identity");

    const std::string presentation = output.str();
    expect(
            presentation.find(
                    "1) source=repository package=shared-provider repository=extra "
                    "provided=virtual-dependency "
                    "provided-specification=virtual-dependency=1.2 "
                    "version=1.2.3-1") != std::string::npos,
            "repository candidate metadata was not presented");
    expect(
            presentation.find(
                    "2) source=AUR package=shared-provider "
                    "PackageBase=shared-provider-base "
                    "provided=virtual-dependency "
                    "provided-specification=virtual-dependency>=2.4 "
                    "version=2.4.0-1") != std::string::npos,
            "AUR candidate metadata was not presented");
    expect(
            presentation.find(
                    ":: Select a provider from [1-2], or press Enter / enter "
                    "q/quit/cancel to cancel: ") != std::string::npos,
            "translated provider prompt sentence was not presented");
}

void test_invalid_and_out_of_range_input_retries() {
    std::istringstream input("not-a-number\n0\n3\n1\n");
    std::ostringstream output;
    ProviderSelectionSession session(input, output, true);

    std::optional<ProvidedDependency> selected =
            session.select_provider("virtual-dependency", candidates());

    expect(selected.has_value(), "valid choice after retries was not selected");
    expect(
            same_provider_identity(selected.value(), repository_candidate()),
            "retry path selected the wrong candidate");
    expect(
            occurrence_count(
                    output.str(),
                    ":: Invalid choice. Enter a number from [1-2], or press "
                    "Enter / enter q/quit/cancel to cancel.") == 3,
            "invalid and out-of-range input did not retry exactly three times");
}

void test_cancel_inputs_return_no_selection() {
    const std::vector<std::string> cancel_inputs{
            "\n", "q\n", "QUIT\n", " cancel \n", ""};
    for(const std::string& cancel_input : cancel_inputs) {
        std::istringstream input(cancel_input);
        std::ostringstream output;
        ProviderSelectionSession session(input, output, true);

        std::optional<ProvidedDependency> selected =
                session.select_provider("virtual-dependency", candidates());
        expect(
                !selected.has_value(),
                "cancel or EOF unexpectedly selected a provider");
        expect(
                output.str().find(":: Invalid choice.") == std::string::npos,
                "cancel or EOF was treated as invalid input");
    }
}

void test_cancelled_dependency_does_not_prompt_or_read_again() {
    std::istringstream input("\n2\n");
    std::ostringstream output;
    ProviderSelectionSession session(input, output, true);

    std::optional<ProvidedDependency> first =
            session.select_provider("virtual-dependency>=1", candidates());
    expect(!first.has_value(), "cancel unexpectedly selected a provider");

    const std::string output_after_cancel = output.str();
    std::optional<ProvidedDependency> second =
            session.select_provider("virtual-dependency<9", candidates());
    expect(
            !second.has_value(),
            "cancelled canonical dependency selected on a later request");
    expect(
            output.str() == output_after_cancel,
            "cancelled canonical dependency prompted again");

    std::string unread_input;
    expect(
            static_cast<bool>(std::getline(input, unread_input)) &&
                    unread_input == "2",
            "cancelled canonical dependency consumed later input");
}

void test_eof_dependency_does_not_prompt_again() {
    std::istringstream input;
    std::ostringstream output;
    ProviderSelectionSession session(input, output, true);

    std::optional<ProvidedDependency> first =
            session.select_provider("virtual-dependency", candidates());
    expect(!first.has_value(), "EOF unexpectedly selected a provider");

    const std::string output_after_eof = output.str();
    std::optional<ProvidedDependency> second =
            session.select_provider("virtual-dependency>=2", candidates());
    expect(
            !second.has_value(),
            "EOF-cancelled canonical dependency selected on a later request");
    expect(
            output.str() == output_after_eof,
            "EOF-cancelled canonical dependency prompted again");
}

void test_canonical_dependency_reuses_source_aware_choice() {
    std::istringstream input("2\n1\n");
    std::ostringstream output;
    ProviderSelectionSession session(input, output, true);

    std::optional<ProvidedDependency> first =
            session.select_provider(" virtual-dependency>=2 ", candidates());
    expect(first.has_value(), "initial provider selection failed");

    std::vector<ProvidedDependency> current_candidates{
            repository_candidate("1.2.4-1"), aur_candidate("2.5.0-1")};
    const std::string presentation_before_reuse = output.str();
    std::optional<ProvidedDependency> reused = session.select_provider(
            "virtual-dependency<9", current_candidates);

    expect(reused.has_value(), "canonical dependency choice was not reused");
    expect(
            reused.value() == current_candidates[1],
            "reuse did not return authoritative current candidate metadata");
    expect(
            output.str() == presentation_before_reuse,
            "reused provider choice prompted a second time");

    std::string unread_input;
    expect(
            static_cast<bool>(std::getline(input, unread_input)) &&
                    unread_input == "1",
            "reused provider choice consumed another input line");
}

void test_missing_previous_identity_throws_typed_conflict() {
    std::istringstream input("2\n");
    std::ostringstream output;
    ProviderSelectionSession session(input, output, true);
    static_cast<void>(
            session.select_provider("virtual-dependency", candidates()));

    bool caught = false;
    try {
        static_cast<void>(session.select_provider(
                "virtual-dependency>=3",
                std::vector<ProvidedDependency>{repository_candidate()}));
    } catch(const ProviderSelectionConflict& error) {
        caught = true;
        expect(
                error.dependency_name() == "virtual-dependency",
                "typed conflict did not expose the canonical dependency name");
    }
    expect(caught, "missing previous provider identity did not throw conflict");
}

void expect_cross_dependency_identity_conflict(
        const ProvidedDependency& first,
        const ProvidedDependency& second,
        const std::string& expected_diagnostic) {
    std::istringstream input("1\n1\n");
    std::ostringstream output;
    ProviderSelectionSession session(input, output, true);
    static_cast<void>(session.select_provider(
            "first-virtual", {first, decoy_candidate("first-virtual")}));

    bool caught = false;
    try {
        static_cast<void>(session.select_provider(
                "second-virtual",
                {second, decoy_candidate("second-virtual")}));
    } catch(const std::runtime_error& error) {
        caught = true;
        expect(
                error.what() == expected_diagnostic,
                "Cross-dependency provider identity diagnostic differs");
    }
    expect(caught, "Cross-dependency provider identity conflict was accepted");
}

void test_cross_dependency_package_identity_conflicts() {
    expect_cross_dependency_identity_conflict(
            repository_identity_candidate("extra", "first-virtual"),
            repository_identity_candidate("core", "second-virtual"),
            "Selected providers use incompatible identities for package "
            "shared-provider: extra/shared-provider and core/shared-provider.");
    expect_cross_dependency_identity_conflict(
            repository_identity_candidate("extra", "first-virtual"),
            aur_identity_candidate(
                    "shared-provider-base", "second-virtual"),
            "Selected providers use incompatible identities for package "
            "shared-provider: extra/shared-provider and aur/shared-provider "
            "(PackageBase: shared-provider-base).");
    expect_cross_dependency_identity_conflict(
            aur_identity_candidate("first-provider-base", "first-virtual"),
            aur_identity_candidate("second-provider-base", "second-virtual"),
            "Selected providers use incompatible identities for package "
            "shared-provider: aur/shared-provider (PackageBase: "
            "first-provider-base) and aur/shared-provider (PackageBase: "
            "second-provider-base).");
}

void test_cross_dependency_same_identity_is_allowed() {
    std::istringstream input("1\n1\n");
    std::ostringstream output;
    ProviderSelectionSession session(input, output, true);
    const ProvidedDependency first =
            repository_identity_candidate("extra", "first-virtual");
    const ProvidedDependency second = repository_identity_candidate(
            "extra", "second-virtual", "2.0-1");
    static_cast<void>(session.select_provider(
            "first-virtual", {first, decoy_candidate("first-virtual")}));
    const std::optional<ProvidedDependency> selected =
            session.select_provider(
                    "second-virtual",
                    {second, decoy_candidate("second-virtual")});
    expect(
            selected.has_value() && selected.value() == second,
            "Same provider identity was rejected across dependency aliases");
}

void test_no_confirm_production_session_is_noninteractive() {
    std::shared_ptr<ProviderSelectionSession> session =
            make_provider_selection_session(true);
    expect(session != nullptr, "production session factory returned null");
    expect(
            !session->is_interactive(),
            "--noconfirm production session remained interactive");
}

} // namespace

int main() {
    try {
        test_noninteractive_session_does_not_read_or_write();
        test_candidate_metadata_and_exact_number_selection();
        test_invalid_and_out_of_range_input_retries();
        test_cancel_inputs_return_no_selection();
        test_cancelled_dependency_does_not_prompt_or_read_again();
        test_eof_dependency_does_not_prompt_again();
        test_canonical_dependency_reuses_source_aware_choice();
        test_missing_previous_identity_throws_typed_conflict();
        test_cross_dependency_package_identity_conflicts();
        test_cross_dependency_same_identity_is_allowed();
        test_no_confirm_production_session_is_noninteractive();
        std::cout << "provider selection tests passed" << std::endl;
        return 0;
    } catch(const std::exception& error) {
        std::cerr << error.what() << std::endl;
        return 1;
    }
}
