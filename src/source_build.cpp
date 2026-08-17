#include "source_build.hpp"

#include "app_config.hpp"
#include "diagnostic_projection.hpp"
#include "interactive_confirmation.hpp"
#include "localization.hpp"
#include "logging.hpp"
#include "package_identifier.hpp"
#include "persistent_checkout.hpp"
#include "process.hpp"
#include "runtime_diagnostic.hpp"
#include "separated_package_base_source_build.hpp"
#include "separated_source_build.hpp"
#include "shell_words.hpp"
#include "trusted_git.hpp"
#include "trusted_cache.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <exception>
#include <fcntl.h>
#include <filesystem>
#include <linux/openat2.h>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <utility>
#include <variant>
#include <vector>

namespace fs = std::filesystem;

struct PreparedSourceBuildExecutionCapabilities {
    ValidatedCachePath checkout;
    ValidatedPrivateCacheRoot artifact_root;
    bool rebuild = false;
    bool clean_build = false;
};

struct SourceBuildPreparationAccess {
    static PreparedSourceBuildNeedsBuild make(
            ValidatedCachePath checkout,
            ValidatedPrivateCacheRoot artifact_root,
            bool rebuild,
            bool clean_build) noexcept {
        return PreparedSourceBuildNeedsBuild(
                std::move(checkout), std::move(artifact_root), rebuild,
                clean_build);
    }
};

struct SourceBuildPreparedExecutionAccess {
    static PreparedSourceBuildExecutionCapabilities consume(
            PreparedSourceBuildNeedsBuild prepared) {
        if(!prepared.checkout_.has_value() ||
           !prepared.artifact_root_.has_value()) {
            throw std::logic_error(localization::translate_message(
                    "Prepared source-build execution capability is invalid or test-only."));
        }
        return PreparedSourceBuildExecutionCapabilities{
                std::move(prepared.checkout_.value()),
                std::move(prepared.artifact_root_.value()),
                prepared.rebuild_,
                prepared.clean_build_};
    }
};

namespace {

struct MakepkgBuildOptions {
    bool rebuild = false;
    bool clean_build = false;
};

enum class SourceBuildPreparationFailureStage {
    ArtifactRoot,
    CheckoutOrReview,
};

class SourceBuildPreparationError final : public std::runtime_error {
    SourceBuildPreparationFailureStage stage_;
    bool unknown_exception_ = false;

public:
    SourceBuildPreparationError(
            SourceBuildPreparationFailureStage stage,
            const std::string& diagnostic,
            bool unknown_exception = false)
        : std::runtime_error(diagnostic), stage_(stage),
          unknown_exception_(unknown_exception) {
    }

    SourceBuildPreparationFailureStage stage() const noexcept {
        return stage_;
    }

    bool unknown_exception() const noexcept {
        return unknown_exception_;
    }
};

class ScopedPrivateUmask final {
    mode_t previous_;

public:
    ScopedPrivateUmask() noexcept : previous_(umask(0077)) {
    }

    ScopedPrivateUmask(const ScopedPrivateUmask&) = delete;
    ScopedPrivateUmask& operator=(const ScopedPrivateUmask&) = delete;

