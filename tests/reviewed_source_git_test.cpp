#include "persistent_checkout.hpp"
#include "process.hpp"
#include "reviewed_source_presentation.hpp"
#include "reviewed_source_projection.hpp"
#include "trusted_cache.hpp"
#include "trusted_cache_test_support.hpp"
#include "trusted_git.hpp"

#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <type_traits>
#include <unistd.h>
#include <utility>
#include <variant>
#include <vector>

namespace fs = std::filesystem;

namespace {

void require(bool condition, const std::string& message) {
    if(!condition) throw std::runtime_error(message);
}

template<typename Function>
void expect_runtime_error(Function&& function, std::string_view message) {
    try {
        std::forward<Function>(function)();
    } catch(const std::runtime_error&) {
        return;
    }
    throw std::runtime_error(std::string(message));
}

AurReviewedSourceReviewIdentity aur_review_identity(
        const std::string& package_base,
        const std::string& remote,
        const SourceRevisionIdentity& target) {
    return AurReviewedSourceReviewIdentity::make(
            PackageBaseIdentity::make(
                    PackageSourceIdentity::aur(
                            SourceLocationIdentity::known_git_remote(remote)),
                    package_base),
            target);
}

template<typename Expected, typename Variant>
const Expected& require_arm(
        const Variant& value, std::string_view message) {
    const Expected* arm = std::get_if<Expected>(&value);
    if(arm == nullptr) throw std::runtime_error(std::string(message));
    return *arm;
}

class TemporaryTree final {
public:
    TemporaryTree() {
        std::string path_template =
                "/tmp/moguet-reviewed-source-git-XXXXXX";
        std::vector<char> writable(
                path_template.begin(), path_template.end());
        writable.push_back('\0');
        char* created = mkdtemp(writable.data());
        if(created == nullptr) {
            throw std::runtime_error(
                    "Failed to create reviewed-source Git fixture root");
        }
        path_ = created;
    }

    TemporaryTree(const TemporaryTree&) = delete;
    TemporaryTree& operator=(const TemporaryTree&) = delete;

    ~TemporaryTree() noexcept {
        std::error_code error;
        fs::remove_all(path_, error);
    }

    const fs::path& path() const noexcept {
        return path_;
    }

private:
    fs::path path_;
};

std::vector<std::string> git_environment(const fs::path& home) {
    return {
            "PATH=/usr/bin:/bin",
            "LC_ALL=C",
            "LANG=C",
            "HOME=" + home.string(),
            "GIT_CONFIG_NOSYSTEM=1",
            "GIT_CONFIG_GLOBAL=/dev/null",
            "GIT_AUTHOR_NAME=Slice 3A Fixture",
            "GIT_AUTHOR_EMAIL=slice3a@example.invalid",
            "GIT_COMMITTER_NAME=Slice 3A Fixture",
            "GIT_COMMITTER_EMAIL=slice3a@example.invalid",
            "GIT_TERMINAL_PROMPT=0",
    };
}

class GitFixture final {
public:
    explicit GitFixture(GitObjectFormat object_format) {
        const fs::path cache_home = tree_.path() / "cache";
        home_ = tree_.path() / "home";
        fs::create_directory(cache_home);
        fs::create_directory(home_);
        require(chmod(cache_home.c_str(), 0700) == 0,
                "Failed to secure fixture cache home");
        require(chmod(home_.c_str(), 0700) == 0,
                "Failed to secure fixture home");
        require(setenv("XDG_CACHE_HOME", cache_home.c_str(), 1) == 0,
                "Failed to set fixture XDG_CACHE_HOME");
        require(setenv("HOME", home_.c_str(), 1) == 0,
                "Failed to set fixture HOME");

        root_.emplace(prepare_test_trusted_cache_root());
        checkout_.emplace(create_trusted_cache_directory(*root_, "checkout"));
        repository_ = checkout_->path();
        remote_url_ = "https://aur.archlinux.org/slice3a-fixture.git";

        std::vector<std::string> init{
                "init", "-q", "-b", "main"};
        if(object_format == GitObjectFormat::Sha256) {
            init.push_back("--object-format=sha256");
        }
        run_git(init);
        run_git({"config", "--local", "remote.origin.url", remote_url_});
        run_git({"config", "--local", "remote.origin.fetch",
                 "+refs/heads/*:refs/remotes/origin/*"});
        run_git({"config", "--local", "branch.main.remote", "origin"});
        run_git({"config", "--local", "branch.main.merge",
                 "refs/heads/main"});
    }

    const std::string& remote_url() const noexcept {
        return remote_url_;
    }

    ValidatedCachePath checkout() const {
        return revalidate_trusted_cache_path(
                *checkout_, CachePathRequirement::ExistingDirectory);
    }

    const fs::path& repository() const noexcept {
        return repository_;
    }

    void write_file(
            const std::string& relative_path,
            std::string_view contents,
            bool executable = false) {
        const fs::path destination = repository_ / fs::path(relative_path);
        if(!destination.parent_path().empty()) {
            fs::create_directories(destination.parent_path());
        }
        std::ofstream output(destination, std::ios::binary | std::ios::trunc);
        if(!output) throw std::runtime_error("Failed to open fixture file");
        output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        output.close();
        if(!output) throw std::runtime_error("Failed to write fixture file");
        require(chmod(destination.c_str(), executable ? 0755 : 0644) == 0,
                "Failed to set fixture file mode");
    }

    std::string read_file(const std::string& relative_path) const {
        std::ifstream input(
                repository_ / fs::path(relative_path), std::ios::binary);
        if(!input) throw std::runtime_error("Failed to read fixture file");
        return std::string(
                std::istreambuf_iterator<char>(input),
                std::istreambuf_iterator<char>());
    }

    void remove_path(const std::string& relative_path) {
        std::error_code error;
        fs::remove(repository_ / fs::path(relative_path), error);
        if(error) throw std::runtime_error("Failed to remove fixture path");
    }

    void rename_path(
            const std::string& old_path, const std::string& new_path) {
        fs::rename(repository_ / fs::path(old_path),
                   repository_ / fs::path(new_path));
    }

    void make_symlink(
            const std::string& target, const std::string& relative_path) {
        const fs::path destination = repository_ / fs::path(relative_path);
        require(::symlink(target.c_str(), destination.c_str()) == 0,
                "Failed to create fixture symlink");
    }

    void copy_file(
            const std::string& source, const std::string& destination) {
        fs::copy_file(
                repository_ / fs::path(source),
                repository_ / fs::path(destination));
    }

    void stage_all() {
        run_git({"add", "-A"});
    }

    std::string commit(std::string_view message) {
        stage_all();
        return commit_index(message);
    }

    std::string commit_index(std::string_view message) {
        run_git({"commit", "-q", "-m", std::string(message)});
        return output_git({"rev-parse", "HEAD"});
    }

    std::string commit_empty(std::string_view message) {
        run_git({"commit", "--allow-empty", "-q", "-m",
                 std::string(message)});
        return output_git({"rev-parse", "HEAD"});
    }

    void set_gitlink(
            const std::string& path, const std::string& commit_oid) {
        run_git({"update-index", "--add", "--cacheinfo",
                 "160000," + commit_oid + "," + path});
    }

