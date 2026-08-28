#include "cli_runtime_contract.hpp"
#include "runtime_diagnostic.hpp"

#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

void expect(bool condition, const std::string& message) {
    if(!condition) throw std::runtime_error(message);
}

ParsedCliArguments invocation(
    std::string operation, std::vector<std::string> operands = {},
    bool local_source = false, bool root_selection = false) {
    ParsedCliArguments parsed;
    parsed.operation = std::move(operation);
    parsed.root_package_selection_requested = root_selection;
    parsed.tokens.push_back(
        ParsedCliToken{parsed.operation, 1, CliTokenRole::Operation});
    if(local_source) {
        parsed.tokens.push_back(ParsedCliToken{
            std::string(cli_authority::LOCAL_SOURCE_OPTION), 2,
            CliTokenRole::PacmanOption});
    }
    for(std::size_t index = 0; index < operands.size(); ++index) {
        parsed.tokens.push_back(ParsedCliToken{
            operands[index], index + 2,
            CliTokenRole::Target});
        parsed.target_token_indices.push_back(parsed.tokens.size() - 1);
    }
    parsed.targets = std::move(operands);
    return parsed;
}

ParsedCliArguments with_pacman_option(
    ParsedCliArguments parsed, std::string option) {
    parsed.tokens.push_back(ParsedCliToken{
        std::move(option), parsed.tokens.size() + 1,
        CliTokenRole::PacmanOption});
    return parsed;
}

void expect_valid(
    const ParsedCliArguments& parsed, const std::string& context) {
    const CliInvocationValidation validation =
        validate_cli_invocation_contract(parsed);
    expect(validation.is_valid(), context + ": invocation was rejected");
}

void expect_issue(
    const ParsedCliArguments& parsed, CliInvocationIssueKind expected_kind,
    DiagnosticClass expected_class, const std::string& context) {
    const CliInvocationValidation validation =
        validate_cli_invocation_contract(parsed);
    expect(!validation.is_valid(), context + ": invocation was accepted");
    expect(
        validation.diagnostic->reason.kind == expected_kind &&
            validation.diagnostic->classification == expected_class,
        context + ": typed diagnostic differs");
}