    ~ScopedPrivateUmask() noexcept {
        static_cast<void>(umask(previous_));
    }
};

enum class UpdateCheckResult {
    NeedsBuild,
    UpToDate,
    Unknown,
};

std::string up_to_date_diagnostic(
        const std::string& package_name,
        const std::string& installed_version) {
    // TRANSLATORS: The placeholders are a package name and version.
    return localization::format_translated_message(
            "{} is up to date ({}). Skipping.",
            package_name,
            installed_version);
}

std::string unknown_update_skip_diagnostic(
        const std::string& package_name,
        SourceBuildUpdateStatusUnknownSkipReason reason) {
    switch(reason) {
        case SourceBuildUpdateStatusUnknownSkipReason::NoConfirm:
            // TRANSLATORS: The placeholders are a package name and the literal --noconfirm option.
            return localization::format_translated_message(
                    "Skipping {}: update status is unknown and {} is set.",
                    package_name,
                    "--noconfirm");
        case SourceBuildUpdateStatusUnknownSkipReason::NonInteractiveStdin:
            // TRANSLATORS: The placeholders are a package name and the literal stdin identity.
            return localization::format_translated_message(
                    "Skipping {}: update status is unknown and {} is non-interactive.",
                    package_name, "stdin");
        case SourceBuildUpdateStatusUnknownSkipReason::UserDeclined:
            // TRANSLATORS: The placeholder is a package name.
            return localization::format_translated_message(
                    "Skipping {}: update status is unknown and the user declined to continue.",
                    package_name);
    }
    throw std::logic_error(localization::translate_message(
            "Unknown source-build update-status skip reason."));
}

SourceBuildExecutionResult source_build_result_from_artifact_outcome(
        ArtifactInstallExecutionOutcome outcome) {
    switch(outcome) {
        case ArtifactInstallExecutionOutcome::Installed:
            return SourceBuildExecutionResult{
                    SourceBuildExecutionStatus::Installed, std::nullopt, {}};
        case ArtifactInstallExecutionOutcome::SkippedAsNeeded:
            return SourceBuildExecutionResult{
                    SourceBuildExecutionStatus::SkippedAsNeeded, std::nullopt, {}};
    }
    throw std::logic_error(localization::translate_message(
            "Unknown artifact install execution outcome."));
}

std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\n\r");
    if(first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\n\r");
    return str.substr(first, (last - first + 1));
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
            // TRANSLATORS: The placeholder is an editor command token.
            throw std::runtime_error(localization::format_translated_message(
                    "Unsafe command token: {}", word));
        }
        words.push_back(word);
    }
    if(words.empty()) {
        throw std::runtime_error(localization::translate_message(
                "Editor command is empty."));
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

void report_confirmation_stop(
        const ConfirmationResult& result,
        DiagnosticPhase phase,
        DiagnosticIdentity identity = {}) {
    report_runtime_diagnostic(
            project_confirmation_diagnostic(
                    result, DiagnosticOperation::Build, phase,
                    std::move(identity)),
            confirmation_stop_diagnostic(result));
}

[[noreturn]] void stop_after_confirmation(
        ConfirmationResult result,
        DiagnosticPhase phase,
        DiagnosticIdentity identity = {}) {
    report_confirmation_stop(result, phase, std::move(identity));
    throw ConfirmationOperationStopped(std::move(result));
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

std::vector<std::string> git_changed_files(
        const ValidatedCachePath& checkout,
        const std::string& expected_remote_url,
        const std::string& branch) {
    return split_lines(trusted_git_diff_name_only(
            checkout, expected_remote_url, branch));
}

bool is_review_sensitive_file(const std::string& path) {
    fs::path file_path(path);
    return file_path.filename() == "PKGBUILD" || file_path.extension() == ".install";
}

void log_update_diff_guidance(
        const ValidatedCachePath& checkout,
        const std::string& expected_remote_url,
        const std::string& branch) {
    const std::string range = "HEAD..origin/" + branch;
    std::vector<std::string> changed_files = git_changed_files(
            checkout, expected_remote_url, branch);
    if(changed_files.empty()) return;

    // TRANSLATORS: The placeholder is a literal Git revision range.
    Logger::info(localization::format_translated_message(
            "Update diff range: {} (existing cache repository).",
            range));

    std::vector<std::string> review_sensitive_files;
    for(const auto& file : changed_files) {
        if(is_review_sensitive_file(file)) review_sensitive_files.push_back(file);
    }
    if(!review_sensitive_files.empty()) {
        // TRANSLATORS: The placeholder is a comma-separated list of file paths.
        Logger::warn(localization::format_translated_message(
                "Review-sensitive file changes: {}",
                join_comma_display_values(review_sensitive_files)));
    }
}

void log_review_targets(const fs::path& pkg_dir, const std::vector<fs::path>& install_scripts) {
    // TRANSLATORS: The placeholder is the literal PKGBUILD artifact name.
    Logger::info(localization::format_translated_message(
            "Review target: {}", "PKGBUILD"));
    if(install_scripts.empty()) return;

    std::vector<std::string> names;
    for(const auto& script : install_scripts) {
        names.push_back(script.string());
    }
    // POLICY: PKGBUILD はここで評価しない。作業ツリーにある *.install だけを、見落とし防止として案内する。
    // TRANSLATORS: The placeholder is a comma-separated list of install script paths.
    Logger::warn(localization::format_translated_message(
            "Install script(s) present; review before build: {}",
            join_comma_display_values(names)));
    // TRANSLATORS: The placeholder is a package checkout directory path.
    Logger::info(localization::format_translated_message(
            "Review directory: {}", pkg_dir.string()));
}

void review_build_files(
        const ValidatedCachePath& checkout,
        const AppConfig& config) {
    const fs::path& pkg_dir = checkout.canonical_path();
    std::vector<fs::path> install_scripts =
            require_safe_persistent_checkout_descendants(checkout);

    if(config.user_config.review.pkgbuild == ReviewPolicy::Skip) {
        // TRANSLATORS: The placeholders are literal artifact names and the --noedit option.
        Logger::info(localization::format_translated_message(
                "Skipping {}/{} review ({}).",
                "PKGBUILD", ".install", "--noedit"));
        return;
    }

    log_review_targets(pkg_dir, install_scripts);

    bool edited = false;

    // TRANSLATORS: The placeholder is the literal PKGBUILD artifact name.
    const std::string edit_pkgbuild_question =
            localization::format_translated_message(
                    "Edit {}?", "PKGBUILD");
    ConfirmationResult edit_pkgbuild_confirmation = request_confirmation(
            edit_pkgbuild_question, ConfirmationDefault::No,
            config.no_confirm);
    if(std::holds_alternative<ConfirmationAccepted>(
               edit_pkgbuild_confirmation)) {
        require_safe_persistent_checkout_review_targets(checkout, install_scripts);
        if(run_command(build_editor_command(config.editor, "PKGBUILD")) != 0) {
            throw std::runtime_error(localization::translate_message(
                    "Editor failed."));
        }
        require_safe_persistent_checkout_review_targets(checkout, install_scripts);
        edited = true;
    } else if(!std::holds_alternative<ConfirmationDeclined>(
                      edit_pkgbuild_confirmation)) {
        DiagnosticIdentity identity;
        identity.canonical_source_identity = pkg_dir.string();
        stop_after_confirmation(
                std::move(edit_pkgbuild_confirmation),
                DiagnosticPhase::Preflight, std::move(identity));
    }

    for(const auto& install_script : install_scripts) {
        // TRANSLATORS: The placeholder is an install script path.
        const std::string edit_question = localization::format_translated_message(
                "Edit install script {}?", install_script.string());
        ConfirmationResult edit_install_confirmation = request_confirmation(
                edit_question, ConfirmationDefault::No,
                config.no_confirm);
        if(std::holds_alternative<ConfirmationAccepted>(
                   edit_install_confirmation)) {
            require_safe_persistent_checkout_review_targets(checkout, install_scripts);
            if(run_command(build_editor_command(config.editor, install_script)) != 0) {
                throw std::runtime_error(localization::translate_message(
                        "Editor failed."));
            }
            require_safe_persistent_checkout_review_targets(checkout, install_scripts);
            edited = true;
        } else if(!std::holds_alternative<ConfirmationDeclined>(
                          edit_install_confirmation)) {
            DiagnosticIdentity identity;
            identity.canonical_source_identity = pkg_dir.string();
            stop_after_confirmation(
                    std::move(edit_install_confirmation),
                    DiagnosticPhase::Preflight, std::move(identity));
        }
    }

    // LANDMINE(#197): editor はreview対象を置換できるため、review開始時の検証結果を持ち越さない。
    require_safe_persistent_checkout_review_targets(checkout, install_scripts);
    if(edited) {
        ConfirmationResult proceed_confirmation = request_confirmation(
                localization::translate_message("Proceed with build?"),
                ConfirmationDefault::Yes, config.no_confirm);
        if(!std::holds_alternative<ConfirmationAccepted>(
                   proceed_confirmation)) {
            DiagnosticIdentity identity;
            identity.canonical_source_identity = pkg_dir.string();
            stop_after_confirmation(
                    std::move(proceed_confirmation),
                    DiagnosticPhase::Preflight, std::move(identity));
        }
    }
}

std::optional<std::string> read_nofollow_regular_file(
        const fs::path& path) {
    struct open_how how {};
    how.flags = O_RDONLY | O_CLOEXEC | O_NOFOLLOW;
    how.resolve = RESOLVE_BENEATH | RESOLVE_NO_SYMLINKS | RESOLVE_NO_XDEV;
    const int descriptor = static_cast<int>(syscall(
            SYS_openat2, AT_FDCWD, path.c_str(), &how, sizeof(how)));
    if(descriptor < 0) return std::nullopt;

    struct stat status {};
    if(fstat(descriptor, &status) != 0 || !S_ISREG(status.st_mode) ||
       status.st_uid != geteuid() ||
       (status.st_mode & (S_IWGRP | S_IWOTH)) != 0) {
        static_cast<void>(close(descriptor));
        return std::nullopt;
    }

    std::string contents;
    char        buffer[4096];
    while(true) {
        const ssize_t read_size = read(descriptor, buffer, sizeof(buffer));
        if(read_size > 0) {
            contents.append(buffer, static_cast<std::size_t>(read_size));
            continue;
        }
        if(read_size == 0) break;
        if(errno == EINTR) continue;
        static_cast<void>(close(descriptor));
        return std::nullopt;
    }
    if(close(descriptor) != 0) return std::nullopt;
    return contents;
}

std::optional<std::string> read_srcinfo_version(const fs::path& pkg_dir) {
    std::optional<std::string> contents =
            read_nofollow_regular_file(pkg_dir / ".SRCINFO");
    if(!contents.has_value()) return std::nullopt;

    std::istringstream file(contents.value());

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
                    // TRANSLATORS: The placeholders are a package name, its previous version, and its installed version.
                    Logger::info(localization::format_translated_message(
                            "{} was updated by the system transaction ({} -> {}); rebuilding the preferred source package.",
                            pkg_name,
                            pre_upgrade_version.value(),
                            installed_version.value()));
                } else {
                    // TRANSLATORS: The placeholders are a package name and its installed version.
                    Logger::info(localization::format_translated_message(
                            "{} was installed by the system transaction at version {}; rebuilding the preferred source package.",
                            pkg_name,
                            installed_version.value()));
                }
                return UpdateCheckResult::NeedsBuild;
            }
        }
    } catch(...) {
        return UpdateCheckResult::Unknown;
    }

    Logger::info(up_to_date_diagnostic(pkg_name, installed_version.value()));
    return UpdateCheckResult::UpToDate;
}

