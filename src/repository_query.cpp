#include "repository_query.hpp"

#include "dependency_spec.hpp"
#include "package_identifier.hpp"
#include "package_metadata.hpp"
#include "process.hpp"
#include "shell_words.hpp"

#include <algorithm>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

namespace {

namespace fs = std::filesystem;

// strict queryはconfigured repository順のprovenanceも含め、1 read phaseをowned snapshot化する。
struct StrictRepositoryMetadataSnapshot {
    std::map<std::string, std::string>                     package_repositories;
    std::map<std::string, std::vector<ProvidedDependency>> providers;
};

using StrictRepositoryMetadataSnapshotResult = std::variant<
        StrictRepositoryMetadataSnapshot,
        RepositoryMetadataFailure>;

// repository query固有のoutput parsingは、汎用string utilityへ持ち上げず局所保持する。
std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\n\r");
    if(first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\n\r");
    return str.substr(first, (last - first + 1));
}

std::string repo_name_from_sync_db(const fs::path& db_path) {
    std::string filename = db_path.filename().string();
    const std::string suffix = ".db";
    if(filename.length() > suffix.length() && filename.substr(filename.length() - suffix.length()) == suffix) {
        return filename.substr(0, filename.length() - suffix.length());
    }
    return db_path.stem().string();
}

bool same_repo_provider(const ProvidedDependency& lhs, const ProvidedDependency& rhs) {
    return lhs.repository == rhs.repository && lhs.package_name == rhs.package_name;
}

void add_repo_provider_candidate(
        std::vector<ProvidedDependency>& candidates, const ProvidedDependency& provider) {
    auto same = [&provider](const ProvidedDependency& existing) {
        return same_repo_provider(existing, provider);
    };
    if(std::find_if(candidates.begin(), candidates.end(), same) != candidates.end()) return;
    candidates.push_back(provider);
}

void add_repo_provider(
        std::map<std::string, std::vector<ProvidedDependency>>& providers, const std::string& provided,
        const std::string& repository, const std::string& package_name) {
    std::string provided_name = provided_dependency_name(provided);
    if(provided_name.empty() || !is_valid_package_name(provided_name)) return;
    add_repo_provider_candidate(providers[provided_name], ProvidedDependency{repository, package_name});
}

void parse_repo_sync_desc(
        const std::string& desc, const std::string& repository,
        std::map<std::string, std::vector<ProvidedDependency>>& providers) {
    std::stringstream        ss(desc);
    std::string              line;
    std::string              package_name;
    std::vector<std::string> package_provides;
    std::string              section;

    auto flush_package = [&]() {
        if(package_name.empty()) return;
        for(const auto& provided : package_provides) {
            add_repo_provider(providers, provided, repository, package_name);
        }
        package_name.clear();
        package_provides.clear();
    };

    while(std::getline(ss, line)) {
        line = trim(line);
        if(line.empty()) continue;

        if(line == "%FILENAME%") {
            flush_package();
            section = line;
            continue;
        }

        if(line.length() >= 2 && line.front() == '%' && line.back() == '%') {
            section = line;
            continue;
        }

        if(section == "%NAME%") {
            package_name = line;
        } else if(section == "%PROVIDES%") {
            package_provides.push_back(line);
        }
    }
    flush_package();
}

RepositoryMetadataFailure repository_metadata_failure(
        RepositoryMetadataFailureKind kind,
        std::optional<std::string> repository_name,
        std::string diagnostic) {
    return RepositoryMetadataFailure{
            kind,
            std::move(repository_name),
            std::move(diagnostic)};
}

bool is_safe_repository_path_component(const std::string& repository_name) {
    if(repository_name.empty() || repository_name == "." || repository_name == "..")
        return false;
    if(repository_name.find('\0') != std::string::npos) return false;

    fs::path component(repository_name);
    return !component.is_absolute() && !component.has_parent_path() &&
           component.filename() == component;
}

struct StrictRepositoryDescriptionRecord {
    bool                       active = false;
    std::optional<std::string> filename;
    std::optional<std::string> package_name;
    std::vector<std::string>   package_provides;
};

std::optional<RepositoryMetadataFailure> parse_repo_sync_desc_strict(
        const std::string& desc, const std::string& repository,
        StrictRepositoryMetadataSnapshot& snapshot) {
    std::stringstream                  stream(desc);
    std::string                        line;
    std::string                        section;
    StrictRepositoryDescriptionRecord record;
    std::set<std::string>              repository_package_names;
    bool                               parsed_package = false;

    auto malformed = [&repository](const std::string& diagnostic) {
        return repository_metadata_failure(
                RepositoryMetadataFailureKind::SyncDatabaseMalformed,
                repository, diagnostic);
    };

    auto flush_package = [&]() -> std::optional<RepositoryMetadataFailure> {
        if(!record.active) return std::nullopt;
        if(!record.filename.has_value() || record.filename->empty()) {
            return malformed(
                    "Repository sync metadata is missing a package filename.");
        }
        if(!record.package_name.has_value() ||
           !is_valid_package_name(record.package_name.value())) {
            return malformed(
                    "Repository sync metadata contains an invalid package name.");
        }
        if(!repository_package_names.insert(record.package_name.value()).second) {
            return malformed(
                    "Repository sync metadata contains a duplicate package name.");
        }

        for(const auto& provided : record.package_provides) {
            ParsedDependency parsed = parse_dependency_string(provided);
            if(!is_valid_package_name(parsed.name) ||
               parsed.has_malformed_constraint()) {
                return malformed(
                        "Repository sync metadata contains an invalid provided dependency.");
            }
        }

        // POLICY: configured repository順の最初のpackageをexact lookupのprovenanceとする。
        snapshot.package_repositories.try_emplace(
                record.package_name.value(), repository);
        for(const auto& provided : record.package_provides) {
            std::string provided_name = provided_dependency_name(provided);
            add_repo_provider_candidate(
                    snapshot.providers[provided_name],
                    ProvidedDependency{repository, record.package_name.value()});
        }

        parsed_package = true;
        record = StrictRepositoryDescriptionRecord{};
        section.clear();
        return std::nullopt;
    };

    while(std::getline(stream, line)) {
        if(line.find('\r') != std::string::npos) {
            return malformed(
                    "Repository sync metadata contains a control character.");
        }
        line = trim(line);
        if(line.empty()) continue;

        bool is_section = line.length() >= 2 && line.front() == '%' &&
                          line.back() == '%';
        if(is_section) {
            if(line == "%FILENAME%") {
                if(auto failure = flush_package(); failure.has_value())
                    return failure;
                record.active = true;
            } else if(!record.active) {
                return malformed(
                        "Repository sync metadata does not start with a package filename.");
            }
            section = line;
            continue;
        }

        if(!record.active || section.empty()) {
            return malformed(
                    "Repository sync metadata contains a value outside a section.");
        }
        if(section == "%FILENAME%") {
            if(record.filename.has_value()) {
                return malformed(
                        "Repository sync metadata contains multiple package filenames.");
            }
            record.filename = line;
        } else if(section == "%NAME%") {
            if(record.package_name.has_value()) {
                return malformed(
                        "Repository sync metadata contains multiple package names.");
            }
            record.package_name = line;
        } else if(section == "%PROVIDES%") {
            record.package_provides.push_back(line);
        }
    }

    if(auto failure = flush_package(); failure.has_value()) return failure;
    if(!parsed_package && !trim(desc).empty()) {
        return malformed(
                "Repository sync metadata does not contain a package entry.");
    }
    return std::nullopt;
}

std::vector<fs::path> repo_sync_db_paths(const fs::path& sync_dir) {
    std::vector<fs::path> paths;
    std::string           repo_list = exec_command("pacman-conf --repo-list 2>/dev/null");
    std::set<std::string> seen;

    std::stringstream ss(repo_list);
    std::string       repo;
    while(std::getline(ss, repo)) {
        repo = trim(repo);
        if(repo.empty()) continue;
        fs::path db_path = sync_dir / (repo + ".db");
        if(fs::exists(db_path) && fs::is_regular_file(db_path)) {
            paths.push_back(db_path);
            seen.insert(db_path.filename().string());
        }
    }

    std::vector<fs::path> fallback_paths;
    for(const auto& entry : fs::directory_iterator(sync_dir)) {
        if(!entry.is_regular_file() || entry.path().extension() != ".db") continue;
        if(seen.count(entry.path().filename().string()) > 0) continue;
        fallback_paths.push_back(entry.path());
    }
    std::sort(fallback_paths.begin(), fallback_paths.end());
    for(const auto& path : fallback_paths) {
        paths.push_back(path);
    }
    return paths;
}

const std::map<std::string, std::vector<ProvidedDependency>>& repo_providers() {
    // POLICY: repo provider 情報は pacman sync DB から作る 1 process 内 cache。
    // ここは単一 thread 前提で、実行中に外部状態を再読込しない。
    static std::map<std::string, std::vector<ProvidedDependency>> s_providers;
    static bool                                                   s_loaded = false;
    if(s_loaded) return s_providers;
    s_loaded = true;

    fs::path sync_dir = "/var/lib/pacman/sync";
    if(!fs::exists(sync_dir)) return s_providers;

    for(const auto& db_path : repo_sync_db_paths(sync_dir)) {
        std::string cmd = "bsdtar -xOf " + shell_words::quote(db_path.string()) + " '*/desc' 2>/dev/null";
        std::string desc = exec_command(cmd.c_str());
        if(desc.empty()) continue;

        parse_repo_sync_desc(desc, repo_name_from_sync_db(db_path), s_providers);
    }

    return s_providers;
}

RepositoryMetadataFailureKind configuration_failure_kind(
        PackageMetadataErrorCode code) {
    return code == PackageMetadataErrorCode::ConfigurationMalformed
            ? RepositoryMetadataFailureKind::ConfigurationMalformed
            : RepositoryMetadataFailureKind::ConfigurationUnavailable;
}

StrictRepositoryMetadataSnapshotResult load_strict_repository_metadata_snapshot() {
    PacmanRepositoryConfiguration configuration;
    try {
        configuration = resolve_pacman_repository_configuration();
    } catch(const PackageMetadataError& error) {
        const PackageMetadataFailure& failure = error.failure();
        return repository_metadata_failure(
                configuration_failure_kind(failure.code),
                std::nullopt, failure.diagnostic);
    }

    StrictRepositoryMetadataSnapshot snapshot;
    fs::path sync_directory = configuration.database_paths.db_path / "sync";

    std::error_code sync_directory_error;
    bool is_sync_directory =
            fs::is_directory(sync_directory, sync_directory_error);
    if(sync_directory_error || !is_sync_directory) {
        return repository_metadata_failure(
                RepositoryMetadataFailureKind::SyncDatabaseUnavailable,
                std::nullopt,
                "Repository sync database directory is unavailable: " +
                        sync_directory.string());
    }

    for(const auto& repository_name : configuration.repository_names) {
        if(!is_safe_repository_path_component(repository_name)) {
            return repository_metadata_failure(
                    RepositoryMetadataFailureKind::ConfigurationMalformed,
                    repository_name,
                    "Repository configuration contains a name that is not a safe path component.");
        }

        fs::path database_path = sync_directory / (repository_name + ".db");
        std::error_code filesystem_error;
        bool is_database_file =
                fs::is_regular_file(database_path, filesystem_error);
        if(filesystem_error || !is_database_file) {
            return repository_metadata_failure(
                    RepositoryMetadataFailureKind::SyncDatabaseUnavailable,
                    repository_name,
                    "Configured repository sync database is unavailable: " +
                            database_path.string());
        }

        std::string command =
                "bsdtar -xOf " + shell_words::quote(database_path.string()) +
                " '*/desc' 2>/dev/null";
        CapturedCommandResult command_result =
                capture_command_output_raw(command.c_str());
        if(command_result.exit_code != 0) {
            return repository_metadata_failure(
                    RepositoryMetadataFailureKind::SyncDatabaseUnavailable,
                    repository_name,
                    "Failed to read configured repository sync database with exit code " +
                            std::to_string(command_result.exit_code) + ".");
        }

        if(auto failure = parse_repo_sync_desc_strict(
                   command_result.output, repository_name, snapshot);
           failure.has_value()) {
            return failure.value();
        }
    }

    return snapshot;
}

const StrictRepositoryMetadataSnapshotResult& strict_repository_metadata_snapshot() {
    // POLICY(#267): success/failureを同じimmutable lazy valueへpublishし、query間のread phaseを固定する。
    static const StrictRepositoryMetadataSnapshotResult s_snapshot =
            load_strict_repository_metadata_snapshot();
    return s_snapshot;
}

} // namespace

