# PackageBase build / required-child selection contract

## 文書の位置づけ

この文書は、AUR source buildにおけるPackageBase build unitと、BuildPlanが要求するpackage childのinstall-selection unitを分離するnormative production contractである。文書の規範上の正本は日本語本文であり、Issue本文や英語要約は来歴・説明として扱う。

- Origin Issue: [#268](https://github.com/seekerkrt/moguet/issues/268)
- Related Issues: [#98](https://github.com/seekerkrt/moguet/issues/98)、[#218](https://github.com/seekerkrt/moguet/issues/218)、[#242](https://github.com/seekerkrt/moguet/issues/242)、[#266](https://github.com/seekerkrt/moguet/issues/266)、[#267](https://github.com/seekerkrt/moguet/issues/267)
- Related PRs: #291〜#296（#268 production slice）、#241、#257〜#261（#242 artifact lifecycle）
- Update history: Issue #373で旧decision 9の本文から安定contractへ分離。
- Related upper decisions: [decision 1](../DECISIONS.md#decision-1)、[decision 2](../DECISIONS.md#decision-2)、[decision 4](../DECISIONS.md#decision-4)、[decision 5](../DECISIONS.md#decision-5)、[decision 6](../DECISIONS.md#decision-6)、[decision 7](../DECISIONS.md#decision-7)

## Contract本文（日本語normative source of truth）

### Identityとbuild unit

AUR source buildでは、PackageBaseをrepository、build、workspace、package transactionの単位とし、BuildPlanが必要とするpackage childをinstall-selectionの単位とする。PackageBase identityとchild identityを単一のpackage nameへflattenしてはならない。

1. 1つのPackageBaseは、1つのinvocation-owned fresh artifact workspaceで1回だけbuildする。expected outputは`makepkg --packagelist`からordered aggregateとして取得する。
2. BuildPlanが要求するchildはrequired-target orderで保持する。childのartifactはfilenameの推測やPackageBase名ではなく、build後のpackage metadata identityでexactly one選択する。
3. expectedだがrequiredでないsibling、debug、その他のartifactはunselected result dataとして保持する。これらへinstall input、update target attribution、install outcome、install reasonを付与しない。
4. selected childrenはPackageBaseごとに1回のpacman transactionへ渡す。childごとのdesired install reasonをそのtransactionで表現できない場合は、部分的にinstallせずmutation前にfail closedとする。

### Artifactとfilesystemの安全境界

expected artifactとactual artifactについて、次をinstall前に証明する。

- expected identityとactual package metadata identityが一致すること。
- required childごとにmatching artifactがexactly oneであること。
- artifactがregular fileであり、invocation-owned workspaceのcontainment内にあること。
- freshness、ownership、filesystem identityを検証できること。
- missing、duplicate、unexpected、unknown identity、containment violationを推測で補わないこと。

PackageBaseとchildのidentity相関、workspaceのcontainment、artifactのfreshnessを証明できない場合は、`--noconfirm`指定でもinstallへ進まない。filenameの規則、directory layout、内部type、module分割はこのcontractが固定するauthorityではない。

### Transaction、failure、cleanup

transaction failureはpackageごとのpartial successを証明しない。failed transaction後にchild successを推測せず、safeなattempt identity / versionをfailure evidenceとしてsuccessful outcomeから分離する。

workspace cleanupはtransaction success後に限る。cleanup failureはtransaction failureへflattenせず、すでにcompletedした全childの正確なinstalled / skipped-as-needed outcomeとunselected identityを保持するpartial successとして報告する。先行して完了したpackageをrollbackしたと報告せず、未実行のchildを成功扱いしない。

### `--noconfirm`とselection

`--noconfirm`は確認を省略するoptionであり、曖昧なartifact selection、mixed install reason、missing metadata、failure、unsafe filesystem stateを突破する許可ではない。requested childとmetadata identityから一意に決まるselectionは対話判断ではないが、identityを証明できないselectionを自動選択してはならない。

## Non-scope / implementationを固定しない範囲

- arbitraryなmultiple-output、sibling、debug artifactを明示なしに全installすること。
- provider選択、conflicts / replaces solver、version constraint solver、package group selection。
- artifact reuse、durable manifest、automatic rollback、unified transaction。
- PackageBase selection、artifact validation、cleanupを特定のmodule、type、syscall、directory layoutへ恒久固定すること。

## Compatibility

利用者向けのsplit package、PackageBase、selected / unselected artifact、`--noconfirm`の要約とroute差分は、[`COMPATIBILITY.md`のPackageBase / child section](../COMPATIBILITY.md#compat-packagebase-child-selection)を参照する。
