#include "source_build.hpp"

#include "app_config.hpp"
#include "logging.hpp"
#include "package_identifier.hpp"
#include "persistent_checkout.hpp"
#include "process.hpp"
#include "separated_source_build.hpp"
#include "shell_words.hpp"
#include "trusted_cache.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct MakepkgBuildOptions {
    bool rebuild = false;
    bool clean_build = false;
};

enum class PromptDefault {
    Yes,
    No,
    None,
};

enum class UpdateCheckResult {
    NeedsBuild,
    UpToDate,
    Unknown,
};

std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\n\r");
    if(first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\n\r");
    return str.substr(first, (last - first + 1));
}

std::string to_lower(std::string str) {
    std::transform(str.begin(), str.end(), str.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return str;
}

bool is_safe_command_token(const std::string& token) {
    if(token.empty()) return false;
    return std::all_of(token.begin(), token.end(), [](unsigned char ch) {
        return std::isalnum(ch) || ch == '/' || ch == '.' || ch == '_' || ch == '+' || ch == '-' || ch == '=' ||
               ch == ':' || ch == '@' || ch == '%';
    });
}

std::vector<std::string> split_command_words(const std::string& command) {
    std::stringstream        ss(command);
    std::string              word;
    std::vector<std::string> words;
    while(ss >> word) {
        if(!is_safe_command_token(word)) {
            throw std::runtime_error("Unsafe command token: " + word);
        }
        words.push_back(word);
    }
    if(words.empty()) {
        throw std::runtime_error("Editor command is empty.");
    }
    return words;
}

std::string join_comma_display_values(const std::vector<std::string>& values) {
    std::stringstream ss;
    for(size_t i = 0; i < values.size(); ++i) {
        if(i > 0) ss << ", ";
        ss << values[i];
    }
    return ss.str();
}

std::optional<bool> prompt_default_value(PromptDefault default_answer) {
    switch(default_answer) {
        case PromptDefault::Yes:
            return true;
        case PromptDefault::No:
            return false;
        case PromptDefault::None:
            return std::nullopt;
    }
    return std::nullopt;
}

std::string prompt_suffix(PromptDefault default_answer) {
    switch(default_answer) {
        case PromptDefault::Yes:
            return "[Y/n]";
        case PromptDefault::No:
            return "[y/N]";
        case PromptDefault::None:
            return "[y/n]";
    }
    return "[y/n]";
}

std::string prompt_answer_label(bool answer) {
    return answer ? "yes" : "no";
}

bool ask_user(
        const std::string& question, PromptDefault default_answer,
        const AppConfig& config) {
    std::optional<bool> default_value = prompt_default_value(default_answer);

    if(config.no_confirm) {
        // POLICY: --noconfirm でも default を持たない prompt は自動回答しない。
        if(default_value.has_value()) {
            Logger::info("Skipping prompt (--noconfirm): " + question + " -> " + prompt_answer_label(default_value.value()));
            return default_value.value();
        }
        throw std::runtime_error("Cannot answer prompt without interaction (--noconfirm): " + question);
    }

    if(!isatty(STDIN_FILENO)) {
        // LANDMINE: 非対話 stdin では、破壊的になり得る yes default を安全に選べない。
        if(default_value.has_value() && default_value.value() == false) {
            Logger::info("Skipping prompt (non-interactive stdin): " + question + " -> no");
            return false;
        }
        throw std::runtime_error("Cannot safely answer prompt with non-interactive stdin: " + question);
    }

    for(;;) {
        std::cout << ":: " << question << " " << prompt_suffix(default_answer) << " ";
        std::string input;
        if(!std::getline(std::cin, input)) {
            throw std::runtime_error("Failed to read prompt input: " + question);
        }

        input = to_lower(trim(input));
        if(input.empty()) {
            if(default_value.has_value()) return default_value.value();
            Logger::warn("Please answer yes or no.");
            continue;
        }
        if(input == "y" || input == "yes") return true;
        if(input == "n" || input == "no") return false;

        Logger::warn("Please answer yes or no.");
    }
}

std::string build_editor_command(const std::string& editor, const fs::path& target) {
    std::vector<std::string> args = split_command_words(editor);
    fs::path editor_target = target;
    if(editor_target.is_relative()) {
        // POLICY(#219): package-controlled basenameをeditor optionにせず、checkout内の明示pathとして渡す。
        editor_target = fs::path(".") / editor_target;
    }
    args.push_back(editor_target.string());
    return shell_words::join(args);
}

std::string get_git_branch() {
    std::string remote_head = exec_command("git symbolic-ref --quiet --short refs/remotes/origin/HEAD 2>/dev/null");
    const std::string prefix = "origin/";
    if(remote_head.starts_with(prefix) && remote_head.length() > prefix.length()) {
        return remote_head.substr(prefix.length());
    }
    if(command_status("git show-ref --verify --quiet refs/remotes/origin/main") == 0) return "main";
    if(command_status("git show-ref --verify --quiet refs/remotes/origin/master") == 0) return "master";
    return "master";
}

std::vector<std::string> split_lines(const std::string& text) {
    std::vector<std::string> lines;
    std::stringstream        stream(text);
    std::string              line;
    while(std::getline(stream, line)) {
        line = trim(line);
        if(!line.empty()) lines.push_back(line);
    }
    return lines;
}

std::vector<std::string> git_changed_files(const std::string& range) {
    std::string cmd = "git diff --name-only " + shell_words::quote(range) + " 2>/dev/null";
    return split_lines(exec_command(cmd.c_str()));
}

bool is_review_sensitive_file(const std::string& path) {
    fs::path file_path(path);
    return file_path.filename() == "PKGBUILD" || file_path.extension() == ".install";
}

void log_update_diff_guidance(const std::string& range) {
    std::vector<std::string> changed_files = git_changed_files(range);
    if(changed_files.empty()) return;

    Logger::info("Update diff range: " + range + " (existing cache repository).");

    std::vector<std::string> review_sensitive_files;
    for(const auto& file : changed_files) {
        if(is_review_sensitive_file(file)) review_sensitive_files.push_back(file);
    }
    if(!review_sensitive_files.empty()) {
        Logger::warn("Review-sensitive file changes: " + join_comma_display_values(review_sensitive_files));
    }
}

void log_review_targets(const fs::path& pkg_dir, const std::vector<fs::path>& install_scripts) {
    Logger::info("Review target: PKGBUILD");
    if(install_scripts.empty()) return;

    std::vector<std::string> names;
    for(const auto& script : install_scripts) {
        names.push_back(script.string());
    }
    // POLICY: PKGBUILD はここで評価しない。作業ツリーにある *.install だけを、見落とし防止として案内する。
    Logger::warn("Install script(s) present; review before build: " + join_comma_display_values(names));
    Logger::info("Review directory: " + pkg_dir.string());
}

void review_build_files(
        const ValidatedCachePath& checkout,
        const AppConfig& config) {
    const fs::path& pkg_dir = checkout.canonical_path();
    std::vector<fs::path> install_scripts =
            require_safe_persistent_checkout_descendants(checkout);

    if(config.no_edit) {
        Logger::info("Skipping PKGBUILD/.install review (--noedit).");
        return;
    }

    log_review_targets(pkg_dir, install_scripts);

    const char* env_editor = std::getenv("EDITOR");
    std::string editor_cmd = (env_editor) ? std::string(env_editor) : config.editor;
    bool        edited = false;

    if(ask_user("Edit PKGBUILD?", PromptDefault::No, config)) {
        require_safe_persistent_checkout_review_targets(checkout, install_scripts);
        if(run_command(build_editor_command(editor_cmd, "PKGBUILD")) != 0) {
            throw std::runtime_error("Editor failed.");
        }
        require_safe_persistent_checkout_review_targets(checkout, install_scripts);
        edited = true;
    }

    for(const auto& install_script : install_scripts) {
        if(ask_user("Edit install script " + install_script.string() + "?", PromptDefault::No, config)) {
            require_safe_persistent_checkout_review_targets(checkout, install_scripts);
            if(run_command(build_editor_command(editor_cmd, install_script)) != 0) {
                throw std::runtime_error("Editor failed.");
            }
            require_safe_persistent_checkout_review_targets(checkout, install_scripts);
            edited = true;
        }
    }

    // LANDMINE(#197): editor はreview対象を置換できるため、review開始時の検証結果を持ち越さない。
    require_safe_persistent_checkout_review_targets(checkout, install_scripts);
    if(edited && !ask_user("Proceed with build?", PromptDefault::Yes, config)) throw std::runtime_error("Aborted.");
}

std::optional<std::string> read_srcinfo_version(const fs::path& pkg_dir) {
    fs::path        srcinfo_path = pkg_dir / ".SRCINFO";
    std::error_code ec;
    if(!fs::is_regular_file(srcinfo_path, ec) || ec) return std::nullopt;

    std::ifstream file(srcinfo_path);
    if(!file) return std::nullopt;

    std::string pkgver;
    std::string pkgrel;
    std::string line;
    while(std::getline(file, line)) {
        std::string trimmed = trim(line);
        if(trimmed.starts_with("pkgver =")) {
            pkgver = trim(trimmed.substr(trimmed.find('=') + 1));
        } else if(trimmed.starts_with("pkgrel =")) {
            pkgrel = trim(trimmed.substr(trimmed.find('=') + 1));
        }
    }

    if(pkgver.empty() || pkgrel.empty()) return std::nullopt;
    return pkgver + "-" + pkgrel;
}

UpdateCheckResult check_update_status(
        const std::string& pkg_name, const fs::path& pkg_dir,
        const SourceInstalledSnapshot& installed_snapshot,
        const std::optional<SourceUpdateBaseline>& update_baseline) {
    const std::optional<std::string>& installed_version =
            installed_snapshot.installed_version;
    if(!installed_version.has_value()) {
        return UpdateCheckResult::NeedsBuild;// インストールされていないのでビルド必要
    }

    // POLICY: upgrade の pre-review 更新判定では PKGBUILD を評価しない。
    // 既存 .SRCINFO が読めない場合は呼び出し元で対話確認または skip へ進める。
    std::optional<std::string> new_ver = read_srcinfo_version(pkg_dir);
    if(!new_ver.has_value()) return UpdateCheckResult::Unknown;

    std::string cmp_cmd = "vercmp " + shell_words::quote(new_ver.value()) + " " + shell_words::quote(installed_version.value()) + " 2>/dev/null";
    std::string cmp_res = exec_command(cmp_cmd.c_str());

    try {
        int version_comparison = std::stoi(cmp_res);
        if(version_comparison > 0) return UpdateCheckResult::NeedsBuild;
        if(version_comparison == 0 && update_baseline.has_value()) {
            const std::optional<std::string>& pre_upgrade_version =
                    update_baseline->installed_version;
            // POLICY(#152,#215): pre/postは同じmetadata mappingのowned full versionなので、
            // system transaction中のversion変化は文字列の不一致として判定する。
            if(!pre_upgrade_version.has_value() ||
               pre_upgrade_version.value() != installed_version.value()) {
                if(pre_upgrade_version.has_value()) {
                    Logger::info(
                            pkg_name + " was updated by the system transaction (" +
                            pre_upgrade_version.value() + " -> " + installed_version.value() +
                            "); rebuilding the preferred source package.");
                } else {
                    Logger::info(
                            pkg_name + " was installed by the system transaction as " +
                            installed_version.value() +
                            "; rebuilding the preferred source package.");
                }
                return UpdateCheckResult::NeedsBuild;
            }
        }
    } catch(...) {
        return UpdateCheckResult::Unknown;
    }

    Logger::info(pkg_name + " is up to date (" + installed_version.value() + "). Skipping.");
    return UpdateCheckResult::UpToDate;
}

bool has_local_package_artifact(const fs::path& pkg_dir) {
    if(!fs::exists(pkg_dir) || !fs::is_directory(pkg_dir)) return false;

    for(const auto& entry : fs::directory_iterator(pkg_dir)) {
        if(!entry.is_regular_file()) continue;

        std::string filename = entry.path().filename().string();
        if(filename.size() >= 4 && filename.substr(filename.size() - 4) == ".sig") continue;
        if(filename.find(".pkg.tar") != std::string::npos) return true;
    }
    return false;
}

bool has_local_srcdir(const fs::path& pkg_dir) {
    fs::path src_dir = pkg_dir / "src";
    return fs::exists(src_dir) && fs::is_directory(src_dir);
}

MakepkgBuildOptions resolve_makepkg_build_options(
        const fs::path& pkg_dir, const AppConfig& config) {
    MakepkgBuildOptions options;
    bool                has_artifact = has_local_package_artifact(pkg_dir);

    if(config.clean_build) {
        options.clean_build = true;
    } else if(has_local_srcdir(pkg_dir)) {
        options.clean_build = ask_user("Clean build existing build directory?", PromptDefault::No, config);
    }

    if(config.rebuild) {
        options.rebuild = true;
    } else if(options.clean_build && has_artifact) {
        options.rebuild = true;
    } else if(has_artifact) {
        options.rebuild = ask_user("Rebuild package?", PromptDefault::No, config);
    }

    return options;
}

} // namespace

