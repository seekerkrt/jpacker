#include "cross_source_version_lock_observation.hpp"

#include "aur_rpc.hpp"
#include "installed_package_relation_inventory.hpp"

#include <algorithm>
#include <exception>
#include <filesystem>
#include <map>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {

enum class AurResponseKind {
    Success,
    NotFound,
    SchemaFailure,
    QueryFailure,
    AllocationFailure,
};

struct AurResponse {
    AurResponseKind kind = AurResponseKind::NotFound;
    std::optional<AurPackageInfo> package;
    std::string diagnostic;
};

struct ObservationFixture {
    PacmanRepositoryConfiguration configuration;
    ForeignPackageInventoryResult foreign_inventory;
    InstalledPackageRelationInventoryResult installed_relations;
    InstalledPackageRuntimeDependencyMetadataInventoryResult
        runtime_dependencies;
    std::map<std::string, StrictRepositoryPackageQueryResult>
        repository_results;
    std::map<std::string, AurResponse> aur_results;
    std::vector<std::string> calls;
};

ObservationFixture g_fixture;

void expect(bool condition, const std::string& message) {
    if(!condition) throw std::runtime_error(message);
}

PackageRelationInstalledDatabaseIdentity installed_database_identity() {
    return PackageRelationInstalledDatabaseIdentity{
        std::filesystem::path("/"),
        std::filesystem::path("/var/lib/pacman")};
}

PackageRelationObservedPackage installed_package(
    std::string package_name, std::string version) {
    return PackageRelationObservedPackage{
        std::move(package_name),
        std::nullopt,
        ObservedVersion::available(
            ObservedVersionSource::InstalledExactPackage,
            std::move(version)),
        {},
        installed_database_identity(),
        PackageRelationObservationRole::Installed,
        {}};
}

DependencyRequirement dependency_requirement(
    const std::string& specification) {
    const DependencyRequirementParseResult parsed =
        parse_dependency_requirement(specification);
    expect(
        parsed.failure() == nullptr,
        "Fixture dependency parse failed: " + specification);
    const DependencyRequirement* requirement = parsed.requirement();
    expect(requirement != nullptr, "Fixture dependency requirement is missing");
    return *requirement;
}

AurPackageInfo aur_package(
    std::string package_name,
    std::string package_base,
    std::string version,
    std::vector<DependencyRequirement> dependencies) {
    AurPackageInfo package;
    package.Name = package_name;
    package.PackageBase = package_base;
    package.Version = version;
    package.constraint_metadata = AurPackageConstraintMetadata{
        std::move(package_name),
        std::move(package_base),
        ObservedVersion::available(
            ObservedVersionSource::AurExactPackage,
            std::move(version)),
        std::move(dependencies),
        {},
        {},
        {},
        {}};
    return package;
}

RepositoryPackagePresent repository_candidate(
    std::string package_name, std::string version) {
    return RepositoryPackagePresent{
        "extra",
        1,
        package_name,
        package_name,
        ObservedVersion::available(
            ObservedVersionSource::RepositoryExactPackage,
            std::move(version)),
        std::vector<std::string>{"core", "extra"},
        {}};
}

void reset_fixture() {
    g_fixture = ObservationFixture{};
    g_fixture.configuration = PacmanRepositoryConfiguration{
        PacmanDatabasePaths{"/", "/var/lib/pacman"},
        {"core", "extra"}};
    g_fixture.foreign_inventory = ForeignPackageInventory{};
    g_fixture.installed_relations = InstalledPackageRelationInventory{
        installed_database_identity(), {}};
    g_fixture.runtime_dependencies =
        InstalledPackageRuntimeDependencyMetadataInventory{};
}

void arrange_virtualbox() {
    reset_fixture();
    g_fixture.foreign_inventory = ForeignPackageInventory{
        InstalledPackageMetadata{
            "virtualbox-ext-oracle",
            "7.2.14-1",
            InstalledPackageReason::Explicit}};
    g_fixture.installed_relations = InstalledPackageRelationInventory{
        installed_database_identity(),
        {installed_package("virtualbox", "7.2.14-1"),
         installed_package(
             "virtualbox-ext-oracle", "7.2.14-1")}};
    g_fixture.runtime_dependencies =
        InstalledPackageRuntimeDependencyMetadataInventory{
            InstalledPackageRuntimeDependencyMetadata{
                "virtualbox-ext-oracle",
                {"virtualbox=7.2.14"}}};
    g_fixture.repository_results["virtualbox"] =
        repository_candidate("virtualbox", "7.2.16-1");
    g_fixture.aur_results["virtualbox-ext-oracle"] = AurResponse{
        AurResponseKind::Success,
        aur_package(
            "virtualbox-ext-oracle",
            "virtualbox-ext-oracle",
            "7.2.16-1",
            {dependency_requirement("virtualbox=7.2.16")}),
        {}};
}