bool is_installed_package(const std::string& pkg_name) {
    if(pkg_name.empty()) return false;
    return command_status("pacman -Q " + shell_words::quote(pkg_name) + " > /dev/null 2>&1") == 0;
}

bool is_repo_package(const std::string& pkg_name) {
    require_valid_package_name(pkg_name);
    std::string cmd = "pacman -Si " + shell_words::quote(pkg_name) + " > /dev/null 2>&1";
    return (command_status(cmd) == 0);
}

std::vector<ProvidedDependency> find_repo_providers(const std::string& dependency_name) {
    if(!is_valid_package_name(dependency_name)) return {};
    const auto& providers = repo_providers();
    auto        it = providers.find(dependency_name);
    if(it == providers.end()) return {};
    return it->second;
}

StrictRepositoryPackageQueryResult query_repository_package_strict(
        const std::string& package_name) {
    require_valid_package_name(package_name);

    const StrictRepositoryMetadataSnapshotResult& snapshot_result =
            strict_repository_metadata_snapshot();
    if(const auto* failure =
               std::get_if<RepositoryMetadataFailure>(&snapshot_result);
       failure != nullptr) {
        return *failure;
    }

    const auto& snapshot =
            std::get<StrictRepositoryMetadataSnapshot>(snapshot_result);
    auto package = snapshot.package_repositories.find(package_name);
    if(package == snapshot.package_repositories.end())
        return RepositoryPackageNotFound{};
    return RepositoryPackagePresent{package->second};
}