bool has_local_package_artifact(const fs::path& pkg_dir) {
    std::error_code directory_error;
    fs::file_status directory_status =
            fs::symlink_status(pkg_dir, directory_error);
    if(directory_error || !fs::is_directory(directory_status)) return false;

    fs::directory_iterator entry(pkg_dir, directory_error);
    const fs::directory_iterator end;
    while(!directory_error && entry != end) {
        std::error_code status_error;
        const fs::file_status status = entry->symlink_status(status_error);
        if(!status_error && fs::is_regular_file(status)) {
            std::string filename = entry->path().filename().string();
            if(filename.size() < 4 ||
               filename.substr(filename.size() - 4) != ".sig") {
                if(filename.find(".pkg.tar") != std::string::npos) {
                    return true;
                }
            }
        }

        entry.increment(directory_error);
    }
    return false;
}

bool has_local_srcdir(const fs::path& pkg_dir) {
    std::error_code ec;
    const fs::file_status status =
            fs::symlink_status(pkg_dir / "src", ec);
    return !ec && fs::is_directory(status);
}

MakepkgBuildOptions resolve_makepkg_build_options(
        const fs::path& pkg_dir, const AppConfig& config) {
    MakepkgBuildOptions options;
    bool                has_artifact = has_local_package_artifact(pkg_dir);

    if(config.user_config.build.mode == BuildMode::Clean) {
        options.clean_build = true;
    } else if(has_local_srcdir(pkg_dir)) {
        ConfirmationResult clean_confirmation = request_confirmation(
                localization::translate_message(
                        "Clean build existing build directory?"),
                ConfirmationDefault::No, config.no_confirm);
        if(std::holds_alternative<ConfirmationAccepted>(
                   clean_confirmation)) {
            options.clean_build = true;
        } else if(std::holds_alternative<ConfirmationDeclined>(
                          clean_confirmation)) {
            options.clean_build = false;
        } else {
            stop_after_confirmation(
                    std::move(clean_confirmation), DiagnosticPhase::Build);
        }
    }

    if(config.user_config.build.mode == BuildMode::Rebuild) {
        options.rebuild = true;
    } else if(options.clean_build && has_artifact) {
        options.rebuild = true;
    } else if(has_artifact) {
        ConfirmationResult rebuild_confirmation = request_confirmation(
                localization::translate_message("Rebuild package?"),
                ConfirmationDefault::No, config.no_confirm);
        if(std::holds_alternative<ConfirmationAccepted>(
                   rebuild_confirmation)) {
            options.rebuild = true;
        } else if(std::holds_alternative<ConfirmationDeclined>(
                          rebuild_confirmation)) {
            options.rebuild = false;
        } else {
            stop_after_confirmation(
                    std::move(rebuild_confirmation), DiagnosticPhase::Build);
        }
    }

    return options;
}

