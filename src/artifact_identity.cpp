#include "artifact_identity.hpp"

#include "artifact_workspace.hpp"
#include "localization.hpp"
#include "logging.hpp"
#include "package_identifier.hpp"
#include "process.hpp"
#include "shell_words.hpp"

#include <cstddef>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr const char* ARTIFACT_IDENTITY_FORMAT = "%n\t%v";

[[noreturn]] void throw_malformed_artifact_identity() {
    // POLICY: package-controlled stdoutをdiagnosticへ埋め込まず、control characterも漏らさない。
    throw std::runtime_error(localization::format_translated_message(
            // TRANSLATORS: {} is the literal command name "pacman".
            "{} returned malformed package artifact identity.", "pacman"));
}

bool is_ascii_control(unsigned char character) {
    return character <= 0x1f || character == 0x7f;
}

ArtifactPackageIdentity parse_artifact_package_identity(
        const std::string& raw_output) {
    if(raw_output.empty()) {
        throw std::runtime_error(localization::format_translated_message(
                // TRANSLATORS: {} is the literal command name "pacman".
                "{} returned no package artifact identity.", "pacman"));
    }
    if(raw_output.find('\r') != std::string::npos) {
        throw_malformed_artifact_identity();
    }

    // pacmanの通常のrecord terminatorは1個だけ許可する。trimはblank/extra lineを隠すため行わない。
    std::string record = raw_output;
    if(record.back() == '\n') record.pop_back();
    if(record.empty() || record.find('\n') != std::string::npos) {
        throw_malformed_artifact_identity();
    }

    std::size_t delimiter = record.find('\t');
    if(delimiter == std::string::npos ||
       record.find('\t', delimiter + 1) != std::string::npos) {
        throw_malformed_artifact_identity();
    }

    std::string package_name = record.substr(0, delimiter);
    std::string full_version = record.substr(delimiter + 1);
    if(package_name.empty() || full_version.empty()) {
        throw_malformed_artifact_identity();
    }

    for(std::size_t index = 0; index < record.size(); ++index) {
        if(index == delimiter) continue;
        unsigned char character = static_cast<unsigned char>(record[index]);
        if(is_ascii_control(character)) throw_malformed_artifact_identity();
    }
    if(!is_valid_package_name(package_name)) {
        throw std::runtime_error(localization::format_translated_message(
                // TRANSLATORS: {} is the literal command name "pacman".
                "{} returned an invalid package name for the artifact.",
                "pacman"));
    }

    return ArtifactPackageIdentity{
            std::move(package_name), std::move(full_version)};
}

CapturedCommandResult capture_artifact_package_identity_output(
        const std::filesystem::path& artifact_path) {
    const std::vector<std::string> arguments = {
            "pacman",
            "-U",
            "--print",
            "--print-format",
            ARTIFACT_IDENTITY_FORMAT,
            "--",
            artifact_path.string(),
    };
    const std::string command = "LC_ALL=C " + shell_words::join(arguments);
    Logger::raw_cmd(command);
    return capture_command_output_raw(command.c_str());
}

ArtifactPackageIdentity require_artifact_package_identity(
        const CapturedCommandResult& result) {
    if(result.exit_code != 0) {
        throw std::runtime_error(localization::format_translated_message(
                // TRANSLATORS: The first placeholder is the literal command
                // name "pacman"; the second is its numeric exit code.
                "{} failed to read package artifact identity with exit code {}.",
                "pacman", result.exit_code));
    }
    return parse_artifact_package_identity(result.output);
}

} // namespace

ArtifactPackageIdentity query_artifact_package_identity(
        const ValidatedPackageArtifactPath& artifact) {
    // LANDMINE: pacmanへpathを渡す直前とstdoutを信用する直前の両方で、同じartifactを再証明する。
    artifact.require_validity();

    CapturedCommandResult result =
            capture_artifact_package_identity_output(artifact.path());

    artifact.require_validity();
    return require_artifact_package_identity(result);
}

ArtifactPackageIdentitySet query_artifact_package_identities(
        const ValidatedPackageArtifactSet& artifacts) {
    // LANDMINE(#268): individual pathではなくaggregate全体を、最初のcommandより前から
    // result返却直前まで各boundaryで再証明する。途中まで得たidentityは公開しない。
    artifacts.require_validity();
    const std::size_t artifact_count = artifacts.size();

    std::vector<ArtifactPackageIdentity> identities;
    identities.reserve(artifact_count);
    for(std::size_t index = 0; index < artifact_count; ++index) {
        artifacts.require_validity();
        CapturedCommandResult result =
                capture_artifact_package_identity_output(
                        artifacts.path_at(index));
        artifacts.require_validity();
        identities.push_back(require_artifact_package_identity(result));
    }

    artifacts.require_validity();
    ArtifactPackageIdentitySet identity_set(std::move(identities));
    artifacts.require_validity();
    return identity_set;
}
