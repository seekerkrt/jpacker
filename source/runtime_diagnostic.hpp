#pragma once

#include "diagnostic_model.hpp"

#include <string>

struct RuntimeDiagnosticPresentation {
    DiagnosticSeverity severity = DiagnosticSeverity::Error;
    std::string        message;

    bool operator==(const RuntimeDiagnosticPresentation&) const = default;
};

std::string diagnostic_class_label(DiagnosticClass classification);
std::string diagnostic_source_label(DiagnosticSourceKind source_kind);
std::string diagnostic_identity_suffix(const DiagnosticIdentity& identity);

template<typename Reason>
RuntimeDiagnosticPresentation present_runtime_diagnostic(
        const NormalizedDiagnostic<Reason>& diagnostic,
        const std::string& reason_message) {
    // classification/severity/identity are independent typed fields. No
    // localized reason text is parsed and no exit decision is recomputed.
    return RuntimeDiagnosticPresentation{
            diagnostic.severity,
            diagnostic_class_label(diagnostic.classification) + ": " +
                    reason_message +
                    diagnostic_identity_suffix(diagnostic.identity)};
}

void report_runtime_diagnostic(
        const RuntimeDiagnosticPresentation& diagnostic);

template<typename Reason>
void report_runtime_diagnostic(
        const NormalizedDiagnostic<Reason>& diagnostic,
        const std::string& reason_message) {
    report_runtime_diagnostic(
            present_runtime_diagnostic(diagnostic, reason_message));
}