void test_operand_contract_connection() {
    for(const char* operation : {
            "deps", "plan", "fetch", "edit-src", "del-src",
            "revert"}) {
        expect_valid(
            invocation(operation, {"one", "two"}),
            std::string{operation} + " multi-target");
        expect_issue(
            invocation(operation), CliInvocationIssueKind::MissingOperand,
            DiagnosticClass::Invalid,
            std::string{operation} + " missing target");
    }

    expect_valid(invocation("build", {"pkg"}), "remote build");
    expect_valid(
        invocation("build", {"pkg", "CFLAGS=-O2", "JOBS=4"}),
        "remote build assignments");
    expect_issue(
        invocation("build", {"pkg", "extra"}),
        CliInvocationIssueKind::ExtraOperand,
        DiagnosticClass::Invalid, "remote build extra bare operand");
    expect_valid(
        invocation("build", {"/source", "JOBS=4"}, true),
        "local build assignments");
    expect_issue(
        invocation("build", {"/source", "extra"}, true),
        CliInvocationIssueKind::ExtraOperand,
        DiagnosticClass::Invalid, "local build extra bare operand");

    for(const char* operation : {
            "upgrade", "upgrade-aur", "upgrade-all", "clean",
            "list-src"}) {
        expect_valid(
            invocation(operation),
            std::string{operation} + " targetless");
        expect_issue(
            invocation(operation, {"ignored-before-slice-3"}),
            CliInvocationIssueKind::ExtraOperand,
            DiagnosticClass::Invalid,
            std::string{operation} + " extra operand");
    }

    expect_valid(
        invocation(
            "add-src",
            {"first", "CFLAGS=-O2", "second", "JOBS=4"}),
        "add-src ordered items");
    expect_issue(
        invocation("add-src", {"CFLAGS=-O2", "first"}),
        CliInvocationIssueKind::InvalidOperandOrdering,
        DiagnosticClass::Invalid, "add-src assignment before package");

    expect_valid(invocation("-G", {"pkg"}), "-G exactly one");
    expect_valid(
        with_pacman_option(
            invocation("-G", {"pkg"}),
            "--output-dir=./exports"),
        "-G attached output directory");
    expect_valid(invocation("-Gp", {"pkg"}), "-Gp exactly one");
    expect_issue(
        invocation("-G", {"one", "two"}),
        CliInvocationIssueKind::ExtraOperand,
        DiagnosticClass::Invalid, "-G extra operand");
    expect_issue(
        with_pacman_option(
            invocation("-Gp", {"pkg"}),
            "--output-dir=./exports"),
        CliInvocationIssueKind::
            MisplacedPkgbuildOutputDirectoryOption,
        DiagnosticClass::Unsupported,
        "-Gp output directory scope");
    expect_issue(
        with_pacman_option(
            invocation("plan", {"pkg"}),
            "--output-dir=./exports"),
        CliInvocationIssueKind::
            MisplacedPkgbuildOutputDirectoryOption,
        DiagnosticClass::Unsupported,
        "other operation output directory scope");
    expect_issue(
        invocation("--output-dir=./exports", {"pkg"}),
        CliInvocationIssueKind::
            MisplacedPkgbuildOutputDirectoryOption,
        DiagnosticClass::Unsupported,
        "global-position output directory scope");
    expect_valid(
        invocation("-S", {"query"}, false, true),
        "-S --select exactly one");
    expect_issue(
        invocation("-S", {"one", "two"}, false, true),
        CliInvocationIssueKind::ExtraOperand,
        DiagnosticClass::Invalid, "-S --select extra operand");

    // Delegated pacman remains an open grammar and is not narrowed to the
    // Moguet-owned cardinality rules.
    expect_valid(
        invocation("-Q", {"one", "two", "--foreign"}),
        "delegated pacman open grammar");
    expect_valid(
        invocation("-S", {"one", "two"}),
        "plain delegated sync grammar");
    expect_issue(
        invocation("unknown-operation"),
        CliInvocationIssueKind::UnknownOperation,
        DiagnosticClass::Unsupported, "unknown Moguet operation");
}

void test_runtime_help_connection() {
    using cli_authority::OperationId;
    const std::array expected = {
        std::pair{OperationId::Build,
                  std::string{
                      "build <pkg> [V=K...] | build --local <directory> [V=K...]"}},
        std::pair{OperationId::Upgrade, std::string{"upgrade"}},
        std::pair{OperationId::Clean, std::string{"clean"}},
        std::pair{OperationId::Deps,
                  std::string{"deps [--recursive] <pkg>..."}},
        std::pair{OperationId::Plan, std::string{"plan <pkg>..."}},
        std::pair{OperationId::Fetch, std::string{"fetch <pkg>..."}},
        std::pair{OperationId::AddSource,
                  std::string{"add-src <item>..."}},
        std::pair{OperationId::EditSource,
                  std::string{"edit-src <pkg>..."}},
        std::pair{OperationId::ListSources,
                  std::string{"list-src"}},
        std::pair{OperationId::DeleteSource,
                  std::string{"del-src <pkg>..."}},
        std::pair{OperationId::Revert,
                  std::string{"revert <pkg>..."}}};
    for(const auto& [operation, syntax] : expected) {
        expect(
            cli_operation_syntax(operation) == syntax,
            "Runtime syntax did not derive canonical cardinality: " +
                syntax);
    }

    const std::vector<std::string> canonical = {
        "build <pkg> [V=K...]",
        "build --local <directory> [V=K...]",
        "upgrade",
        "upgrade-aur",
        "upgrade-all",
        "clean",
        "deps [--recursive] <pkg>...",
        "plan <pkg>...",
        "fetch <pkg>...",
        "add-src <item>...",
        "edit-src <pkg>...",
        "list-src",
        "del-src <pkg>...",
        "revert <pkg>...",
        "-G <pkg> [--output-dir=DIR]",
        "-Gp <pkg>",
        "-S --select [--needed] <query>",
    };
    expect(
        cli_canonical_grammar() == canonical,
        "Canonical public grammar projection differs");
}

