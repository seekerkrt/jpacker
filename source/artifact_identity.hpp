#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

class ValidatedPackageArtifactPath;
class ValidatedPackageArtifactSet;

// package archiveからpacmanが読み出したidentity。
// filesystem capabilityとは分離し、requested packageとの一致判定はpure modelへ委ねる。
struct ArtifactPackageIdentity {
    std::string package_name;
    std::string full_version;
};

class ArtifactPackageIdentitySet;

// ValidatedPackageArtifactSet内のstable positionと、archiveから取得したidentity。
// pathやfilesystem cleanup capabilityは保持しない。
class IndexedArtifactPackageIdentity final {
    std::size_t artifact_index_ = 0;
    ArtifactPackageIdentity identity_;

    IndexedArtifactPackageIdentity(
        std::size_t artifact_index,
        ArtifactPackageIdentity identity) noexcept;

    friend class ArtifactPackageIdentitySet;

public:
    IndexedArtifactPackageIdentity(
        const IndexedArtifactPackageIdentity&) = default;
    IndexedArtifactPackageIdentity(
        IndexedArtifactPackageIdentity&&) noexcept = default;
    IndexedArtifactPackageIdentity& operator=(
        const IndexedArtifactPackageIdentity&) = delete;
    IndexedArtifactPackageIdentity& operator=(
        IndexedArtifactPackageIdentity&&) noexcept = delete;
    ~IndexedArtifactPackageIdentity() = default;

    std::size_t artifact_index() const noexcept {
        return artifact_index_;
    }

    const ArtifactPackageIdentity& identity() const noexcept {
        return identity_;
    }
};

// Aggregate順のidentityを、欠落・重複しないstable artifact indexと一体で保持する。
// raw identity vectorからのconstructionはquery/test factoryだけへ閉じる。
class ArtifactPackageIdentitySet final {
    std::vector<IndexedArtifactPackageIdentity> entries_;
    bool is_active_ = true;

    explicit ArtifactPackageIdentitySet(
        std::vector<ArtifactPackageIdentity> identities);

    void require_active() const;

    friend ArtifactPackageIdentitySet query_artifact_package_identities(
        const ValidatedPackageArtifactSet& artifacts);
#ifdef MOGUET_ENABLE_ARTIFACT_IDENTITY_TEST_HOOKS
    friend ArtifactPackageIdentitySet
    make_artifact_package_identity_set_for_test(
        std::vector<ArtifactPackageIdentity> identities);
#endif

public:
    ArtifactPackageIdentitySet(const ArtifactPackageIdentitySet&) = delete;
    ArtifactPackageIdentitySet& operator=(
        const ArtifactPackageIdentitySet&) = delete;
    ArtifactPackageIdentitySet(
        ArtifactPackageIdentitySet&& other) noexcept;
    ArtifactPackageIdentitySet& operator=(
        ArtifactPackageIdentitySet&&) = delete;
    ~ArtifactPackageIdentitySet() = default;

    std::size_t size() const;

    const IndexedArtifactPackageIdentity& entry_at(
        std::size_t position) const;
};

// Arbitraryなraw pathを受けず、post-build validation済みartifactだけを照会する。
ArtifactPackageIdentity query_artifact_package_identity(
    const ValidatedPackageArtifactPath& artifact);

// Aggregateをborrowし、各artifactをaggregate順に一回ずつpacmanへ照会する。
// 全件成功とaggregate-wide revalidationを通過した場合だけclosed resultを返す。
ArtifactPackageIdentitySet query_artifact_package_identities(
    const ValidatedPackageArtifactSet& artifacts);

#ifdef MOGUET_ENABLE_ARTIFACT_IDENTITY_TEST_HOOKS
// Pure adapter test用。filesystem capabilityやpathを生成せず、indexはvector順に固定する。
inline ArtifactPackageIdentitySet make_artifact_package_identity_set_for_test(
    std::vector<ArtifactPackageIdentity> identities) {
    return ArtifactPackageIdentitySet(std::move(identities));
}
#endif