StrictRepositoryProvidersQueryResult query_repository_providers_strict(
        const std::string& dependency_name) {
    require_valid_package_name(dependency_name);

    const StrictRepositoryMetadataSnapshotResult& snapshot_result =
            strict_repository_metadata_snapshot();
    if(const auto* failure =
               std::get_if<RepositoryMetadataFailure>(&snapshot_result);
       failure != nullptr) {
        return *failure;
    }

    const auto& snapshot =
            std::get<StrictRepositoryMetadataSnapshot>(snapshot_result);
    auto providers = snapshot.providers.find(dependency_name);
    if(providers == snapshot.providers.end())
        return std::vector<ProvidedDependency>{};
    return providers->second;
}

std::vector<InstalledPackage> get_foreign_packages() {
    std::vector<InstalledPackage> packages;
    std::string                   output = exec_command("pacman -Qm 2>/dev/null");
    if(output.empty()) return packages;

    std::stringstream ss(output);
    std::string       line;
    while(std::getline(ss, line)) {
        line = trim(line);
        if(line.empty()) continue;

        std::stringstream line_ss(line);
        InstalledPackage  pkg;
        if(line_ss >> pkg.name >> pkg.version) {
            require_valid_package_name(pkg.name);
            packages.push_back(pkg);
        }
    }
    return packages;
}

std::set<std::string> get_foreign_package_names() {
    std::set<std::string> names;
    for(const auto& pkg : get_foreign_packages()) {
        names.insert(pkg.name);
    }
    return names;
}