struct PreparedSourceBuildCheckout {
    ValidatedCachePath    checkout;
    MakepkgBuildOptions   makepkg_options;
};

using SourceBuildCheckoutPreparation =
        std::variant<
                SourceBuildUpToDate,
                SourceBuildUpdateStatusUnknownSkipped,
                PreparedSourceBuildCheckout>;

SourceBuildCheckoutPreparation prepare_source_build_checkout(
        const SourceBuildRequest& request,
        const std::string& display_name,
        SourceBuildUpdatePolicy update_policy,
        const ValidatedCacheRoot& build_root,
        const AppConfig& config) {
    require_valid_package_name(request.checkout_name);
    if(update_policy == SourceBuildUpdatePolicy::OnlyIfUpdated &&
       !request.installed_snapshot.has_value()) {
        // TRANSLATORS: The placeholder is a package name.
        throw std::runtime_error(localization::format_translated_message(
                "No authoritative installed package snapshot was supplied for {}.",
                request.package_name));
    }
    // TRANSLATORS: The placeholder is a package or PackageBase name.
    Logger::info(localization::format_translated_message(
            "Processing {}...", display_name));
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
                std::string current_url =
                        trusted_git_remote_origin_url(pkg_path);
                if(!remote_url_matches_expected(current_url, request.git_url)) {
                    Logger::warn(localization::translate_message(
                            "Remote URL mismatch. Re-cloning..."));
                } else {
                    needs_clone = false;
                }
            }

            if(!needs_clone) {
                Logger::info(localization::translate_message(
                        "Updating repository..."));
                WorkDirGuard wd_repo(pkg_path);
                pkg_path = revalidate_trusted_cache_path(
                        pkg_path, CachePathRequirement::ExistingDirectory);
                require_safe_persistent_checkout_descendants(pkg_path);
                {
                    ScopedPrivateUmask private_umask;
                    if(trusted_git_fetch_origin(
                               pkg_path, request.git_url) != 0) {
                        throw std::runtime_error(localization::translate_message(
                                "Failed to fetch updates."));
                    }
                }

                // fetch中にcheckoutまたはreview対象が差し替えられた場合、
                // branch検出を含む後続git commandへ進む前にauthorityを失効させる。
                pkg_path = revalidate_trusted_cache_path(
                        pkg_path, CachePathRequirement::ExistingDirectory);
                require_safe_persistent_checkout_descendants(pkg_path);

                std::string branch = trusted_git_detect_remote_branch(
                        pkg_path, request.git_url);
                // TRANSLATORS: The placeholder is a literal Git branch name.
                Logger::info(localization::format_translated_message(
                        "Detected branch: {}", branch));

                if(config.user_config.review.diff == ReviewPolicy::Prompt) {
                    int diff_ret = trusted_git_diff_quiet(
                            pkg_path, request.git_url, branch);
                    if(diff_ret > 1) {
                        throw std::runtime_error(localization::translate_message(
                                "Failed to compare repository changes."));
                    }
                    if(diff_ret == 1) {
                        log_update_diff_guidance(
                                pkg_path, request.git_url, branch);
                        ConfirmationResult diff_confirmation =
                                request_confirmation(
                                        localization::format_translated_message(
                                                "Updates were detected in the existing cache repository. View the {} diff?",
                                                "Git"),
                                        ConfirmationDefault::No,
                                        config.no_confirm);
                        if(std::holds_alternative<ConfirmationAccepted>(
                                   diff_confirmation)) {
                            static_cast<void>(trusted_git_show_diff(
                                    pkg_path, request.git_url, branch));
                        } else if(!std::holds_alternative<
                                          ConfirmationDeclined>(
                                          diff_confirmation)) {
                            DiagnosticIdentity identity;
                            identity.requested_package =
                                    request.package_name;
                            identity.package_base = request.checkout_name;
                            stop_after_confirmation(
                                    std::move(diff_confirmation),
                                    DiagnosticPhase::Fetch,
                                    std::move(identity));
                        }
                    }
                }

                // LANDMINE: reset は build/install 経路だけで許可する。fetch 経路へ持ち込まない。
                pkg_path = revalidate_trusted_cache_path(
                        pkg_path, CachePathRequirement::ExistingDirectory);
                require_safe_persistent_checkout_descendants(pkg_path);
                {
                    ScopedPrivateUmask private_umask;
                    if(trusted_git_reset_hard(
                               pkg_path, request.git_url, branch) != 0) {
                        throw std::runtime_error(
                                localization::translate_message(
                                        "Failed to reset repository."));
                    }
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
            pkg_path = create_trusted_cache_directory(
                    build_root, request.checkout_name);
            Logger::info(localization::translate_message(
                    "Cloning repository..."));
            DirCleanupGuard cleanup_guard(pkg_path);
            {
                ScopedPrivateUmask private_umask;
                if(trusted_git_clone_persistent_checkout(
                           pkg_path, request.git_url) != 0) {
                    // TRANSLATORS: The placeholder is a package checkout name.
                    throw std::runtime_error(localization::format_translated_message(
                            "Failed to clone {}",
                            request.checkout_name));
                }
            }

            pkg_path = revalidate_trusted_cache_path(
                    pkg_path, CachePathRequirement::ExistingDirectory);
            require_safe_persistent_checkout_descendants(pkg_path);
            {
                WorkDirGuard wd_repo(pkg_path);
                std::string current_url = trim(
                        trusted_git_remote_origin_url(pkg_path));
                if(current_url.empty()) {
                    // TRANSLATORS: The placeholders are a literal Git configuration key and a package checkout name.
                    throw std::runtime_error(localization::format_translated_message(
                            "Missing {} for {}.",
                            "remote.origin.url",
                            request.checkout_name));
                }
                if(!remote_url_matches_expected(current_url, request.git_url)) {
                    // TRANSLATORS: The placeholder is a package checkout name.
                    throw std::runtime_error(localization::format_translated_message(
                            "Remote URL mismatch for {}.",
                            request.checkout_name));
                }
            }
            pkg_path = revalidate_trusted_cache_path(
                    pkg_path, CachePathRequirement::ExistingDirectory);
            require_safe_persistent_checkout_descendants(pkg_path);
            cleanup_guard.commit();
        }
    }

    MakepkgBuildOptions makepkg_options;
    {
        pkg_path = revalidate_trusted_cache_path(
                pkg_path, CachePathRequirement::ExistingDirectory);
        require_safe_persistent_checkout_descendants(pkg_path);
        WorkDirGuard wd(pkg_path);

        if(update_policy == SourceBuildUpdatePolicy::OnlyIfUpdated) {
            UpdateCheckResult update_check = check_update_status(
                    request.package_name, ".",
                    request.installed_snapshot.value(), request.update_baseline);
            if(update_check == UpdateCheckResult::UpToDate) {
                static_cast<void>(revalidate_trusted_cache_path(
                        pkg_path, CachePathRequirement::ExistingDirectory));
                return SourceBuildUpToDate{
                        up_to_date_diagnostic(
                                request.package_name,
                                request.installed_snapshot->installed_version.value())};
            }
            if(update_check == UpdateCheckResult::Unknown) {
                // TRANSLATORS: The placeholders are the literal .SRCINFO file name and a package name.
                Logger::warn(localization::format_translated_message(
                        "Unable to determine update status from {} for {}.",
                        ".SRCINFO",
                        request.package_name));
                // TRANSLATORS: The placeholder is the literal PKGBUILD artifact name.
                Logger::warn(localization::format_translated_message(
                        "Skipping pre-review {} evaluation.", "PKGBUILD"));
                ConfirmationResult continue_confirmation =
                        request_confirmation(
                                localization::format_translated_message(
                                        "Update status is unknown because {} is missing or incomplete. Continue to review/build?",
                                        ".SRCINFO"),
                                ConfirmationDefault::No,
                                config.no_confirm);
                if(const auto* declined =
                           std::get_if<ConfirmationDeclined>(
                                   &continue_confirmation)) {
                    SourceBuildUpdateStatusUnknownSkipReason reason =
                            SourceBuildUpdateStatusUnknownSkipReason::
                                    UserDeclined;
                    if(declined->origin ==
                       ConfirmationDecisionOrigin::NoConfirm) {
                        reason = SourceBuildUpdateStatusUnknownSkipReason::
                                NoConfirm;
                    } else if(declined->origin ==
                              ConfirmationDecisionOrigin::
                                      NonInteractiveDefault) {
                        reason = SourceBuildUpdateStatusUnknownSkipReason::
                                NonInteractiveStdin;
                    }
                    std::string diagnostic = unknown_update_skip_diagnostic(
                            request.package_name, reason);
                    Logger::warn(diagnostic);
                    static_cast<void>(revalidate_trusted_cache_path(
                            pkg_path,
                            CachePathRequirement::ExistingDirectory));
                    return SourceBuildUpdateStatusUnknownSkipped{
                            reason,
                            std::move(diagnostic)};
                }
                if(!std::holds_alternative<ConfirmationAccepted>(
                           continue_confirmation)) {
                    DiagnosticIdentity identity;
                    identity.requested_package = request.package_name;
                    identity.package_base = request.checkout_name;
                    stop_after_confirmation(
                            std::move(continue_confirmation),
                            DiagnosticPhase::Preflight,
                            std::move(identity));
                }
            }
        }

        review_build_files(pkg_path, config);
        makepkg_options = resolve_makepkg_build_options(".", config);
    }

    const std::string custom_environment = serialize_source_build_environment(
            request.custom_environment, request.empty_value_policy);
    if(!trim(custom_environment).empty()) {
        // TRANSLATORS: The placeholder is a literal serialized environment assignment list.
        Logger::info(localization::format_translated_message(
                "Applying custom build flags: {}", custom_environment));
    } else {
        // TRANSLATORS: The placeholder is the literal makepkg.conf file name.
        Logger::info(localization::format_translated_message(
                "Using default {} settings.", "makepkg.conf"));
    }

    // LANDMINE(#175,#197,#242,#268): review/editor後のcheckoutだけを
    // singular/set lifecycleへ渡す。private artifact rootはowner側で作る。
    pkg_path = revalidate_trusted_cache_path(
            pkg_path, CachePathRequirement::ExistingDirectory);
    require_safe_persistent_checkout_descendants(pkg_path);
    return PreparedSourceBuildCheckout{
            std::move(pkg_path), makepkg_options};
}

