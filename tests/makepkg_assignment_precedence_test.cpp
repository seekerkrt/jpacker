#include "artifact_workspace.hpp"
#include "local_source_metadata_evaluation.hpp"
#include "local_source_root.hpp"
#include "makepkg_syncdeps_pacman_contract.hpp"
#include "process.hpp"
#include "shell_words.hpp"
#include "trusted_cache_test_support.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;

constexpr std::string_view PACKAGE_NAME =
    "moguet-assignment-precedence";
constexpr std::string_view RUNTIME_DEPENDENCY =
    "moguet-runtime-dependency>=1";
constexpr std::string_view BUILD_DEPENDENCY =
    "moguet-build-dependency";
constexpr std::string_view CONFIG_SELECTED = "CONFIG_SELECTED";
constexpr std::string_view CFLAGS_VALUE = "ARGV_SENTINEL_CFLAGS";
constexpr std::string_view CXXFLAGS_VALUE = "ARGV_SENTINEL_CXXFLAGS";
constexpr std::string_view LDFLAGS_VALUE = "ARGV_SENTINEL_LDFLAGS";
constexpr std::string_view MAKEFLAGS_VALUE = "ARGV_SENTINEL_MAKEFLAGS";
constexpr std::string_view PROBE_VALUE = "ARGV_SENTINEL_PROBE";

void expect(bool condition, const std::string& message) {
    if(!condition) throw std::runtime_error(message);
}

void expect_equal(
    const std::string& context, const std::string& actual,
    std::string_view expected) {
    if(actual == expected) return;
    throw std::runtime_error(
        context + ": expected [" + std::string(expected) +
        "], actual [" + actual + "]");
}

class TemporaryDirectory final {
    fs::path path_;

public:
    TemporaryDirectory() {
        std::string path_template =
            (fs::temp_directory_path() /
             "moguet-makepkg-assignment-precedence-XXXXXX")
                .string();
        std::vector<char> buffer(
            path_template.begin(), path_template.end());
        buffer.push_back('\0');
        char* created = mkdtemp(buffer.data());
        if(created == nullptr) {
            throw std::runtime_error(
                "Failed to create makepkg precedence fixture root.");
        }
        path_ = created;
    }

    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

    ~TemporaryDirectory() noexcept {
        std::error_code error;
        fs::remove_all(path_, error);
    }

    const fs::path& path() const noexcept {
        return path_;
    }
};

class ScopedEnvironmentVariable final {
    std::string key_;
    std::optional<std::string> previous_value_;

public:
    ScopedEnvironmentVariable(
        std::string key, const std::optional<std::string>& value)
        : key_(std::move(key)) {
        const char* previous = std::getenv(key_.c_str());
        if(previous != nullptr) previous_value_ = previous;

        const int status = value.has_value()
                               ? setenv(key_.c_str(), value->c_str(), 1)
                               : unsetenv(key_.c_str());
        if(status != 0) {
            throw std::runtime_error(
                "Failed to configure test environment variable: " +
                key_);
        }
    }

    ScopedEnvironmentVariable(const ScopedEnvironmentVariable&) = delete;
    ScopedEnvironmentVariable& operator=(
        const ScopedEnvironmentVariable&) = delete;

    ~ScopedEnvironmentVariable() noexcept {
        if(previous_value_.has_value()) {
            static_cast<void>(setenv(
                key_.c_str(), previous_value_->c_str(), 1));
        } else {
            static_cast<void>(unsetenv(key_.c_str()));
        }
    }
};

class TestProcessEnvironment final {
    std::vector<std::unique_ptr<ScopedEnvironmentVariable>> variables_;

