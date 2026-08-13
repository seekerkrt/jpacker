#include "runtime_diagnostic.hpp"

#include "logging.hpp"

#include <stdexcept>

std::string diagnostic_class_label(DiagnosticClass classification) {
    // NO_TRANSLATE(Issue #350): Stable diagnostic taxonomy tokens. Slice 4
    // owns full gettext semantic parity for surrounding prose.
    switch(classification) {
    case DiagnosticClass::Invalid:
        return "Invalid";
    case DiagnosticClass::Unsupported:
        return "Unsupported";
    case DiagnosticClass::Ambiguous:
        return "Ambiguous";
    case DiagnosticClass::Cancelled:
        return "Cancelled";
    case DiagnosticClass::Unavailable:
        return "Unavailable";
    case DiagnosticClass::QueryFailure:
        return "QueryFailure";
    case DiagnosticClass::MetadataFailure:
        return "MetadataFailure";
    case DiagnosticClass::RequiresCheck:
        return "RequiresCheck";
    case DiagnosticClass::Blocked:
        return "Blocked";
    case DiagnosticClass::PartialFailure:
        return "PartialFailure";
    case DiagnosticClass::ExecutionFailure:
        return "ExecutionFailure";
    case DiagnosticClass::InternalInconsistency:
        return "InternalInconsistency";
    }
    throw std::logic_error("Unknown diagnostic classification.");
}

std::string diagnostic_source_label(DiagnosticSourceKind source_kind) {
    // NO_TRANSLATE(Issue #350): Stable typed source tokens.
    switch(source_kind) {
    case DiagnosticSourceKind::Unspecified:
        return "unspecified";
    case DiagnosticSourceKind::RepositoryBinary:
        return "repository-binary";
    case DiagnosticSourceKind::RepositorySource:
        return "repository-source";
    case DiagnosticSourceKind::Aur:
        return "aur";
    case DiagnosticSourceKind::Local:
        return "local";
    case DiagnosticSourceKind::Pacman:
        return "pacman";
    }
    throw std::logic_error("Unknown diagnostic source kind.");
}

std::string diagnostic_identity_suffix(const DiagnosticIdentity& identity) {
    std::string suffix;
    auto append = [&suffix](const std::string& field, const std::string& value) {
        suffix += suffix.empty() ? " [" : ", ";
        suffix += field;
        suffix += "=";
        suffix += value;
    };
    if(identity.source_kind != DiagnosticSourceKind::Unspecified) {
        append("source", diagnostic_source_label(identity.source_kind));
    }
    if(identity.repository.has_value()) {
        append("repository", identity.repository.value());
    }
    if(identity.requested_package.has_value()) {
        append("package", identity.requested_package.value());
    }
    if(identity.package_base.has_value()) {
        append("PackageBase", identity.package_base.value());
    }
    if(identity.canonical_source_identity.has_value()) {
        append("source-identity", identity.canonical_source_identity.value());
    }
    if(identity.local_root.has_value()) {
        append("local-root", identity.local_root->string());
    }
    if(!suffix.empty()) suffix += "]";
    return suffix;
}

void report_runtime_diagnostic(
        const RuntimeDiagnosticPresentation& diagnostic) {
    switch(diagnostic.severity) {
    case DiagnosticSeverity::Info:
        Logger::info(diagnostic.message);
        return;
    case DiagnosticSeverity::Warning:
        Logger::warn(diagnostic.message);
        return;
    case DiagnosticSeverity::Error:
        Logger::error(diagnostic.message);
        return;
    }
    throw std::logic_error("Unknown runtime diagnostic severity.");
}
