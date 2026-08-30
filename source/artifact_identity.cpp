#include "artifact_identity.hpp"

#include "artifact_archive_metadata.hpp"
#include "artifact_workspace.hpp"

#include <cstddef>
#include <utility>
#include <vector>

ArtifactPackageIdentity query_artifact_package_identity(
    const ValidatedPackageArtifactPath& artifact) {
    // LANDMINE(#485): path provenance remains independent from archive
    // metadata. Same-inode content mutation is not a receipt, so Slice 2 and
    // later must still correlate the actual transaction/current identity.
    artifact.require_validity();
    ArtifactPackageIdentity identity =
#ifdef MOGUET_ENABLE_ARTIFACT_IDENTITY_TEST_HOOKS
        artifact_archive_metadata::query_with_test_stub(artifact.path());
#else
        artifact_archive_metadata::query_with_libalpm(
            artifact_archive_metadata::QueryAuthority(artifact));
#endif
    artifact.require_validity();
    return identity;
}

ArtifactPackageIdentitySet query_artifact_package_identities(
    const ValidatedPackageArtifactSet& artifacts) {
    // LANDMINE(#268): individual pathではなくaggregate全体を、最初のarchive queryより前から
    // result返却直前まで各boundaryで再証明する。途中まで得たidentityは公開しない。
    artifacts.require_validity();
    const std::size_t artifact_count = artifacts.size();

    std::vector<ArtifactPackageIdentity> identities;
    identities.reserve(artifact_count);
    for(std::size_t index = 0; index < artifact_count; ++index) {
        artifacts.require_validity();
        ArtifactPackageIdentity identity =
#ifdef MOGUET_ENABLE_ARTIFACT_IDENTITY_TEST_HOOKS
            artifact_archive_metadata::query_with_test_stub(
                artifacts.path_at(index));
#else
            artifact_archive_metadata::query_with_libalpm(
                artifact_archive_metadata::QueryAuthority(
                    artifacts, index));
#endif
        artifacts.require_validity();
        identities.push_back(std::move(identity));
    }

    artifacts.require_validity();
    ArtifactPackageIdentitySet identity_set(std::move(identities));
    artifacts.require_validity();
    return identity_set;
}