    void set(
        const std::string& key,
        const std::optional<std::string>& value) {
        variables_.push_back(
            std::make_unique<ScopedEnvironmentVariable>(key, value));
    }

public:
    explicit TestProcessEnvironment(const fs::path& fixture_root) {
        const fs::path home = fixture_root / "home";
        const fs::path cache_home = fixture_root / "xdg-cache";
        const fs::path config_home = fixture_root / "xdg-config";
        const fs::path state_home = fixture_root / "xdg-state";
        fs::create_directory(home);
        fs::create_directory(cache_home);
        fs::create_directory(config_home);
        fs::create_directory(state_home);
        fs::permissions(
            home, fs::perms::owner_all, fs::perm_options::replace);
        fs::permissions(
            cache_home, fs::perms::owner_all,
            fs::perm_options::replace);
        fs::permissions(
            config_home, fs::perms::owner_all,
            fs::perm_options::replace);
        fs::permissions(
            state_home, fs::perms::owner_all,
            fs::perm_options::replace);

        set("HOME", home.string());
        set("XDG_CACHE_HOME", cache_home.string());
        set("XDG_CONFIG_HOME", config_home.string());
        set("XDG_STATE_HOME", state_home.string());
        set("PATH", "/usr/bin:/bin");
        set("LANG", "C");
        set("LC_ALL", "C");
        set("MAKEPKG_LIBRARY", "/usr/share/makepkg");

        for(const char* key : {
                "PKGDEST", "SRCDEST", "SRCPKGDEST", "LOGDEST",
                "BUILDDIR", "PKGEXT", "SRCEXT", "GPGKEY",
                "PACKAGER", "CARCH", "MAKEPKG_CONF",
                "MOGUET_CONFIG_SELECTED", "SOURCE_DATE_EPOCH"}) {
            set(key, std::nullopt);
        }
    }
};

void write_private_file(
    const fs::path& path, const std::string& contents) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if(!output) {
        throw std::runtime_error(
            "Failed to create fixture file: " + path.string());
    }
    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    output.close();
    if(!output) {
        throw std::runtime_error(
            "Failed to finish fixture file: " + path.string());
    }
    fs::permissions(
        path, fs::perms::owner_read | fs::perms::owner_write,
        fs::perm_options::replace);
}

void write_private_executable(
    const fs::path& path, const std::string& contents) {
    write_private_file(path, contents);
    fs::permissions(
        path, fs::perms::owner_all, fs::perm_options::replace);
}

std::string fake_pacman_script() {
    return R"SCRIPT(#!/bin/sh
set -eu
: "${MOGUET_FAKE_PACMAN_LOG:?}"
: "${MOGUET_FAKE_PACMAN_STATE:?}"
{
    printf '%s\n' BEGIN
    for argument do
        printf 'ARG\t%s\n' "$argument"
    done
    printf '%s\n' END
} >>"$MOGUET_FAKE_PACMAN_LOG"

operation=
for argument do
    case "$argument" in
        -T|-S|-Qi)
            operation=$argument
            break
            ;;
    esac
done

case "$operation" in
    -T)
        after_operation=false
        missing=false
        for argument do
            if [ "$after_operation" = true ]; then
                if ! grep -Fqx -- "$argument" \
                    "$MOGUET_FAKE_PACMAN_STATE"; then
                    printf '%s\n' "$argument"
                    missing=true
                fi
            elif [ "$argument" = -T ]; then
                after_operation=true
            fi
        done
        if [ "$missing" = true ]; then
            exit 127
        fi
        exit 0
        ;;
    -S)
        after_asdeps=false
        for argument do
            if [ "$after_asdeps" = true ]; then
                printf '%s\n' "$argument" >>"$MOGUET_FAKE_PACMAN_STATE"
            elif [ "$argument" = --asdeps ]; then
                after_asdeps=true
            fi
        done
        exit 0
        ;;
    -Qi)
        exit 0
        ;;
    *)
        exit 95
        ;;
esac
)SCRIPT";
}

std::vector<std::vector<std::string>> read_fake_pacman_calls(
    const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    if(!input) {
        throw std::runtime_error(
            "Failed to read fake PACMAN call log: " + path.string());
    }

    std::vector<std::vector<std::string>> calls;
    std::vector<std::string> current;
    bool in_call = false;
    std::string line;
    while(std::getline(input, line)) {
        if(line == "BEGIN") {
            if(in_call) {
                throw std::runtime_error(
                    "Nested BEGIN in fake PACMAN call log.");
            }
            current.clear();
            in_call = true;
            continue;
        }
        if(line == "END") {
            if(!in_call) {
                throw std::runtime_error(
                    "END without BEGIN in fake PACMAN call log.");
            }
            calls.push_back(current);
            in_call = false;
            continue;
        }
        constexpr std::string_view argument_prefix = "ARG\t";
        if(!in_call ||
           line.compare(0, argument_prefix.size(), argument_prefix) != 0) {
            throw std::runtime_error(
                "Malformed fake PACMAN call log record.");
        }
        current.push_back(line.substr(argument_prefix.size()));
    }
    if(!input.eof() || in_call) {
        throw std::runtime_error(
            "Incomplete fake PACMAN call log.");
    }
    return calls;
}

