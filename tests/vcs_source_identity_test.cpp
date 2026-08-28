#include "vcs_source_identity.hpp"

#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

static_assert(!std::is_default_constructible_v<VcsSelector>);
static_assert(!std::is_default_constructible_v<VcsSourceIdentity>);
static_assert(!std::is_default_constructible_v<AurRecipeRevision>);
static_assert(!std::is_default_constructible_v<UpstreamGitRevision>);
static_assert(!std::is_convertible_v<AurRecipeRevision, UpstreamGitRevision>);
static_assert(!std::is_convertible_v<UpstreamGitRevision, AurRecipeRevision>);
static_assert(!std::is_constructible_v<
              UpstreamGitRevision,
              const AurRecipeRevision&>);
static_assert(!std::is_constructible_v<
              AurRecipeRevision,
              const UpstreamGitRevision&>);

using UpstreamRevisionConsumer = void (*)(const UpstreamGitRevision&);
static_assert(!std::is_invocable_v<
              UpstreamRevisionConsumer,
              const AurRecipeRevision&>);

void require(bool condition, const std::string& message) {
    if(!condition) throw std::runtime_error(message);
}

template <typename Function>
void expect_invalid_argument(Function&& function) {
    try {
        std::forward<Function>(function)();
    } catch(const std::invalid_argument&) {
        return;
    }
    throw std::runtime_error("Expected std::invalid_argument.");
}

VcsSourceIdentity git_source(VcsSelector selector) {
    return VcsSourceIdentity::make(
        VcsKind::Git,
        "https://example.invalid/upstream.git",
        std::move(selector));
}

void test_candidate_vcs_kinds_are_closed_and_representable() {
    const std::vector<VcsKind> kinds = {
        VcsKind::Git,
        VcsKind::Svn,
        VcsKind::Hg,
        VcsKind::Bzr,
        VcsKind::Cvs,
        VcsKind::Darcs};

    for(VcsKind kind : kinds) {
        const VcsSourceIdentity source = VcsSourceIdentity::make(
            kind,
            "https://example.invalid/source",
            VcsSelector::default_head());
        require(source.kind() == kind, "VCS kind was not retained.");
    }

    expect_invalid_argument([] {
        static_cast<void>(VcsSourceIdentity::make(
            static_cast<VcsKind>(6),
            "https://example.invalid/source",
            VcsSelector::default_head()));
    });
}

void test_selector_roles_and_tracking_behavior_are_distinct() {
    const VcsSelector default_head = VcsSelector::default_head();
    const VcsSelector branch = VcsSelector::branch("main");
    const VcsSelector fixed = VcsSelector::fixed_revision("12345");
    const VcsSelector tag = VcsSelector::tag("v1.0.0");
    const VcsSelector unsupported =
        VcsSelector::unsupported("date=2026-08-26");
    const VcsSelector unrecognized =
        VcsSelector::unrecognized("future-selector=value");

    require(default_head.kind() == VcsSelectorKind::DefaultHead &&
                default_head.tracking_behavior() ==
                    VcsSelectorTrackingBehavior::Floating &&
                default_head.value() == nullptr,
            "Default HEAD selector differs.");
    require(branch.kind() == VcsSelectorKind::Branch &&
                branch.tracking_behavior() ==
                    VcsSelectorTrackingBehavior::Floating &&
                branch.value() != nullptr && *branch.value() == "main",
            "Branch selector differs.");
    require(fixed.kind() == VcsSelectorKind::FixedRevision &&
                fixed.tracking_behavior() ==
                    VcsSelectorTrackingBehavior::Fixed,
            "Fixed revision was treated as floating.");
    require(tag.kind() == VcsSelectorKind::Tag &&
                tag.tracking_behavior() ==
                    VcsSelectorTrackingBehavior::Fixed,
            "Tag selector was treated as floating.");
    require(unsupported.kind() == VcsSelectorKind::Unsupported &&
                unsupported.tracking_behavior() ==
                    VcsSelectorTrackingBehavior::Indeterminate,
            "Unsupported selector was flattened.");
    require(unrecognized.kind() == VcsSelectorKind::Unrecognized &&
                unrecognized.tracking_behavior() ==
                    VcsSelectorTrackingBehavior::Indeterminate,
            "Unrecognized selector was flattened.");

    expect_invalid_argument([] {
        static_cast<void>(VcsSelector::branch(""));
    });
    expect_invalid_argument([] {
        static_cast<void>(VcsSelector::fixed_revision("bad\nrevision"));
    });
}

void test_source_identity_retains_selector_and_architecture() {
    const VcsSourceIdentity source = VcsSourceIdentity::make(
        VcsKind::Hg,
        "hg+https://example.invalid/project",
        VcsSelector::branch("stable"),
        "aarch64");

    require(source.kind() == VcsKind::Hg &&
                source.source_location() ==
                    "hg+https://example.invalid/project" &&
                source.selector().kind() == VcsSelectorKind::Branch &&
                source.architecture() != nullptr &&
                *source.architecture() == "aarch64",
            "VCS source identity lost an owned field.");

    expect_invalid_argument([] {
        static_cast<void>(VcsSourceIdentity::make(
            VcsKind::Git, "", VcsSelector::default_head()));
    });
    expect_invalid_argument([] {
        static_cast<void>(VcsSourceIdentity::make(
            VcsKind::Git,
            "https://example.invalid/source with-space",
            VcsSelector::default_head()));
    });
    expect_invalid_argument([] {
        static_cast<void>(VcsSourceIdentity::make(
            VcsKind::Git,
            "https://example.invalid/source",
            VcsSelector::default_head(),
            "bad architecture"));
    });
}

void test_aur_recipe_and_upstream_revision_roles_are_not_exchangeable() {
    const std::string object_id(40, 'a');
    const AurRecipeRevision recipe_revision =
        AurRecipeRevision::git_commit(object_id);
    const UpstreamGitRevision upstream_revision =
        UpstreamGitRevision::git_commit(
            git_source(VcsSelector::default_head()), object_id);

    require(recipe_revision.value() == upstream_revision.value(),
            "Shared validated Git OID payload differs.");
    require(upstream_revision.source().kind() == VcsKind::Git,
            "Upstream revision lost its source identity.");

    expect_invalid_argument([&object_id] {
        static_cast<void>(UpstreamGitRevision::git_commit(
            VcsSourceIdentity::make(
                VcsKind::Svn,
                "svn+https://example.invalid/source",
                VcsSelector::default_head()),
            object_id));
    });
}

} // namespace

void run_vcs_source_identity_tests() {
    test_candidate_vcs_kinds_are_closed_and_representable();
    test_selector_roles_and_tracking_behavior_are_distinct();
    test_source_identity_retains_selector_and_architecture();
    test_aur_recipe_and_upstream_revision_roles_are_not_exchangeable();
}
