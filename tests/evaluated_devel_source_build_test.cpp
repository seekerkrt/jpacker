#include "evaluated_devel_source_build.hpp"

#include "process.hpp"
#include "reviewed_source_acceptance.hpp"
#include "reviewed_source_presentation.hpp"
#include "reviewed_source_review.hpp"
#include "reviewed_source_state_store.hpp"
#include "reviewed_source_trusted_review.hpp"
#include "trusted_cache.hpp"
#include "trusted_cache_test_support.hpp"
#include "xdg_generation_store.hpp"

#include <algorithm>
#include <cstdlib>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <type_traits>
#include <unistd.h>
#include <utility>
#include <variant>
#include <vector>

namespace {

namespace fs = std::filesystem;

static_assert(!std::is_default_constructible_v<
              EvaluatedDevelSourceBuildProof>);
static_assert(!std::is_copy_constructible_v<
              EvaluatedDevelSourceBuildProof>);
static_assert(std::is_move_constructible_v<
              EvaluatedDevelSourceBuildProof>);
static_assert(!std::is_default_constructible_v<
              EvaluatedDevelSourceProjection>);
static_assert(!std::is_default_constructible_v<
              FreshDevelPackageArtifact>);
static_assert(!std::is_constructible_v<
              FreshDevelPackageArtifact,
              fs::path>);
static_assert(std::is_invocable_v<
              decltype(build_evaluated_devel_source),
              InvocationOwnedSourceBuildContext,
              InvocationOwnedMakepkgEnvironment>);
static_assert(!std::is_invocable_v<
              decltype(build_evaluated_devel_source),
              PackageBaseIdentity,
              std::string,
              fs::path>);

void require(bool condition, std::string_view message) {
    if(!condition) throw std::runtime_error(std::string(message));
}

template <typename Expected, typename Variant>
Expected take_arm(Variant& value, std::string_view message) {
    Expected* arm = std::get_if<Expected>(&value);
    if(arm == nullptr) throw std::runtime_error(std::string(message));
    return std::move(*arm);
}

template <typename Expected, typename Variant>
const Expected& require_arm(
    const Variant& value,
    std::string_view message) {
    const Expected* arm = std::get_if<Expected>(&value);
    if(arm == nullptr) throw std::runtime_error(std::string(message));
    return *arm;
}

class TemporaryTree final {
public:
    explicit TemporaryTree(std::string_view label) {
        std::string path_template =
            "/tmp/moguet-evaluated-build-test-" +
            std::string(label) + "-XXXXXX";
        std::vector<char> writable(
            path_template.begin(), path_template.end());
        writable.push_back('\0');
        char* created = ::mkdtemp(writable.data());
        require(created != nullptr, "Failed to create test root");
        path_ = created;
    }

    TemporaryTree(const TemporaryTree&) = delete;
    TemporaryTree& operator=(const TemporaryTree&) = delete;

    ~TemporaryTree() noexcept {
        std::error_code error;
        fs::remove_all(path_, error);
    }

    [[nodiscard]] const fs::path& path() const noexcept {
        return path_;
    }

private:
    fs::path path_;
};

class ScopedEnvironmentVariable final {
public:
    ScopedEnvironmentVariable(
        std::string name,
        const std::optional<std::string>& value)
        : name_(std::move(name)) {
        const char* previous = std::getenv(name_.c_str());
        if(previous != nullptr) previous_ = previous;
        const int status = value.has_value()
                               ? ::setenv(
                                     name_.c_str(), value->c_str(), 1)
                               : ::unsetenv(name_.c_str());
        require(status == 0, "Failed to set fixture environment");
    }

    ScopedEnvironmentVariable(const ScopedEnvironmentVariable&) = delete;
    ScopedEnvironmentVariable& operator=(
        const ScopedEnvironmentVariable&) = delete;