std::optional<ArtifactInstallExecutionOutcome> execute_source_build(
        const SourceBuildRequest& request,
        DesiredInstallReason desired_reason,
        const PacmanDatabasePaths& database_paths,
        const AppConfig& config) {
    require_valid_package_name(request.package_name);
    require_valid_package_name(request.checkout_name);
    if(request.only_if_updated && !request.installed_snapshot.has_value()) {
        throw std::runtime_error(
                "Authoritative installed package snapshot was not supplied for " +
                request.package_name + ".");
    }
    Logger::info("Processing " + request.package_name + "...");
    ValidatedCacheRoot build_root = prepare_trusted_cache_root();
    ValidatedCachePath pkg_path = require_trusted_cache_path(
            build_root, request.checkout_name,
            CachePathRequirement::ExistingOrMissing);

    {
        WorkDirGuard wd(build_root);
        bool         needs_clone = true;

        if(pkg_path.exists() && pkg_path.is_directory() &&
           has_safe_persistent_checkout_git_directory(pkg_path)) {
            require_safe_persistent_checkout_descendants(pkg_path);
            {
                WorkDirGuard wd_repo(pkg_path);
                std::string  current_url = exec_command("git config --get remote.origin.url");
                if(!remote_url_matches_expected(current_url, request.git_url)) {
                    Logger::warn("Remote URL mismatch. Re-cloning...");
                } else {
                    needs_clone = false;
                }
            }

            if(!needs_clone) {
                Logger::info("Updating repository...");
                WorkDirGuard wd_repo(pkg_path);
                pkg_path = revalidate_trusted_cache_path(
                        pkg_path, CachePathRequirement::ExistingDirectory);
                require_safe_persistent_checkout_descendants(pkg_path);
                if(run_command("git fetch origin") != 0) throw std::runtime_error("Failed to fetch updates.");

                std::string branch = get_git_branch();
                Logger::info("Detected branch: " + branch);

                if(!config.no_diff) {
                    std::string remote_ref = "origin/" + branch;
                    int diff_ret = run_command("git diff --quiet " + shell_words::quote("HEAD.." + remote_ref));
                    if(diff_ret > 1) {
                        throw std::runtime_error("Failed to compare repository changes.");
                    }
                    if(diff_ret == 1) {
                        log_update_diff_guidance("HEAD.." + remote_ref);
                        if(ask_user("Updates detected in existing cache repository. View git diff?", PromptDefault::No, config)) {
                            run_command("git diff " + shell_words::quote("HEAD.." + remote_ref) + " --color=always");
                        }
                    }
                }

                // LANDMINE: reset は build/install 経路だけで許可する。fetch 経路へ持ち込まない。
                pkg_path = revalidate_trusted_cache_path(
                        pkg_path, CachePathRequirement::ExistingDirectory);
                require_safe_persistent_checkout_descendants(pkg_path);
                if(run_command("git reset --hard " + shell_words::quote("origin/" + branch)) != 0) {
                    throw std::runtime_error("Failed to reset repository.");
                }
                pkg_path = revalidate_trusted_cache_path(
                        pkg_path, CachePathRequirement::ExistingDirectory);
                require_safe_persistent_checkout_descendants(pkg_path);
            }
        }

        if(needs_clone) {
            if(pkg_path.exists()) {
                // POLICY(#175): remote mismatch/non-repository cleanup is limited to the validated cache entry.
                remove_trusted_cache_path(pkg_path);
            }
            pkg_path = require_trusted_cache_path(
                    build_root, request.checkout_name, CachePathRequirement::Missing);
            Logger::info("Cloning repository...");
            DirCleanupGuard cleanup_guard(pkg_path);
            if(run_command("git clone " + shell_words::quote(request.git_url) + " " + shell_words::quote(request.checkout_name)) != 0) {
                throw std::runtime_error("Failed to clone " + request.checkout_name);
            }

            pkg_path = require_trusted_cache_path(
                    build_root, request.checkout_name, CachePathRequirement::ExistingDirectory);
            require_safe_persistent_checkout_descendants(pkg_path);
            {
                WorkDirGuard wd_repo(pkg_path);
                std::string  current_url = trim(exec_command("git config --get remote.origin.url"));
                if(current_url.empty()) throw std::runtime_error("Missing remote.origin.url for " + request.checkout_name + ".");
                if(!remote_url_matches_expected(current_url, request.git_url)) {
                    throw std::runtime_error("Remote URL mismatch for " + request.checkout_name + ": " + current_url);
                }
            }
            cleanup_guard.commit();
        }
    }

    MakepkgBuildOptions makepkg_options;
    {
        pkg_path = revalidate_trusted_cache_path(
                pkg_path, CachePathRequirement::ExistingDirectory);
        require_safe_persistent_checkout_descendants(pkg_path);
        WorkDirGuard wd(pkg_path);

        if(request.only_if_updated) {
            UpdateCheckResult update_check = check_update_status(
                    request.package_name, pkg_path.canonical_path(),
                    request.installed_snapshot.value(), request.update_baseline);
            if(update_check == UpdateCheckResult::UpToDate) {
                return std::nullopt; // 更新不要なので終了
            }
            if(update_check == UpdateCheckResult::Unknown) {
                Logger::warn("Unable to determine update status from .SRCINFO for " + request.package_name + ".");
                Logger::warn("Skipping pre-review PKGBUILD evaluation.");
                if(config.no_confirm) {
                    Logger::warn("Skipping " + request.package_name + ": update status is unknown and --noconfirm is set.");
                    return std::nullopt;
                }
                if(!isatty(STDIN_FILENO)) {
                    Logger::warn("Skipping " + request.package_name + ": update status is unknown and stdin is non-interactive.");
                    return std::nullopt;
                }
                if(!ask_user("Update status is unknown because .SRCINFO is missing or incomplete. Continue to review/build?",
                            PromptDefault::No, config)) {
                    return std::nullopt;
                }
            }
        }

        review_build_files(pkg_path, config);
        makepkg_options = resolve_makepkg_build_options(
                pkg_path.canonical_path(), config);
    }

    const std::string custom_environment = serialize_source_build_environment(
            request.custom_environment, request.empty_value_policy);
    if(!trim(custom_environment).empty()) {
        Logger::info("Applying custom build flags: " + custom_environment);
    } else {
        Logger::info("Using default makepkg.conf settings.");
    }

    // LANDMINE(#175,#197,#242): review/editor後のcheckoutだけをshared lifecycleへ渡す。
    // private artifact root factoryはfilesystemを作り得るため、global preflight完了後の
    // このPackageBase実行phaseで初めて呼ぶ。
    pkg_path = revalidate_trusted_cache_path(
            pkg_path, CachePathRequirement::ExistingDirectory);
    require_safe_persistent_checkout_descendants(pkg_path);
    ValidatedPrivateCacheRoot artifact_root =
            prepare_private_trusted_cache_root();
    return execute_separated_source_build_unit(
            SeparatedSourceBuildUnitRequest{
                    pkg_path,
                    std::move(artifact_root),
                    request.package_name,
                    request.checkout_name,
                    desired_reason,
                    request.custom_environment,
                    request.empty_value_policy,
                    database_paths},
            SeparatedSourceBuildUnitOptions{
                    .no_confirm = config.no_confirm,
                    .needed = request.needed,
                    .rm_deps = config.rm_deps,
                    .rebuild = makepkg_options.rebuild,
                    .clean_build = makepkg_options.clean_build});
}