bool has_issue(
    const CrossSourceVersionLockObservationResult& result,
    CrossSourceVersionLockObservationIssueKind kind) {
    for(const CrossSourceVersionLockObservationIssue& issue : result.issues) {
        if(issue.kind == kind) return true;
    }
    return false;
}

CrossSourceVersionLockAssessment require_single_assessment(
    const CrossSourceVersionLockObservationResult& result,
    const std::string& context) {
    expect(result.candidates.size() == 1, context + ": candidate count differs");
    return assess_cross_source_version_lock_candidate(
        result.candidates.front());
}

} // namespace

PacmanRepositoryConfiguration resolve_pacman_repository_configuration() {
    g_fixture.calls.push_back("repository-configuration");
    return g_fixture.configuration;
}

ForeignPackageInventoryResult query_foreign_package_inventory(
    const PacmanRepositoryConfiguration& configuration) {
    g_fixture.calls.push_back("foreign-inventory");
    if(configuration.repository_names !=
           g_fixture.configuration.repository_names ||
       configuration.database_paths.root_dir !=
           g_fixture.configuration.database_paths.root_dir ||
       configuration.database_paths.db_path !=
           g_fixture.configuration.database_paths.db_path) {
        throw std::runtime_error(
            "Foreign inventory received a different repository configuration");
    }
    return g_fixture.foreign_inventory;
}

InstalledPackageRelationInventoryResult query_installed_package_relations(
    const PacmanDatabasePaths& paths) {
    g_fixture.calls.push_back("installed-relations");
    if(paths.root_dir != g_fixture.configuration.database_paths.root_dir ||
       paths.db_path != g_fixture.configuration.database_paths.db_path) {
        throw std::runtime_error(
            "Installed relation query received different database paths");
    }
    return g_fixture.installed_relations;
}

InstalledPackageRuntimeDependencyMetadataInventoryResult
query_installed_package_runtime_dependency_metadata(
    const PacmanDatabasePaths& paths) {
    g_fixture.calls.push_back("installed-runtime-dependencies");
    if(paths.root_dir != g_fixture.configuration.database_paths.root_dir ||
       paths.db_path != g_fixture.configuration.database_paths.db_path) {
        throw std::runtime_error(
            "Runtime dependency query received different database paths");
    }
    return g_fixture.runtime_dependencies;
}

StrictRepositoryPackageQueryResult query_repository_package_strict(
    const PacmanRepositoryConfiguration& configuration,
    const std::string& package_name) {
    g_fixture.calls.push_back("repository:" + package_name);
    if(configuration.repository_names !=
           g_fixture.configuration.repository_names ||
       configuration.database_paths.root_dir !=
           g_fixture.configuration.database_paths.root_dir ||
       configuration.database_paths.db_path !=
           g_fixture.configuration.database_paths.db_path) {
        throw std::runtime_error(
            "Repository candidate query received a different configuration");
    }
    const auto result = g_fixture.repository_results.find(package_name);
    return result == g_fixture.repository_results.end()
               ? StrictRepositoryPackageQueryResult{
                     RepositoryPackageNotFound{
                         configuration.repository_names}}
               : result->second;
}

std::optional<AurPackageInfo> AurClient::info_strict(
    const std::string& package_name) {
    g_fixture.calls.push_back("aur:" + package_name);
    const auto response = g_fixture.aur_results.find(package_name);
    if(response == g_fixture.aur_results.end()) {
        throw std::runtime_error("Unexpected exact AUR query: " + package_name);
    }
    switch(response->second.kind) {
        case AurResponseKind::Success:
            return response->second.package;
        case AurResponseKind::NotFound:
            return std::nullopt;
        case AurResponseKind::SchemaFailure:
            throw AurRpcResponseError(response->second.diagnostic);
        case AurResponseKind::QueryFailure:
            throw std::runtime_error(response->second.diagnostic);
        case AurResponseKind::AllocationFailure:
            throw std::bad_alloc();
    }
    throw std::logic_error("Unknown exact AUR fixture response");
}