    void update_remote(const std::string& commit_oid) {
        run_git({"update-ref", "refs/remotes/origin/main", commit_oid});
    }

    std::string output_git(std::vector<std::string> arguments) const {
        std::vector<std::string> complete{
                "-C", repository_.string()};
        complete.insert(
                complete.end(), arguments.begin(), arguments.end());
        CapturedCommandResult result = capture_explicit_process_output_raw(
                ExplicitProcessInvocation{
                        "/usr/bin/git", std::move(complete),
                        git_environment(home_)});
        if(result.exit_code != 0 || result.stdout_capture_limit_exceeded ||
           result.output.empty() || result.output.back() != '\n') {
            throw std::runtime_error("Fixture Git capture failed");
        }
        result.output.pop_back();
        return result.output;
    }

    void run_git(std::vector<std::string> arguments) const {
        std::vector<std::string> complete{
                "-C", repository_.string()};
        complete.insert(
                complete.end(), arguments.begin(), arguments.end());
        const int exit_code = run_explicit_process(
                ExplicitProcessInvocation{
                        "/usr/bin/git", std::move(complete),
                        git_environment(home_)},
                true, true);
        if(exit_code != 0) {
            throw std::runtime_error(
                    "Fixture Git command failed with exit code " +
                    std::to_string(exit_code));
        }
    }

private:
    TemporaryTree tree_;
    fs::path home_;
    fs::path repository_;
    std::string remote_url_;
    std::optional<ValidatedCacheRoot> root_;
    std::optional<ValidatedCachePath> checkout_;
};

const ReviewedSourceFileChange* find_new_path(
        const std::vector<ReviewedSourceFileChange>& changes,
        std::string_view path) {
    for(const ReviewedSourceFileChange& change : changes) {
        const bool matches = std::visit(
                [path](const auto& value) {
                    using Change = std::decay_t<decltype(value)>;
                    if constexpr(std::is_same_v<Change, ReviewedSourceDeleted>) {
                        return false;
                    } else {
                        return value.new_version.path().raw_bytes() == path;
                    }
                },
                change);
        if(matches) return &change;
    }
    return nullptr;
}

const ReviewedSourceFileChange* find_old_path(
        const std::vector<ReviewedSourceFileChange>& changes,
        std::string_view path) {
    for(const ReviewedSourceFileChange& change : changes) {
        const bool matches = std::visit(
                [path](const auto& value) {
                    using Change = std::decay_t<decltype(value)>;
                    if constexpr(std::is_same_v<Change, ReviewedSourceAdded>) {
                        return false;
                    } else {
                        return value.old_version.path().raw_bytes() == path;
                    }
                },
                change);
        if(matches) return &change;
    }
    return nullptr;
}

const ReviewedSourceReviewEntry* find_materialized_new_path(
        const std::vector<ReviewedSourceReviewEntry>& entries,
        std::string_view path) {
    for(const ReviewedSourceReviewEntry& entry : entries) {
        const bool matches = std::visit(
                [path](const auto& value) {
                    using Change = std::decay_t<decltype(value)>;
                    if constexpr(std::is_same_v<Change, ReviewedSourceDeleted>) {
                        return false;
                    } else {
                        return value.new_version.path().raw_bytes() == path;
                    }
                },
                entry.change);
        if(matches) return &entry;
    }
    return nullptr;
}

const ReviewedSourceReviewEntry* find_materialized_old_path(
        const std::vector<ReviewedSourceReviewEntry>& entries,
        std::string_view path) {
    for(const ReviewedSourceReviewEntry& entry : entries) {
        const bool matches = std::visit(
                [path](const auto& value) {
                    using Change = std::decay_t<decltype(value)>;
                    if constexpr(std::is_same_v<Change, ReviewedSourceAdded>) {
                        return false;
                    } else {
                        return value.old_version.path().raw_bytes() == path;
                    }
                },
                entry.change);
        if(matches) return &entry;
    }
    return nullptr;
}

bool git_marker_is_binary(const ReviewedSourceFileChange& change) {
    return std::visit(
            [](const auto& value) {
                return std::holds_alternative<ReviewedSourceBinaryChange>(
                        value.content);
            },
            change);
}

std::string repeated_lines(char prefix, int count) {
    std::string result;
    for(int i = 0; i < count; ++i) {
        result += prefix;
        result += "-line-" + std::to_string(i) + "\n";
    }
    return result;
}

struct Sha1History {
    std::string first;
    std::string same_tree;
    std::string target;
    std::string weird_path;
};

Sha1History populate_sha1_history(GitFixture& fixture) {
    std::string weird_path = "nested/control-";
    weird_path.push_back('\t');
    weird_path.push_back('\n');
    weird_path.push_back('\x1b');
    weird_path.push_back(static_cast<char>(0xff));

    fixture.write_file("PKGBUILD", "pkgname=fixture\npkgver=1\npkgrel=1\n");
    fixture.write_file(".SRCINFO", "pkgbase = fixture\npkgver = 1\n");
    fixture.write_file("modified", "before\n");
    fixture.write_file("deleted", "delete me\n");
    fixture.write_file("rename-old", repeated_lines('r', 30));
    fixture.write_file("low-old", repeated_lines('a', 30));
    fixture.write_file("type", "regular before\n");
    fixture.write_file("gitlink-type", "regular before\n");
    fixture.write_file("mode", "same contents\n");
    fixture.write_file("binary.dat", std::string("binary\0one", 10));
    fixture.write_file("copy-source", "copy source\n");
    fixture.write_file("space name", "space path\n");
    fixture.write_file(weird_path, "weird before\n");
    const std::string first = fixture.commit("first");
    const std::string same_tree = fixture.commit_empty("same tree");

    fixture.write_file("PKGBUILD", "pkgname=fixture\npkgver=2\npkgrel=1\n");
    fixture.write_file(".SRCINFO", "pkgbase = fixture\npkgver = 2\n");
    fixture.write_file("modified", "after\nmore\n");
    fixture.remove_path("deleted");
    fixture.rename_path("rename-old", "rename-new");
    fixture.write_file(
            "rename-new", repeated_lines('r', 30) + "small change\n");
    fixture.rename_path("low-old", "low-new");
    fixture.write_file("low-new", repeated_lines('z', 30));
    fixture.remove_path("type");
    fixture.make_symlink("PKGBUILD", "type");
    fixture.write_file("mode", "same contents\n", true);
    fixture.write_file("binary.dat", std::string("binary\0two", 10));
    fixture.copy_file("copy-source", "copy-target");
    fixture.write_file("unknown.future", "unknown extension\n");
    fixture.write_file(weird_path, "weird after\n");
    fixture.remove_path("gitlink-type");
    fixture.stage_all();
    fixture.set_gitlink("gitlink-type", first);
    const std::string target = fixture.commit_index("target");
    fixture.update_remote(target);
    return Sha1History{first, same_tree, target, std::move(weird_path)};
}

void require_failure_reason(
        const TrustedGitReviewedSourceProjectionResult& result,
        TrustedGitReviewFailureReason reason,
        std::string_view message) {
    const auto& failure = require_arm<TrustedGitReviewFailure>(result, message);
    require(failure.reason == reason, std::string(message));
}

void test_sha1_projection_and_pinned_ref() {
    GitFixture fixture(GitObjectFormat::Sha1);
    const Sha1History history = populate_sha1_history(fixture);
    ValidatedCachePath checkout = fixture.checkout();

    const auto resolved = trusted_git_resolve_remote_commit(
            checkout, fixture.remote_url(), "main");
    const SourceRevisionIdentity& target =
            require_arm<SourceRevisionIdentity>(
                    resolved, "SHA-1 target resolution failed");
    require(target.git_commit() != nullptr &&
                    *target.git_commit() == history.target,
            "Resolved target did not pin the remote ref");

    fixture.update_remote(history.first);
    fixture.write_file(".gitattributes", "PKGBUILD -diff\n");
    fixture.write_file(
            "PKGBUILD",
            "pkgname=fixture\npkgver=2\npkgrel=1\n# dirty worktree\n");
    const std::string dirty_pkgbuild = fixture.read_file("PKGBUILD");
    const std::string index_before = fixture.read_file(".git/index");
    const std::string head_before = fixture.output_git({"rev-parse", "HEAD"});

    const SourceRevisionIdentity baseline =
            SourceRevisionIdentity::git_commit(history.same_tree);
    const auto update_result = trusted_git_project_reviewed_source(
            checkout, fixture.remote_url(), target, baseline);
    if(const auto* failure = std::get_if<TrustedGitReviewFailure>(
               &update_result)) {
        throw std::runtime_error(
                "Ancestor update projection failed: reason=" +
                std::to_string(static_cast<int>(failure->reason)) +
                " stage=" +
                std::to_string(static_cast<int>(failure->stage)) +
                " record=" + std::to_string(failure->record_index) +
                " exit=" +
                (failure->exit_code.has_value()
                         ? std::to_string(*failure->exit_code)
                         : std::string("none")));
    }
    const auto& update = require_arm<ReviewedSourceUpdateReview>(
            update_result, "Ancestor update projection failed");
    require(update.relation == ReviewedSourceHistoryRelation::Ancestor,
            "Ancestor relation was not retained");
    require(update.target.git_commit() != nullptr &&
                    *update.target.git_commit() == history.target,
            "Projection re-resolved the moved mutable ref");

    const auto* pkgbuild_change = find_new_path(update.changes, "PKGBUILD");
    require(pkgbuild_change != nullptr &&
                    std::holds_alternative<ReviewedSourceModified>(
                            *pkgbuild_change),
            "PKGBUILD modification was not retained");
    require(std::holds_alternative<ReviewedSourceTextChange>(
                    std::get<ReviewedSourceModified>(*pkgbuild_change).content),
            "Dirty worktree attributes changed pinned target classification");

    const auto* srcinfo_change = find_new_path(update.changes, ".SRCINFO");
    require(srcinfo_change != nullptr &&
                    std::get<ReviewedSourceModified>(*srcinfo_change)
                                    .new_version.classification() ==
                            ReviewedSourceFileClassification::GeneratedMetadata,
            ".SRCINFO generated metadata classification was lost");

    const auto* renamed = find_new_path(update.changes, "rename-new");
    require(renamed != nullptr &&
                    std::holds_alternative<ReviewedSourceRenamed>(*renamed) &&
                    std::get<ReviewedSourceRenamed>(*renamed)
                                    .old_version.path().raw_bytes() ==
                            "rename-old" &&
                    std::get<ReviewedSourceRenamed>(*renamed).similarity >= 50,
            ">=50% rename was not retained with old/new paths");
    require(find_old_path(update.changes, "low-old") != nullptr &&
                    std::holds_alternative<ReviewedSourceDeleted>(
                            *find_old_path(update.changes, "low-old")) &&
                    find_new_path(update.changes, "low-new") != nullptr &&
                    std::holds_alternative<ReviewedSourceAdded>(
                            *find_new_path(update.changes, "low-new")),
            "<50% move did not remain Deleted + Added");
    require(find_new_path(update.changes, "copy-target") != nullptr &&
                    std::holds_alternative<ReviewedSourceAdded>(
                            *find_new_path(update.changes, "copy-target")),
            "Copy detection was unexpectedly enabled");
    require(std::holds_alternative<ReviewedSourceTypeChanged>(
                    *find_new_path(update.changes, "type")),
            "Regular-to-symlink change was not TypeChanged");
    require(std::holds_alternative<ReviewedSourceTypeChanged>(
                    *find_new_path(update.changes, "gitlink-type")),
            "Regular-to-gitlink change was not TypeChanged");
    require(std::holds_alternative<ReviewedSourceModified>(
                    *find_new_path(update.changes, "mode")),
            "Regular-to-executable change was not Modified");
    const auto* binary = find_new_path(update.changes, "binary.dat");
    require(binary != nullptr &&
                    std::holds_alternative<ReviewedSourceBinaryChange>(
                            std::get<ReviewedSourceModified>(*binary).content),
            "Binary change was flattened");
    require(find_new_path(update.changes, "unknown.future") != nullptr,
            "Unknown extension was filtered out");
    require(find_new_path(update.changes, history.weird_path) != nullptr,
            "Opaque control/invalid UTF-8 path bytes were lost");
    require(fixture.read_file("PKGBUILD") == dirty_pkgbuild,
            "Read-only projection changed the dirty worktree");
    require(fixture.read_file(".git/index") == index_before,
            "Read-only projection changed the Git index");
    require(fixture.output_git({"rev-parse", "HEAD"}) == head_before,
            "Read-only projection changed HEAD");
    require(fixture.output_git(
                    {"rev-parse", "refs/remotes/origin/main"}) ==
                    history.first,
            "Read-only projection changed the moved remote ref");

    const std::string expected_pkgbuild_object =
            std::get<ReviewedSourceModified>(*pkgbuild_change)
                    .new_version.object_id().value();
    fixture.remove_path("PKGBUILD");
    const auto deleted_worktree_result = trusted_git_project_reviewed_source(
            checkout, fixture.remote_url(), target, baseline);
    const auto& deleted_worktree_update =
            require_arm<ReviewedSourceUpdateReview>(
                    deleted_worktree_result,
                    "Deleted worktree PKGBUILD changed pinned projection");
    const auto* deleted_worktree_pkgbuild = find_new_path(
            deleted_worktree_update.changes, "PKGBUILD");
    require(deleted_worktree_pkgbuild != nullptr &&
                    std::holds_alternative<ReviewedSourceModified>(
                            *deleted_worktree_pkgbuild) &&
                    std::get<ReviewedSourceModified>(
                            *deleted_worktree_pkgbuild)
                                    .new_version.object_id().value() ==
                            expected_pkgbuild_object,
            "Deleted worktree PKGBUILD replaced target-tree authority");
    require(!fs::exists(fixture.repository() / "PKGBUILD"),
            "Pinned projection recreated the deleted worktree PKGBUILD");
    require(fixture.read_file(".git/index") == index_before,
            "Pinned projection changed the index after worktree deletion");
    require(fixture.output_git({"rev-parse", "HEAD"}) == head_before,
            "Pinned projection changed HEAD after worktree deletion");

    const auto initial_result = trusted_git_project_reviewed_source(
            checkout, fixture.remote_url(), target, std::nullopt);
    const auto& initial = require_arm<ReviewedSourceInitialFullReview>(
            initial_result, "Initial full review failed");
    require(!initial.changes.empty(), "Initial full review was empty");
    for(const auto& change : initial.changes) {
        require(std::holds_alternative<ReviewedSourceAdded>(change),
                "Initial full review contained a non-Added status");
    }
    require(find_new_path(initial.changes, "space name") != nullptr,
            "Space-containing tracked path was filtered out");

    require(std::holds_alternative<ReviewedSourceAlreadyReviewed>(
                    trusted_git_project_reviewed_source(
                            checkout, fixture.remote_url(), target, target)),
            "Same revision was not AlreadyReviewed");

    const SourceRevisionIdentity first =
            SourceRevisionIdentity::git_commit(history.first);
    const SourceRevisionIdentity same_tree =
            SourceRevisionIdentity::git_commit(history.same_tree);
    const auto same_tree_result = trusted_git_project_reviewed_source(
            checkout, fixture.remote_url(), same_tree, first);
    const auto& same_tree_update = require_arm<ReviewedSourceUpdateReview>(
            same_tree_result,
            "Different commits with the same tree were rejected");
    require(same_tree_update.changes.empty(),
            "Same-tree commits produced file changes");

    const std::string first_tree = fixture.output_git(
            {"rev-parse", history.first + "^{tree}"});
    const std::string dangling = fixture.output_git(
            {"commit-tree", first_tree, "-m", "dangling"});
    const auto nonancestor_result = trusted_git_project_reviewed_source(
            checkout, fixture.remote_url(), target,
            SourceRevisionIdentity::git_commit(dangling));
    require(require_arm<ReviewedSourceUpdateReview>(
                    nonancestor_result,
                    "Dangling non-ancestor baseline was unavailable")
                            .relation ==
                    ReviewedSourceHistoryRelation::NonAncestor,
            "Dangling baseline did not retain NonAncestor relation");

    const auto missing_result = trusted_git_project_reviewed_source(
            checkout, fixture.remote_url(), target,
            SourceRevisionIdentity::git_commit(std::string(40, 'f')));
    require(std::holds_alternative<ReviewedSourceRebaselineFullReview>(
                    missing_result),
            "Missing baseline did not produce RebaselineFullReview");
    const std::string blob = fixture.output_git(
            {"rev-parse", history.target + ":PKGBUILD"});
    require(std::holds_alternative<ReviewedSourceRebaselineFullReview>(
                    trusted_git_project_reviewed_source(
                            checkout, fixture.remote_url(), target,
                            SourceRevisionIdentity::git_commit(blob))),
            "Non-commit baseline did not produce RebaselineFullReview");
    require(std::holds_alternative<ReviewedSourceRebaselineFullReview>(
                    trusted_git_project_reviewed_source(
                            checkout, fixture.remote_url(), target,
                            SourceRevisionIdentity::git_commit(first_tree))),
            "Tree baseline did not produce RebaselineFullReview");
    fixture.run_git({"tag", "-a", "baseline-tag", history.first,
                     "-m", "baseline tag"});
    const std::string tag = fixture.output_git(
            {"rev-parse", "baseline-tag^{tag}"});
    require(std::holds_alternative<ReviewedSourceRebaselineFullReview>(
                    trusted_git_project_reviewed_source(
                            checkout, fixture.remote_url(), target,
                            SourceRevisionIdentity::git_commit(tag))),
            "Tag baseline did not produce RebaselineFullReview");

    fixture.run_git({"replace", "-f", history.target, dangling});
    const auto replacement_result = trusted_git_project_reviewed_source(
            checkout, fixture.remote_url(), target, std::nullopt);
    const auto& replacement_initial =
            require_arm<ReviewedSourceInitialFullReview>(
                    replacement_result,
                    "Replacement-ref-isolated projection failed");
    const auto* replacement_pkgbuild =
            find_new_path(replacement_initial.changes, "PKGBUILD");
    const std::string target_pkgbuild = fixture.output_git(
            {"--no-replace-objects", "rev-parse",
             history.target + ":PKGBUILD"});
    require(replacement_pkgbuild != nullptr &&
                    std::get<ReviewedSourceAdded>(*replacement_pkgbuild)
                                    .new_version.object_id().value() ==
                            target_pkgbuild,
            "Replacement ref changed pinned target tree authority");
    fixture.run_git({"replace", "-d", history.target});

    fixture.write_file(".git/info/attributes", "PKGBUILD -diff\n");
    require_failure_reason(
            trusted_git_project_reviewed_source(
                    checkout, fixture.remote_url(), target, baseline),
            TrustedGitReviewFailureReason::LocalAttributeOverride,
            "Local attributes override was not refused");
    fixture.remove_path(".git/info/attributes");

    fixture.write_file(".git/info/grafts", history.target + "\n");
    require_failure_reason(
            trusted_git_project_reviewed_source(
                    checkout, fixture.remote_url(), target, baseline),
            TrustedGitReviewFailureReason::LocalHistoryOverride,
            "Local graft override was not refused");
    fixture.remove_path(".git/info/grafts");

    fixture.write_file(".git/shallow", history.target + "\n");
    require_failure_reason(
            trusted_git_project_reviewed_source(
                    checkout, fixture.remote_url(), target, baseline),
            TrustedGitReviewFailureReason::ShallowRepositoryUnsupported,
            "Shallow repository was not refused");
    fixture.remove_path(".git/shallow");

    set_trusted_git_review_machine_stream_limit_for_test(1);
    const auto limited_result = trusted_git_project_reviewed_source(
            checkout, fixture.remote_url(), target, std::nullopt);
    set_trusted_git_review_machine_stream_limit_for_test(std::nullopt);
    require_failure_reason(
            limited_result,
            TrustedGitReviewFailureReason::CaptureLimitExceeded,
            "Truncated machine stream was parsed");

    fixture.run_git({"update-ref", "-d", "refs/remotes/origin/main"});
    const auto missing_target = trusted_git_resolve_remote_commit(
            checkout, fixture.remote_url(), "main");
    require(require_arm<TrustedGitReviewFailure>(
                    missing_target, "Missing target ref was not a failure")
                            .reason ==
                    TrustedGitReviewFailureReason::CommandFailed,
            "Missing target ref failure reason drifted");
}

void test_sha1_content_materialization() {
    GitFixture fixture(GitObjectFormat::Sha1);
    std::string weird_path = "nested/content-";
    weird_path.push_back('\t');
    weird_path.push_back('\n');
    weird_path.push_back('\x1b');
    weird_path.push_back(static_cast<char>(0xff));

    fixture.write_file("PKGBUILD", "pkgname=material\npkgver=1\npkgrel=1\n");
    fixture.write_file("material.install", "post_install() { :; }\n");
    fixture.write_file("forced.bin", std::string("old\0payload", 11));
    fixture.write_file("binary.dat", std::string("binary\0old", 10));
    fixture.write_file("generic.txt", "before\n");
    fixture.write_file("deleted", "deleted content\n");
    fixture.write_file("mode-only", "same mode content\n");
    fixture.write_file("type", "regular target\n");
    fixture.write_file("rename-old", repeated_lines('r', 30));
    fixture.write_file(weird_path, "weird before\n");
    const std::string baseline_oid = fixture.commit("material baseline");

    fixture.write_file(
            ".gitattributes",
            "PKGBUILD -diff\n*.install -diff\nforced.bin diff\n");
    fixture.write_file("PKGBUILD", "pkgname=material\npkgver=2\npkgrel=1\n");
    fixture.write_file(
            "material.install", "post_install() { echo changed; }\n");
    fixture.write_file("forced.bin", std::string("new\0payload", 11));
    fixture.write_file("binary.dat", std::string("binary\0new", 10));
    fixture.write_file("generic.txt", "after\n");
    fixture.remove_path("deleted");
    fixture.write_file("mode-only", "same mode content\n", true);
    fixture.remove_path("type");
    fixture.make_symlink("PKGBUILD", "type");
    fixture.rename_path("rename-old", "rename-new");
    fixture.write_file(
            "rename-new", repeated_lines('r', 30) + "rename change\n");
    fixture.write_file(".SRCINFO", "pkgbase = material\npkgver = 2\n");
    fixture.write_file(weird_path, "weird after\n");
    fixture.stage_all();
    const std::string missing_gitlink_oid(40, '9');
    fixture.set_gitlink("missing-submodule", missing_gitlink_oid);
    const std::string target_oid = fixture.commit_index("material target");
    fixture.update_remote(target_oid);

    const SourceRevisionIdentity target =
            SourceRevisionIdentity::git_commit(target_oid);
    const SourceRevisionIdentity baseline =
            SourceRevisionIdentity::git_commit(baseline_oid);
    const auto projected = trusted_git_project_reviewed_source(
            fixture.checkout(), fixture.remote_url(), target, baseline);
    const auto& update = require_arm<ReviewedSourceUpdateReview>(
            projected, "Materialization fixture projection failed");

    const std::string target_pkgbuild_blob = fixture.output_git(
            {"rev-parse", target_oid + ":PKGBUILD"});
    const std::string replacement_blob = fixture.output_git(
            {"rev-parse", target_oid + ":generic.txt"});
    fixture.run_git({"replace", "-f", target_pkgbuild_blob, replacement_blob});

    fixture.write_file(".gitattributes", "PKGBUILD diff\n");
    fixture.write_file("PKGBUILD", "dirty worktree\n");
    const std::string dirty_pkgbuild = fixture.read_file("PKGBUILD");
    const std::string index_before = fixture.read_file(".git/index");
    const std::string head_before = fixture.output_git({"rev-parse", "HEAD"});

    const auto materialized = trusted_git_materialize_reviewed_source_review(
            fixture.checkout(), fixture.remote_url(),
            ReviewedSourceProjection(update));
    if(const auto* failure = std::get_if<TrustedGitReviewFailure>(
               &materialized)) {
        throw std::runtime_error(
                "SHA-1 materialization trusted Git failure: reason=" +
                std::to_string(static_cast<int>(failure->reason)) +
                " stage=" +
                std::to_string(static_cast<int>(failure->stage)) +
                " exit=" +
                (failure->exit_code.has_value()
                         ? std::to_string(*failure->exit_code)
                         : std::string("none")));
    }
    if(const auto* failure = std::get_if<ReviewedSourceReviewFailure>(
               &materialized)) {
        std::string path = "unavailable";
        if(failure->entry_index < update.changes.size()) {
            const ReviewedSourceFileChange& failed_change =
                    update.changes[failure->entry_index];
            path = std::visit(
                    [](const auto& value) {
                        using Change = std::decay_t<decltype(value)>;
                        if constexpr(std::is_same_v<Change, ReviewedSourceDeleted>) {
                            return value.old_version.path().escaped_display();
                        } else {
                            return value.new_version.path().escaped_display();
                        }
                    },
                    failed_change);
        }
        throw std::runtime_error(
                "SHA-1 materialization model failure: reason=" +
                std::to_string(static_cast<int>(failure->reason)) +
                " entry=" + std::to_string(failure->entry_index) +
                " path=" + path +
                " record=" + std::to_string(failure->record_index) +
                " hunk=" + std::to_string(failure->hunk_index));
    }
    const auto& verified_update =
            require_arm<ReviewedSourceVerifiedMaterializedReview>(
                    materialized,
                    "SHA-1 reviewed content materialization failed");
    const AurReviewedSourceReviewIdentity review_identity =
            aur_review_identity(
                    "slice3a-fixture", fixture.remote_url(), target);
    const auto trusted_materialized =
            trusted_git_materialize_aur_reviewed_source_review(
                    fixture.checkout(), review_identity,
                    ReviewedSourceProjection(update));
    const auto& trusted_review =
            require_arm<TrustedAurReviewedSourceReview>(
                    trusted_materialized,
                    "SHA-1 trusted AUR review provenance was not sealed");
    require(trusted_review.valid() &&
                    trusted_review.identity() == review_identity &&
                    trusted_review.verified_review() == verified_update,
            "SHA-1 trusted review lost identity or 3B provenance");

    const auto wrong_target =
            trusted_git_materialize_aur_reviewed_source_review(
                    fixture.checkout(),
                    aur_review_identity(
                            "slice3a-fixture", fixture.remote_url(), baseline),
                    ReviewedSourceProjection(update));
    const auto& wrong_target_failure = require_arm<TrustedGitReviewFailure>(
            wrong_target,
            "Wrong exact target was sealed into trusted AUR review");
    require(wrong_target_failure.reason ==
                            TrustedGitReviewFailureReason::
                                    ReviewIdentityMismatch &&
                    wrong_target_failure.stage ==
                            TrustedGitReviewStage::TargetValidation,
            "Wrong exact target produced the wrong trusted rejection");

    const SourceRevisionIdentity wrong_format_revision =
            SourceRevisionIdentity::git_commit(std::string(64, 'a'));
    const auto wrong_format =
            trusted_git_materialize_aur_reviewed_source_review(
                    fixture.checkout(),
                    aur_review_identity(
                            "slice3a-fixture", fixture.remote_url(),
                            wrong_format_revision),
                    ReviewedSourceProjection(update));
    require(require_arm<TrustedGitReviewFailure>(
                    wrong_format,
                    "Wrong object format was sealed into trusted AUR review")
                            .reason ==
                    TrustedGitReviewFailureReason::ObjectFormatMismatch,
            "Wrong object format produced the wrong trusted rejection");

    expect_runtime_error(
            [&] {
                static_cast<void>(
                        trusted_git_materialize_aur_reviewed_source_review(
                                fixture.checkout(),
                                aur_review_identity(
                                        "other-base",
                                        "https://aur.archlinux.org/other-base.git",
                                        target),
                                ReviewedSourceProjection(update)));
            },
            "Same-OID cross-PackageBase/remote review was sealed");
    const auto& materialized_update =
            require_arm<ReviewedSourceMaterializedUpdateReview>(
                    verified_update.review(),
                    "SHA-1 verified materialization kind drifted");
    const auto& entries = materialized_update.review.entries;
    require(materialized_update.review.readiness ==
                    ReviewedSourceReviewReadiness::ManualInspectionRequired,
            "Generic binary/gitlink readiness was flattened to Complete");

    const auto* pkgbuild = find_materialized_new_path(entries, "PKGBUILD");
    require(pkgbuild != nullptr && git_marker_is_binary(pkgbuild->change) &&
                    pkgbuild->representation ==
                            ReviewedSourceReviewRepresentation::
                                    CompleteTextPatch &&
                    pkgbuild->readiness ==
                            ReviewedSourceReviewReadiness::Complete &&
                    pkgbuild->patch.has_value(),
            "PKGBUILD -diff hid exact textual materialization");
    bool pkgbuild_new_line_observed = false;
    for(const ReviewedSourcePatchHunk& hunk : pkgbuild->patch->hunks) {
        for(const ReviewedSourcePatchLine& line : hunk.lines) {
            if(line.kind == ReviewedSourcePatchLineKind::Added &&
               line.line.bytes == "pkgver=2") {
                pkgbuild_new_line_observed = true;
            }
        }
    }
    require(pkgbuild_new_line_observed,
            "Replacement ref or dirty worktree changed PKGBUILD patch bytes");
    const ReviewedSourcePresentationResult rendered =
            render_reviewed_source_presentation(verified_update);
    const auto& rendered_text =
            require_arm<ReviewedSourceRenderedPresentation>(
                    rendered,
                    "Trusted SHA-1 materialization did not render");
    require(rendered_text.text.find("+pkgver=2") != std::string::npos,
            "Trusted SHA-1 verified patch was not presented");

    const auto* install = find_materialized_new_path(
            entries, "material.install");
    require(install != nullptr && git_marker_is_binary(install->change) &&
                    install->representation ==
                            ReviewedSourceReviewRepresentation::
                                    CompleteTextPatch &&
                    install->readiness ==
                            ReviewedSourceReviewReadiness::Complete,
            "Root install -diff hid exact textual materialization");

    const auto* forced = find_materialized_new_path(entries, "forced.bin");
    require(forced != nullptr && !git_marker_is_binary(forced->change) &&
                    forced->representation ==
                            ReviewedSourceReviewRepresentation::ContainsNul &&
                    forced->readiness ==
                            ReviewedSourceReviewReadiness::
                                    ManualInspectionRequired &&
                    !forced->patch.has_value(),
            "NUL blob forced text became a textual patch");

    const auto* binary = find_materialized_new_path(entries, "binary.dat");
    require(binary != nullptr && git_marker_is_binary(binary->change) &&
                    binary->representation ==
                            ReviewedSourceReviewRepresentation::ContainsNul,
            "Git-binary/NUL content lost independent classification");

    const auto* generic = find_materialized_new_path(entries, "generic.txt");
    require(generic != nullptr && !git_marker_is_binary(generic->change) &&
                    generic->representation ==
                            ReviewedSourceReviewRepresentation::
                                    CompleteTextPatch &&
                    generic->patch.has_value(),
            "Git-text/text content was not materialized");

    const auto* mode = find_materialized_new_path(entries, "mode-only");
    require(mode != nullptr &&
                    mode->representation ==
                            ReviewedSourceReviewRepresentation::NoContentChange &&
                    std::holds_alternative<ReviewedSourceModified>(mode->change),
            "Mode-only change did not remain Modified + NoContentChange");

    const auto* renamed = find_materialized_new_path(entries, "rename-new");
    require(renamed != nullptr &&
                    std::holds_alternative<ReviewedSourceRenamed>(
                            renamed->change) &&
                    renamed->representation ==
                            ReviewedSourceReviewRepresentation::
                                    CompleteTextPatch &&
                    renamed->patch.has_value(),
            "Rename with content change lost typed patch materialization");

    const auto* type = find_materialized_new_path(entries, "type");
    require(type != nullptr &&
                    std::holds_alternative<ReviewedSourceTypeChanged>(
                            type->change) &&
                    type->representation ==
                            ReviewedSourceReviewRepresentation::
                                    CompleteTextPatch &&
                    type->patch.has_value(),
            "Regular-to-symlink TypeChanged lost blob patch");

    const auto* deleted = find_materialized_old_path(entries, "deleted");
    require(deleted != nullptr &&
                    deleted->representation ==
                            ReviewedSourceReviewRepresentation::CompleteFullText,
            "Deleted text did not retain complete old content");

    const auto* srcinfo = find_materialized_new_path(entries, ".SRCINFO");
    require(srcinfo != nullptr &&
                    srcinfo->representation ==
                            ReviewedSourceReviewRepresentation::CompleteFullText &&
                    std::get<ReviewedSourceAdded>(srcinfo->change)
                                    .new_version.classification() ==
                            ReviewedSourceFileClassification::GeneratedMetadata,
            ".SRCINFO generated metadata materialization drifted");

    const auto* gitlink = find_materialized_new_path(
            entries, "missing-submodule");
    require(gitlink != nullptr &&
                    gitlink->representation ==
                            ReviewedSourceReviewRepresentation::GitlinkMetadata &&
                    gitlink->readiness ==
                            ReviewedSourceReviewReadiness::
                                    ManualInspectionRequired &&
                    !gitlink->new_observation->text,
            "Missing gitlink was read as a blob");

    const auto* weird = find_materialized_new_path(entries, weird_path);
    require(weird != nullptr && weird->patch.has_value(),
            "Arbitrary filename bytes were lost during materialization");
    require(fixture.read_file("PKGBUILD") == dirty_pkgbuild,
            "Materialization changed dirty worktree content");
    require(fixture.read_file(".git/index") == index_before,
            "Materialization changed Git index");
    require(fixture.output_git({"rev-parse", "HEAD"}) == head_before,
            "Materialization changed HEAD");
}

void test_baseline_object_access_failures() {
    {
        GitFixture fixture(GitObjectFormat::Sha1);
        fixture.write_file(
                "PKGBUILD", "pkgname=fixture\npkgver=1\npkgrel=1\n");
        const std::string baseline_oid = fixture.commit("baseline");
        fixture.write_file(
                "PKGBUILD", "pkgname=fixture\npkgver=2\npkgrel=1\n");
        const std::string target_oid = fixture.commit("target");
        fixture.update_remote(target_oid);
        const auto resolved = trusted_git_resolve_remote_commit(
                fixture.checkout(), fixture.remote_url(), "main");
        const SourceRevisionIdentity& target =
                require_arm<SourceRevisionIdentity>(
                        resolved,
                        "Baseline failure fixture target resolution failed");

        const fs::path baseline_object =
                fixture.repository() / ".git" / "objects" /
                baseline_oid.substr(0, 2) / baseline_oid.substr(2);
        require(chmod(baseline_object.c_str(), 0000) == 0,
                "Failed to make baseline commit unreadable");
        const auto unreadable_result = trusted_git_project_reviewed_source(
                fixture.checkout(), fixture.remote_url(), target,
                SourceRevisionIdentity::git_commit(baseline_oid));
        const auto& unreadable_failure = require_arm<TrustedGitReviewFailure>(
                unreadable_result,
                "Unreadable baseline commit was flattened to rebaseline");
        require(unreadable_failure.reason ==
                            TrustedGitReviewFailureReason::CommandFailed &&
                        unreadable_failure.stage ==
                            TrustedGitReviewStage::BaselineValidation,
                "Unreadable baseline failure taxonomy drifted");

        require(chmod(baseline_object.c_str(), 0644) == 0,
                "Failed to restore baseline object permissions");
        std::ofstream corrupt(
                baseline_object, std::ios::binary | std::ios::trunc);
        corrupt << "corrupt";
        corrupt.close();
        require(static_cast<bool>(corrupt),
                "Failed to corrupt baseline commit fixture");
        const auto corrupt_result = trusted_git_project_reviewed_source(
                fixture.checkout(), fixture.remote_url(), target,
                SourceRevisionIdentity::git_commit(baseline_oid));
        const auto& corrupt_failure = require_arm<TrustedGitReviewFailure>(
                corrupt_result,
                "Corrupt baseline commit was flattened to rebaseline");
        require(corrupt_failure.reason ==
                            TrustedGitReviewFailureReason::CommandFailed &&
                        corrupt_failure.stage ==
                            TrustedGitReviewStage::BaselineValidation,
                "Corrupt baseline failure taxonomy drifted");
    }

    {
        GitFixture fixture(GitObjectFormat::Sha1);
        fixture.write_file(
                "PKGBUILD", "pkgname=packed\npkgver=1\npkgrel=1\n");
        const std::string baseline_oid = fixture.commit("packed baseline");
        fixture.run_git({"repack", "-ad"});
        fixture.run_git({"prune-packed"});
        fixture.write_file(
                "PKGBUILD", "pkgname=packed\npkgver=2\npkgrel=1\n");
        const std::string target_oid = fixture.commit("loose target");
        fixture.update_remote(target_oid);
        const auto resolved = trusted_git_resolve_remote_commit(
                fixture.checkout(), fixture.remote_url(), "main");
        const SourceRevisionIdentity& target =
                require_arm<SourceRevisionIdentity>(
                        resolved,
                        "Packed baseline fixture target resolution failed");

        fs::path pack_file;
        fs::path pack_index;
        for(const auto& entry : fs::directory_iterator(
                    fixture.repository() / ".git" / "objects" / "pack")) {
            if(entry.path().extension() == ".pack") {
                require(pack_file.empty(),
                        "Packed baseline fixture created multiple pack files");
                pack_file = entry.path();
            } else if(entry.path().extension() == ".idx") {
                require(pack_index.empty(),
                        "Packed baseline fixture created multiple pack indexes");
                pack_index = entry.path();
            }
        }
        require(!pack_file.empty() && !pack_index.empty(),
                "Packed baseline fixture did not create a pack pair");
        require(chmod(pack_file.c_str(), 0000) == 0,
                "Failed to make baseline pack unreadable");
        const auto unreadable_pack_result =
                trusted_git_project_reviewed_source(
                        fixture.checkout(), fixture.remote_url(), target,
                        SourceRevisionIdentity::git_commit(baseline_oid));
        require(chmod(pack_file.c_str(), 0444) == 0,
                "Failed to restore baseline pack permissions");
        const auto& unreadable_pack_failure =
                require_arm<TrustedGitReviewFailure>(
                        unreadable_pack_result,
                        "Unreadable packed baseline was flattened to rebaseline");
        require(unreadable_pack_failure.reason ==
                            TrustedGitReviewFailureReason::CommandFailed &&
                        unreadable_pack_failure.stage ==
                            TrustedGitReviewStage::BaselineValidation,
                "Unreadable packed baseline failure taxonomy drifted");

        require(chmod(pack_index.c_str(), 0000) == 0,
                "Failed to make baseline pack index unreadable");
        const auto unreadable_index_result =
                trusted_git_project_reviewed_source(
                        fixture.checkout(), fixture.remote_url(), target,
                        SourceRevisionIdentity::git_commit(baseline_oid));
        require(chmod(pack_index.c_str(), 0444) == 0,
                "Failed to restore baseline pack index permissions");
        const auto& unreadable_index_failure =
                require_arm<TrustedGitReviewFailure>(
                        unreadable_index_result,
                        "Unreadable pack index was flattened to rebaseline");
        require(unreadable_index_failure.reason ==
                            TrustedGitReviewFailureReason::CommandFailed &&
                        unreadable_index_failure.stage ==
                            TrustedGitReviewStage::BaselineValidation,
                "Unreadable pack index failure taxonomy drifted");

        require(chmod(pack_index.c_str(), 0600) == 0,
                "Failed to make baseline pack index writable");
        std::ofstream corrupt_index(
                pack_index, std::ios::binary | std::ios::trunc);
        corrupt_index << "corrupt";
        corrupt_index.close();
        require(static_cast<bool>(corrupt_index),
                "Failed to corrupt baseline pack index");
        const auto corrupt_index_result =
                trusted_git_project_reviewed_source(
                        fixture.checkout(), fixture.remote_url(), target,
                        SourceRevisionIdentity::git_commit(baseline_oid));
        const auto& corrupt_index_failure =
                require_arm<TrustedGitReviewFailure>(
                        corrupt_index_result,
                        "Corrupt pack index was flattened to rebaseline");
        require(corrupt_index_failure.reason ==
                            TrustedGitReviewFailureReason::CommandFailed &&
                        corrupt_index_failure.stage ==
                            TrustedGitReviewStage::BaselineValidation,
                "Corrupt pack index failure taxonomy drifted: reason=" +
                        std::to_string(static_cast<int>(
                                corrupt_index_failure.reason)) +
                        " stage=" + std::to_string(static_cast<int>(
                                corrupt_index_failure.stage)) +
                        " exit=" +
                        (corrupt_index_failure.exit_code.has_value()
                                 ? std::to_string(
                                           *corrupt_index_failure.exit_code)
                                 : std::string("none")));
    }
}

void test_missing_gitlink_object_remains_supported() {
    GitFixture fixture(GitObjectFormat::Sha1);
    fixture.write_file("PKGBUILD", "pkgname=gitlink\npkgver=1\npkgrel=1\n");
    fixture.stage_all();
    const std::string missing_commit(40, '1');
    fixture.set_gitlink("missing-submodule", missing_commit);
    const std::string target_oid = fixture.commit_index("missing gitlink target");
    fixture.update_remote(target_oid);
    const auto resolved = trusted_git_resolve_remote_commit(
            fixture.checkout(), fixture.remote_url(), "main");
    const SourceRevisionIdentity& target = require_arm<SourceRevisionIdentity>(
            resolved,
            "Missing-gitlink target resolution failed");
    const auto projection = trusted_git_project_reviewed_source(
            fixture.checkout(), fixture.remote_url(), target, std::nullopt);
    const auto& initial = require_arm<ReviewedSourceInitialFullReview>(
            projection, "Missing-gitlink initial projection failed");
    const auto* change = find_new_path(initial.changes, "missing-submodule");
    const auto* added = change == nullptr
            ? nullptr
            : std::get_if<ReviewedSourceAdded>(change);
    require(added != nullptr &&
                    added->new_version.mode() ==
                            ReviewedSourceFileMode::Gitlink &&
                    !added->new_version.blob_size().has_value() &&
                    added->new_version.object_id().value() == missing_commit,
            "Missing gitlink object became a projection requirement");
    require(std::holds_alternative<ReviewedSourceRebaselineFullReview>(
                    trusted_git_project_reviewed_source(
                            fixture.checkout(), fixture.remote_url(), target,
                            SourceRevisionIdentity::git_commit(
                                    std::string(40, 'f')))),
            "Missing gitlink object made missing-baseline integrity fail");
}

void test_sha256_projection_and_strict_config() {
    {
        GitFixture fixture(GitObjectFormat::Sha256);
        fixture.write_file("PKGBUILD", "pkgname=sha256\npkgver=1\npkgrel=1\n");
        fixture.write_file(".SRCINFO", "pkgbase = sha256\npkgver = 1\n");
        fixture.write_file("payload.unknown", std::string("binary\0data", 11));
        const std::string target_oid = fixture.commit("sha256 initial");
        fixture.update_remote(target_oid);
        const auto resolved = trusted_git_resolve_remote_commit(
                fixture.checkout(), fixture.remote_url(), "main");
        const auto& target = require_arm<SourceRevisionIdentity>(
                resolved, "SHA-256 target resolution failed");
        require(target.git_commit() != nullptr &&
                        target.git_commit()->size() == 64 &&
                        target.git_object_format() != nullptr &&
                        *target.git_object_format() == GitObjectFormat::Sha256,
                "SHA-256 target identity was not retained");
        const auto projection = trusted_git_project_reviewed_source(
                fixture.checkout(), fixture.remote_url(), target,
                std::nullopt);
        const auto& initial = require_arm<ReviewedSourceInitialFullReview>(
                projection, "SHA-256 initial projection failed");
        require(initial.changes.size() == 3,
                "SHA-256 tracked inventory count drifted");
        for(const auto& change : initial.changes) {
            const auto& added = require_arm<ReviewedSourceAdded>(
                    change, "SHA-256 initial status was not Added");
            require(added.new_version.object_id().format() ==
                            GitObjectFormat::Sha256,
                    "SHA-256 tree object ID format was lost");
        }
        const auto initial_materialized =
                trusted_git_materialize_reviewed_source_review(
                        fixture.checkout(), fixture.remote_url(),
                        ReviewedSourceProjection(initial));
        const auto& verified_initial =
                require_arm<ReviewedSourceVerifiedMaterializedReview>(
                        initial_materialized,
                        "SHA-256 initial content materialization failed");
        const AurReviewedSourceReviewIdentity review_identity =
                aur_review_identity(
                        "slice3a-fixture", fixture.remote_url(), target);
        const auto trusted_initial =
                trusted_git_materialize_aur_reviewed_source_review(
                        fixture.checkout(), review_identity,
                        ReviewedSourceProjection(initial));
        const auto& trusted_review =
                require_arm<TrustedAurReviewedSourceReview>(
                        trusted_initial,
                        "SHA-256 trusted AUR review provenance was not sealed");
        require(trusted_review.valid() &&
                        trusted_review.identity() == review_identity &&
                        trusted_review.identity().git_object_format() ==
                                GitObjectFormat::Sha256 &&
                        trusted_review.verified_review() == verified_initial,
                "SHA-256 trusted review lost identity or 3B provenance");
        const auto& materialized_initial =
                require_arm<ReviewedSourceMaterializedInitialFullReview>(
                        verified_initial.review(),
                        "SHA-256 verified initial kind drifted");
        const auto* initial_pkgbuild = find_materialized_new_path(
                materialized_initial.review.entries, "PKGBUILD");
        const auto* initial_binary = find_materialized_new_path(
                materialized_initial.review.entries, "payload.unknown");
        require(initial_pkgbuild != nullptr &&
                        initial_pkgbuild->representation ==
                                ReviewedSourceReviewRepresentation::
                                        CompleteFullText &&
                        initial_binary != nullptr &&
                        initial_binary->representation ==
                                ReviewedSourceReviewRepresentation::ContainsNul,
                "SHA-256 cat-file content classification drifted");
        require(std::holds_alternative<ReviewedSourceRebaselineFullReview>(
                        trusted_git_project_reviewed_source(
                                fixture.checkout(), fixture.remote_url(),
                                target,
                                SourceRevisionIdentity::git_commit(
                                        std::string(64, 'f')))),
                "Missing SHA-256 baseline did not rebaseline");
        require(std::holds_alternative<ReviewedSourceRebaselineFullReview>(
                        trusted_git_project_reviewed_source(
                                fixture.checkout(), fixture.remote_url(),
                                target,
                                SourceRevisionIdentity::git_commit(
                                        std::string(40, 'f')))),
                "Mismatched baseline object format did not rebaseline");

        fixture.write_file(".gitattributes", "PKGBUILD -diff\n");
        fixture.write_file("PKGBUILD", "pkgname=sha256\npkgver=2\npkgrel=1\n");
        const std::string updated_oid = fixture.commit("sha256 update");
        fixture.update_remote(updated_oid);
        const SourceRevisionIdentity updated =
                SourceRevisionIdentity::git_commit(updated_oid);
        const auto update_projection = trusted_git_project_reviewed_source(
                fixture.checkout(), fixture.remote_url(), updated, target);
        const auto& sha256_update = require_arm<ReviewedSourceUpdateReview>(
                update_projection, "SHA-256 update projection failed");
        const auto update_materialized =
                trusted_git_materialize_reviewed_source_review(
                        fixture.checkout(), fixture.remote_url(),
                        ReviewedSourceProjection(sha256_update));
        const auto& verified_update =
                require_arm<ReviewedSourceVerifiedMaterializedReview>(
                        update_materialized,
                        "SHA-256 blob-to-blob patch materialization failed");
        const auto& materialized_update =
                require_arm<ReviewedSourceMaterializedUpdateReview>(
                        verified_update.review(),
                        "SHA-256 verified update kind drifted");
        const auto* updated_pkgbuild = find_materialized_new_path(
                materialized_update.review.entries, "PKGBUILD");
        require(updated_pkgbuild != nullptr &&
                        git_marker_is_binary(updated_pkgbuild->change) &&
                        updated_pkgbuild->representation ==
                                ReviewedSourceReviewRepresentation::
                                        CompleteTextPatch &&
                        updated_pkgbuild->patch.has_value() &&
                        updated_pkgbuild->patch->old_object_id.format() ==
                                GitObjectFormat::Sha256 &&
                        updated_pkgbuild->patch->new_object_id.format() ==
                                GitObjectFormat::Sha256,
                "SHA-256 PKGBUILD -diff hid full-OID textual patch");
    }

    {
        GitFixture invalid(GitObjectFormat::Sha256);
        invalid.write_file("PKGBUILD", "pkgname=invalid\npkgver=1\npkgrel=1\n");
        const std::string target_oid = invalid.commit("invalid config target");
        invalid.update_remote(target_oid);
        std::string config = invalid.read_file(".git/config");
        const std::string expected = "repositoryformatversion = 1";
        const std::size_t location = config.find(expected);
        require(location != std::string::npos,
                "SHA-256 fixture config format key was missing");
        config.replace(location, expected.size(),
                       "repositoryformatversion = 0");
        invalid.write_file(".git/config", config);
        bool rejected = false;
        try {
            static_cast<void>(trusted_git_resolve_remote_commit(
                    invalid.checkout(), invalid.remote_url(), "main"));
        } catch(const std::runtime_error&) {
            rejected = true;
        }
        require(rejected,
                "Mismatched SHA-256 repository format pair was accepted");
    }
}

void run_tests() {
    test_sha1_projection_and_pinned_ref();
    test_sha1_content_materialization();
    test_baseline_object_access_failures();
    test_missing_gitlink_object_remains_supported();
    test_sha256_projection_and_strict_config();
}

} // namespace

int main() {
    try {
        run_tests();
    } catch(const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "reviewed source Git tests: all checks passed\n";
    return 0;
}
