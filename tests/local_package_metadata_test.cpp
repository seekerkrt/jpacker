#include "local_package_metadata.hpp"

#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

static_assert(!std::is_default_constructible_v<LocalPackageMetadataParseResult>);
static_assert(std::is_copy_constructible_v<LocalPackageMetadataParseResult>);
static_assert(
        std::is_nothrow_move_constructible_v<LocalPackageMetadataParseResult>);
static_assert(!std::is_copy_assignable_v<LocalPackageMetadataParseResult>);
static_assert(!std::is_move_assignable_v<LocalPackageMetadataParseResult>);

namespace {

namespace fs = std::filesystem;

using Comparison = LocalPackageMetadataComparison;
using RelationKind = LocalPackageMetadataRelationKind;
using RelationTarget = LocalPackageMetadataRelationTarget;

void expect(bool condition, const std::string& message) {
    if(!condition) throw std::runtime_error(message);
}

std::string read_fixture(
        const fs::path& fixture_directory, std::string_view name) {
    const fs::path path = fixture_directory / std::string(name);
    std::ifstream input(path, std::ios::binary);
    if(!input) {
        throw std::runtime_error(
                "Failed to open local package metadata fixture: " +
                path.string());
    }
    return std::string(
            std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>());
}

const LocalPackageMetadata& require_success(
        const LocalPackageMetadataParseResult& result,
        std::string_view context) {
    const std::string context_text(context);
    expect(result.is_success(), context_text + ": parse did not succeed");
    expect(result.metadata() != nullptr, context_text + ": metadata is absent");
    expect(result.failure() == nullptr, context_text + ": failure is exposed");
    return *result.metadata();
}

void expect_failure(
        std::string_view source, LocalPackageMetadataParseErrorCode code,
        std::size_t line, const std::string& context) {
    const LocalPackageMetadataParseResult result =
            parse_local_package_metadata(source);
    expect(!result.is_success(), context + ": parse unexpectedly succeeded");
    expect(result.metadata() == nullptr, context + ": partial metadata is exposed");
    expect(result.failure() != nullptr, context + ": failure is absent");
    expect(
            *result.failure() == LocalPackageMetadataParseFailure{code, line},
            context + ": failure code or line differs");
}

RelationTarget package_target(
        std::string name,
        std::optional<Comparison> comparison = std::nullopt,
        std::optional<std::string> version = std::nullopt) {
    return RelationTarget{
            LocalPackageMetadataRelationTargetKind::Package,
            std::move(name), comparison, std::move(version)};
}

RelationTarget soname_target(std::string name) {
    return RelationTarget{
            LocalPackageMetadataRelationTargetKind::Soname,
            std::move(name), std::nullopt, std::nullopt};
}

LocalPackageMetadataRelation relation(
        RelationKind kind, std::string raw_value,
        RelationTarget target,
        std::optional<std::string> package_name = std::nullopt,
        std::optional<std::string> architecture_qualifier = std::nullopt,
        std::optional<std::string> optdepends_description = std::nullopt) {
    const LocalPackageMetadataScopeKind scope_kind = package_name.has_value()
            ? LocalPackageMetadataScopeKind::ChildPackage
            : LocalPackageMetadataScopeKind::PackageBase;
    return LocalPackageMetadataRelation{
            kind,
            std::move(raw_value),
            std::move(target),
            LocalPackageMetadataScope{scope_kind, std::move(package_name)},
            std::move(architecture_qualifier),
            std::move(optdepends_description),
            false};
}

LocalPackageMetadataRelation unset_relation(
        RelationKind kind, std::string package_name,
        std::optional<std::string> architecture_qualifier = std::nullopt) {
    return LocalPackageMetadataRelation{
            kind,
            "",
            std::nullopt,
            LocalPackageMetadataScope{
                    LocalPackageMetadataScopeKind::ChildPackage,
                    std::move(package_name)},
            std::move(architecture_qualifier),
            std::nullopt,
            true};
}

std::string minimal_metadata(std::string_view package_name = "minimal") {
    const std::string identity(package_name);
    return "pkgbase = " + identity + "\n" +
            "pkgver = 1\n" +
            "pkgrel = 1\n" +
            "arch = any\n" +
            "pkgname = " + identity + "\n";
}

void test_valid_split_fixture(const fs::path& fixture_directory) {
    const LocalPackageMetadataParseResult result =
            parse_local_package_metadata(
                    read_fixture(fixture_directory, "valid-split.srcinfo"));
    const LocalPackageMetadata& metadata =
            require_success(result, "valid split fixture");

    expect(metadata.package_base == "sample-suite", "PackageBase differs");
    expect(
            metadata.epoch == std::optional<std::string>{"2"},
            "epoch differs");
    expect(metadata.pkgver == "1.0+git.r42", "pkgver bytes differ");
    expect(metadata.pkgrel == "3.1", "pkgrel bytes differ");
    expect(
            metadata.architectures ==
                    std::vector<std::string>{"x86_64", "aarch64"},
            "PackageBase architectures differ");
    expect(
            metadata.children ==
                    std::vector<LocalPackageMetadataChild>{
                            {"sample-suite-cli",
                             true,
                             false,
                             {"x86_64"}},
                            {"sample-suite-libs",
                             true,
                             false,
                             {"aarch64"}}},
            "ordered child metadata differs");

    const std::vector<LocalPackageMetadataRelation> expected_relations = {
            relation(
                    RelationKind::Depends, "runtime>=2",
                    package_target(
                            "runtime", Comparison::GreaterThanOrEqual, "2")),
            relation(
                    RelationKind::Depends, "lib:libexample.so.1",
                    soname_target("lib:libexample.so.1"), std::nullopt,
                    "x86_64"),
            relation(
                    RelationKind::Makedepends, "cmake>=3.29",
                    package_target(
                            "cmake", Comparison::GreaterThanOrEqual,
                            "3.29")),
            relation(
                    RelationKind::Checkdepends, "pytest<9",
                    package_target("pytest", Comparison::LessThan, "9")),
            relation(
                    RelationKind::Optdepends,
                    "helper>=1: keeps: inner  double spaces",
                    package_target(
                            "helper", Comparison::GreaterThanOrEqual, "1"),
                    std::nullopt, std::nullopt,
                    "keeps: inner  double spaces"),
            relation(
                    RelationKind::Provides, "virtual-suite=1.0",
                    package_target(
                            "virtual-suite", Comparison::Equal, "1.0")),
            relation(
                    RelationKind::Conflicts, "legacy-suite<1",
                    package_target(
                            "legacy-suite", Comparison::LessThan, "1")),
            relation(
                    RelationKind::Replaces, "old-suite",
                    package_target("old-suite")),
            relation(
                    RelationKind::Depends, "cli-runtime=4",
                    package_target("cli-runtime", Comparison::Equal, "4"),
                    "sample-suite-cli"),
            relation(
                    RelationKind::Depends, "cli-accelerator>=2",
                    package_target(
                            "cli-accelerator", Comparison::GreaterThanOrEqual,
                            "2"),
                    "sample-suite-cli", "x86_64"),
            relation(
                    RelationKind::Checkdepends, "child-test",
                    package_target("child-test"), "sample-suite-cli"),
            relation(
                    RelationKind::Optdepends,
                    "child-helper: child description",
                    package_target("child-helper"), "sample-suite-cli",
                    std::nullopt, "child description"),
            relation(
                    RelationKind::Provides, "suite-command=2",
                    package_target("suite-command", Comparison::Equal, "2"),
                    "sample-suite-cli"),
            relation(
                    RelationKind::Conflicts, "suite-legacy",
                    package_target("suite-legacy"), "sample-suite-cli"),
            relation(
                    RelationKind::Replaces, "suite-old",
                    package_target("suite-old"), "sample-suite-cli"),
            unset_relation(RelationKind::Depends, "sample-suite-libs"),
            relation(
                    RelationKind::Checkdepends, "child-check>=7",
                    package_target(
                            "child-check", Comparison::GreaterThanOrEqual,
                            "7"),
                    "sample-suite-libs", "aarch64"),
            relation(
                    RelationKind::Optdepends,
                    "libs-docs: documentation in 日本語",
                    package_target("libs-docs"), "sample-suite-libs",
                    std::nullopt, "documentation in 日本語"),
            relation(
                    RelationKind::Provides, "lib:libsample.so.2",
                    soname_target("lib:libsample.so.2"),
                    "sample-suite-libs"),
    };
    expect(
            metadata.relations == expected_relations,
            "typed relation/order/scope/qualifier differs");
}

void test_architecture_contract() {
    const LocalPackageMetadataParseResult result =
            parse_local_package_metadata(
                    "pkgbase = architecture-model\n"
                    "pkgver = 1\n"
                    "pkgrel = 1\n"
                    "arch = X86_64\n"
                    "arch = aarch64\n"
                    "depends_X86_64 = runtime\n"
                    "pkgname = inherited\n"
                    "pkgname = cleared\n"
                    "arch =\n"
                    "depends =\n"
                    "pkgname = narrowed\n"
                    "arch =\n"
                    "arch = aarch64\n"
                    "depends_aarch64 = child-runtime\n"
                    "pkgname = portable\n"
                    "arch = any\n");
    const LocalPackageMetadata& metadata =
            require_success(result, "architecture model");
    expect(
            metadata.children ==
                    std::vector<LocalPackageMetadataChild>{
                            {"inherited", false, false, {}},
                            {"cleared", true, true, {}},
                            {"narrowed", true, true, {"aarch64"}},
                            {"portable", true, false, {"any"}}},
            "child architecture override/unset model differs");
    expect(
            metadata.relations.size() == 3,
            "architecture model relation count differs");
    expect(
            metadata.relations[0].architecture_qualifier == "X86_64" &&
                    metadata.relations[2].architecture_qualifier == "aarch64",
            "architecture qualifiers were flattened");

    using ErrorCode = LocalPackageMetadataParseErrorCode;
    expect_failure(
            "pkgbase = invalid-arch\n"
            "pkgver = 1\n"
            "pkgrel = 1\n"
            "arch = x86-64\n",
            ErrorCode::InvalidArchitecture, 4,
            "invalid architecture characters");
    expect_failure(
            "pkgbase = empty-base-arch\n"
            "pkgver = 1\n"
            "pkgrel = 1\n"
            "arch =\n",
            ErrorCode::EmptyRequiredValue, 4,
            "empty PackageBase architecture");
    expect_failure(
            "pkgbase = duplicate-arch\n"
            "pkgver = 1\n"
            "pkgrel = 1\n"
            "arch = x86_64\n"
            "arch = x86_64\n",
            ErrorCode::DuplicateArchitecture, 5,
            "duplicate base architecture");
    expect_failure(
            "pkgbase = any-mix\n"
            "pkgver = 1\n"
            "pkgrel = 1\n"
            "arch = any\n"
            "arch = x86_64\n",
            ErrorCode::ConflictingArchitecture, 5,
            "any mixed with another architecture");
    expect_failure(
            "pkgbase = child-outside-base\n"
            "pkgver = 1\n"
            "pkgrel = 1\n"
            "arch = x86_64\n"
            "pkgname = child-outside-base\n"
            "arch = aarch64\n",
            ErrorCode::ConflictingArchitecture, 6,
            "child architecture outside PackageBase set");
    expect_failure(
            "pkgbase = late-clear\n"
            "pkgver = 1\n"
            "pkgrel = 1\n"
            "arch = x86_64\n"
            "pkgname = late-clear\n"
            "arch = x86_64\n"
            "arch =\n",
            ErrorCode::ConflictingArchitecture, 7,
            "child architecture clear after values");
    expect_failure(
            "pkgbase = duplicate-child-clear\n"
            "pkgver = 1\n"
            "pkgrel = 1\n"
            "arch = x86_64\n"
            "pkgname = duplicate-child-clear\n"
            "arch =\n"
            "arch =\n",
            ErrorCode::DuplicateArchitecture, 7,
            "duplicate child architecture clear");
    expect_failure(
            "pkgbase = qualifier-any\n"
            "pkgver = 1\n"
            "pkgrel = 1\n"
            "arch = any\n"
            "depends_any = runtime\n",
            ErrorCode::InvalidArchitectureQualifier, 5,
            "any relation qualifier");
    expect_failure(
            "pkgbase = qualifier-mismatch\n"
            "pkgver = 1\n"
            "pkgrel = 1\n"
            "arch = x86_64\n"
            "depends_aarch64 = runtime\n"
            "pkgname = qualifier-mismatch\n",
            ErrorCode::InvalidArchitectureQualifier, 5,
            "PackageBase qualifier outside architecture set");
    expect_failure(
            "pkgbase = child-qualifier-mismatch\n"
            "pkgver = 1\n"
            "pkgrel = 1\n"
            "arch = x86_64\n"
            "arch = aarch64\n"
            "pkgname = child-qualifier-mismatch\n"
            "arch = aarch64\n"
            "depends_x86_64 = runtime\n",
            ErrorCode::InvalidArchitectureQualifier, 8,
            "child qualifier outside override set");
}

void test_relation_validation(const fs::path& fixture_directory) {
    using ErrorCode = LocalPackageMetadataParseErrorCode;
    expect_failure(
            read_fixture(fixture_directory, "invalid-relation.srcinfo"),
            ErrorCode::InvalidRelation, 5,
            "malformed relation fixture");

    const auto relation_source = [](std::string_view field_and_value) {
        return std::string(
                       "pkgbase = invalid-relation\n"
                       "pkgver = 1\n"
                       "pkgrel = 1\n"
                       "arch = any\n") +
                std::string(field_and_value) + "\n" +
                "pkgname = invalid-relation\n";
    };
    expect_failure(
            relation_source("provides = virtual-api>=2"),
            ErrorCode::InvalidRelation, 5,
            "provides non-equality constraint");
    expect_failure(
            relation_source("depends = runtime>="),
            ErrorCode::InvalidRelation, 5,
            "missing relation version");
    expect_failure(
            relation_source("depends = runtime>=1=2"),
            ErrorCode::InvalidRelation, 5,
            "conflicting relation operators");
    expect_failure(
            relation_source("depends = bad/name>=1"),
            ErrorCode::InvalidRelation, 5,
            "invalid relation package name");
    expect_failure(
            relation_source("depends = runtime: description"),
            ErrorCode::InvalidRelation, 5,
            "description on non-optional dependency");
    expect_failure(
            relation_source("makedepends = lib:libbuild.so.1"),
            ErrorCode::InvalidRelation, 5,
            "soname build dependency");
    expect_failure(
            relation_source("optdepends = helper: "),
            ErrorCode::InvalidRelation, 5,
            "empty optional dependency description");

    const LocalPackageMetadataParseResult valid =
            parse_local_package_metadata(
                    "pkgbase = typed-relations\n"
                    "pkgver = 1\n"
                    "pkgrel = 1\n"
                    "arch = x86_64\n"
                    "depends = runtime<=2:1.0-3.1\n"
                    "provides = virtual-api=2.0\n"
                    "optdepends = helper>=1: optional feature: mode A\n"
                    "pkgname = typed-relations\n");
    const LocalPackageMetadata& metadata =
            require_success(valid, "typed relation forms");
    expect(
            metadata.relations.size() == 3,
            "typed relation form count differs");
    expect(
            metadata.relations[0].target->comparison ==
                            Comparison::LessThanOrEqual &&
                    metadata.relations[0].target->version == "2:1.0-3.1" &&
                    metadata.relations[2].optdepends_description ==
                            "optional feature: mode A",
            "typed comparison/version/description differs");
}

void test_invalid_fixtures(const fs::path& fixture_directory) {
    const auto check_fixture = [&fixture_directory](
                                       std::string_view name,
                                       LocalPackageMetadataParseErrorCode code,
                                       std::size_t line) {
        expect_failure(
                read_fixture(fixture_directory, name), code, line,
                std::string(name));
    };

    check_fixture(
            "invalid-duplicate-child.srcinfo",
            LocalPackageMetadataParseErrorCode::DuplicatePackageName, 7);
    check_fixture(
            "invalid-missing-pkgrel.srcinfo",
            LocalPackageMetadataParseErrorCode::MissingPkgrel, 5);
    check_fixture(
            "invalid-package-identity.srcinfo",
            LocalPackageMetadataParseErrorCode::InvalidPackageIdentity, 5);
    check_fixture(
            "invalid-malformed-line.srcinfo",
            LocalPackageMetadataParseErrorCode::MalformedLine, 3);
}

void test_required_identity_and_scalar_failures() {
    using ErrorCode = LocalPackageMetadataParseErrorCode;
    expect_failure("", ErrorCode::MissingPackageBase, 1, "empty source");
    expect_failure(
            "pkgbase = missing-version\n"
            "pkgrel = 1\n"
            "arch = any\n"
            "pkgname = missing-version\n",
            ErrorCode::MissingPkgver, 5, "missing pkgver");
    expect_failure(
            "pkgbase = missing-release\n"
            "pkgver = 1\n"
            "arch = any\n"
            "pkgname = missing-release\n",
            ErrorCode::MissingPkgrel, 5, "missing pkgrel");
    expect_failure(
            "pkgbase = missing-base-arch\n"
            "pkgver = 1\n"
            "pkgrel = 1\n"
            "pkgname = missing-base-arch\n"
            "arch = x86_64\n",
            ErrorCode::MissingArchitecture, 6, "missing PackageBase arch");
    expect_failure(
            "pkgbase = missing-child\n"
            "pkgver = 1\n"
            "pkgrel = 1\n"
            "arch = any\n",
            ErrorCode::MissingPackageName, 5, "missing child");

    expect_failure(
            "pkgbase = invalid-epoch\n"
            "epoch = -1\n",
            ErrorCode::InvalidEpoch, 2, "invalid epoch");
    expect_failure(
            "pkgbase = invalid-version\n"
            "pkgver = 1-2\n",
            ErrorCode::InvalidPkgver, 2, "invalid pkgver");
    expect_failure(
            "pkgbase = invalid-release\n"
            "pkgrel = 1.2.3\n",
            ErrorCode::InvalidPkgrel, 2, "invalid pkgrel");
    expect_failure(
            "pkgbase = repeated\n"
            "pkgbase = repeated\n",
            ErrorCode::DuplicatePackageBase, 2, "duplicate PackageBase");
    expect_failure(
            "pkgbase = first\n"
            "pkgbase = second\n",
            ErrorCode::ConflictingPackageBase, 2,
            "conflicting PackageBase");
    expect_failure(
            "pkgbase = duplicate-epoch\n"
            "epoch = 1\n"
            "epoch = 2\n",
            ErrorCode::DuplicateEpoch, 3, "duplicate epoch");
    expect_failure(
            "pkgbase = duplicate-version\n"
            "pkgver = 1\n"
            "pkgver = 2\n",
            ErrorCode::DuplicatePkgver, 3, "duplicate pkgver");
    expect_failure(
            "pkgbase = duplicate-release\n"
            "pkgrel = 1\n"
            "pkgrel = 2\n",
            ErrorCode::DuplicatePkgrel, 3, "duplicate pkgrel");
    expect_failure(
            "pkgbase = empty-version\n"
            "pkgver =\n",
            ErrorCode::EmptyRequiredValue, 2, "empty pkgver");
}

void test_package_identity_contract() {
    const LocalPackageMetadataParseResult allowed =
            parse_local_package_metadata(minimal_metadata("a+_@.-valid"));
    require_success(allowed, "allowed ASCII package identity");

    using ErrorCode = LocalPackageMetadataParseErrorCode;
    const std::vector<std::string> invalid_identities = {
            "-leading-hyphen",
            ".leading-dot",
            "bad/name",
            "bad name",
            "päckage",
    };
    for(const std::string& identity : invalid_identities) {
        expect_failure(
                "pkgbase = " + identity + "\n",
                ErrorCode::InvalidPackageIdentity, 1,
                "invalid PackageBase " + identity);
    }
}

void test_section_and_field_grammar() {
    using ErrorCode = LocalPackageMetadataParseErrorCode;
    expect_failure(
            "pkgdesc = before PackageBase\n" + minimal_metadata(),
            ErrorCode::InvalidFieldScope, 1,
            "field before PackageBase section");
    expect_failure(
            minimal_metadata() + "pkgver = 2\n",
            ErrorCode::InvalidFieldScope, 6, "pkgver in child section");
    expect_failure(
            minimal_metadata() + "makedepends = cmake\n",
            ErrorCode::InvalidFieldScope, 6,
            "makedepends in child section");
    expect_failure(
            "pkgbase = malformed-field\n"
            "BadField = value\n",
            ErrorCode::MalformedLine, 2, "invalid field name");
    expect_failure(
            "pkgbase = empty-base-relation\n"
            "pkgver = 1\n"
            "pkgrel = 1\n"
            "arch = any\n"
            "depends =\n",
            ErrorCode::EmptyRequiredValue, 5, "empty base relation");

    const LocalPackageMetadataParseResult child_checkdepends_result =
            parse_local_package_metadata(
                    minimal_metadata() +
                    "checkdepends = child-check\n");
    const LocalPackageMetadata& child_metadata = require_success(
            child_checkdepends_result, "child checkdepends scope");
    expect(
            child_metadata.relations ==
                    std::vector<LocalPackageMetadataRelation>{relation(
                            RelationKind::Checkdepends, "child-check",
                            package_target("child-check"), "minimal")},
            "child checkdepends was not retained");

    const LocalPackageMetadataParseResult ignored_fields =
            parse_local_package_metadata(
                    "# generated comment\n"
                    "pkgbase = ignored-fields\n"
                    "pkgdesc = description\n"
                    "pkgver = 1\n"
                    "pkgrel = 1\n"
                    "arch = X86_64\n"
                    "sha256sums_X86_64 = SKIP\n"
                    "pkgname = ignored-fields\n"
                    "options =\n");
    const LocalPackageMetadata& ignored_metadata =
            require_success(ignored_fields, "ignored legitimate fields");
    expect(
            ignored_metadata.relations.empty(),
            "unrelated fields produced recognized relations");
}

void test_control_characters_and_line_endings() {
    using ErrorCode = LocalPackageMetadataParseErrorCode;
    std::string nul_source = "pkgbase = control";
    nul_source.push_back('\0');
    nul_source += "identity\n";
    expect_failure(
            nul_source, ErrorCode::ControlCharacter, 1,
            "NUL in metadata");

    std::string control_source = "pkgbase = control";
    control_source.push_back('\x01');
    control_source += "identity\n";
    expect_failure(
            control_source, ErrorCode::ControlCharacter, 1,
            "control byte in metadata");

    const LocalPackageMetadataParseResult crlf_result =
            parse_local_package_metadata(
                    "pkgbase = crlf\r\n"
                    "\tpkgver = 1\r\n"
                    "\tpkgrel = 1\r\n"
                    "\tarch = any\r\n"
                    "pkgname = crlf\r\n");
    require_success(crlf_result, "CRLF metadata");
}

void test_result_value_semantics() {
    const LocalPackageMetadataParseResult parsed =
            parse_local_package_metadata(
                    minimal_metadata("value-semantics"));
    const LocalPackageMetadataParseResult copied(parsed);
    const LocalPackageMetadataParseResult moved{
            LocalPackageMetadataParseResult(copied)};
    const LocalPackageMetadata& metadata =
            require_success(moved, "copied and moved result");
    expect(
            metadata.package_base == "value-semantics",
            "result value semantics changed metadata");
}

} // namespace

int main(int argc, char** argv) {
    try {
        if(argc > 2) {
            throw std::runtime_error(
                    "Usage: local-package-metadata-test [fixture-directory]");
        }
        const fs::path fixture_directory = argc == 2
                ? fs::path(argv[1])
                : fs::path("tests/fixtures/local-package-metadata");

        test_valid_split_fixture(fixture_directory);
        test_architecture_contract();
        test_relation_validation(fixture_directory);
        test_invalid_fixtures(fixture_directory);
        test_required_identity_and_scalar_failures();
        test_package_identity_contract();
        test_section_and_field_grammar();
        test_control_characters_and_line_endings();
        test_result_value_semantics();
    } catch(const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    std::cout << "local package metadata tests: all checks passed\n";
    return 0;
}
