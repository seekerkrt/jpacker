#pragma once

#include "artifact_identity.hpp"

#include <cstddef>
#include <filesystem>

namespace artifact_archive_metadata {

class QueryAuthority final {
public:
    QueryAuthority() = delete;
    explicit QueryAuthority(
        const ValidatedPackageArtifactPath& artifact);
    QueryAuthority(
        const ValidatedPackageArtifactSet& artifacts,
        std::size_t artifact_index);

    void require_validity() const;
    [[nodiscard]] const std::filesystem::path& path() const;

private:
    const ValidatedPackageArtifactPath* artifact_ = nullptr;
    const ValidatedPackageArtifactSet* artifacts_ = nullptr;
    std::size_t artifact_index_ = 0;
};

// Reads one package archive through libalpm without opening a transaction or
// consulting package databases. The caller owns filesystem provenance and
// must hold the validated artifact query authority and revalidate it around
// this read.
ArtifactPackageIdentity query_with_libalpm(
    const QueryAuthority& authority);

#ifdef MOGUET_ENABLE_ARTIFACT_IDENTITY_TEST_HOOKS
// Existing filesystem/aggregate tests replace only the archive reader. Their
// process stub remains test authority and never enters a production target.
ArtifactPackageIdentity query_with_test_stub(
    const std::filesystem::path& artifact_path);

// Deterministic actual-archive fixture only. Production has no raw-path
// entrypoint without QueryAuthority.
ArtifactPackageIdentity query_with_libalpm_for_test(
    const std::filesystem::path& artifact_path);
#endif

} // namespace artifact_archive_metadata
