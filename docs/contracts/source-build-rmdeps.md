# Separated source-build の `--rmdeps` contract

## 文書の位置づけ

この文書は、separated AUR / source-build lifecycleにおける`--rmdeps`の意味、拒否境界、pacman-only routeでの消費を定めるnormative production contractである。文書の規範上の正本は日本語本文であり、runtime behaviorを変更せず現行実装の安全契約を説明する。

- Origin Issue: [#269](https://github.com/seekerkrt/moguet/issues/269)
- Related Issues: [#123](https://github.com/seekerkrt/moguet/issues/123)、[#218](https://github.com/seekerkrt/moguet/issues/218)、[#242](https://github.com/seekerkrt/moguet/issues/242)、[#266](https://github.com/seekerkrt/moguet/issues/266)、[#267](https://github.com/seekerkrt/moguet/issues/267)
- Related PRs: #298（#269 policy）、#241、#257〜#261（#242 separated lifecycle）
- Update history: Issue #373で旧decision 10の本文から安定contractへ分離。
- Related upper decisions: [decision 1](../DECISIONS.md#decision-1)、[decision 2](../DECISIONS.md#decision-2)、[decision 4](../DECISIONS.md#decision-4)、[decision 5](../DECISIONS.md#decision-5)、[decision 6](../DECISIONS.md#decision-6)、[decision 7](../DECISIONS.md#decision-7)

## Contract本文（日本語normative source of truth）

### Optionのauthority

旧combined lifecycleでは、`makepkg -sicr`がdependency同期、source artifactのbuild、package install、dependency cleanupを一続きで所有していた。#242以降のseparated lifecycleでは、build-only makepkg、invocation-owned fresh `PKGDEST`、検証済みartifactを扱うtyped `pacman -U` install transactionへ責務を分離している。

`--rmdeps`はpacman optionではない。makepkg由来の意味を持つMoguet global optionとしてMoguetが認識し、source-buildとpacman-onlyのroute境界で処理する。pacmanへ転送して解釈させてはならない。

### Source-build route

source-build routeでは、今回のinvocationが導入したmake / check dependency集合をMoguetがauthoritativeに所有できない。build前後のinstalled package差分だけでは、次を安全に区別できない。

- 並行するpackage transaction。
- build前から存在するdependencyとExplicit package。
- `base-devel`。
- install reasonの変化。
- invocation外で導入または変更されたpackage。

したがって、source-build routeの`--rmdeps`はunsupportedであり、意味のあるcleanup要求をsilent ignoreしてはならない。callerは既存のpreflight boundaryに従い、checkout、workspace、process、metadata query、makepkg、artifact install、pacman、sudoなどのexternal mutationより前にfail closedする。

`--rmdeps`を次のいずれにも変換してはならない。

- `makepkg -r`または旧combined lifecycleの暗黙復活。
- `pacman -Rns`、`pacman -Qdt`、system-wide orphan cleanup。
- Moguet独自のdependency cleanup。
- automatic rollback。

`--noconfirm`は削除やcleanupの暗黙許可ではなく、この拒否を突破しない。将来supportを検討する場合も、cleanup ownership、protected state、preview / confirmation、build / install / cleanup resultの分離、isolated testを別の設計として証明する必要がある。

### pacman-only route

pacman-only routeでは、Moguetがmakepkg dependency installation lifecycleを実行しない。したがって、今回のinvocationが導入したdependencyをcleanupするためのinvocation-owned dependency集合も発生しない。

このrouteではMoguetが`--rmdeps`をglobal optionとして消費するが、作用させないno-opとする。pacmanへ転送せず、pacmanにunknown optionや別の意味として解釈させない。これはsource-build routeで意味のあるcleanup要求をsilent ignoreすることとは異なる。pacman-onlyではcleanup対象となるlifecycleとauthoritative ownershipが存在しないため、安全に作用させる対象がないのである。

この整理はdecision 1の「同じ意味を安全に保てない場合は黙って無視せず、未対応であることを示して実行前に停止する」と矛盾しない。source-build routeには意味のあるcleanup要求とmutation riskがあるためfail closedし、pacman-only routeにはcleanup lifecycle自体がなく、Moguetがoptionを消費してpacmanへ誤転送しないことが、optionの意味を安全に保つ明示的なroute処理だからである。

### Routeの境界

| Route | 契約 |
| --- | --- |
| AUR / source-build、`build`、separated PackageBase lifecycle | source resolutionまたは既存callerのall-target preflightに従い、external mutationより前に拒否する |
| `upgrade-aur` / `upgrade-all` | query、log / cache初期化、source preparation、system / AUR mutationより前に拒否する。targetやphaseが0件でも成功へ変換しない |
| registered `upgrade`にregularかつvalidなsource targetがある | source preparationとsystem mutationより前に拒否する |
| registered `upgrade`にsource targetがなくpacman-only system upgradeへ縮退する | Moguetが消費するが作用させず、system `pacman -Syu`へ転送しない |
| その他のpacman-only route | Moguet global optionとして消費するが作用させず、pacmanへ転送しない |

## Non-scope / implementationを固定しない範囲

- dependency cleanupの新規support、削除範囲の拡張、orphan cleanup。
- runtime parser、route selection、makepkg / pacman argv、exit codeの変更。
- cleanup実装を特定のsnapshot形式、database API、rollback機構へ恒久固定すること。

## Compatibility

利用者向けの`--rmdeps` option分類、source-build fail-closed、pacman-only no-op、route matrix、pass-through policyは、[`COMPATIBILITY.md`の`--rmdeps` section](../COMPATIBILITY.md#compat-rmdeps)を参照する。
