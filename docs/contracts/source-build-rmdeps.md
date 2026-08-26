# Separated source-build の `--rmdeps` contract

## 文書の位置づけ

この文書は、separated AUR / source-build lifecycleにおける`--rmdeps`の意味、拒否境界、pacman-only routeでの消費を定めるnormative production contractである。あわせてIssue #404で将来のsupportへ進むために必要なcleanup ownershipとinteractionのstaged authorityを定める。文書の規範上の正本は日本語本文である。

current production behaviorとstaged targetは混同しない。Issue #404 Slice 1完了後も、production source-buildの`--rmdeps`はunsupportedかつfail closedであり、dependency removal、preview、promptは接続されていない。production removalを接続できる最初の段階はSlice 4である。

- Origin Issue: [#269](https://github.com/seekerkrt/moguet/issues/269)
- Staged extension: [#404](https://github.com/seekerkrt/moguet/issues/404)
- Related Issues: [#123](https://github.com/seekerkrt/moguet/issues/123)、[#152](https://github.com/seekerkrt/moguet/issues/152)、[#218](https://github.com/seekerkrt/moguet/issues/218)、[#242](https://github.com/seekerkrt/moguet/issues/242)、[#266](https://github.com/seekerkrt/moguet/issues/266)、[#267](https://github.com/seekerkrt/moguet/issues/267)、[#271](https://github.com/seekerkrt/moguet/issues/271)、[#350](https://github.com/seekerkrt/moguet/issues/350)
- Related PRs: #298（#269 policy）、#241、#257〜#261（#242 separated lifecycle）
- Update history: Issue #373で旧decision 10の本文から安定contractへ分離。Issue #404 Slice 1でcurrent lifecycle監査、causal ownership、future interaction boundaryを追加。
- Related upper decisions: [decision 1](../DECISIONS.md#decision-1)、[decision 2](../DECISIONS.md#decision-2)、[decision 4](../DECISIONS.md#decision-4)、[decision 5](../DECISIONS.md#decision-5)、[decision 6](../DECISIONS.md#decision-6)、[decision 7](../DECISIONS.md#decision-7)

## Contract本文（日本語normative source of truth）

### Optionのauthority

旧combined lifecycleでは、`makepkg -sicr`がdependency同期、source artifactのbuild、package install、dependency cleanupを一続きで所有していた。#242以降のseparated lifecycleでは、build-only makepkg、invocation-owned fresh `PKGDEST`、検証済みartifactを扱うtyped `pacman -U` install transactionへ責務を分離している。

`--rmdeps`はpacman optionではない。makepkg由来の意味を持つMoguet global optionとしてMoguetが認識し、source-buildとpacman-onlyのroute境界で処理する。pacmanへ転送して解釈させてはならない。

将来cleanupをsupportする場合もcanonical public surfaceは既存の`--rmdeps`を再利用する。`--cleanup-build-deps`、`--remove-build-deps`等の同義optionやaliasを追加しない。これはfuture supportのoption authorityだけを固定する判断であり、current routeのsupport範囲を広げるものではない。

### Current lifecycle監査（2026-08-27）

current separated source-buildの「build-only」は、source artifactのinstallをmakepkgへ委譲せず、検証済みartifactを後段のtyped `pacman -U` transactionへ渡すという意味である。package database mutationが一切ないという意味ではない。current makepkg build commandは概ね次であり、`-s`によってmakepkg内部からdependency installationが発生し得る。

```text
makepkg -sc
```

`--noconfirm`、rebuild、clean buildのcurrent optionはこのexact baselineへ追加されるが、`-r`は追加されない。Moguetはfresh `PKGDEST`、artifact validation、後段のartifact installを所有する一方、`makepkg -s`内部のdependency transactionをpackage単位のcausal resultとして受け取っていない。

current metadata境界には次の能力と不足がある。

- 個別installed package queryはownedなname、version、`Explicit` / `Dependency` / `Unknown` install reasonを返す。
- current full local package snapshotは同一read phaseのname / versionを保持するが、全packageのinstall reasonを含まない。
- source-build invocationは、makepkg dependency transactionを跨ぐinstall-reason付きpre/post ownership snapshotやtransaction correlationを現在構成しない。
- metadata query failure、`PackageNotFound`、`Unknown` reasonは区別されるが、その区別だけでinvocation ownershipは証明されない。

利用者が明示選択したrepository providerは、invocation全体でdeduplicateしたexact `pacman -S [--asdeps] --needed` transactionへ別phaseとして渡される。ただしcurrent resultはactual package state changeを`Unknown`として保持し、cleanup inventoryを作らない。Moguetがphaseを開始した事実と、削除可能なpackage単位のcausal ownership proofは別である。

複数root / PackageBaseのsource-build work itemはorderedに実行され、BuildPlan上のrole、PackageBase、provider identityは保持される。しかしcurrent lifecycleには、makepkgが実際に導入したpackageを各edgeへcorrelateし、後続work itemからの共有利用が終わったことを証明するcleanup lifetime modelはない。package名や実行順だけから共有終了を推測してはならない。

### `NewlyObserved != InvocationOwned`

pre/post observationで次を確認できても、cleanup ownershipの証明としては不十分である。

```text
pre:  package absent
post: package present, reason=Dependency
```

この差分は、そのpackageがread phaseの間に新しく観測されたという`NewlyObserved`のevidenceに留まり、current invocationが導入を所有する`InvocationOwned`ではない。pre/post snapshot差分だけでは、少なくとも次を区別できない。

- `makepkg -s`が開始したdependency transaction。
- 並行するpacman transaction。
- invocation外で行われたpackage install。
- invocation外で行われたinstall reason変更。

したがって、旧#269が「ownershipを証明できない」とした問題を、snapshot fieldや観測回数を増やしただけで解決済みとしてはならない。現在orphanであること、package名がdependency edgeに現れること、makepkgが導入したように見えることもcausal proofではない。

Issue #404の後続Sliceでは、少なくとも次を別dimensionのtyped stateとして保持する。

- pre-existing / newly-observed。
- `Explicit` / `Dependency` / `Unknown` install reason。
- dependency role、root、PackageBase、selected providerとのcorrelation。
- current invocationがauthoritativeに因果関係を所有できるか。
- shared / still-required state。
- verified / protected / unknown / invalid classification。

cleanup eligibleへ昇格できるのは、Moguetがauthoritativeに所有するdependency transactionまたは同等のcausal proofへcorrelateでき、pre-existingではなく、`Dependency` reason、identity、installed stateを削除直前まで再validationでき、後続build unitから共有利用されないpackageだけである。proof、metadata、identity、reason、shared stateのいずれかを確定できないpackageは`Protected` / `Unknown`側へ倒し、cleanup candidateへ含めない。

Slice 1では、このcausal proofやtyped modelの具体的方式を実装・固定しない。Slice 2はfilesystem / pacman mutationへ未接続のpure model、Slice 3はmetadata / lifecycle correlation adapterであり、snapshot差分単独をownership authorityにしてはならない。

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

`--noconfirm`は削除やcleanupの暗黙許可ではなく、この拒否を突破しない。将来supportを検討する場合も、上記のcausal ownership、protected state、preview / confirmation、build / install / cleanup resultの分離を満たす必要がある。

### pacman-only route

pacman-only routeでは、Moguetがmakepkg dependency installation lifecycleを実行しない。したがって、今回のinvocationが導入したdependencyをcleanupするためのinvocation-owned dependency集合も発生しない。

このrouteではMoguetが`--rmdeps`をglobal optionとして消費するが、作用させないno-opとする。pacmanへ転送せず、pacmanにunknown optionや別の意味として解釈させない。これはsource-build routeで意味のあるcleanup要求をsilent ignoreすることとは異なる。pacman-onlyではcleanup対象となるlifecycleとauthoritative ownershipが存在しないため、安全に作用させる対象がないのである。

この整理はdecision 1の「同じ意味を安全に保てない場合は黙って無視せず、未対応であることを示して実行前に停止する」と矛盾しない。source-build routeには意味のあるcleanup要求とmutation riskがあるためfail closedし、pacman-only routeにはcleanup lifecycle自体がなく、Moguetがoptionを消費してpacmanへ誤転送しないことが、optionの意味を安全に保つ明示的なroute処理だからである。

### Routeの境界

| Route | 契約 |
| --- | --- |
| AUR / source-build、`build`、separated PackageBase lifecycle | source resolutionまたは既存callerのall-target preflightに従い、external mutationより前に拒否する |
| local `build --local` | operation-local parserでlocal root inspectionより前に拒否し、local / remote dependency lifecycleへ`--rmdeps`を渡さない |
| `upgrade-aur` / `upgrade-all` | query、log / cache初期化、source preparation、system / AUR mutationより前に拒否する。targetやphaseが0件でも成功へ変換しない |
| registered `upgrade`にregularかつvalidなsource targetがある | source preparationとsystem mutationより前に拒否する |
| registered `upgrade`にsource targetがなくpacman-only system upgradeへ縮退する | Moguetが消費するが作用させず、system `pacman -Syu`へ転送しない |
| その他のpacman-only route | Moguet global optionとして消費するが作用させず、pacmanへ転送しない |

### Issue #404 staged interaction authority（production未接続）

このsectionはSlice 4以降が接続時に満たすinteraction authorityであり、current runtimeにcleanup promptが存在するという記述ではない。

- `--rmdeps`未指定では、通常buildへcleanup promptを追加せず、cleanup mutationを行わない。
- interactive `--rmdeps`では、causal proofを満たすverified candidateが存在する場合だけpreviewを行う。candidateはpromptより先に表示する。
- confirmationはNo defaultの`[y/N]`を使う。
- explicit acceptance後も、candidateのcurrent reason、identity、installed state、shared stateをcleanup mutation直前に再validationする。再validationできないcandidateは削除しない。
- `No` / `Declined`はcleanupをskipする。完了済みbuild / install successをfailureへflattenしない。
- `q` / `quit` / `cancel`とinteractive EOFはformal cancellationである。cleanup mutationを開始せず、完了済みbuild / installをrollbackしない。operation resultではbuild / install outcomeとcleanup cancellationを別phaseとして保持する。
- cleanup failureもbuild / install resultとは別phaseとして保持し、先行successを失わない。ただしinvocation全体の成功へ丸めない。
- `--rmdeps`と`--noconfirm`の併用はcleanup approvalへ変換しない。初期supportではinvocationのexternal mutationより前にfail closedする。
- non-TTYからの`--rmdeps`もapprovalを推測しない。初期supportではinvocationのexternal mutationより前にfail closedする。
- 将来non-interactive cleanupをsupportする場合は、`--noconfirm`から推測せず、別のexplicit authorityを定義する。

boolean token、EOF、typed outcome、non-rollbackの共通意味は[interactive confirmation contract](interactive-confirmation.md)に従う。ただしcleanup requestに対する初期`--noconfirm` / non-TTYのroute裁定は、safe default Noで通常buildを継続するのではなく、上記のとおりmutation前fail-closedとする。

production preview、prompt、mutation直前revalidation、exact candidate removalを接続できる最初の段階はSlice 4である。Slice 1〜3でfuture interactionをstub executorや仮のcandidateへ接続してはならない。

## Non-scope / implementationを固定しない範囲

- Slice 1でのdependency cleanup support、remove executor、orphan cleanup、broad autoremove。
- `pacman -R` / `-Rs` / `-Rns`、`pacman -Qdt` / `-Qdtq`、`makepkg -r`の追加。
- cleanup candidate model、install-reason付きfull snapshot adapter、causal correlation adapterの先行実装。
- production preview / prompt / revalidation / removal、local / upgrade系supportの先行開放。
- runtime parser、route selection、makepkg / pacman argv、current exit codeの変更。
- current lifecycle監査で確認した実装moduleや`-sc` argvを将来の恒久実装として固定すること。
- causal proofを特定のsnapshot形式、database API、transaction API、rollback機構へ過剰に先決めすること。

## Compatibility

利用者向けの`--rmdeps` option分類、source-build fail-closed、pacman-only no-op、route matrix、pass-through policyは、[`COMPATIBILITY.md`の`--rmdeps` section](../COMPATIBILITY.md#compat-rmdeps)を参照する。