void test_typed_runtime_diagnostic_connection() {
    const std::array classifications = {
        DiagnosticClass::Invalid,
        DiagnosticClass::Unsupported,
        DiagnosticClass::Ambiguous,
        DiagnosticClass::Declined,
        DiagnosticClass::Cancelled,
        DiagnosticClass::Unavailable,
        DiagnosticClass::InputFailure,
        DiagnosticClass::QueryFailure,
        DiagnosticClass::MetadataFailure,
        DiagnosticClass::RequiresCheck,
        DiagnosticClass::Blocked,
        DiagnosticClass::PartialFailure,
        DiagnosticClass::ExecutionFailure,
        DiagnosticClass::InternalInconsistency};
    const std::array labels = {
        "Invalid", "Unsupported", "Ambiguous", "Declined",
        "Cancelled", "Unavailable", "Input failure",
        "Query failure", "Metadata failure",
        "Requires check", "Blocked", "Partial failure",
        "Execution failure", "Internal inconsistency"};
    for(std::size_t index = 0; index < classifications.size(); ++index) {
        expect(
            diagnostic_class_label(classifications[index]) ==
                labels[index],
            "Runtime diagnostic taxonomy label differs");
    }

    NormalizedDiagnostic<std::string> diagnostic{
        DiagnosticClass::Ambiguous,
        DiagnosticSeverity::Warning,
        DiagnosticOperation::RootPackageSelection,
        DiagnosticPhase::Selection,
        DiagnosticIdentity{
            DiagnosticSourceKind::RepositorySource,
            std::string{"extra"},
            std::string{"requested-child"},
            std::string{"selected-base"},
            std::string{"repository-source:extra/selected-base"},
            std::nullopt},
        "typed-reason",
        DiagnosticRequiredAction::SelectCandidate,
        DiagnosticBlockingDecision::BlocksCurrentOperation,
        DiagnosticExitStatusEffect::Failure,
        std::nullopt};
    const RuntimeDiagnosticPresentation presentation =
        present_runtime_diagnostic(
            diagnostic,
            "raw detail says Cancelled and InternalInconsistency");
    expect(
        presentation.severity == DiagnosticSeverity::Warning,
        "Runtime presentation inferred severity from class");
    expect(
        presentation.message.starts_with("Ambiguous: ") &&
            presentation.message.find("source=repository-source") !=
                std::string::npos &&
            presentation.message.find("repository=extra") !=
                std::string::npos &&
            presentation.message.find("package=requested-child") !=
                std::string::npos &&
            presentation.message.find("PackageBase=selected-base") !=
                std::string::npos,
        "Runtime presentation lost typed source/PackageBase identity");

    diagnostic.classification = DiagnosticClass::Cancelled;
    const RuntimeDiagnosticPresentation cancelled =
        present_runtime_diagnostic(
            diagnostic, "raw detail says Ambiguous");
    expect(
        cancelled.message.starts_with("Cancelled: "),
        "Runtime presentation classified a localized/raw string");
}

} // namespace

int main() {
    try {
        test_operand_contract_connection();
        std::cout << "  ok: runtime operand contract connection\n";
        test_runtime_help_connection();
        std::cout << "  ok: runtime help metadata connection\n";
        test_typed_runtime_diagnostic_connection();
        std::cout << "  ok: typed runtime diagnostic connection\n";
        std::cout << "Runtime CLI connection tests: all checks passed\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr << "runtime_cli_connection_test: " << error.what()
                  << std::endl;
        return 1;
    }
}