void require_package_base_source_build_request(
        const SourceBuildRequest& request,
        const std::vector<RequiredPackageArtifactTarget>& required_targets) {
    require_valid_package_name(request.checkout_name);
    if(request.git_url.empty()) {
        throw std::logic_error(
                localization::format_translated_message(
                        "{} set source-build request has an empty {} URL.",
                        "PackageBase", "Git"));
    }
    if(request.only_if_updated) {
        throw std::logic_error(
                localization::format_translated_message(
                        "{} set source-build does not support only-if-updated execution.",
                        "PackageBase"));
    }
    if(required_targets.empty()) {
        throw std::logic_error(
                localization::format_translated_message(
                        "{} set source-build requires at least one required package target.",
                        "PackageBase"));
    }
    for(std::size_t index = 0; index < required_targets.size(); ++index) {
        const RequiredPackageArtifactTarget& target = required_targets[index];
        require_valid_package_name(target.package_base);
        require_valid_package_name(target.package_name);
        if(target.package_base != request.checkout_name) {
            throw std::logic_error(
                    localization::format_translated_message(
                            "{} set source-build required target attribution is inconsistent.",
                            "PackageBase"));
        }
        if(std::any_of(
                   required_targets.begin(),
                   required_targets.begin() + index,
                   [&target](const RequiredPackageArtifactTarget& existing) {
                       return existing.package_name == target.package_name;
                   })) {
            throw std::logic_error(
                    localization::format_translated_message(
                            "{} set source-build contains a duplicate required package target.",
                            "PackageBase"));
        }
        switch(target.desired_reason) {
        case DesiredInstallReason::Explicit:
        case DesiredInstallReason::Dependency:
            break;
        default:
            throw std::logic_error(
                    localization::format_translated_message(
                            "{} set source-build has an unknown install reason.",
                            "PackageBase"));
        }
    }
    if(required_targets.size() == 1) {
        require_valid_package_name(request.package_name);
        if(request.package_name != required_targets.front().package_name) {
            throw std::logic_error(
                    localization::format_translated_message(
                            "{} set source-build singular request identity is inconsistent.",
                            "PackageBase"));
        }
    } else if(!request.package_name.empty()) {
        throw std::logic_error(
                localization::format_translated_message(
                        "{} set source-build multiple request must not expose a singular package name.",
                        "PackageBase"));
    }
}

} // namespace

