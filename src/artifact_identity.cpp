#include "artifact_identity.hpp"

#include "logging.hpp"
#include "package_identifier.hpp"
#include "process.hpp"
#include "shell_words.hpp"

#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr const char* ARTIFACT_IDENTITY_FORMAT = "%n\t%v";

[[noreturn]] void throw_malformed_artifact_identity() {
    // POLICY: package-controlled stdoutをdiagnosticへ埋め込まず、control characterも漏らさない。
    throw std::runtime_error("pacman returned malformed package artifact identity.");
}

bool is_ascii_control(unsigned char character) {
    return character <= 0x1f || character == 0x7f;
}

ArtifactPackageIdentity parse_artifact_package_identity(
        const std::string& raw_output) {
    if(raw_output.empty()) {
        throw std::runtime_error("pacman returned no package artifact identity.");
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
        throw std::runtime_error("pacman returned an invalid package name for the artifact.");
    }

    return ArtifactPackageIdentity{
            std::move(package_name), std::move(full_version)};
}

} // namespace

ArtifactPackageIdentity query_artifact_package_identity(
        const ValidatedPackageArtifactPath& artifact) {
    // LANDMINE: pacmanへpathを渡す直前とstdoutを信用する直前の両方で、同じartifactを再証明する。
    artifact.require_validity();

    const std::vector<std::string> arguments = {
            "pacman",
            "-U",
            "--print",
            "--print-format",
            ARTIFACT_IDENTITY_FORMAT,
            "--",
            artifact.path().string(),
    };
    const std::string command = "LC_ALL=C " + shell_words::join(arguments);
    Logger::raw_cmd(command);
    CapturedCommandResult result = capture_command_output_raw(command.c_str());

    artifact.require_validity();
    if(result.exit_code != 0) {
        throw std::runtime_error(
                "pacman failed to read package artifact identity with exit code " +
                std::to_string(result.exit_code) + ".");
    }
    return parse_artifact_package_identity(result.output);
}
