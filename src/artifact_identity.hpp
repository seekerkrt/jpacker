#pragma once

#include "artifact_workspace.hpp"

#include <string>

// package archiveからpacmanが読み出したidentity。
// filesystem capabilityとは分離し、requested packageとの一致判定はpure modelへ委ねる。
struct ArtifactPackageIdentity {
    std::string package_name;
    std::string full_version;
};

// Arbitraryなraw pathを受けず、post-build validation済みartifactだけを照会する。
ArtifactPackageIdentity query_artifact_package_identity(
        const ValidatedPackageArtifactPath& artifact);