    ~ScopedEnvironmentVariable() noexcept {
        if(previous_.has_value()) {
            static_cast<void>(::setenv(
                name_.c_str(), previous_->c_str(), 1));
        } else {
            static_cast<void>(::unsetenv(name_.c_str()));
        }
    }

private:
    std::string name_;
    std::optional<std::string> previous_;
};

std::vector<std::string> git_environment(const fs::path& home) {
    return {
        "PATH=/usr/bin:/bin",
        "LC_ALL=C",
        "LANG=C",
        "HOME=" + home.string(),
        "GIT_CONFIG_NOSYSTEM=1",
        "GIT_CONFIG_SYSTEM=/dev/null",
        "GIT_CONFIG_GLOBAL=/dev/null",
        "GIT_AUTHOR_NAME=Slice 4 Fixture",
        "GIT_AUTHOR_EMAIL=slice4@example.invalid",
        "GIT_COMMITTER_NAME=Slice 4 Fixture",
        "GIT_COMMITTER_EMAIL=slice4@example.invalid",
        "GIT_TERMINAL_PROMPT=0",
    };
}

CapturedCommandResult capture_process(
    std::string executable,
    std::vector<std::string> arguments,
    std::vector<std::string> environment,
    const fs::path* working_directory = nullptr,
    std::size_t limit = 16U * 1024U * 1024U) {
    int directory_descriptor = -1;
    if(working_directory != nullptr) {
        directory_descriptor = ::open(
            working_directory->c_str(),
            O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        require(directory_descriptor >= 0,
                "Failed to open fixture process cwd");
    }
    ExplicitProcessInvocation invocation{
        std::move(executable), std::move(arguments),
        std::move(environment), limit};
    if(directory_descriptor >= 0) {
        invocation.working_directory_fd = directory_descriptor;
    }
    CapturedCommandResult result =
        capture_explicit_process_output_raw(invocation, true);
    if(directory_descriptor >= 0) {
        static_cast<void>(::close(directory_descriptor));
    }
    return result;
}

void require_process_success(
    std::string executable,
    std::vector<std::string> arguments,
    std::vector<std::string> environment,
    const fs::path* working_directory = nullptr) {
    CapturedCommandResult result = capture_process(
        std::move(executable), std::move(arguments),
        std::move(environment), working_directory);
    require(
        result.exit_code == 0 &&
            !result.stdout_capture_limit_exceeded,
        "Fixture process failed");
}

std::string require_output_line(CapturedCommandResult result) {
    require(
        result.exit_code == 0 &&
            !result.stdout_capture_limit_exceeded &&
            !result.output.empty() && result.output.back() == '\n',
        "Fixture process output failed");
    result.output.pop_back();
    require(
        !result.output.empty() &&
            result.output.find('\n') == std::string::npos,
        "Fixture process did not produce one line");
    return result.output;
}

class UpstreamGitFixture final {
public:
    explicit UpstreamGitFixture(
        std::string label,
        GitObjectFormat object_format = GitObjectFormat::Sha1)
        : tree_(label), label_(std::move(label)),
          object_format_(object_format) {
        home_ = tree_.path() / "home";
        remote_ = tree_.path() / "upstream.git";
        work_ = tree_.path() / "upstream-work";
        fs::create_directory(home_);
        std::vector<std::string> bare_init{
            "init", "--bare", "--initial-branch=main"};
        std::vector<std::string> work_init{
            "init", "--initial-branch=main"};
        if(object_format_ == GitObjectFormat::Sha256) {
            bare_init.push_back("--object-format=sha256");
            work_init.push_back("--object-format=sha256");
        }
        bare_init.push_back(remote_.string());
        work_init.push_back(work_.string());
        require_process_success(
            "/usr/bin/git", std::move(bare_init),
            git_environment(home_));
        require_process_success(
            "/usr/bin/git", std::move(work_init),
            git_environment(home_));
        run_git({"remote", "add", "origin", remote_.string()});
        url_ = "https://fixture.invalid/" + label_ + ".git";
        commit("revision-one\n");
    }

    [[nodiscard]] const std::string& url() const noexcept {
        return url_;
    }

    [[nodiscard]] const fs::path& remote() const noexcept {
        return remote_;
    }

    [[nodiscard]] const std::string& oid() const noexcept {
        return oid_;
    }

    std::string commit(std::string_view payload) {
        write_file(work_ / "payload.txt", payload);
        run_git({"add", "--", "payload.txt"});
        run_git({"commit", "-q", "-m", std::string(payload)});
        oid_ = output_git({"rev-parse", "HEAD"});
        run_git({"push", "-u", "origin", "main"});
        return oid_;
    }

private:
    static void write_file(
        const fs::path& path,
        std::string_view contents) {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        require(static_cast<bool>(output),
                "Failed to open upstream fixture file");
        output.write(
            contents.data(),
            static_cast<std::streamsize>(contents.size()));
        output.close();
        require(static_cast<bool>(output),
                "Failed to write upstream fixture file");
    }

    void run_git(std::vector<std::string> arguments) const {
        std::vector<std::string> complete{"-C", work_.string()};
        complete.insert(
            complete.end(), arguments.begin(), arguments.end());
        require_process_success(
            "/usr/bin/git", std::move(complete),
            git_environment(home_));
    }

    [[nodiscard]] std::string output_git(
        std::vector<std::string> arguments) const {
        std::vector<std::string> complete{"-C", work_.string()};
        complete.insert(
            complete.end(), arguments.begin(), arguments.end());
        return require_output_line(capture_process(
            "/usr/bin/git", std::move(complete),
            git_environment(home_)));
    }

    TemporaryTree tree_;
    std::string label_;
    GitObjectFormat object_format_;
    fs::path home_;
    fs::path remote_;
    fs::path work_;
    std::string url_;
    std::string oid_;
};

enum class RecipeShape {
    Valid,
    RawEvaluatedMismatch,
    MultipleGit,
    UnsupportedVcs,
    UnsupportedSelector,
    DynamicVersionDrift,
};

class ReviewedBuildFixture final {
public:
    ReviewedBuildFixture(
        std::string label,
        const UpstreamGitFixture& upstream,
        RecipeShape shape = RecipeShape::Valid,
        bool prepare_mutation = false,
        bool exact_branch = false,
        bool tracked_local_source = false)
        : tree_(label), upstream_(upstream),
          package_base_("example-base"),
          package_name_("moguet-slice4-" + label),
          aur_remote_("https://aur.archlinux.org/example-base.git") {
        cache_home_ = tree_.path() / "cache";
        state_home_ = tree_.path() / "state";
        home_ = tree_.path() / "home";
        for(const fs::path& directory :
            {cache_home_, state_home_, home_}) {
            fs::create_directory(directory);
            fs::permissions(
                directory, fs::perms::owner_all,
                fs::perm_options::replace);
        }
        environment_.push_back(
            std::make_unique<ScopedEnvironmentVariable>(
                "XDG_CACHE_HOME", cache_home_.string()));
        environment_.push_back(
            std::make_unique<ScopedEnvironmentVariable>(
                "XDG_STATE_HOME", state_home_.string()));
        environment_.push_back(
            std::make_unique<ScopedEnvironmentVariable>(
                "HOME", home_.string()));

        cache_root_.emplace(prepare_test_trusted_cache_root());
        checkout_.emplace(create_trusted_cache_directory(
            *cache_root_, package_base_));
        repository_ = checkout_->path();
        run_git({"init", "-q", "-b", "main"});
        run_git({"config", "--local", "remote.origin.url", aur_remote_});
        run_git({"config", "--local", "remote.origin.fetch",
                 "+refs/heads/*:refs/remotes/origin/*"});
        run_git({"config", "--local", "branch.main.remote", "origin"});
        run_git({"config", "--local", "branch.main.merge",
                 "refs/heads/main"});
        write_file(
            "PKGBUILD",
            pkgbuild(
                shape, prepare_mutation, exact_branch,
                tracked_local_source));
        write_file(
            ".SRCINFO",
            srcinfo(shape, exact_branch, tracked_local_source));
        if(tracked_local_source) {
            write_file("fixture.patch", "reviewed local source\n");
        }
        recipe_oid_ = commit("reviewed recipe");
        run_git({"update-ref", "refs/remotes/origin/main", recipe_oid_});
    }

    [[nodiscard]] const std::string& recipe_oid() const noexcept {
        return recipe_oid_;
    }

    [[nodiscard]] const std::string& package_base() const noexcept {
        return package_base_;
    }

    [[nodiscard]] const std::string& package_name() const noexcept {
        return package_name_;
    }

    [[nodiscard]] const fs::path& home() const noexcept {
        return home_;
    }

    [[nodiscard]] const UpstreamGitFixture& upstream() const noexcept {
        return upstream_;
    }

    [[nodiscard]] InvocationOwnedSourceBuildContext make_context() {
        InvocationOwnedSourceBuildContextResult result =
            create_invocation_owned_source_build_context(
                make_pinned_build());
        if(const auto* failure =
               std::get_if<InvocationOwnedSourceBuildContextFailure>(
                   &result)) {
            std::ostringstream message;
            message << "Context creation failed: stage="
                    << static_cast<int>(failure->stage)
                    << " reason=" << static_cast<int>(failure->reason);
            if(failure->system_error.has_value()) {
                message << " errno=" << failure->system_error->value();
            }
            if(failure->diagnostic.has_value()) {
                message << " diagnostic=" << *failure->diagnostic;
            }
            throw std::runtime_error(message.str());
        }
        return take_arm<InvocationOwnedSourceBuildContext>(
            result, "Context creation returned no context");
    }

    [[nodiscard]] InvocationOwnedMakepkgEnvironment make_environment(
        const InvocationOwnedSourceBuildContext& context) const {
        const std::string rewrite_key =
            "url.file://" + upstream_.remote().string() + ".insteadOf";
        SourceBuildEnvironment customization{{
            {"HOME", home_.string()},
            {"PATH", "/usr/bin:/bin"},
            {"LANG", "C"},
            {"LC_ALL", "C"},
            {"MAKEPKG_LIBRARY", "/usr/share/makepkg"},
            {"GIT_TERMINAL_PROMPT", "0"},
            {"GIT_CONFIG_COUNT", "1"},
            {"GIT_CONFIG_KEY_0", rewrite_key},
            {"GIT_CONFIG_VALUE_0", upstream_.url()},
        }};
        InvocationOwnedMakepkgEnvironmentResult result =
            context.make_makepkg_environment(
                customization,
                SourceEnvironmentEmptyValuePolicy::Forward);
        return take_arm<InvocationOwnedMakepkgEnvironment>(
            result, "Makepkg environment creation failed");
    }

    [[nodiscard]] std::string stale_packagelist_filename() const {
        const fs::path pkgdest = tree_.path() / "stale-pkgdest";
        const fs::path builddir = tree_.path() / "stale-builddir";
        const fs::path srcdest = tree_.path() / "stale-srcdest";
        fs::create_directory(pkgdest);
        fs::create_directory(builddir);
        fs::create_directory(srcdest);
        std::vector<std::string> environment{
            "HOME=" + home_.string(),
            "PATH=/usr/bin:/bin",
            "LANG=C",
            "LC_ALL=C",
            "MAKEPKG_LIBRARY=/usr/share/makepkg",
            "PKGDEST=" + pkgdest.string(),
            "BUILDDIR=" + builddir.string(),
            "SRCDEST=" + srcdest.string(),
        };
        CapturedCommandResult result = capture_process(
            "/usr/bin/makepkg", {"--packagelist"},
            std::move(environment), &repository_);
        return fs::path(require_output_line(std::move(result)))
            .filename()
            .string();
    }

private:
    [[nodiscard]] std::string source_value(
        RecipeShape shape,
        bool exact_branch,
        bool raw) const {
        if(shape == RecipeShape::UnsupportedVcs) {
            return package_name_ + "::hg+https://fixture.invalid/repo";
        }
        std::string remote = upstream_.url();
        if(shape == RecipeShape::RawEvaluatedMismatch && raw) {
            remote = "https://raw.fixture.invalid/untrusted.git";
        }
        std::string value = package_name_ + "::git+" + remote;
        if(shape == RecipeShape::UnsupportedSelector) {
            value += "#tag=v1";
        } else if(exact_branch) {
            value += "#branch=main";
        }
        return value;
    }

    [[nodiscard]] std::string pkgbuild(
        RecipeShape shape,
        bool prepare_mutation,
        bool exact_branch,
        bool tracked_local_source) const {
        const std::string effective_source =
            source_value(shape, exact_branch, false);
        std::string second_source;
        if(shape == RecipeShape::MultipleGit) {
            second_source =
                "\n    \"second::git+https://fixture.invalid/second.git\"";
        }
        if(tracked_local_source) {
            second_source += "\n    \"fixture.patch\"";
        }
        std::string prepare;
        if(prepare_mutation) {
            prepare =
                "prepare() {\n"
                "    cd \"$srcdir/$pkgname\"\n"
                "    printf 'prepared\\n' >> payload.txt\n"
                "}\n\n";
        }
        const std::string pkgver_function =
            shape == RecipeShape::DynamicVersionDrift
                ? "pkgver() {\n"
                  "    cd \"$srcdir/$pkgname\"\n"
                  "    local counter=.moguet-pkgver-counter\n"
                  "    local value=0\n"
                  "    if [[ -f $counter ]]; then read -r value < $counter; fi\n"
                  "    ((value += 1))\n"
                  "    printf '%s\\n' \"$value\" > $counter\n"
                  "    printf '1.r%s.g%s' \"$value\" \"$(git rev-parse --short=12 HEAD)\"\n"
                  "}\n\n"
                : "pkgver() {\n"
                  "    cd \"$srcdir/$pkgname\"\n"
                  "    printf '1.r%s.g%s' \"$(git rev-list --count HEAD)\" \"$(git rev-parse --short=12 HEAD)\"\n"
                  "}\n\n";
        return "pkgbase=" + package_base_ + "\n"
                                            "pkgname=" +
               package_name_ + "\n"
                               "pkgver=0\n"
                               "pkgrel=1\n"
                               "pkgdesc='Moguet Slice 4 fixture'\n"
                               "arch=('any')\n"
                               "license=('GPL-3.0-or-later')\n"
                               "source=(\"" +
               effective_source + "\"" +
               second_source + ")\n"
                               "sha256sums=('SKIP'" +
               (shape == RecipeShape::MultipleGit ? " 'SKIP'" : "") +
               (tracked_local_source ? " 'SKIP'" : "") +
               ")\n\n" + pkgver_function + prepare +
               "package() {\n"
               "    install -Dm644 \"$srcdir/$pkgname/payload.txt\" \"$pkgdir/usr/share/$pkgname/payload.txt\"\n"
               "}\n";
    }

    [[nodiscard]] std::string srcinfo(
        RecipeShape shape,
        bool exact_branch,
        bool tracked_local_source) const {
        std::string result =
            "pkgbase = " + package_base_ + "\n"
                                           "\tpkgdesc = Moguet Slice 4 fixture\n"
                                           "\tpkgver = 0\n"
                                           "\tpkgrel = 1\n"
                                           "\tarch = any\n"
                                           "\tlicense = GPL-3.0-or-later\n"
                                           "\tsource = " +
            source_value(shape, exact_branch, true) +
            "\n\tsha256sums = SKIP\n";
        if(shape == RecipeShape::MultipleGit) {
            result +=
                "\tsource = second::git+https://fixture.invalid/second.git\n"
                "\tsha256sums = SKIP\n";
        }
        if(tracked_local_source) {
            result +=
                "\tsource = fixture.patch\n"
                "\tsha256sums = SKIP\n";
        }
        result += "pkgname = " + package_name_ + "\n";
        return result;
    }

    void write_file(
        const std::string& relative_path,
        std::string_view contents) const {
        const fs::path path = repository_ / relative_path;
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        require(static_cast<bool>(output),
                "Failed to open reviewed fixture file");
        output.write(
            contents.data(),
            static_cast<std::streamsize>(contents.size()));
        output.close();
        require(static_cast<bool>(output),
                "Failed to write reviewed fixture file");
    }

    void run_git(std::vector<std::string> arguments) const {
        std::vector<std::string> complete{"-C", repository_.string()};
        complete.insert(
            complete.end(), arguments.begin(), arguments.end());
        require_process_success(
            "/usr/bin/git", std::move(complete),
            git_environment(home_));
    }

    [[nodiscard]] std::string output_git(
        std::vector<std::string> arguments) const {
        std::vector<std::string> complete{"-C", repository_.string()};
        complete.insert(
            complete.end(), arguments.begin(), arguments.end());
        return require_output_line(capture_process(
            "/usr/bin/git", std::move(complete),
            git_environment(home_)));
    }

    [[nodiscard]] std::string commit(std::string_view message) const {
        run_git({"add", "-A"});
        run_git({"commit", "-q", "-m", std::string(message)});
        return output_git({"rev-parse", "HEAD"});
    }

    [[nodiscard]] ValidatedCachePath checkout() const {
        return revalidate_trusted_cache_path(
            *checkout_, CachePathRequirement::ExistingDirectory);
    }

    [[nodiscard]] AurReviewedSourceReviewIdentity identity() const {
        return AurReviewedSourceReviewIdentity::make(
            PackageBaseIdentity::make(
                PackageSourceIdentity::aur(
                    SourceLocationIdentity::known_git_remote(aur_remote_)),
                package_base_),
            SourceRevisionIdentity::git_commit(recipe_oid_));
    }

    [[nodiscard]] static ExplicitConfirmationResult explicit_yes() {
        ExplicitConfirmationInputParseResult parsed =
            parse_explicit_confirmation_input("yes");
        return take_arm<ExplicitConfirmationAcceptance>(
            parsed, "Explicit yes was not accepted");
    }

    [[nodiscard]] AcceptedReviewedSourceTarget accept_initial(
        ReviewedSourceReviewRequirement requirement) const {
        ReviewedSourceVerifiedMaterializedReview verified =
            seal_reviewed_source_materialized_review_for_test(
                ReviewedSourceMaterializedInitialFullReview{
                    identity().target_revision(),
                    ReviewedSourceReviewBody{
                        ReviewedSourceReviewReadiness::Complete, {}}});
        TrustedAurReviewedSourceReview trusted =
            make_trusted_aur_reviewed_source_review_fixture_for_test(
                std::move(verified));
        ReviewedSourceVerifiedLifecycleResult bound =
            bind_reviewed_source_verified_review(
                std::move(requirement), std::move(trusted));
        ReviewedSourceVerifiedLifecycleTarget target =
            take_arm<ReviewedSourceVerifiedLifecycleTarget>(
                bound, "Verified review did not bind");
        std::ostringstream output;
        PresentedReviewedSourceTargetResult presented =
            present_reviewed_source_target(std::move(target), output);
        ReviewedSourceAcceptanceDisposition disposition =
            decide_reviewed_source_acceptance(
                take_arm<PresentedReviewedSourceTarget>(
                    presented, "Review presentation failed"),
                explicit_yes());
        return take_arm<AcceptedReviewedSourceTarget>(
            disposition, "Review acceptance did not produce a target");
    }

    [[nodiscard]] PinnedReviewedSourceBuild make_pinned_build() {
        ReviewedSourceLifecyclePlanResult planned =
            plan_reviewed_source_lifecycle(identity());
        if(auto* requirement =
               std::get_if<ReviewedSourceReviewRequirement>(&planned)) {
            AcceptedReviewedSourceCheckoutResult materialized =
                materialize_accepted_reviewed_source_checkout(
                    accept_initial(std::move(*requirement)), checkout());
            AcceptedReviewedSourceCheckout accepted =
                take_arm<AcceptedReviewedSourceCheckout>(
                    materialized,
                    "Accepted checkout materialization failed");
            ReviewedSourcePublicationResult publication =
                publish_accepted_reviewed_source_checkout(
                    std::move(accepted));
            return take_arm<PinnedReviewedSourceBuild>(
                publication,
                "Accepted publication did not produce a pin");
        }
        ReviewedSourceAlreadyReviewedContinue already =
            take_arm<ReviewedSourceAlreadyReviewedContinue>(
                planned, "Reviewed lifecycle did not produce a build route");
        AlreadyReviewedSourceCheckoutResult materialized =
            materialize_already_reviewed_source_checkout(
                std::move(already), checkout());
        AlreadyReviewedSourceCheckout checkout_capability =
            take_arm<AlreadyReviewedSourceCheckout>(
                materialized,
                "Already-reviewed checkout materialization failed");
        ReviewedSourcePublicationResult confirmed =
            confirm_already_reviewed_source_checkout(
                std::move(checkout_capability));
        return take_arm<PinnedReviewedSourceBuild>(
            confirmed,
            "Already-reviewed confirmation did not produce a pin");
    }

    TemporaryTree tree_;
    const UpstreamGitFixture& upstream_;
    std::string package_base_;
    std::string package_name_;
    std::string aur_remote_;
    fs::path cache_home_;
    fs::path state_home_;
    fs::path home_;
    fs::path repository_;
    std::string recipe_oid_;
    std::vector<std::unique_ptr<ScopedEnvironmentVariable>> environment_;
    std::optional<ValidatedCacheRoot> cache_root_;
    std::optional<ValidatedCachePath> checkout_;
};

EvaluatedDevelSourceBuildProof build_success(
    ReviewedBuildFixture& fixture) {
    InvocationOwnedSourceBuildContext context = fixture.make_context();
    InvocationOwnedMakepkgEnvironment environment =
        fixture.make_environment(context);
    EvaluatedDevelSourceBuildResult result =
        build_evaluated_devel_source(
            std::move(context), std::move(environment));
    if(const auto* failure =
           std::get_if<EvaluatedDevelSourceBuildFailure>(&result)) {
        std::ostringstream message;
        message << "Slice 4 build failed: stage="
                << static_cast<int>(failure->stage)
                << " reason=" << static_cast<int>(failure->reason);
        if(failure->process_outcome.has_value()) {
            message << " process-arm="
                    << failure->process_outcome->index();
            if(const auto* exited = std::get_if<BoundedProcessExited>(
                   &*failure->process_outcome)) {
                message << " exit=" << exited->exit_code;
            }
        }
        if(failure->system_error.has_value()) {
            message << " errno=" << failure->system_error->value()
                    << " " << failure->system_error->message();
        }
        if(failure->diagnostic.has_value()) {
            message << " diagnostic=" << *failure->diagnostic;
        }
        throw std::runtime_error(message.str());
    }
    return take_arm<EvaluatedDevelSourceBuildProof>(
        result, "Slice 4 build returned no proof");
}

void cleanup_proof(EvaluatedDevelSourceBuildProof& proof) {
    InvocationOwnedSourceBuildContextCleanupResult cleanup =
        proof.cleanup();
    require(
        std::holds_alternative<InvocationOwnedSourceBuildContextCleaned>(
            cleanup),
        "Slice 4 proof cleanup failed");
    require(!proof.valid(), "Cleaned Slice 4 proof remained active");
}

std::string sha256_path(const fs::path& path) {
    return require_output_line(capture_process(
                                   "/usr/bin/sha256sum", {path.string()},
                                   {"PATH=/usr/bin:/bin", "LANG=C", "LC_ALL=C"}))
        .substr(0, 64);
}

std::string mtree_digest(const fs::path& archive) {
    CapturedCommandResult result = capture_process(
        "/usr/bin/bsdtar", {"-xOf", archive.string(), ".MTREE"},
        {"PATH=/usr/bin:/bin", "LANG=C", "LC_ALL=C"});
    require(
        result.exit_code == 0 && !result.output.empty(),
        "Failed to extract fixture MTREE");
    return xdg_generation_store_raw_contents_sha256(result.output);
}

std::string archive_member(
    const fs::path& archive,
    const std::string& member) {
    CapturedCommandResult result = capture_process(
        "/usr/bin/bsdtar", {"-xOf", archive.string(), member},
        {"PATH=/usr/bin:/bin", "LANG=C", "LC_ALL=C"});
    require(result.exit_code == 0,
            "Failed to extract fixture archive member");
    return result.output;
}

void test_valid_dynamic_build_and_prepare_mutation() {
    UpstreamGitFixture upstream("valid-prepare");
    ReviewedBuildFixture fixture(
        "valid-prepare", upstream, RecipeShape::Valid, true, true);
    const std::string stale = fixture.stale_packagelist_filename();
    EvaluatedDevelSourceBuildProof proof = build_success(fixture);

    const std::string* reviewed_oid = proof.reviewed_binding()
                                          .reviewed_recipe_revision()
                                          .value()
                                          .git_commit();
    const std::string* built_oid = proof.actual_built_revision()
                                       .revision()
                                       .value()
                                       .git_commit();
    const ArtifactPackageIdentity& artifact =
        proof.artifact().evidence().identity;
    require(reviewed_oid != nullptr && *reviewed_oid == fixture.recipe_oid(),
            "Reviewed recipe OID was not retained");
    require(
        proof.snapshot_identity().reviewed_binding() ==
            proof.reviewed_binding(),
        "Reviewed snapshot binding drifted");
    require(
        proof.evaluated_source().git_source().source_location() ==
                upstream.url() &&
            proof.evaluated_source().git_source().selector().kind() ==
                VcsSelectorKind::Branch &&
            proof.evaluated_source().source_count() == 1 &&
            proof.evaluated_source().tracked_local_source_count() == 0,
        "Evaluated source projection differs");
    require(built_oid != nullptr && *built_oid == upstream.oid(),
            "Actual complete Git OID differs from the built workspace");
    require(
        built_oid->size() == 40 || built_oid->size() == 64,
        "Actual built OID is abbreviated");
    require(
        artifact.package_name == fixture.package_name() &&
            artifact.package_base.value() != nullptr &&
            *artifact.package_base.value() == fixture.package_base() &&
            artifact.architecture.value() != nullptr &&
            *artifact.architecture.value() == "any" &&
            artifact.full_version.find(upstream.oid().substr(0, 12)) !=
                std::string::npos,
        "Dynamic artifact metadata differs");
    require(
        stale.find("-0-1-") != std::string::npos &&
            proof.artifact().path().filename().string() != stale,
        "Pre-preparation stale packagelist became final identity");
    require(
        proof.artifact().evidence().archive_digest.value() ==
            sha256_path(proof.artifact().path()),
        "Archive SHA-256 differs from the exact artifact");
    require(
        proof.artifact().evidence().mtree_digest.value() ==
            mtree_digest(proof.artifact().path()),
        "MTREE SHA-256 differs from the exact archive member");
    require(
        archive_member(
            proof.artifact().path(),
            "usr/share/" + fixture.package_name() + "/payload.txt") ==
            "revision-one\nprepared\n",
        "prepare() tracked-file mutation did not reach the artifact");
    cleanup_proof(proof);
}

void test_reviewed_local_source_remains_supported_input() {
    UpstreamGitFixture upstream("tracked-local");
    ReviewedBuildFixture fixture(
        "tracked-local", upstream, RecipeShape::Valid,
        false, false, true);
    EvaluatedDevelSourceBuildProof proof = build_success(fixture);
    require(
        proof.evaluated_source().source_count() == 2 &&
            proof.evaluated_source().tracked_local_source_count() == 1 &&
            proof.actual_built_revision().revision().value().git_commit() !=
                nullptr &&
            *proof.actual_built_revision()
                    .revision()
                    .value()
                    .git_commit() == upstream.oid(),
        "Reviewed local source changed the single Git workspace proof");
    cleanup_proof(proof);
}

void test_sha256_upstream_revision() {
    UpstreamGitFixture upstream(
        "sha256-upstream", GitObjectFormat::Sha256);
    ReviewedBuildFixture fixture("sha256-upstream", upstream);
    EvaluatedDevelSourceBuildProof proof = build_success(fixture);
    const SourceRevisionIdentity& revision =
        proof.actual_built_revision().revision().value();
    require(
        revision.git_commit() != nullptr &&
            revision.git_commit()->size() == 64 &&
            revision.git_object_format() != nullptr &&
            *revision.git_object_format() == GitObjectFormat::Sha256 &&
            *revision.git_commit() == upstream.oid(),
        "SHA-256 upstream repository lost its complete object format");
    cleanup_proof(proof);
}

void expect_failure(
    ReviewedBuildFixture& fixture,
    EvaluatedDevelSourceBuildFailureReason expected_reason) {
    InvocationOwnedSourceBuildContext context = fixture.make_context();
    const fs::path root = context.owned_root();
    InvocationOwnedMakepkgEnvironment environment =
        fixture.make_environment(context);
    EvaluatedDevelSourceBuildResult result =
        build_evaluated_devel_source(
            std::move(context), std::move(environment));
    const auto& failure = require_arm<EvaluatedDevelSourceBuildFailure>(
        result, "Unsupported fixture produced a proof");
    require(failure.reason == expected_reason,
            "Unsupported fixture returned a different typed failure");
    require(!failure.cleanup_consequence.has_value(),
            "Ordinary failure also reported cleanup failure");
    require(!fs::exists(root),
            "Ordinary Slice 4 failure left its private context");
}

void test_source_projection_fail_closed() {
    UpstreamGitFixture upstream("source-negative");
    {
        ReviewedBuildFixture fixture(
            "raw-eval", upstream,
            RecipeShape::RawEvaluatedMismatch);
        expect_failure(
            fixture,
            EvaluatedDevelSourceBuildFailureReason::RawEvaluatedSourceMismatch);
    }
    {
        ReviewedBuildFixture fixture(
            "multi-git", upstream, RecipeShape::MultipleGit);
        expect_failure(
            fixture,
            EvaluatedDevelSourceBuildFailureReason::UnsupportedSourceShape);
    }
    {
        ReviewedBuildFixture fixture(
            "unsupported-vcs", upstream,
            RecipeShape::UnsupportedVcs);
        expect_failure(
            fixture,
            EvaluatedDevelSourceBuildFailureReason::UnsupportedSourceShape);
    }
    {
        ReviewedBuildFixture fixture(
            "unsupported-selector", upstream,
            RecipeShape::UnsupportedSelector);
        expect_failure(
            fixture,
            EvaluatedDevelSourceBuildFailureReason::UnsupportedSourceShape);
    }
}

struct BuiltObservation {
    std::string oid;
    std::string version;
};

BuiltObservation build_observation(
    std::string label,
    UpstreamGitFixture& upstream) {
    ReviewedBuildFixture fixture(std::move(label), upstream);
    EvaluatedDevelSourceBuildProof proof = build_success(fixture);
    const std::string* oid = proof.actual_built_revision()
                                 .revision()
                                 .value()
                                 .git_commit();
    require(oid != nullptr, "Dynamic fixture has no Git OID");
    BuiltObservation result{
        *oid, proof.artifact().evidence().identity.full_version};
    cleanup_proof(proof);
    return result;
}

void test_two_upstream_revisions_change_dynamic_identity() {
    UpstreamGitFixture upstream("two-revisions");
    const BuiltObservation first =
        build_observation("dynamic-first", upstream);
    upstream.commit("revision-two\n");
    const BuiltObservation second =
        build_observation("dynamic-second", upstream);
    require(first.oid != second.oid,
            "Two upstream revisions produced one actual Git OID");
    require(first.version != second.version,
            "Dynamic package identity did not follow the actual revision");
    require(
        first.version.find(first.oid.substr(0, 12)) !=
                std::string::npos &&
            second.version.find(second.oid.substr(0, 12)) !=
                std::string::npos,
        "Dynamic versions are not tied to their complete Git proofs");
}

void test_dynamic_version_drift_fails_closed() {
    UpstreamGitFixture upstream("dynamic-drift");
    ReviewedBuildFixture fixture(
        "dynamic-drift", upstream,
        RecipeShape::DynamicVersionDrift);
    expect_failure(
        fixture,
        EvaluatedDevelSourceBuildFailureReason::DynamicVersionUnavailable);
}

void write_file(const fs::path& path, std::string_view bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    require(static_cast<bool>(output), "Failed to open injected file");
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    output.close();
    require(static_cast<bool>(output), "Failed to finish injected file");
}

void test_stale_pkgdest_and_extra_artifact() {
    UpstreamGitFixture upstream("inventory-negative");
    {
        ReviewedBuildFixture fixture("stale-pkgdest", upstream);
        InvocationOwnedSourceBuildContext context = fixture.make_context();
        const fs::path root = context.owned_root();
        write_file(context.pkgdest() / "stale.pkg.tar.zst", "stale");
        InvocationOwnedMakepkgEnvironment environment =
            fixture.make_environment(context);
        EvaluatedDevelSourceBuildResult result =
            build_evaluated_devel_source(
                std::move(context), std::move(environment));
        require(
            require_arm<EvaluatedDevelSourceBuildFailure>(
                result, "Stale PKGDEST produced a proof")
                    .reason ==
                EvaluatedDevelSourceBuildFailureReason::ArtifactInventoryMismatch,
            "Stale PKGDEST returned the wrong failure");
        require(!fs::exists(root),
                "Stale PKGDEST failure left its context");
    }
    {
        ReviewedBuildFixture fixture("extra-artifact", upstream);
        set_evaluated_devel_source_build_test_hook(
            [](EvaluatedDevelSourceBuildTestEvent event,
               const fs::path& root,
               const fs::path&) {
                if(event ==
                   EvaluatedDevelSourceBuildTestEvent::AfterPackageBuild) {
                    write_file(
                        root / "pkgdest" / "extra.pkg.tar.zst",
                        "extra");
                }
            });
        expect_failure(
            fixture,
            EvaluatedDevelSourceBuildFailureReason::ArtifactInventoryMismatch);
        set_evaluated_devel_source_build_test_hook({});
    }
}

void test_artifact_replacement() {
    UpstreamGitFixture upstream("replacement");
    ReviewedBuildFixture fixture("replacement", upstream);
    set_evaluated_devel_source_build_test_hook(
        [](EvaluatedDevelSourceBuildTestEvent event,
           const fs::path&,
           const fs::path& artifact) {
            if(event !=
               EvaluatedDevelSourceBuildTestEvent::AfterArtifactInventory) {
                return;
            }
            const fs::path displaced = artifact.parent_path() / "displaced";
            fs::rename(artifact, displaced);
            write_file(artifact, "replacement");
        });
    expect_failure(
        fixture,
        EvaluatedDevelSourceBuildFailureReason::ArtifactReplacement);
    set_evaluated_devel_source_build_test_hook({});
}

void test_ambiguous_workspace_and_artifact_hardlink() {
    UpstreamGitFixture upstream("ambiguity-hardlink");
    {
        ReviewedBuildFixture fixture("ambiguous-workspace", upstream);
        set_evaluated_devel_source_build_test_hook(
            [](EvaluatedDevelSourceBuildTestEvent event,
               const fs::path& root,
               const fs::path&) {
                if(event ==
                   EvaluatedDevelSourceBuildTestEvent::AfterSourcePreparation) {
                    fs::create_directories(
                        root / "build" / "ambiguous" / ".git");
                }
            });
        expect_failure(
            fixture,
            EvaluatedDevelSourceBuildFailureReason::SourceWorkspaceAmbiguous);
        set_evaluated_devel_source_build_test_hook({});
    }
    {
        ReviewedBuildFixture fixture("artifact-hardlink", upstream);
        const fs::path external_link =
            fixture.home() / "external-artifact-hardlink";
        set_evaluated_devel_source_build_test_hook(
            [external_link](EvaluatedDevelSourceBuildTestEvent event,
                            const fs::path& root,
                            const fs::path&) {
                if(event !=
                   EvaluatedDevelSourceBuildTestEvent::AfterPackageBuild) {
                    return;
                }
                std::vector<fs::path> artifacts;
                for(const fs::directory_entry& entry :
                    fs::directory_iterator(root / "pkgdest")) {
                    if(entry.is_regular_file()) {
                        artifacts.push_back(entry.path());
                    }
                }
                require(artifacts.size() == 1,
                        "Hardlink hook did not find one artifact");
                fs::create_hard_link(artifacts.front(), external_link);
            });
        expect_failure(
            fixture,
            EvaluatedDevelSourceBuildFailureReason::ArtifactInventoryMismatch);
        set_evaluated_devel_source_build_test_hook({});
    }
}

fs::path build_foreign_artifact(TemporaryTree& tree) {
    const fs::path recipe = tree.path() / "foreign-recipe";
    const fs::path pkgdest = tree.path() / "foreign-pkgdest";
    const fs::path builddir = tree.path() / "foreign-build";
    const fs::path srcdest = tree.path() / "foreign-srcdest";
    const fs::path home = tree.path() / "foreign-home";
    for(const fs::path& directory :
        {recipe, pkgdest, builddir, srcdest, home}) {
        fs::create_directory(directory);
    }
    write_file(
        recipe / "PKGBUILD",
        "pkgname=moguet-foreign-artifact\n"
        "pkgver=9\n"
        "pkgrel=1\n"
        "pkgdesc='foreign'\n"
        "arch=('any')\n"
        "license=('GPL-3.0-or-later')\n"
        "package() { install -dm755 \"$pkgdir/usr/share/moguet-foreign-artifact\"; }\n");
    std::vector<std::string> environment{
        "HOME=" + home.string(),
        "PATH=/usr/bin:/bin",
        "LANG=C",
        "LC_ALL=C",
        "MAKEPKG_LIBRARY=/usr/share/makepkg",
        "PKGDEST=" + pkgdest.string(),
        "BUILDDIR=" + builddir.string(),
        "SRCDEST=" + srcdest.string(),
    };
    require_process_success(
        "/usr/bin/makepkg", {"--nodeps", "--noconfirm"},
        environment, &recipe);
    std::vector<fs::path> artifacts;
    for(const fs::directory_entry& entry :
        fs::directory_iterator(pkgdest)) {
        if(entry.is_regular_file()) artifacts.push_back(entry.path());
    }
    require(artifacts.size() == 1,
            "Foreign fixture did not build exactly one artifact");
    return artifacts.front();
}

void test_artifact_metadata_mismatch() {
    UpstreamGitFixture upstream("metadata-mismatch");
    ReviewedBuildFixture fixture("metadata-mismatch", upstream);
    TemporaryTree foreign_tree("foreign-artifact");
    const fs::path foreign = build_foreign_artifact(foreign_tree);
    set_evaluated_devel_source_build_test_hook(
        [foreign](EvaluatedDevelSourceBuildTestEvent event,
                  const fs::path& root,
                  const fs::path&) {
            if(event !=
               EvaluatedDevelSourceBuildTestEvent::AfterPackageBuild) {
                return;
            }
            std::vector<fs::path> artifacts;
            for(const fs::directory_entry& entry :
                fs::directory_iterator(root / "pkgdest")) {
                if(entry.is_regular_file()) artifacts.push_back(entry.path());
            }
            require(artifacts.size() == 1,
                    "Metadata hook did not find one artifact");
            fs::copy_file(
                foreign, artifacts.front(),
                fs::copy_options::overwrite_existing);
        });
    expect_failure(
        fixture,
        EvaluatedDevelSourceBuildFailureReason::ArtifactMetadataMismatch);
    set_evaluated_devel_source_build_test_hook({});
}

void test_cross_context_environment_rejected() {
    UpstreamGitFixture upstream("cross-context");
    ReviewedBuildFixture fixture("cross-context", upstream);
    InvocationOwnedSourceBuildContext first = fixture.make_context();
    InvocationOwnedSourceBuildContext second = fixture.make_context();
    const fs::path first_root = first.owned_root();
    const fs::path second_root = second.owned_root();
    InvocationOwnedMakepkgEnvironment second_environment =
        fixture.make_environment(second);
    EvaluatedDevelSourceBuildResult result =
        build_evaluated_devel_source(
            std::move(first), std::move(second_environment));
    require(
        require_arm<EvaluatedDevelSourceBuildFailure>(
            result, "Cross-context environment produced a proof")
                .reason ==
            EvaluatedDevelSourceBuildFailureReason::EnvironmentLineageMismatch,
        "Cross-context environment returned the wrong failure");
    require(!fs::exists(first_root),
            "Rejected context A was not cleaned");
    InvocationOwnedSourceBuildContextCleanupResult cleanup =
        second.cleanup();
    require(
        std::holds_alternative<InvocationOwnedSourceBuildContextCleaned>(
            cleanup) &&
            !fs::exists(second_root),
        "Context B cleanup failed");
}

void test_cleanup_failure_preserves_primary() {
    UpstreamGitFixture upstream("cleanup-failure");
    ReviewedBuildFixture fixture("cleanup-failure", upstream);
    InvocationOwnedSourceBuildContext context = fixture.make_context();
    const fs::path root = context.owned_root();
    write_file(context.pkgdest() / "stale.pkg.tar.zst", "stale");
    InvocationOwnedMakepkgEnvironment environment =
        fixture.make_environment(context);
    bool injected = false;
    set_invocation_owned_source_build_context_test_hook(
        [&injected](InvocationOwnedSourceBuildContextTestEvent event,
                    const fs::path& owned_root) {
            if(event ==
                   InvocationOwnedSourceBuildContextTestEvent::BeforeCleanup &&
               !injected) {
                injected = true;
                write_file(owned_root / "unexpected", "cleanup blocker");
            }
        });
    EvaluatedDevelSourceBuildResult result =
        build_evaluated_devel_source(
            std::move(context), std::move(environment));
    set_invocation_owned_source_build_context_test_hook({});
    const auto& failure = require_arm<EvaluatedDevelSourceBuildFailure>(
        result, "Cleanup-failure fixture produced a proof");
    require(
        failure.reason ==
                EvaluatedDevelSourceBuildFailureReason::ArtifactInventoryMismatch &&
            failure.cleanup_consequence.has_value() &&
            failure.cleanup_consequence->failure.reason ==
                InvocationOwnedSourceBuildContextFailureReason::ConcurrentReplacement,
        "Cleanup failure replaced or lost the primary failure");
    require(fs::exists(root),
            "Injected cleanup failure did not retain the exact test root");
    struct stat root_status{};
    require(
        ::lstat(root.c_str(), &root_status) == 0 &&
            S_ISDIR(root_status.st_mode) &&
            root_status.st_uid == ::geteuid(),
        "Retained cleanup fixture root lost test ownership");
    std::vector<fs::path> retained_directories{root};
    for(const fs::directory_entry& entry :
        fs::recursive_directory_iterator(root)) {
        struct stat status{};
        require(
            ::lstat(entry.path().c_str(), &status) == 0 &&
                status.st_uid == ::geteuid() &&
                !S_ISLNK(status.st_mode),
            "Retained cleanup fixture contains a foreign entry");
        if(S_ISDIR(status.st_mode)) {
            retained_directories.push_back(entry.path());
        }
    }
    for(const fs::path& directory : retained_directories) {
        fs::permissions(
            directory, fs::perms::owner_all,
            fs::perm_options::replace);
    }
    std::error_code cleanup_error;
    fs::remove_all(root, cleanup_error);
    require(!cleanup_error && !fs::exists(root),
            "Failed to remove the exact cleanup-failure test root");
}

std::vector<fs::path> context_root_inventory() {
    std::vector<fs::path> roots;
    for(const fs::directory_entry& entry : fs::directory_iterator("/tmp")) {
        const std::string leaf = entry.path().filename().string();
        if(!leaf.starts_with("moguet-source-build-context-")) continue;
        struct stat status{};
        if(::lstat(entry.path().c_str(), &status) == 0 &&
           S_ISDIR(status.st_mode) && status.st_uid == ::geteuid()) {
            roots.push_back(entry.path());
        }
    }
    std::sort(roots.begin(), roots.end());
    return roots;
}

} // namespace

int main() {
    const std::vector<fs::path> before = context_root_inventory();
    try {
        test_valid_dynamic_build_and_prepare_mutation();
        test_reviewed_local_source_remains_supported_input();
        test_sha256_upstream_revision();
        test_source_projection_fail_closed();
        test_two_upstream_revisions_change_dynamic_identity();
        test_dynamic_version_drift_fails_closed();
        test_stale_pkgdest_and_extra_artifact();
        test_artifact_replacement();
        test_ambiguous_workspace_and_artifact_hardlink();
        test_artifact_metadata_mismatch();
        test_cross_context_environment_rejected();
        test_cleanup_failure_preserves_primary();
        set_evaluated_devel_source_build_test_hook({});
        set_invocation_owned_source_build_context_test_hook({});
        require(
            context_root_inventory() == before,
            "Focused test left an invocation-owned context root");
    } catch(const std::exception& error) {
        set_evaluated_devel_source_build_test_hook({});
        set_invocation_owned_source_build_context_test_hook({});
        std::cerr << "evaluated devel source-build tests failed: "
                  << error.what() << '\n';
        return 1;
    }
    std::cout << "evaluated devel source-build tests passed\n";
    return 0;
}
