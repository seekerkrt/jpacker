#include "source_package_identity.hpp"

#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

void run_vcs_source_identity_tests();
void run_source_entry_parser_tests();
void run_srcinfo_source_metadata_tests();

namespace {

static_assert(!std::is_default_constructible_v<SourceLocationIdentity>);
static_assert(!std::is_default_constructible_v<PackageSourceIdentity>);
static_assert(!std::is_default_constructible_v<PackageBaseIdentity>);
static_assert(!std::is_default_constructible_v<PackageChildIdentity>);
static_assert(!std::is_default_constructible_v<SourceRevisionIdentity>);
static_assert(!std::is_default_constructible_v<PackageVersionIdentity>);
static_assert(!std::is_default_constructible_v<PackageArchitectureIdentity>);
static_assert(!std::is_default_constructible_v<SourceAwarePackageIdentity>);

template<typename Function>
void expect_invalid_argument(Function&& function) {
    try {
        std::forward<Function>(function)();
    } catch(const std::invalid_argument&) {
        return;
    }
    throw std::runtime_error("Expected std::invalid_argument.");
}

void require(bool condition, const std::string& message) {
    if(!condition) throw std::runtime_error(message);
}

PackageSourceIdentity aur_source() {
    return PackageSourceIdentity::aur(
            SourceLocationIdentity::known_git_remote(
                    "https://aur.archlinux.org/example-base.git"));
}

PackageChildIdentity aur_child(
        const std::string& package_base = "example-base",
        const std::string& package_name = "example-child") {
    return PackageChildIdentity::make(
            PackageBaseIdentity::make(aur_source(), package_base),
            package_name);
}

void test_source_location_states_and_source_combinations() {
    const SourceLocationIdentity repository_location =
            SourceLocationIdentity::known_git_remote(
                    "https://gitlab.archlinux.org/archlinux/packaging/packages/example.git");
    const PackageSourceIdentity repository =
            PackageSourceIdentity::repository("extra", repository_location);
    require(repository.kind() == PackageSourceKind::Repository,
            "Repository source kind was lost.");
    require(repository.repository_name() != nullptr &&
                    *repository.repository_name() == "extra",
            "Repository identity was lost.");
    require(repository.location().state() == SourceLocationState::Known &&
                    repository.location().value() != nullptr,
            "Known repository location was lost.");

    const PackageSourceIdentity aur = PackageSourceIdentity::aur(
            SourceLocationIdentity::unknown(SourceLocationKind::GitRemote));
    require(aur.kind() == PackageSourceKind::Aur &&
                    aur.repository_name() == nullptr &&
                    aur.location().state() == SourceLocationState::Unknown,
            "Unknown AUR location was flattened.");

    const SourceLocationIdentity failed_local_location =
            SourceLocationIdentity::unavailable(
                    SourceLocationKind::LocalPath,
                    IdentityUnavailableReason::ObservationFailed);
    const PackageSourceIdentity local =
            PackageSourceIdentity::local(failed_local_location);
    require(local.kind() == PackageSourceKind::Local &&
                    local.location().state() ==
                            SourceLocationState::Unavailable &&
                    local.location().unavailable_reason() != nullptr &&
                    *local.location().unavailable_reason() ==
                            IdentityUnavailableReason::ObservationFailed,
            "Unavailable local location was flattened.");

    expect_invalid_argument([] {
        static_cast<void>(PackageSourceIdentity::repository(
                "",
                SourceLocationIdentity::unknown(
                        SourceLocationKind::GitRemote)));
    });
    expect_invalid_argument([] {
        static_cast<void>(PackageSourceIdentity::repository(
                "bad\nrepository",
                SourceLocationIdentity::unknown(
                        SourceLocationKind::GitRemote)));
    });
    expect_invalid_argument([] {
        static_cast<void>(PackageSourceIdentity::aur(
                SourceLocationIdentity::known_local_path("/tmp/source")));
    });
    expect_invalid_argument([] {
        static_cast<void>(PackageSourceIdentity::local(
                SourceLocationIdentity::unknown(
                        SourceLocationKind::GitRemote)));
    });
    expect_invalid_argument([] {
        static_cast<void>(SourceLocationIdentity::known_git_remote(""));
    });
    expect_invalid_argument([] {
        static_cast<void>(SourceLocationIdentity::known_git_remote(
                "https://example.invalid/source repo.git"));
    });
    expect_invalid_argument([] {
        static_cast<void>(SourceLocationIdentity::known_local_path(
                "relative/source"));
    });
    expect_invalid_argument([] {
        static_cast<void>(SourceLocationIdentity::known_local_path(
                std::string("/tmp/") + std::string("\xc0\xaf", 2)));
    });
    expect_invalid_argument([] {
        static_cast<void>(SourceLocationIdentity::unknown(
                static_cast<SourceLocationKind>(-1)));
    });
}

void test_package_base_and_child_are_distinct_identities() {
    const PackageBaseIdentity base =
            PackageBaseIdentity::make(aur_source(), "suite-base");
    const PackageChildIdentity first =
            PackageChildIdentity::make(base, "suite-cli");
    const PackageChildIdentity second =
            PackageChildIdentity::make(base, "suite-gui");

    require(first.package_base() == second.package_base(),
            "Split children lost their shared PackageBase identity.");
    require(first != second,
            "Distinct split children collapsed to their PackageBase.");
    require(first.package_name() == "suite-cli" &&
                    first.package_base().package_base() == "suite-base",
            "Package child and PackageBase fields were mixed.");

    const PackageChildIdentity other_source = PackageChildIdentity::make(
            PackageBaseIdentity::make(
                    PackageSourceIdentity::repository(
                            "extra",
                            SourceLocationIdentity::unknown(
                                    SourceLocationKind::GitRemote)),
                    "suite-base"),
            "suite-cli");
    require(first != other_source,
            "Same child/base names from different sources collapsed.");

    expect_invalid_argument([] {
        static_cast<void>(PackageBaseIdentity::make(aur_source(), ""));
    });
    expect_invalid_argument([] {
        static_cast<void>(PackageBaseIdentity::make(
                aur_source(), "invalid/base"));
    });
    expect_invalid_argument([] {
        static_cast<void>(PackageChildIdentity::make(
                PackageBaseIdentity::make(aur_source(), "valid-base"),
                ""));
    });
    expect_invalid_argument([] {
        static_cast<void>(PackageChildIdentity::make(
                PackageBaseIdentity::make(aur_source(), "valid-base"),
                "bad child"));
    });
}

void test_source_revision_states_and_git_commit_contract() {
    const std::string sha1(40, 'a');
    const SourceRevisionIdentity known_sha1 =
            SourceRevisionIdentity::git_commit(sha1);
    require(known_sha1.state() == SourceRevisionState::Known &&
                    known_sha1.git_object_format() != nullptr &&
                    *known_sha1.git_object_format() == GitObjectFormat::Sha1 &&
                    known_sha1.git_commit() != nullptr &&
                    *known_sha1.git_commit() == sha1,
            "Known SHA-1 commit identity was not retained.");

    const SourceRevisionIdentity known_sha256 =
            SourceRevisionIdentity::git_commit(std::string(64, 'b'));
    require(known_sha256.git_object_format() != nullptr &&
                    *known_sha256.git_object_format() ==
                            GitObjectFormat::Sha256,
            "Known SHA-256 commit identity was not retained.");

    const SourceRevisionIdentity unknown = SourceRevisionIdentity::unknown();
    const SourceRevisionIdentity absent = SourceRevisionIdentity::absent();
    const SourceRevisionIdentity unavailable =
            SourceRevisionIdentity::unavailable(
                    IdentityUnavailableReason::AuthorityUnavailable);
    const SourceRevisionIdentity inapplicable =
            SourceRevisionIdentity::inapplicable();
    require(unknown.state() == SourceRevisionState::Unknown &&
                    absent.state() == SourceRevisionState::Absent &&
                    unavailable.state() == SourceRevisionState::Unavailable &&
                    inapplicable.state() ==
                            SourceRevisionState::Inapplicable,
            "Source revision states were collapsed.");
    require(unknown.git_commit() == nullptr &&
                    absent.git_commit() == nullptr &&
                    unavailable.git_commit() == nullptr &&
                    inapplicable.git_commit() == nullptr,
            "Non-known source revision carried a commit payload.");
    require(unavailable.unavailable_reason() != nullptr &&
                    *unavailable.unavailable_reason() ==
                            IdentityUnavailableReason::AuthorityUnavailable,
            "Unavailable revision reason was lost.");

    expect_invalid_argument([] {
        static_cast<void>(SourceRevisionIdentity::git_commit(""));
    });
    expect_invalid_argument([] {
        static_cast<void>(SourceRevisionIdentity::git_commit(
                std::string(39, 'a')));
    });
    expect_invalid_argument([] {
        static_cast<void>(SourceRevisionIdentity::git_commit(
                std::string(40, 'A')));
    });
    expect_invalid_argument([] {
        std::string invalid(40, 'a');
        invalid[10] = 'g';
        static_cast<void>(SourceRevisionIdentity::git_commit(invalid));
    });
}

void test_package_version_and_architecture_evidence() {
    const PackageVersionIdentity composite =
            PackageVersionIdentity::composite("2:1.4.0-3");
    require(composite.state() == PackageVersionState::Known &&
                    composite.representation() != nullptr &&
                    *composite.representation() ==
                            PackageVersionRepresentation::Composite &&
                    composite.full_version() != nullptr &&
                    *composite.full_version() == "2:1.4.0-3" &&
                    composite.pkgver() == nullptr,
            "Composite package version was reparsed or lost.");

    const PackageVersionIdentity structured =
            PackageVersionIdentity::pkgver_pkgrel("2", "1.4.0", "3");
    require(structured.representation() != nullptr &&
                    *structured.representation() ==
                            PackageVersionRepresentation::PkgverPkgrel &&
                    structured.full_version() != nullptr &&
                    *structured.full_version() == "2:1.4.0-3" &&
                    structured.epoch() != nullptr &&
                    *structured.epoch() == "2" &&
                    structured.pkgver() != nullptr &&
                    *structured.pkgver() == "1.4.0" &&
                    structured.pkgrel() != nullptr &&
                    *structured.pkgrel() == "3",
            "Structured pkgver/pkgrel identity was not retained.");
    require(composite != structured,
            "Structural equality hid version evidence representation.");

    const PackageVersionIdentity unknown =
            PackageVersionIdentity::unknown();
    const PackageVersionIdentity unavailable =
            PackageVersionIdentity::unavailable(
                    IdentityUnavailableReason::ObservationFailed);
    require(unknown.state() == PackageVersionState::Unknown &&
                    unknown.full_version() == nullptr &&
                    unavailable.state() ==
                            PackageVersionState::Unavailable &&
                    unavailable.unavailable_reason() != nullptr,
            "Package version observation states were flattened.");

    const PackageArchitectureIdentity architecture =
            PackageArchitectureIdentity::known({"x86_64", "aarch64"});
    require(architecture.state() == PackageArchitectureState::Known &&
                    architecture.architectures() ==
                            std::vector<std::string>({"aarch64", "x86_64"}),
            "Architecture identity is not canonical.");
    require(PackageArchitectureIdentity::unknown().architectures().empty(),
            "Unknown architecture carried values.");
    require(PackageArchitectureIdentity::unavailable(
                    IdentityUnavailableReason::AuthorityUnavailable)
                    .unavailable_reason() != nullptr,
            "Unavailable architecture reason was lost.");

    expect_invalid_argument([] {
        static_cast<void>(PackageVersionIdentity::composite(""));
    });
    expect_invalid_argument([] {
        static_cast<void>(PackageVersionIdentity::composite("1.0 1"));
    });
    expect_invalid_argument([] {
        static_cast<void>(PackageVersionIdentity::pkgver_pkgrel(
                "epoch", "1.0", "1"));
    });
    expect_invalid_argument([] {
        static_cast<void>(PackageVersionIdentity::pkgver_pkgrel(
                std::nullopt, "", "1"));
    });
    expect_invalid_argument([] {
        static_cast<void>(PackageArchitectureIdentity::known({}));
    });
    expect_invalid_argument([] {
        static_cast<void>(PackageArchitectureIdentity::known(
                {"x86_64", "x86_64"}));
    });
    expect_invalid_argument([] {
        static_cast<void>(PackageArchitectureIdentity::known(
                {"bad architecture"}));
    });
}

void test_source_aware_structural_equality() {
    const SourceAwarePackageIdentity identity =
            SourceAwarePackageIdentity::make(
                    aur_child(),
                    SourceRevisionIdentity::unknown(),
                    PackageVersionIdentity::composite("1.0-1"),
                    PackageArchitectureIdentity::known({"x86_64"}));
    const SourceAwarePackageIdentity same = identity;
    require(identity == same,
            "Identical source-aware package values are not equal.");

    const SourceAwarePackageIdentity revision_changed =
            SourceAwarePackageIdentity::make(
                    aur_child(),
                    SourceRevisionIdentity::git_commit(std::string(40, 'c')),
                    PackageVersionIdentity::composite("1.0-1"),
                    PackageArchitectureIdentity::known({"x86_64"}));
    require(identity != revision_changed,
            "Source revision was omitted from structural equality.");

    const SourceAwarePackageIdentity release_changed =
            SourceAwarePackageIdentity::make(
                    aur_child(),
                    SourceRevisionIdentity::unknown(),
                    PackageVersionIdentity::composite("1.0-2"),
                    PackageArchitectureIdentity::known({"x86_64"}));
    require(identity != release_changed,
            "Package release was omitted from structural equality.");
}

} // namespace

int main() {
    try {
        test_source_location_states_and_source_combinations();
        test_package_base_and_child_are_distinct_identities();
        test_source_revision_states_and_git_commit_contract();
        test_package_version_and_architecture_evidence();
        test_source_aware_structural_equality();
        run_vcs_source_identity_tests();
        run_source_entry_parser_tests();
        run_srcinfo_source_metadata_tests();
    } catch(const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    std::cout << "source package identity tests: all checks passed\n";
    return 0;
}