SourceBuildPreparationOutcome prepare_source_build_for_execution(
        const SourceBuildRequest& request,
        const std::string& display_name,
        SourceBuildUpdatePolicy update_policy,
        const ValidatedCacheRoot& cache_root,
        const AppConfig& config) {
    // POLICY: checkoutのclone/fetch/resetより先に、artifact ownerと同じ
    // private cache contractを証明し、NeedsBuild capabilityへ一緒に保持する。
    ValidatedPrivateCacheRoot artifact_root = [&]() {
        try {
            return prepare_private_trusted_cache_root(cache_root);
        } catch(const TrustedCacheError&) {
            throw;
        } catch(const ConfirmationOperationStopped&) {
            throw;
        } catch(const std::exception& error) {
            throw SourceBuildPreparationError(
                    SourceBuildPreparationFailureStage::ArtifactRoot,
                    error.what());
        } catch(...) {
            throw SourceBuildPreparationError(
                    SourceBuildPreparationFailureStage::ArtifactRoot,
                    localization::translate_message(
                            "Private artifact workspace preparation failed."),
                    true);
        }
    }();

    SourceBuildCheckoutPreparation checkout_preparation = [&]() {
        try {
            return prepare_source_build_checkout(
                    request, display_name, update_policy, cache_root,
                    config);
        } catch(const TrustedCacheError&) {
            throw;
        } catch(const ConfirmationOperationStopped&) {
            throw;
        } catch(const std::exception& error) {
            throw SourceBuildPreparationError(
                    SourceBuildPreparationFailureStage::CheckoutOrReview,
                    error.what());
        } catch(...) {
            throw SourceBuildPreparationError(
                    SourceBuildPreparationFailureStage::CheckoutOrReview,
                    localization::translate_message(
                            "Source checkout or build preparation failed."),
                    true);
        }
    }();

    if(auto* up_to_date =
               std::get_if<SourceBuildUpToDate>(&checkout_preparation)) {
        return std::move(*up_to_date);
    }
    if(auto* skipped = std::get_if<
               SourceBuildUpdateStatusUnknownSkipped>(
                       &checkout_preparation)) {
        return std::move(*skipped);
    }
    PreparedSourceBuildCheckout checkout = std::move(
            std::get<PreparedSourceBuildCheckout>(checkout_preparation));
    return SourceBuildPreparationAccess::make(
            std::move(checkout.checkout), std::move(artifact_root),
            checkout.makepkg_options.rebuild,
            checkout.makepkg_options.clean_build);
}