std::string makepkg_config(
    const fs::path& config_pkgdest,
    const std::string& architecture) {
    return "CARCH=" + shell_words::quote(architecture) + "\n" +
           "CHOST=" +
           shell_words::quote(
               std::string(architecture) + "-unknown-linux-gnu") +
           "\n"
           "CPPFLAGS=''\n"
           "CFLAGS='CONFIG_SENTINEL_CFLAGS'\n"
           "CXXFLAGS='CONFIG_SENTINEL_CXXFLAGS'\n"
           "LDFLAGS='CONFIG_SENTINEL_LDFLAGS'\n"
           "LTOFLAGS=''\n"
           "DEBUG_CFLAGS=''\n"
           "DEBUG_CXXFLAGS=''\n"
           "MAKEFLAGS='CONFIG_SENTINEL_MAKEFLAGS'\n"
           "BUILDENV=(!distcc !color !ccache !check !sign)\n"
           "OPTIONS=(!strip !docs !libtool !staticlibs !emptydirs "
           "!zipman !purge !debug !lto !autodeps)\n"
           "INTEGRITY_CHECK=(sha256)\n"
           "PKGEXT='.pkg.tar'\n"
           "SRCEXT='.src.tar'\n"
           "PACKAGER='Moguet Test <moguet@example.invalid>'\n"
           "PACMAN='/usr/bin/false'\n"
           "PACMAN_AUTH=('/usr/bin/false')\n"
           "PKGDEST=" +
           shell_words::quote(config_pkgdest.string()) +
           "\n"
           "MOGUET_CONFIG_SELECTED='CONFIG_SELECTED'\n"
           "MOGUET_PROBE='CONFIG_SENTINEL_PROBE'\n"
           "MOGUET_EMPTY='CONFIG_NONEMPTY'\n"
           "MOGUET_SPECIAL='CONFIG_SENTINEL_SPECIAL'\n";
}

std::string pkgbuild() {
    return R"PKGBUILD(pkgname=moguet-assignment-precedence
pkgver="${MOGUET_CONFIG_SELECTED:?temporary config was not selected}_${CFLAGS}_${CXXFLAGS}_${LDFLAGS}_${MAKEFLAGS}_${MOGUET_PROBE}_${MOGUET_EMPTY:-EMPTY}"
pkgrel=1
arch=('any')
license=('custom')
depends=('moguet-runtime-dependency>=1')
makedepends=('moguet-build-dependency')

package() {
    local observation_root="$pkgdir/usr/share/moguet-assignment-precedence"
    install -dm755 "$observation_root"
    printf '%s' "$CFLAGS" > "$observation_root/cflags"
    printf '%s' "$CXXFLAGS" > "$observation_root/cxxflags"
    printf '%s' "$LDFLAGS" > "$observation_root/ldflags"
    printf '%s' "$MAKEFLAGS" > "$observation_root/makeflags"
    printf '%s' "$MOGUET_PROBE" > "$observation_root/probe"
    printf '%s' "$MOGUET_EMPTY" > "$observation_root/empty"
    printf '%s' "$MOGUET_SPECIAL" > "$observation_root/special"
}
)PKGBUILD";
}

std::string expected_version() {
    return std::string(CONFIG_SELECTED) + "_" + std::string(CFLAGS_VALUE) +
           "_" + std::string(CXXFLAGS_VALUE) + "_" +
           std::string(LDFLAGS_VALUE) + "_" +
           std::string(MAKEFLAGS_VALUE) + "_" +
           std::string(PROBE_VALUE) + "_EMPTY";
}

SourceBuildEnvironment make_source_environment(
    const fs::path& config_path, const std::string& special_value,
    const fs::path& fake_pacman, const fs::path& fake_pacman_log,
    const fs::path& fake_pacman_state) {
    return SourceBuildEnvironment{{
        {"MAKEPKG_CONF", config_path.string()},
        {"CFLAGS", "first"},
        {"CFLAGS", std::string(CFLAGS_VALUE)},
        {"CXXFLAGS", std::string(CXXFLAGS_VALUE)},
        {"LDFLAGS", std::string(LDFLAGS_VALUE)},
        {"MAKEFLAGS", std::string(MAKEFLAGS_VALUE)},
        {"MOGUET_PROBE", std::string(PROBE_VALUE)},
        {"MOGUET_EMPTY", ""},
        {"MOGUET_SPECIAL", special_value},
        {"PACMAN", fake_pacman.string()},
        {"PACMAN_AUTH", "/usr/bin/env"},
        {"MOGUET_FAKE_PACMAN_LOG", fake_pacman_log.string()},
        {"MOGUET_FAKE_PACMAN_STATE", fake_pacman_state.string()},
    }};
}