namespace {

void test_virtualbox_candidate_observation_is_complete_and_compatible() {
    arrange_virtualbox();

    const CrossSourceVersionLockObservationResult result =
        observe_cross_source_version_lock_candidates();
    expect(
        result.status == CrossSourceVersionLockObservationStatus::Complete &&
            result.issues.empty(),
        "VirtualBox candidate observation was not complete");
    const CrossSourceVersionLockAssessment assessment =
        require_single_assessment(result, "VirtualBox candidate");
    expect(
        assessment.status ==
            CrossSourceVersionLockStatus::CompatibleReplacement,
        "VirtualBox candidate did not project CompatibleReplacement");
    expect(
        g_fixture.calls ==
            std::vector<std::string>{
                "repository-configuration",
                "foreign-inventory",
                "installed-relations",
                "installed-runtime-dependencies",
                "repository:virtualbox",
                "aur:virtualbox-ext-oracle"},
        "Candidate observation crossed an unexpected authority boundary");
}

void test_complete_zero_differs_from_observation_failure() {
    arrange_virtualbox();
    g_fixture.repository_results["virtualbox"] =
        RepositoryPackageNotFound{
            std::vector<std::string>{"core", "extra"}};

    const CrossSourceVersionLockObservationResult no_candidate =
        observe_cross_source_version_lock_candidates();
    expect(
        no_candidate.status ==
                CrossSourceVersionLockObservationStatus::Complete &&
            no_candidate.candidates.empty() &&
            no_candidate.issues.empty(),
        "Confirmed repository absence was not complete zero correlation");

    reset_fixture();
    g_fixture.foreign_inventory = PackageMetadataFailure{
        PackageMetadataErrorCode::QueryFailed,
        "fixture foreign inventory failure"};
    const CrossSourceVersionLockObservationResult failed =
        observe_cross_source_version_lock_candidates();
    expect(
        failed.status == CrossSourceVersionLockObservationStatus::Failed &&
            failed.candidates.empty() &&
            has_issue(
                failed,
                CrossSourceVersionLockObservationIssueKind::
                    ForeignInventoryUnavailable),
        "Observation failure was flattened into complete empty candidates");
}

void test_installed_dependency_metadata_unavailable_fails_closed() {
    arrange_virtualbox();
    g_fixture.runtime_dependencies =
        InstalledPackageRuntimeDependencyMetadataInventoryFailure{
            {},
            std::size_t(0),
            PackageMetadataFailure{
                PackageMetadataErrorCode::QueryFailed,
                "fixture installed dependency failure"}};

    const CrossSourceVersionLockObservationResult result =
        observe_cross_source_version_lock_candidates();
    expect(
        result.status == CrossSourceVersionLockObservationStatus::Failed &&
            result.candidates.empty() &&
            has_issue(
                result,
                CrossSourceVersionLockObservationIssueKind::
                    InstalledRuntimeDependencyInventoryUnavailable),
        "Installed dependency failure became an empty success");
}

void test_repository_candidate_query_failure_is_partial() {
    arrange_virtualbox();
    g_fixture.repository_results["virtualbox"] = RepositoryMetadataFailure{
        RepositoryMetadataFailureKind::SyncDatabaseUnavailable,
        std::string("extra"),
        "fixture repository query failure",
        std::vector<std::string>{"core", "extra"}};

    const CrossSourceVersionLockObservationResult result =
        observe_cross_source_version_lock_candidates();
    expect(
        result.status == CrossSourceVersionLockObservationStatus::Partial &&
            result.candidates.empty() &&
            has_issue(
                result,
                CrossSourceVersionLockObservationIssueKind::
                    RepositoryCandidateUnavailable),
        "Repository query failure was treated as confirmed absence");
}

void test_aur_replacement_not_found_is_complete() {
    arrange_virtualbox();
    g_fixture.aur_results["virtualbox-ext-oracle"] = AurResponse{
        AurResponseKind::NotFound, std::nullopt, {}};

    const CrossSourceVersionLockObservationResult result =
        observe_cross_source_version_lock_candidates();
    const CrossSourceVersionLockAssessment assessment =
        require_single_assessment(result, "AUR replacement not found");
    expect(
        result.status == CrossSourceVersionLockObservationStatus::Complete &&
            result.issues.empty() &&
            assessment.status ==
                CrossSourceVersionLockStatus::MissingReplacement,
        "Confirmed AUR absence was not retained losslessly");
}

void test_aur_schema_failure_is_metadata_unavailable() {
    arrange_virtualbox();
    g_fixture.aur_results["virtualbox-ext-oracle"] = AurResponse{
        AurResponseKind::SchemaFailure,
        std::nullopt,
        "fixture AUR schema failure"};

    const CrossSourceVersionLockObservationResult result =
        observe_cross_source_version_lock_candidates();
    const CrossSourceVersionLockAssessment assessment =
        require_single_assessment(result, "AUR metadata unavailable");
    expect(
        result.status == CrossSourceVersionLockObservationStatus::Partial &&
            assessment.status == CrossSourceVersionLockStatus::Unknown &&
            has_issue(
                result,
                CrossSourceVersionLockObservationIssueKind::
                    AurReplacementMetadataUnavailable),
        "AUR schema failure was not typed metadata-unavailable");
}

void test_aur_query_failure_is_not_not_found() {
    arrange_virtualbox();
    g_fixture.aur_results["virtualbox-ext-oracle"] = AurResponse{
        AurResponseKind::QueryFailure,
        std::nullopt,
        "fixture AUR transport failure"};

    const CrossSourceVersionLockObservationResult result =
        observe_cross_source_version_lock_candidates();
    const CrossSourceVersionLockAssessment assessment =
        require_single_assessment(result, "AUR query failure");
    expect(
        result.status == CrossSourceVersionLockObservationStatus::Partial &&
            assessment.status ==
                CrossSourceVersionLockStatus::QueryFailure &&
            assessment.status !=
                CrossSourceVersionLockStatus::MissingReplacement &&
            has_issue(
                result,
                CrossSourceVersionLockObservationIssueKind::
                    AurReplacementQueryFailure),
        "AUR query failure was flattened into absence");
}

void test_aur_allocation_failure_propagates() {
    arrange_virtualbox();
    g_fixture.aur_results["virtualbox-ext-oracle"] = AurResponse{
        AurResponseKind::AllocationFailure,
        std::nullopt,
        {}};

    bool propagated = false;
    try {
        static_cast<void>(observe_cross_source_version_lock_candidates());
    } catch(const std::bad_alloc&) {
        propagated = true;
    }
    expect(
        propagated,
        "AUR allocation failure was flattened into a query failure");
}

void test_indirect_replacement_dependency_remains_unknown() {
    arrange_virtualbox();
    g_fixture.aur_results["virtualbox-ext-oracle"] = AurResponse{
        AurResponseKind::Success,
        aur_package(
            "virtualbox-ext-oracle",
            "virtualbox-ext-oracle",
            "7.2.16-1",
            {dependency_requirement(
                "virtualbox-provider=7.2.16")}),
        {}};

    const CrossSourceVersionLockObservationResult result =
        observe_cross_source_version_lock_candidates();
    const CrossSourceVersionLockAssessment assessment =
        require_single_assessment(result, "indirect replacement dependency");
    expect(
        result.status == CrossSourceVersionLockObservationStatus::Complete &&
            assessment.status == CrossSourceVersionLockStatus::Unknown,
        "Indirect/provider dependency was resolved implicitly");
}

void test_duplicate_matching_installed_dependency_is_partial() {
    arrange_virtualbox();
    g_fixture.runtime_dependencies =
        InstalledPackageRuntimeDependencyMetadataInventory{
            InstalledPackageRuntimeDependencyMetadata{
                "virtualbox-ext-oracle",
                {"virtualbox=7.2.14",
                 "virtualbox=7.2.14"}}};

    const CrossSourceVersionLockObservationResult result =
        observe_cross_source_version_lock_candidates();
    expect(
        result.status == CrossSourceVersionLockObservationStatus::Partial &&
            result.candidates.empty() &&
            has_issue(
                result,
                CrossSourceVersionLockObservationIssueKind::
                    DuplicateInstalledRuntimeDependency) &&
            std::find(
                g_fixture.calls.begin(),
                g_fixture.calls.end(),
                "aur:virtualbox-ext-oracle") ==
                g_fixture.calls.end(),
        "Duplicate installed dependency was selected implicitly");
}

void test_ambiguous_repository_candidate_identity_reaches_assessment() {
    arrange_virtualbox();
    RepositoryPackagePresent ambiguous =
        repository_candidate("virtualbox-alternate", "7.2.16-1");
    g_fixture.repository_results["virtualbox"] = std::move(ambiguous);

    const CrossSourceVersionLockObservationResult result =
        observe_cross_source_version_lock_candidates();
    const CrossSourceVersionLockAssessment assessment =
        require_single_assessment(result, "ambiguous candidate identity");
    expect(
        result.status == CrossSourceVersionLockObservationStatus::Complete &&
            assessment.status ==
                CrossSourceVersionLockStatus::Ambiguous,
        "Ambiguous repository identity was accepted as a correlation");
}

} // namespace

void run_cross_source_version_lock_observation_tests() {
    test_virtualbox_candidate_observation_is_complete_and_compatible();
    test_complete_zero_differs_from_observation_failure();
    test_installed_dependency_metadata_unavailable_fails_closed();
    test_repository_candidate_query_failure_is_partial();
    test_aur_replacement_not_found_is_complete();
    test_aur_schema_failure_is_metadata_unavailable();
    test_aur_query_failure_is_not_not_found();
    test_aur_allocation_failure_propagates();
    test_indirect_replacement_dependency_remains_unknown();
    test_duplicate_matching_installed_dependency_is_partial();
    test_ambiguous_repository_candidate_identity_reaches_assessment();
}