SourceBuildExecutionResult execute_source_build_typed(
        const SourceBuildRequest& request,
        const ValidatedCacheRoot& cache_root,
        DesiredInstallReason desired_reason,
        const PacmanDatabasePaths& database_paths,
        const AppConfig& config) {
    // generic/direct/system compatibility pathはsingular identityを引き続き要求する。
    require_valid_package_name(request.package_name);
    SourceBuildPreparationOutcome preparation = [&]() {
        try {
            return prepare_source_build_for_execution(
                    request, request.package_name,
                    request.only_if_updated
                            ? SourceBuildUpdatePolicy::OnlyIfUpdated
                            : SourceBuildUpdatePolicy::AlwaysBuild,
                    cache_root, config);
        } catch(const SourceBuildPreparationError& error) {
            throw std::runtime_error(error.what());
        }
    }();
    if(const auto* up_to_date =
               std::get_if<SourceBuildUpToDate>(&preparation)) {
        return SourceBuildExecutionResult{
                SourceBuildExecutionStatus::UpToDate,
                std::nullopt,
                up_to_date->diagnostic};
    }
    if(const auto* skipped = std::get_if<
               SourceBuildUpdateStatusUnknownSkipped>(&preparation)) {
        return SourceBuildExecutionResult{
                SourceBuildExecutionStatus::UpdateStatusUnknownSkipped,
                skipped->reason,
                skipped->diagnostic};
    }
    PreparedSourceBuildExecutionCapabilities prepared =
            SourceBuildPreparedExecutionAccess::consume(std::move(
                    std::get<PreparedSourceBuildNeedsBuild>(preparation)));

    return source_build_result_from_artifact_outcome(
            execute_separated_source_build_unit(
                    SeparatedSourceBuildUnitRequest{
                            std::move(prepared.checkout),
                            std::move(prepared.artifact_root),
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
                            .rebuild = prepared.rebuild,
                            .clean_build = prepared.clean_build}));
}