std::string extract_artifact_member(
    const fs::path& artifact, std::string_view member) {
    const std::string command = shell_words::join(
        {"/usr/bin/bsdtar", "-xOf", artifact.string(),
         std::string(member)});
    const CapturedCommandResult result =
        capture_command_output_raw(command.c_str());
    if(result.exit_code != 0) {
        throw std::runtime_error(
            "Failed to extract artifact observation member: " +
            std::string(member));
    }
    return result.output;
}

void test_real_makepkg_assignment_precedence(
    const fs::path& fixture_root) {
    const fs::path source_root_path = fixture_root / "local-source";
    const fs::path config_path = fixture_root / "makepkg.conf";
    const fs::path config_pkgdest = fixture_root / "config-pkgdest";
    const fs::path fake_pacman = fixture_root / "fake-pacman";
    const fs::path fake_pacman_log = fixture_root / "fake-pacman.log";
    const fs::path fake_pacman_state = fixture_root / "fake-pacman.state";
    fs::create_directory(source_root_path);
    fs::create_directory(config_pkgdest);
    fs::permissions(
        source_root_path, fs::perms::owner_all,
        fs::perm_options::replace);
    fs::permissions(
        config_pkgdest, fs::perms::owner_all,
        fs::perm_options::replace);

    const std::string special_value =
        "spaces 'single' \"double\" literal $ dollar "
        "backslash\\ Unicode-日本語\nnewline=tail";
    const SourceBuildEnvironment source_environment =
        make_source_environment(
            config_path, special_value, fake_pacman, fake_pacman_log,
            fake_pacman_state);
    const std::string architecture =
        resolve_local_source_effective_architecture(source_environment);
    write_private_file(
        config_path, makepkg_config(config_pkgdest, architecture));
    write_private_executable(fake_pacman, fake_pacman_script());
    write_private_file(source_root_path / "PKGBUILD", pkgbuild());

    LocalSourceRoot source_root =
        open_local_source_root(source_root_path, true);
    const LocalSourceBuildMetadata metadata =
        evaluate_local_source_metadata(
            source_root, source_environment, architecture);
    expect_equal(
        "local --printsrcinfo pkgbase",
        metadata.metadata().package_base, PACKAGE_NAME);
    expect_equal(
        "local --printsrcinfo effective pkgver",
        metadata.metadata().pkgver, expected_version());
    expect(
        metadata.metadata().children.size() == 1 &&
            metadata.metadata().children.front().name == PACKAGE_NAME,
        "local --printsrcinfo child identity differs");
    expect(
        materialize_source_build_environment_assignment_words(
            metadata.source_environment(),
            SourceEnvironmentEmptyValuePolicy::Forward) ==
            materialize_source_build_environment_assignment_words(
                source_environment,
                SourceEnvironmentEmptyValuePolicy::Forward),
        "local metadata did not preserve the first-seen assignment snapshot");

    ValidatedCacheRoot cache_root = prepare_test_trusted_cache_root();
    const fs::path checkout_path = cache_root.path() / "source-checkout";
    fs::create_directory(checkout_path);
    fs::permissions(
        checkout_path, fs::perms::owner_all,
        fs::perm_options::replace);
    write_private_file(checkout_path / "PKGBUILD", pkgbuild());
    const ValidatedCachePath checkout = require_trusted_cache_path(
        cache_root, checkout_path,
        CachePathRequirement::ExistingDirectory);

    ValidatedPrivateCacheRoot private_root =
        prepare_private_trusted_cache_root(cache_root);
    ArtifactWorkspace workspace =
        create_artifact_workspace(std::move(private_root));
    ArtifactMakepkgContext context = prepare_artifact_makepkg_context(
        checkout, workspace, source_environment,
        SourceEnvironmentEmptyValuePolicy::Forward);
    expect(
        !context.command_environment().ordered_assignments.empty() &&
            context.command_environment()
                    .ordered_assignments.back()
                    .key == "PKGDEST" &&
            context.command_environment()
                    .ordered_assignments.back()
                    .value == workspace.canonical_path().string(),
        "invocation-owned PKGDEST is not the final assignment");

    const ExpectedPackageArtifactPath expected =
        query_makepkg_packagelist(workspace, context);
    const std::string expected_filename =
        std::string(PACKAGE_NAME) + "-" + expected_version() +
        "-1-any.pkg.tar";
    expect_equal(
        "real makepkg --packagelist artifact identity",
        expected.path().filename().string(), expected_filename);
    expect(
        expected.path().parent_path() == workspace.canonical_path(),
        "real makepkg --packagelist escaped the owned PKGDEST");
    expect(
        expected.path().parent_path() != config_pkgdest,
        "temporary config PKGDEST overrode the invocation-owned PKGDEST");

    // Keep the call-shape characterization isolated from any earlier makepkg
    // query. The fake PACMAN is a normal-user executable and never reaches
    // the host package database.
    write_private_file(fake_pacman_log, "");
    write_private_file(fake_pacman_state, "");
    const int build_status = context.run_makepkg_build_only(
        workspace, expected, ArtifactMakepkgBuildOptions{true, false, false});
    expect(build_status == 0, "real makepkg build-only failed");
    expect(
        fs::is_regular_file(expected.path()),
        "real makepkg did not create the expected package artifact");
    expect(
        fs::is_empty(config_pkgdest),
        "real makepkg wrote an artifact to the config-defined PKGDEST");

    const std::vector<std::vector<std::string>> pacman_calls =
        read_fake_pacman_calls(fake_pacman_log);
    const std::vector<std::vector<std::string>> expected_pacman_calls{
        {"-T", std::string(RUNTIME_DEPENDENCY)},
        {"--noconfirm", "-S", "--asdeps",
         std::string(RUNTIME_DEPENDENCY)},
        {"-T", std::string(RUNTIME_DEPENDENCY)},
        {"-T", std::string(BUILD_DEPENDENCY)},
        {"--noconfirm", "-S", "--asdeps",
         std::string(BUILD_DEPENDENCY)},
        {"-T", std::string(BUILD_DEPENDENCY)},
        {"-Qi"},
    };
    expect(
        pacman_calls == expected_pacman_calls,
        "real makepkg PACMAN call shape changed");

    const std::vector<MakepkgSyncDependencyPacmanCallKind> expected_kinds{
        MakepkgSyncDependencyPacmanCallKind::DependencyCheck,
        MakepkgSyncDependencyPacmanCallKind::DependencyInstall,
        MakepkgSyncDependencyPacmanCallKind::DependencyCheck,
        MakepkgSyncDependencyPacmanCallKind::DependencyCheck,
        MakepkgSyncDependencyPacmanCallKind::DependencyInstall,
        MakepkgSyncDependencyPacmanCallKind::DependencyCheck,
        MakepkgSyncDependencyPacmanCallKind::InstalledPackageQuery,
    };
    for(std::size_t index = 0; index < pacman_calls.size(); ++index) {
        const MakepkgSyncDependencyPacmanCall parsed =
            parse_makepkg_sync_dependency_pacman_call(pacman_calls[index]);
        expect(
            parsed.is_supported() && parsed.kind() == expected_kinds[index],
            "real makepkg emitted a PACMAN call outside the strict grammar");
    }

    constexpr std::string_view observation_root =
        "usr/share/moguet-assignment-precedence/";
    expect_equal(
        "package CFLAGS",
        extract_artifact_member(
            expected.path(),
            std::string(observation_root) + "cflags"),
        CFLAGS_VALUE);
    expect_equal(
        "package CXXFLAGS",
        extract_artifact_member(
            expected.path(),
            std::string(observation_root) + "cxxflags"),
        CXXFLAGS_VALUE);
    expect_equal(
        "package LDFLAGS",
        extract_artifact_member(
            expected.path(),
            std::string(observation_root) + "ldflags"),
        LDFLAGS_VALUE);
    expect_equal(
        "package MAKEFLAGS",
        extract_artifact_member(
            expected.path(),
            std::string(observation_root) + "makeflags"),
        MAKEFLAGS_VALUE);
    expect_equal(
        "package generic probe",
        extract_artifact_member(
            expected.path(),
            std::string(observation_root) + "probe"),
        PROBE_VALUE);
    expect_equal(
        "package defined-empty assignment",
        extract_artifact_member(
            expected.path(),
            std::string(observation_root) + "empty"),
        "");
    expect_equal(
        "package special assignment bytes",
        extract_artifact_member(
            expected.path(),
            std::string(observation_root) + "special"),
        special_value);
}

} // namespace

int main() {
    try {
        TemporaryDirectory fixture;
        TestProcessEnvironment environment(fixture.path());
        test_real_makepkg_assignment_precedence(fixture.path());
        std::cout << "makepkg assignment precedence tests passed\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr << "makepkg assignment precedence test failed: "
                  << error.what() << '\n';
        return 1;
    }
}