PackageBaseSourceBuildExecutionResult
execute_prepared_source_build_package_base_typed(
        const SourceBuildRequest& request,
        const std::vector<RequiredPackageArtifactTarget>& required_targets,
        PreparedSourceBuildNeedsBuild prepared,
        const PacmanDatabasePaths& database_paths,
        const AppConfig& config) {
    require_package_base_source_build_request(request, required_targets);
    require_supported_separated_install_options(config.rm_deps);
    require_unclaimed_artifact_pkgdest(request.custom_environment);
    PreparedSourceBuildExecutionCapabilities capabilities =
            SourceBuildPreparedExecutionAccess::consume(
                    std::move(prepared));

    return execute_separated_package_base_source_build(
            SeparatedPackageBaseSourceBuildRequest{
                    std::move(capabilities.checkout),
                    std::move(capabilities.artifact_root),
                    request.checkout_name,
                    required_targets,
                    request.custom_environment,
                    request.empty_value_policy,
                    database_paths},
            SeparatedSourceBuildUnitOptions{
                    .no_confirm = config.no_confirm,
                    .needed = request.needed,
                    .rm_deps = config.rm_deps,
                    .rebuild = capabilities.rebuild,
                    .clean_build = capabilities.clean_build});
}

PackageBaseSourceBuildExecutionResult
execute_source_build_package_base_typed(
        const SourceBuildRequest& request,
        const std::vector<RequiredPackageArtifactTarget>& required_targets,
        const ValidatedCacheRoot& cache_root,
        const PacmanDatabasePaths& database_paths,
        const AppConfig& config) {
    // request/target correlationはcheckout/cache mutationより前に再証明する。
    require_package_base_source_build_request(request, required_targets);
    require_supported_separated_install_options(config.rm_deps);
    require_unclaimed_artifact_pkgdest(request.custom_environment);
    SourceBuildPreparationOutcome preparation = [&]() {
        try {
            return prepare_source_build_for_execution(
                    request, request.checkout_name,
                    SourceBuildUpdatePolicy::AlwaysBuild,
                    cache_root, config);
        } catch(const TrustedCacheError&) {
            throw;
        } catch(const SourceBuildPreparationError& error) {
            const bool artifact_root_failure =
                    error.stage() ==
                    SourceBuildPreparationFailureStage::ArtifactRoot;
            std::string diagnostic;
            if(error.unknown_exception()) {
                diagnostic = artifact_root_failure
                        ? localization::format_translated_message(
                                  "{} private artifact workspace preparation failed.",
                                  "PackageBase")
                        : localization::format_translated_message(
                                  "{} source checkout or build preparation failed.",
                                  "PackageBase");
            } else {
                diagnostic = artifact_root_failure
                        ? localization::format_translated_message(
                                  "{} private artifact workspace preparation failed: {}",
                                  "PackageBase", error.what())
                        : localization::format_translated_message(
                                  "{} source checkout or build preparation failed: {}",
                                  "PackageBase", error.what());
            }
            throw SeparatedPackageBaseSourceBuildPhaseError(
                    SeparatedPackageBaseSourceBuildFailurePhase::Build,
                    std::move(diagnostic));
        }
    }();
    if(!std::holds_alternative<PreparedSourceBuildNeedsBuild>(preparation)) {
        throw std::logic_error(
                localization::format_translated_message(
                        "{} set source-build unexpectedly produced an update-status result.",
                        "PackageBase"));
    }
    return execute_prepared_source_build_package_base_typed(
            request, required_targets,
            std::move(std::get<PreparedSourceBuildNeedsBuild>(preparation)),
            database_paths, config);
}

std::optional<ArtifactInstallExecutionOutcome> execute_source_build(
        const SourceBuildRequest& request,
        const ValidatedCacheRoot& cache_root,
        DesiredInstallReason desired_reason,
        const PacmanDatabasePaths& database_paths,
        const AppConfig& config) {
    const SourceBuildExecutionResult result = execute_source_build_typed(
            request, cache_root, desired_reason, database_paths, config);
    switch(result.status) {
        case SourceBuildExecutionStatus::Installed:
            return ArtifactInstallExecutionOutcome::Installed;
        case SourceBuildExecutionStatus::SkippedAsNeeded:
            return ArtifactInstallExecutionOutcome::SkippedAsNeeded;
        case SourceBuildExecutionStatus::UpToDate:
        case SourceBuildExecutionStatus::UpdateStatusUnknownSkipped:
            return std::nullopt;
    }
    throw std::logic_error(localization::translate_message(
            "Unknown source-build execution status."));
}
