# Separated source-build の `--rmdeps` contract

## 文書の位置づけ

この文書は、separated AUR / source-build lifecycleにおける`--rmdeps`の意味、拒否境界、pacman-only routeでの消費を定めるnormative production contractである。あわせてIssue #404で将来のsupportへ進むために必要なcleanup ownershipとinteractionのstaged authorityを定める。文書の規範上の正本は日本語本文である。

current production behaviorとstaged targetは混同しない。Issue #404 Slice 3.6完了後も、production source-buildの`--rmdeps`はunsupportedかつfail closedであり、dependency removal、preview、promptは接続されていない。Slice 3.6はselected repository provider transactionだけにproduction-capableなtrusted receipt transportを追加する。`pacman -U`と`makepkg -s`は未対応であり、policy protection等の独立gateも残るためremovalへ進まない。

- Origin Issue: [#269](https://github.com/seekerkrt/moguet/issues/269)
- Staged extension: [#404](https://github.com/seekerkrt/moguet/issues/404)
- Makepkg syncdeps extension: [#484](https://github.com/seekerkrt/moguet/issues/484)
- Related Issues: [#123](https://github.com/seekerkrt/moguet/issues/123)、[#152](https://github.com/seekerkrt/moguet/issues/152)、[#218](https://github.com/seekerkrt/moguet/issues/218)、[#242](https://github.com/seekerkrt/moguet/issues/242)、[#266](https://github.com/seekerkrt/moguet/issues/266)、[#267](https://github.com/seekerkrt/moguet/issues/267)、[#271](https://github.com/seekerkrt/moguet/issues/271)、[#350](https://github.com/seekerkrt/moguet/issues/350)
- Related PRs: #298（#269 policy）、#241、#257〜#261（#242 separated lifecycle）
- Update history: Issue #373で旧decision 10の本文から安定contractへ分離。Issue #404 Slice 1でcurrent lifecycle監査、causal ownership、future interaction boundaryを追加。Slice 2でproduction未接続のpure cleanup classification authorityを追加。Slice 3でinstall-reason付きfull local snapshotとproduction未接続のmetadata / lifecycle adapterを追加し、current causal authority不足をNO-GOとして固定。Slice 3.5でtransaction token、owner、command outcome、machine receipt completeness、package operation、invocation ledgerをpure typed contractとして追加した。Slice 3.6でpackage-installed root helper、root-owned transaction state、transaction-local Install hook、one-shot machine receipt、selected-provider typed transportを追加し、Slice 3.5 ledgerへactual `Install` setをprojectできるproduction-capable pathを成立させた。Slice 3.7でmakepkg syncdepsのpublic instrumentation authorityを監査し、安全なroot-owned adapter案は独立security redesignが必要なためDEFER、Issue #404はRETURN-HOMEと判定した。Issue #484 Slice 1でmakepkg専用session model、strict PACMAN argv grammar、custom `PACMAN` / `PACMAN_AUTH` policyをproduction未接続のstable boundaryとして追加し、C1〜C4をGOとした。public cleanup routeとprivileged adapter、他mutation ownerは未接続である。
- Issue #484 Slice 2 update: disjointなroot-owned multi-session state、installed internal adapter、pidfdを保持するlauncher / child lifetime supervisor、synthetic 0〜2 transaction protocolを追加した。real pacman / ALPM receipt、actual makepkg `PACMAN`接続、production caller、public cleanupは未接続である。
- Issue #484 Slice 2 security repair: normal-user process / ancestor treeをroot role authorityにせず、root-owned exact role channel、packet credential + retained pidfd、mutual response binding、supervisor failure時のbounded child reap、interrupted used retirement recoveryへ置き換えた。Synthetic firewallとselected-provider分離は維持する。
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
- Slice 3のfull local package state snapshotは、1つの`PackageMetadataSession`がpreloadしたlocal DB cacheを1回だけ走査し、全entryのownedなname、version、`Explicit` / `Dependency` / `Unknown` install reasonを同じread phaseとして保持する。invalid / empty name、empty version、duplicate name、invalid cache entryはsnapshot failureであり、skipやlast-write-winsにしない。
- full snapshot成功時にkeyが存在しない場合だけ、そのread phaseでのconfirmed absenceである。session open、local DB、cache、query、metadata validationのfailureはtyped failureであり、empty inventoryやabsenceへ変換しない。
- production source-build invocationは、このsnapshotをmakepkg dependency transaction前後でcaptureしたり、transaction ownershipへ接続したりしていない。Slice 3 adapter APIもproduction routeから未使用である。
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

### Slice 2 pure cleanup classification authority（production未接続）

Slice 2のcleanup modelは、filesystem、environment、libalpm session、pacman、makepkg、process、sudo、prompt、cleanup executorを参照しないpure value / pure classifierである。production source-build lifecycleからは呼び出さず、cleanup candidateやremoval capabilityをcurrent runtimeへ公開しない。

入力evidenceは少なくとも次を独立したtyped dimensionとして保持する。

- baseline observation: `PreExisting` / `NewlyObserved` / `Unknown`。
- current installed state: `Present` / `Absent` / `Unknown`。current metadataが得られた場合も、既存の`Explicit` / `Dependency` / `Unknown` install reasonを別stateとして保持する。source-aware candidateのpackage versionは`Known` / `Unknown` / `Unavailable`を保持し、`Present`なcurrent metadataのversionとknown valueが異なる場合だけ明白なidentity contradictionとする。`Unknown` / `Unavailable`を不一致へ変換しない。
- causal ownership: authoritativeな`InvocationOwned` / known `NotInvocationOwned` / `Unknown`。baseline observationとは別型であり、`NewlyObserved`から変換しない。
- role / correlation: requested root、source-aware package child、PackageBase、BuildPlan dependency edge identity、typed dependency requirement、provider identityとresolution、`Root` / `RuntimeDependency` / `BuildDependency` / `CheckDependency`を保持する。同一packageの複数role、root、PackageBase、edgeを単一roleやpackage nameへflattenしない。
- correlation-set coverage: `Complete` / `Incomplete` / `Unknown`。これはcandidateに関係する全root、PackageBase、role、BuildPlan dependency edgeをcleanup classificationに必要な範囲でauthoritativeにprojectできたかを表すcandidate-level authorityである。
- shared lifetime: `StillRequired` / `NoLongerRequired` / `Unknown`。correlation数や実行順だけからshared lifetimeを推測しない。
- evidence quality: current package evidenceと各correlationを`Verified` / `Unverified`として保持する。per-correlation `Verified`は、その1件についてrole、edge、typed requirement、selected / resolved providerとprovided dependency identityのassociationをadapterがauthoritative inputへ照合済みであることを表す。別のroot、role、edgeが存在しないことやcorrelation集合全体の完全性は証明しない。これはcleanup classificationでも、Slice 4のmutation直前revalidationでもない。
- policy protection: package名heuristicではなく、adapter / policyが与えるtyped `Protected` / `NotProtected` / `Unknown`を保持できる。Slice 2は`base-devel` group queryを行わない。

classificationは`Eligible` / `Protected` / `Unknown` / `Invalid`を返し、次のprecedenceを固定する。

1. typed stateの範囲外、known candidate versionと`Present`なcurrent metadata versionの不一致、correlation package / provider identityの不一致、consumer requirementとdirect packageまたはproviderのprovided dependency identityの明白な不一致等、structurally malformedな入力は`Invalid`。
2. `PreExisting`、current `Absent`、`Explicit`、known `NotInvocationOwned`、root、runtime dependency、`StillRequired`、typed policy protectionのいずれかがあれば`Protected`。
3. positive protectionがなく、baseline、current installed state / reason、package version authority、causal ownership、identity / edge correlation verification、correlation-set coverage、build / check role correlation、shared lifetime、policy evidenceの必要なproofが不足すれば`Unknown`。coverage `Incomplete` / `Unknown`もこのarmであり、verifiedな1 edgeから残りのedge不在を推測しない。
4. すべてのproofが揃った場合だけ`Eligible`。

`Eligible`には、少なくとも`NewlyObserved`、current `Present`、`Dependency` reason、current metadataと一致するknown package version、authoritative `InvocationOwned`、verified current identity、verifiedなbuild / check dependency edge correlation、correlation-set coverage `Complete`、runtime / root roleなし、`NoLongerRequired`、追加policy protectionなしがすべて必要である。selected providerであるという事実自体はclassificationを変えない。selected providerがruntime roleを持てば`Protected`であり、build / checkだけで他のproofも揃う場合に限り`Eligible`となり得る。

Slice 3 adapterは、BuildPlan自体のcompletenessと、このcandidateに到達する全root、PackageBase、role、dependency edgeのprojectionを確認できる場合だけcoverage `Complete`を生成できる。partial BuildPlan、root subset、edge observation failure、completeness authority不在のいずれかがあれば`Complete`を生成してはならず、判明している範囲に応じて`Incomplete`または`Unknown`を使う。consumer requirementとprovider capabilityのversion constraintやSONAME semanticsはSlice 2 pure classifierで再評価せず、adapterがassociationを証明できないcorrelationは`Unverified`として`Unknown`へ倒す。pure classifierがtyped identity同士から直接確認できる明白なname contradictionだけは`Invalid`とする。

結果reasonはuser-facing proseではないtyped valueであり、選択されたprecedence armのreasonだけをcanonical enum orderで返す。`Invalid`は情報不足の別名にせず、情報不足は`Unknown`へ倒す。Slice 2のverificationはcurrent evidenceのqualityまでであり、Slice 4が所有するmutation直前revalidationを表したり代替したりしない。

### Slice 3 metadata / lifecycle adapter authority（production未接続）

Slice 3 adapterは、install-reason付きbaseline/current snapshot、typed `BuildPlan`、source-aware resolved candidate、prepared production work item、work-item outcome、selected repository provider transaction resultをread-onlyで受け取り、Slice 2の`InvocationOwnedCleanupCandidate`へprojectする。pacman、makepkg、filesystem、sudo、prompt、removeを呼ばず、production source-build / local build / CLI routeからは呼ばない。

baseline/current projectionは次を固定する。

- baseline snapshot成功 + package存在は`PreExisting`。
- baseline snapshot成功 + package不在は`NewlyObserved`。これはownershipではない。
- baseline snapshot failureは`Unknown`。
- current snapshot成功 + package存在は`Present`とexact current metadata。
- current snapshot成功 + package不在は`Absent`。
- current snapshot failureは`Unknown`かつmetadata authorityなし。

source-aware candidateは`ResolvedDependencyCandidate`またはrepository exact observation等のtyped authorityからのみ構成し、installed name/versionやpackage名からsource / PackageBaseを補完しない。repository providerのcurrent identityはPackageBaseを保持しないため、そのprovider edgeは`Unverified`かつcoverage `Incomplete`である。authoritative candidate identity自体を構成できない場合はcandidateを捏造せずtyped projection failureを返す。known candidate versionとcurrent installed versionの不一致はSlice 2 classifierの`Invalid`へ渡し、`Unknown` / `Unavailable` versionは推測でknownへ変換しない。

correlation coverage `Complete`は少なくとも次の全条件が揃う場合だけ生成する。

- `project_build_plan_state()`がconstructed / completeであり、provider decisionがuniqueまたはselectedとして確定している。
- required artifact target projectionが成功し、全root、PackageBase、package target、role、execution orderが相互に整合する。
- prepared work itemがBuildPlan orderと1対1で対応し、各PackageBaseのtyped source identityとrequired targetを保持する。
- candidateに関係する全dependency edgeがtyped requirement、resolved source-aware candidate、successful constraint evaluation、requiring package、root attribution、provider associationを保持する。

resolution failureやprovider candidate observation failureで全集合自体を断言できない場合は`Unknown`、unresolved / ambiguous / cancelled provider、missing source context、missing requirement / resolved identity、repository provider PackageBase不足等の既知gapは`Incomplete`へ倒す。verifiedな1 edge、vector件数、package名一致だけから`Complete`を作らない。baseline/current metadata failureはcorrelation集合の列挙可否とは別dimensionであり、集合を列挙できても各correlationとcurrent package evidenceを`Unverified`へ倒してclassifierの`Eligible`を禁止する。

shared lifetimeはtyped lifecycle boundaryを`BeforeBuildCompletion` / `AfterWorkItem` / `AfterSuccessfulInvocation` / `Unknown`として区別する。後続work itemのverified edgeがcandidateを必要とする場合とroot / runtime roleは`StillRequired`、全work itemのbuild / install success、coverage `Complete`、build / check-only、後続利用なしをすべて確認できる場合だけ`NoLongerRequired`とする。それ以外は`Unknown`である。local routeはcurrent prepared remote dependency invocationがlocal root unitの完全集合を所有しないため、Slice 3ではcoverage / shared lifetimeを安全側へ倒す。

causal ownershipはbaselineやshared lifetimeとは独立してprojectする。current `makepkg -s` outcome、`SelectedRepositoryProviderTransactionResult::Succeeded`、`PackageStateChange::Unknown`、command exit 0、または将来のtransaction-level aggregate `Changed`からpackage単位`InvocationOwned`を生成しない。authoritativeにpre-existingと確認したpackageもcausal stateを推測せず、baseline `PreExisting`によって`Protected`とし、causal ownershipは`Unknown`を保つ。

Slice 3にはgroup等のcomplete policy inventory authorityがないため、policy protectionは`Unknown`である。このためcurrent adapterはproduction evidenceから`Eligible`を生成しない。

### Slice 3.5 causal transaction receipt authority（production transport未接続）

Slice 3.5は、snapshot、command success、transaction targetをcausal proofへ昇格させず、package manager transactionから得るmachine receiptの必要条件をpure typed contractとして固定する。

pacman 7.1.0 / libalpm 16.0.1のinstalled manualと、host package databaseを共有しないisolated Arch transaction fixtureで、ALPM hookの次のsemanticsを確認した。

- `Operation = Install`はtransaction開始時点で存在しないpackageに一致する。versionが同じか新しいかにかかわらず、既に存在するpackageは`Upgrade`であり`Install`ではない。
- `Type = Package`、`Target = *`、`NeedsTargets`はmatched package nameをstdinへ渡す。localized pacman prose、stdout / stderr、pacman logではない。
- `PostTransaction` hookはtransaction commit failure時に実行されない。
- `pacman --hookdir`はabsoluteな追加hook directoryであり、複数指定時は後のdirectoryが同名hookをoverrideする。custom hookを指定したpacman invocationへだけ追加できるが、directoryとhook executableのtrustはpacmanをrootで実行する前に別途証明する必要がある。

pure receipt modelは少なくとも次を別dimensionとして保持する。

- `InvocationDependencyTransactionOwner`: selected repository provider、source artifact install、makepkg syncdeps、unknown。
- `InvocationDependencyTransactionCommandOutcome`: not attempted、succeeded、failed、unknown。
- 64 lowercase hexのtransaction token。token generationやtransportはこのpure modelの責務ではない。
- `PacmanTransactionReceiptState`: unavailable、incomplete、complete、invalid。
- package単位の`Install` / `Upgrade` operation。`Remove` / unknown operation、invalid package name、duplicate package name、token / owner mismatchはinvalidである。
- `InvocationDependencyTransactionLedger`: requested package identity、command outcome、receiptをtransaction順に保持する。requested targetとactual `Install` packageを同一視せず、solver-introduced packageもreceipt evidenceとして保持できる。後続transactionの`Upgrade`やreceipt failureで、先行transactionの`Install` evidenceを上書きしない。

receipt completenessは「hookらしい出力があった」ことではない。exact transaction tokenとownerが一致し、machine protocolがexplicitなfinal recordへ到達し、全recordがvalidかつduplicateなしの場合だけ`Complete`である。missing、partial / truncated、unexpected pre-existing data、malformed record、identity mismatchは`Unavailable` / `Incomplete` / `Invalid`として保持し、packageをnewly installedへ公開しない。command outcomeはreceipt completenessと独立しており、command failureではcomplete-looking receiptがあってもownershipを認めない。

Slice 3 adapterは、次の全条件が揃う場合だけcausal dimensionを`InvocationOwned`へprojectできる。

- baselineが`NewlyObserved`であり、これ自体はownership proofとして使用しない。
- current packageが`Present`かつverifiedで、candidate nameおよびknown versionと矛盾しない。
- ledger entryのcommand outcomeが`Succeeded`である。
- ledger token、owner、requested package identityがstructurally validである。
- receiptがそのexact token / ownerに対して`Complete`である。
- candidate packageがactual `Install` recordに存在する。

complete receiptでcandidateが省略された場合、`Upgrade`だけが存在する場合、receiptがmissing / incomplete / invalidの場合、command failure、token / owner mismatch、legacy `PackageStateChange::Changed`の場合は`NotInvocationOwned`へ推測せず`Unknown`へ倒す。pre-existing packageと`Install` receiptが矛盾する場合も`InvocationOwned`へ上げず、既存classifierの`PreExisting` protectionを維持する。

#### Slice 3.5時点のprivilege / transport boundary

ALPM hook actionはpacman transaction内でrootとして実行され得る。Slice 3.5時点のMoguet packageは、causal receipt専用のroot-owned helper、root-owned hook directory、privileged IPC、root-owned `/run` stateをinstallしていなかった。このためSlice 3.5ではproduction hook transportを接続しなかった。

次は安全な代替ではなく禁止する。

- user-writable temporary hook fileまたはhook directoryを`sudo pacman --hookdir`へ渡すこと。pacmanが読む前の差し替えによりarbitrary root executableを起動できる。
- current running Moguet path、build treeの`./build/moguet`、user-writable wrapper / helperをhook `Exec`にすること。
- root hookがuser指定path、predictable `/tmp` path、symlink-following pathをopen / truncate / createすること。
- stdout / stderr、pacman log、timestamp、pre/post snapshotへreceiptを混在させ、parserで拾うこと。
- `sh -c`、`tee`、shell quotingによってarbitrary root writeを安全化したとみなすこと。

安全なproduction transportを追加するには、少なくともroot-owned installed hook / dedicated helperのprovenance、arbitrary pathを受けないfenced protocol、transaction tokenをexact pacman invocationへ束縛するprivileged transport、symlink / owner / mode / inode replacement防止、partial write検出、install / uninstall payload contract、actual installed-package validationが必要である。Slice 3.6は次節の限定scopeでこの不足を解決する。開発treeのhelperをroot実行するtestでは代替しない。

### Slice 3.6 trusted ALPM receipt transport

#### Threat modelとhelper provenance

Slice 3.6は、unprivileged userによるhook / state差し替え、symlink substitution、arbitrary root write、stale receipt reuse、token replay、別transaction receiptの誤帰属、partial / malformed / duplicate receipt、wrong owner、path / executable / shell injection、development-tree executableのroot実行を防ぐ。Moguetがuser inputを任意root capabilityへ変換しないことをboundaryとする。root権限そのものを取得したactorや、同じ利用者が既存sudo authorityを意図的に別pacman invocationへ悪用することは、新しいsecurity boundaryとして扱わない。

packageはinternal executable `moguet-alpm-receipt-helper`を`${CMAKE_INSTALL_LIBEXECDIR}/moguet/`へinstallする。canonical Arch layoutは`/usr/libexec/moguet/moguet-alpm-receipt-helper`、mode `0755`、package install時のownerはrootである。public command、PATH lookup対象、man page対象ではない。production Moguetと生成hookはconfigure時に確定した同じabsolute installed pathだけを使い、source tree、build tree、cwd、home、`/tmp`、caller指定executableをroot実行しない。receipt-capable transportはhelper、`/usr/bin/sudo`、`/usr/bin/pacman`についてroot ownership、regular executable、group / world non-writableなancestor / file identityをnofollow descriptor walkで確認する。rootによる置換は上記threat model外だが、unprivileged置換を許容しない。

#### Token、root-owned state、filesystem policy

unprivileged transportがLinux `getrandom(2)`から256 bitを直接取得し、64文字のlowercase hexへencodeする。`rand()`、timestamp、counter、`std::random_device` fallbackは使わず、generation failureはtransaction開始前にfail closedする。

runtime stateはboot-localな次の固定hierarchyだけを使う。

```text
/run/moguet/                              root:root 0700
  alpm-receipts/                          root:root 0700
    active/                               root:root 0700
      <64-lowercase-hex-token>/           root:root 0700
        prepared                          root:root 0600
        hooks/                            root:root 0700
          moguet-install-<token>.hook     root:root 0600
        receipt                           root:root 0600, Complete時だけ
    used/                                 root:root 0700
      <token>/                            root:root 0700, empty tombstone
```

`/tmp`、`TMPDIR`、XDG runtime、home、build directory、caller指定pathは使わない。全操作はnofollow-openしたdirectory descriptorから`openat` / `mkdirat` / `fstatat(AT_SYMLINK_NOFOLLOW)`等で相対解決し、expected uid / type / exact modeとnamed entryのdevice / inode identityを確認する。file作成は`O_CREAT|O_EXCL|O_NOFOLLOW`相当、transaction publicationとreceipt finalizationは`renameat2(RENAME_NOREPLACE)`、file / directoryは`fsync`してから公開する。kernel / filesystemがno-replace publicationを提供できない場合はfallbackせずfail closedする。

PREPAREはfinal token directoryを直接組み立てず、private staging directoryへprepared stateとhookを完成させてからactive tokenへatomic publishする。既存active / preparing / used token、symlink、unexpected objectはreuseしない。CONSUME / ABORTはactive tokenをusedへatomic retireしてからexact known childrenだけをcleanupし、empty tombstoneをboot終了まで残す。これによりconsume / abort済みtokenは再prepareできない。`/run`はboot開始時にclearされるephemeral authorityであり、background sweeperやbroad directory sweepは追加しない。unexpected stateやexact cleanup failureは隠さずnonzeroとし、root-owned stateを推測で再帰削除しない。

#### PREPARE、hook、RECORD、CONSUME、ABORT

helper CLIは次の4つのfixed verbだけを持つ。owner stringは`selected-repository-provider`だけを受理する。

```text
prepare <token> selected-repository-provider -- <requested-package>...
record  <token> selected-repository-provider
consume <token> selected-repository-provider
abort   <token> selected-repository-provider
```

output path、hook path、state root、executable、command、environmentを指定する引数は存在しない。PREPAREはtoken、owner、nonempty / unique / validなexact requested package nameを検証する。requested setはprepared audit stateでありcausal proofやreceipt filterではない。

PREPAREが作るunique hook filenameにはtokenを含め、system / later hook directoryとの実用上のname collisionを避ける。hookのdynamic dataはvalidated tokenとfixed ownerだけで、package name一覧を`Exec`へ埋めない。

```ini
[Trigger]
Operation = Install
Type = Package
Target = *

[Action]
When = PostTransaction
Exec = /usr/libexec/moguet/moguet-alpm-receipt-helper record <token> selected-repository-provider
NeedsTargets
```

actual configured helper pathはinstall prefixに対応するabsolute pathである。shell、`sh -c`、PATH lookup、quoting、user-writable configを介さない。`Operation = Install`だけを対象とし、Upgradeのfull inventoryや「Installがない」というnegative proofは作らない。PostTransactionが実行されないfailureやInstall non-matchはpositive proof unavailableである。

RECORDはrootで、exact prepared state / hook identityを再検証してからNeedsTargets stdinをEOFまで読む。全recordをlocale-neutralなpackage grammarで検証し、duplicate、control character、missing final newline、empty input、100000件超、1 package 4096 bytes超、protocol全体16 MiB超を拒否する。正常なInstall setだけを`receipt.partial`へwrite + fsyncし、final `receipt`が存在しないことをno-replace renameで保証してComplete publishする。duplicate RECORD、partial file、pre-existing receipt、unexpected extra stateはfail closedである。

CONSUMEはexact token / owner / prepared state / hook / receipt owner・mode・type・inodeを再検証する。Complete receiptがなければvalidな`Missing` responseを返すが、これをNotInvocationOwned証明へ使わない。Complete / Missingのresponse bytesを構成後、active stateをone-shot retire / cleanupしてからstdoutへ固定machine protocolだけを返す。diagnosticはstderrでありmachine stdoutへ混在させない。ABORTはpacman nonzero / exec failure時にexact token stateだけをretire / cleanupし、complete-looking receiptがあってもcommand failureをownershipへ変換しない。

#### Machine protocol

protocolはstrict line format version 1で、field順、tab separator、final newline、`END` markerを固定する。unknown / reordered / duplicate fieldを許容しない。PREPARE responseはheader、token、owner、derived `HOOKDIR`、`END`、receipt responseはheader、token、owner、`STATE`、0件以上の`INSTALL`、`END`を持つ。`STATE=Complete`は1件以上のunique valid `INSTALL`を必要とし、`STATE=Missing`は`INSTALL`を持たない。stored Complete receiptとCONSUME stdoutはpacman localized stdout / stderr / logではなく、root-owned helper protocolである。unprivileged sideはbounded raw stdoutをstrict parseし、その後にSlice 3.5 validatorへtoken / owner / `Install` observationとして渡す。

#### Exact transaction bindingとsudo argv

bindingは次のchainで成立する。

```text
getrandom token
  -> root PREPARE state + unique root-owned hookdir
  -> same selected-provider pacman argvの --hookdir
  -> hook内fixed token / owner
  -> same execution resultのcommand outcome
  -> exact token / owner CONSUME
  -> same InvocationDependencyTransactionLedger entry
```

helperとpacmanは`ExplicitProcessInvocation`のargv-vectorで`execve(2)`へ渡し、executableはabsolute `/usr/bin/sudo`、sudoのtargetもabsolute helper / `/usr/bin/pacman`とする。child environmentは`PATH=/usr/bin`と`LC_ALL=C`のfixed setだけで、caller environmentをroot boundaryへpreserveしない。shell executionはない。表示用にargvをquoteすることは実行authorityではない。

PID、`/proc` start time、pidfd等の脆い追加bindingは採用しない。unrelated pacman transactionはunique `--hookdir`を持たないためreceiptへ入らない。same userがtoken / hookdirを観測して別のsudo pacmanへ意図的に与える行為は、既存root authorizationの意図的悪用であり今回の境界外である。

#### selected-provider integrationとfailure semantics

既存の`execute_selected_repository_provider_transaction(invocation, config)`は変更せず、helper / `/run` / hookへ依存しない。別のtyped `SelectedRepositoryProviderTrustedReceiptRequest` overloadだけがtrusted transportを要求する。current public `--rmdeps` routeはこのcapabilityを生成せず、通常buildのargv / failure contractを変えない。

receipt-capable pathはtrusted PREPARE failureならpacman前にblockする。pacman nonzeroではABORTし、ledger command outcomeを`Failed`とする。pacman success後のmissing / malformed / consume failureはcompleted package transactionをfailureへ書き換えず、receipt dimensionをUnavailable / Incomplete / Invalid側へ倒してcausal proofだけを禁止する。requested targetとreceipt actual Install setは別vectorであり、solver-introduced Installもそのままledgerへ保持する。

#### Mutation pathごとの結果

- selected repository provider: Moguet-owned `sudo pacman -S [--asdeps] --needed`に対するproduction-capable typed receipt pathが成立する。baseline absent、current verified Present / Dependency、command Succeeded、exact token / owner、Complete actual Install receiptが揃えばSlice 3.5 ledgerからpackage単位`CleanupCausalOwnership::InvocationOwned`を生成できる。legacy result successやaggregate Changedだけからは生成しない。
- typed artifact install: singular / PackageBase setのexact artifact identity、desired reason、`--needed` policy、transaction resultは保持するが、pre-transaction installed observationとpacman successだけではexternal transaction raceを排除できない。trusted receipt transportなしでは`pacman -U`の`Installed` outcomeをnewly installed ownershipへ読み替えない。
- makepkg syncdeps: current `makepkg -sc`内部の`pacman -S --asdeps`へcustom hookを安全にattachするproduction pathはない。`PACMAN` / `PACMAN_AUTH` override、wrapper、injected configをuser-writableな形で追加しない。

makepkg `-s`分離もSlice 3.5では行わない。current BuildPlanはdepends / makedepends / checkdepends、repository / AUR / local candidate、typed constraint、provider、PackageBase / split child、multi-root、local architectureを広く保持するが、remote AUR RPC projectionはmakepkgのcurrent PKGBUILD / architecture-qualified evaluationそのものではなく、direct repository dependency transaction全体もMoguet-owned phaseへ移されていない。pacman solverを再実装せず、already-satisfied state、provider、version constraint、split / architecture semanticsをmakepkgと同等に保つ証明がないため、`makepkg -s`を外さない。

`makepkg -r` / `-scr`へ戻すこともIssue #404のseparated artifact install、preview、explicit confirmation、phase separationを満たさないため採用しない。

### Slice 3.5 causal authority readiness（historical）: NO-GO

pure typed receiptとadapter projectionは、authoritative inputを与えるtest上でpackage単位の`CleanupCausalOwnership::InvocationOwned`を生成できる。しかしcurrent production lifecycleからその`Complete` receiptを安全に構成するtransportはなく、実在production evidence pathからは`InvocationOwned`を1件も生成できない。したがってcausal authority readinessはNO-GOである。

Slice 4 removalへ進む前に、少なくとも次のいずれかを提供する別Slice / redesignが必要である。

- package単位のmachine-readable changed-setとtransaction result。
- external transaction raceを排除できるtransaction ownership tokenまたはpackage-manager lock / transaction authority。
- makepkg syncdepsをMoguet-owned dependency install phaseへ分離し、そのexact inputとactual changed packageを相関するlifecycle。

stdout / stderr、localized output、pacman log、timestamp近接、orphan state、BuildPlan上のpackage名はこの不足を埋めるauthorityではない。

### Slice 3.6 readiness

- trusted transport readiness: **GO**。installed helper、root-owned state、transaction-local Install hook、exact token / owner、atomic Complete receipt、one-shot consume / abort、security negative、networkなしのinstalled Arch transaction fixtureが成立する。
- selected-provider causal readiness: **GO**。production-capable selected-provider typed pathからSlice 3.5 ledgerを構成し、actual Install packageを`InvocationOwned`へprojectできる。
- overall causal authority: **PARTIAL**。`pacman -U` / source artifact installと`makepkg -s` syncdepsはUnknownのままである。
- Slice 4 readiness: **NO-GO**。policy protection、route completeness、shared lifetime、mutation直前revalidation等が独立blockerとして残る。

### Slice 3.7 makepkg syncdeps causal authority audit（2026-08-28）

current Moguetは、validated checkoutをcurrent directoryとして、custom `V=K`をfirst-seen orderで渡し、invocation-ownedなabsolute `PKGDEST`を最後のassignmentとして追加した上で、次のbuild-only baselineを実行する。

```text
makepkg -sc
```

`--noconfirm`、`-f`、`-C`は対応するcurrent optionが有効な場合だけこの順で追加する。`-c`はsuccessful build後のwork file cleanupであり、`-C` / `--cleanbuild`や`-r` / `--rmdeps`ではない。Moguetはpacman用`--config` / `--hookdir`、makepkg用`--config`、`PACMAN`、`PACMAN_AUTH`を通常buildへ追加しない。makepkgは既定ではsystem makepkg config、drop-in、user configを読み、明示された`MAKEPKG_CONF`等のcurrent environment authorityも保持する。

pacman 7.1.0 / makepkg 7.1.0のpublic manual、installed makepkg implementation、host package databaseを使わないfake pacman / auth characterizationから、current syncdeps flowを次のとおり確認した。

- makepkg自身がPKGBUILDをsourceし、current `CARCH`の`depends_<arch>`、`makedepends_<arch>`、`checkdepends_<arch>`をglobal arrayへmergeする。
- runtime `depends`を先に`pacman -T`で確認し、不足時だけ`pacman -S --asdeps`へ渡す。その後、`makedepends`と、有効な`check()`に対応する`checkdepends`を同様に確認する。したがって1回のmakepkg buildはsync dependency install transactionを0〜2回開始し得る。
- current Moguetは`--check` / `--nocheck`を追加しないため、`check()` / `checkdepends`の有効性はmakepkg configとPKGBUILDが所有する。characterizationではdefault check有効時に`makedepends + checkdepends`が同じ2回目のtransactionへ入り、`--nocheck`では`checkdepends`だけが除外された。
- already-satisfied dependencyは`pacman -T`後にinstall transactionを開始しない。version constraint、installed provider / versioned providesのsatisfactionと、不足時のrepository provider選択、solver-introduced dependency、conflict等はpacman / libalpmが所有する。
- split package固有の`package_<name>()`内`depends`はsyncdeps対象ではなく、buildに必要なdependencyはglobal `depends` / `makedepends`へ置くというupstream contractを維持する。
- dependency transaction failureはmakepkg failureとしてbuild前に停止する。command failureとreceipt completenessは別dimensionのままである。

#### Instrumentation candidate result

- Candidate A（dedicated pacman config / HookDir）: **NO-GO**。pacmanの`--config` / `--hookdir`と`pacman.conf`の`HookDir`はpublic contractだが、makepkgには内部pacmanへこれらを渡すpublic pass-throughがない。global hook directoryへdynamic hookを置く方式もunrelated pacman transactionからexact tokenを分離できない。internal `PACMAN_OPTS`等のcurrent shell implementationへ依存しない。
- Candidate B（`PACMAN` / `PACMAN_AUTH`）: user-writable wrapper / config / build treeをroot pathへ到達させる形は**NO-GO**。両者はpublic overrideだがhook専用authorityではなく、`PACMAN`はpacman互換command全体、`PACMAN_AUTH`はroot command prefix全体を置き換える。PKGBUILDも同じshell contextでsourceされ、characterizationではPKGBUILD側`PACMAN_AUTH`がactual dependency install prefixへ伝播した。package-installed root-owned dedicated adapterなら理論上はfail-closedに構成できるが、transactionごとのtoken生成、0〜2回のinternal transaction集約、root-owned multi-receipt state、parent makepkg outcomeとのcorrelation、owner拡張、adapter bypass時のMissing処理が必要であり、独立security Sliceへ**DEFER**する。
- Candidate C（`makepkg -s`を外してMoguetがsyncdeps）: **NO-GO**。current BuildPlanはcurrent checkoutのPKGBUILD evaluation、effective makepkg config、architecture merge、check enablement、split-package syncdeps rule、already-satisfied/provider/version semanticsをmakepkgと同一authorityで再現しない。`makepkg --printsrcinfo`等の追加評価だけでもcurrent flow全体の代替proofにはならない。makepkg / pacman semanticsをMoguetへ複製しない。
- Candidate D（pre/post snapshot + selected-provider receipt）: **NO-GO**。selected-provider receiptが証明するのはそのdirect transactionだけであり、makepkg内部transactionのsnapshot差分を`InvocationOwned`へ昇格しない。
- Candidate E（pacman log / stdout parser）: **NO-GO**。localized prose、log、timestampはtransaction-local causal receiptではない。

Slice 3.5のpure ledgerと`InvocationDependencyTransactionOwner::MakepkgSyncDependencies`は将来adapterから再利用できる。一方、Slice 3.6 protocol / helper / transportはownerをselected repository providerへ固定し、1 token / 1 Complete receiptをone-shotで扱う。makepkg内部の複数transactionへそのまま接続できず、概念の再利用とproduction transportの小変更を同一視しない。

#### Slice 3.7 readiness / return-home decision

- makepkg syncdeps causal readiness: **DEFER**。安全なroot-owned adapterの方向は存在するが、限定的なintegrationではなくSlice 3.6同等以上のprivileged adapter / IPC redesignを必要とする。
- selected-provider causal readiness: **GO**のまま維持する。
- overall causal authority: **PARTIAL**。makepkg syncdepsとtyped dependency artifactの`pacman -U`はUnknownのままである。
- Slice 4 production cleanup readiness: **NO-GO**。causal authorityに加えてpolicy protection、route / correlation completeness、shared lifetime、mutation直前revalidationが未成立である。
- Issue #404 continuation recommendation: **RETURN-HOME**。v2.5.0では成立済みのmodel / adapter / selected-provider trusted receipt foundationを保持し、public source-build `--rmdeps`はunsupported / fail-closedのままとする。makepkg syncdeps authority、残るcleanup candidate authority、preview / confirmation / mutation executorは最大3件のfollow-up候補へ分離し、Issue #404内で新しいsecurity subsystemを開始しない。

### Issue #484 Slice 1 makepkg session stable boundary（production未接続）

Issue #484 Slice 1は、privileged helper、root-owned state、pidfd、real pacman mutation、`ArtifactMakepkgContext` capability接続を実装しない。その前段として、Slice 2以降が満たすmakepkg専用session contractと、documented `PACMAN` boundaryで受理できるcurrent argv grammarをpure typed modelとして固定する。current default makepkg route、selected-provider Slice 3.6 protocol、public `--rmdeps` behaviorは変更しない。

#### C1 — trusted zero / bypass

session resultは次を区別し、empty transaction ledgerだけをtrusted zeroへ変換しない。

- `Missing` / `Incomplete`。
- adapter `Bypassed` / `Unsupported` / `Conflict`。coverage `Unknown`も独立して保持する。
- `CompleteZero` / `CompleteOne` / `CompleteTwo`。
- structurally malformedな`Invalid`。

`CompleteZero`には、terminal `Complete` session、adapter coverage `Complete`、exact makepkg outcomeとnumeric exit code、positive process/lifetime binding、fixed makepkg owner、valid session identity、explicit transaction count `0`、empty ledgerのすべてを要求する。1件または2件でも同じterminal authorityと各transactionのexact Succeeded / Failed outcomeを要求し、declared countとledger件数を一致させる。3件目、count mismatch、missing / duplicate / out-of-order ordinal、duplicate token、session / transaction token mismatchは`Invalid`である。

adapter coverageがincomplete、unknown、bypassed、unsupported、conflictingのsessionは、内部にcomplete-looking Install receiptがあってもpositive receipt authorityを公開しない。absence、bypass、unsupportedをzero transactionへflattenしない。

session receiptはraw / factualなordered transaction observationを保持するが、generic cleanup ledgerを公開しない。package単位のpositive causal projectionはvalidated makepkg session receipt全体を受け取り、trusted terminal、complete coverage、session / process binding、fixed makepkg ownerを再確認するowner-specific boundaryだけが行う。

#### C2 — session / process binding

session identityは64文字lowercase hexのsession tokenとinvoking uidを保持し、ownerは`InvocationDependencyTransactionOwner::MakepkgSyncDependencies`へ固定する。session tokenはcorrelation dataであり、それ単独をbearer authorityとして扱わない。

Slice 1のtyped requirementはpackage-installed codeとexact launcher / makepkg child lifetimeを一体で要求するが、
normal-user processの`/proc/exe` identityだけをexecution integrityへ昇格しない。Slice 2のconcrete bindingは
root-owned launcher、bound child、transaction adapter、root supervisorを別roleとし、rootがfork前後に発行した
private channel、packetごとのkernel credential、保持中pidfdを一体で証明する
`RootOwnedLauncherAndExactRoleChannels`である。`PidOnly`、`TimestampOnly`、ancestor tree、session token、
installed inodeだけ、missing、incomplete、unknownはpositive authorityにならない。

各transactionはsession token、1-based ordinal、独立transaction token、opaque dependency specification argv、command outcome、receiptを保持する。session factoryは全transaction ownerを`MakepkgSyncDependencies`に固定し、selected-provider ownerまたはmixed-owner ledgerを拒否する。selected-providerのowner constant、one-token state machine、protocol/APIは一般化しない。

successful transactionのactual `Install` receiptとparent makepkg outcomeは別dimensionである。transaction 1がSucceeded + Complete Install、transaction 2がFailed、makepkgがFailedの場合も、transaction 1のfactual receipt、transaction 2のpositive proof不在、exact makepkg failureをすべて保持する。makepkg failureは先行するcausal factを消さず、causal factはbuild successまたはcleanup `Eligible`を意味しない。

#### C3 — strict PACMAN argv grammar

parserはPACMAN executableを除くargv vectorを入力とし、execution、shell、solver、PKGBUILD evaluation、dependency normalizationを行わない。current makepkg `-s` / `-sc` routeで受理する形は次だけである。

```text
-T <dependency-specification>...
-Qi
[--noconfirm] [--noprogressbar] [--color never] -S --asdeps <dependency-specification>...
```

dependency specificationはnon-emptyかつoption-shapedでない1 argv elementとしてlosslessに保持し、version、provider、SONAME等のsemanticsをMoguet側で解釈しない。safe optionのduplicate、missing / unexpected value、operation/order ambiguityはunsupportedである。

少なくとも`--config`、`--hookdir`、`--root`、`--sysroot`、`--dbpath`、`--cachedir`、`--gpgdir`、`--logfile`、`-R*`、`-U*`、`--nodeps`、`--assume-installed`、`--dbonly`、`--noscriptlet`、unknown option、`-S --asdeps`以外のmutation operationをfail closedで拒否する。Slice 1 parserはreal pacmanを起動しない。

host characterizationはreal `/usr/bin/makepkg -sc --noconfirm`とnormal-user fake PACMANを使い、runtime dependencyとbuild dependencyについてそれぞれ`-T`、`-S --asdeps`、`-T`再確認がorderedに発生し、buildinfo生成で最後に`-Qi`が発生するcurrent shapeを確認する。fake PACMANはtemporary user-owned log/stateだけを操作し、host package database、sudo、real pacman transactionへ到達しない。

#### C4 — PACMAN / PACMAN_AUTH policy

route policyは、`PACMAN`についてmissing、invocation-owned installed adapter、custom、conflict、unknownを区別し、`PACMAN_AUTH`についてmakepkg default、custom、conflict、unknownを区別する。observed valueはlosslessに保持する。同じinstalled adapter path文字列であってもcustom originならtrusted routeへ昇格しない。

supported armはinvocation-owned installed adapterとmakepkg default authの組だけであり、custom / conflict / unknownを黙ってoverride、chain、default化しない。route policyのSupported自体もreceipt authorityではない。`PACMAN_AUTH`はprivilege prefixであり、session completion、adapter coverage、transaction-local Install receiptを証明しない。

#### Slice 1 readiness

- C1 trusted zero / bypass: **GO**。
- C2 session / process binding contract: **GO**。actual process handleとroot stateは未実装。
- C3 strict PACMAN grammar: **GO**。parse / classificationのみでmutationなし。
- C4 custom PACMAN / PACMAN_AUTH policy: **GO**。
- Slice 2 readiness: **GO**。root-owned multi-session state / installed provenanceへ進める。
- public `--rmdeps` / cleanup preview / removal: **NO-GO**のまま。

### Issue #484 Slice 2 root-owned state / installed provenance

Slice 2はpackage-installed internal executable
`/usr/libexec/moguet/moguet-makepkg-syncdeps-adapter`と、selected-providerとはdisjointな
`/run/moguet/makepkg-syncdeps/`を追加する。executableはmode `0755`のroot-owned non-setuid payloadで、
`PATH` lookup、caller指定のexecutable / state root / output pathを受理しない。ownerは
`makepkg-sync-dependencies`へ固定し、selected-provider helperのowner、verb、state namespace、receiptを
受理またはconsumeしない。

session tokenとtransaction tokenはLinux `getrandom(2)`だけから256 bitを取得して64文字lowercase hexへ
変換する。失敗時にPRNG、timestamp、counterへfallbackしない。tokenだけをauthorityにしない。

normal-user entryはroot verb authorityを持たず、unsafe `LD_*` / `GLIBC_TUNABLES`とtraced entryをsudo前に
negative rejectionする。このcheckをnormal-user execution integrityのpositive proofには使わない。sudo後のinstalled
root supervisorがroot-owned launcherをforkし、launcherがbarrier内でbound childとtransaction adapterをforkして
invoking uidへdropする。drop前にenvironment/codeをroot processへ固定し、drop後はsupplementary groupをclear、
`NO_NEW_PRIVS`、non-dumpable、parent-death policyを設定する。launcher、child、transaction adapter、supervisor / brokerを
別roleとし、同じancestor treeにいるだけではroleを取得できない。

control IPCはrebind可能なabstract/pathname listenerをauthority pathに使わず、root supervisorがrole fork前に作成した
private `SOCK_SEQPACKET` channelだけを使う。request bytesと同時にkernelが付与する`SCM_CREDENTIALS`のpid / uid / gidを、
rootが保持するそのroleのexact pidfdと照合し、request完了までhandleを保持する。numeric PIDを再openせず、`PPid` chainを
authorizationへ使わない。root側はrequestごとにretained pidfd lifetime、installed executable identity、tracer不在も
再検証する。drop-uid roleがroot responseを検証するときは、fork前から継承したexact supervisor pidfd、non-rebindable
channel、packet credentialをauthorityとし、CAP_SYS_PTRACEをambient付与しない。root supervisor / required kernel
primitiveを確認できない場合はfallbackせずfailureへ倒す。

external `session-*` / `transaction-*` root verbは拒否し、private role channel以外からstate mutationへ到達させない。
launcher死亡時はsupervisorがactive stateをpositive manifestなしでretireする。supervisor死亡時はlauncherがsupervisor
pidfdとchild pidfdを同時pollし、adapter / childへSIGTERM、bounded grace後SIGKILLを送り、subreaperとしてreapする。
pre-release failureはbarrierをreleaseしない。response manifestはsyntaxだけでなくsession token、invoking uid、supervisor /
launcher / child / transaction-adapter pidfd identity、count、terminal、outcomeをretained root role stateと照合する。

root stateはexplicit count `0`、ordered `1`、ordered `2`を別々に保持し、各entryは1-based ordinal、独立token、
opaque dependency specification argv、command outcome、synthetic receipt observationを持つ。transactionは
prepare / record / finalize / consume / abort、sessionはbind / finalize / consume / abortをone-shotで遷移する。
third、ordinal mismatch、reentrant transactionは新しいtransaction slotを作る前に拒否し、session coverageを
`Unsupported`へ固定する。terminal zeroはterminal file、coverage `Complete`、exact bound child outcome、explicit
count `0`の組だけから生成し、empty directory、process disappearance、timestampから推測しない。

session consume / abortはactive directory全体をusedへno-replace renameしてreplayを先にdenyする。rename後または
各known unlink / final fsyncの途中でprocessが死んだ場合、次prepareはglobal lock下で全used entryのremaining shapeを
先にpreflightし、owner/type/modeとknown fixed namesだけが残るentryをidempotentにempty tombstoneまで回復する。
unknown entry、symlink、owner/mode driftはその回復attemptの最初のunlinkより前にfail closedとし、timestampやblind
recursive removalを使わない。

Slice 2 manifestのtransaction evidenceは`Synthetic`と明示し、actual ALPM `Install` receiptまたはpackage単位の
`InvocationOwned`へ昇格しない。これはinstalled/root filesystemとprocess lifetime state machineのauthorityであり、
real `/usr/bin/pacman`、transaction-local hook、actual makepkg `PACMAN` adapter、`ArtifactMakepkgContext`はSlice 3以降へ
残す。public `--rmdeps`、cleanup preview / prompt / removalは引き続きNO-GOである。

### Slice 4 production cleanup readiness: NO-GO

causal authorityとは別に、Slice 3.6でもgroup / `base-devel`等のcomplete policy inventory authorityはなく、`CleanupPolicyProtection::Unknown`を維持する。source / correlation completeness、local routeのcomplete ownership、shared lifetime、current identity / reason / state、future mutation-time revalidationもそれぞれ独立したgateである。selected-provider causal transportがGOでも、policyが`Unknown`のままならclassifierは`Eligible`を生成せず、Slice 4 GOにはしない。

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

production preview、prompt、mutation直前revalidation、exact candidate removalを接続できる最初の段階はSlice 4である。Slice 1〜3.6でfuture interactionをstub executorや仮のcandidateへ接続してはならない。

## Non-scope / implementationを固定しない範囲

- Slice 1〜2でのdependency cleanup support、remove executor、orphan cleanup、broad autoremove。
- `pacman -R` / `-Rs` / `-Rns`、`pacman -Qdt` / `-Qdtq`、`makepkg -r`の追加。
- Slice 1〜2でのinstall-reason付きfull snapshot adapter、causal correlation adapter、production lifecycle接続。
- Slice 3 adapterのproduction lifecycle / public route接続と、causal authority不足のままのpreview / executor接続。
- production preview / prompt / revalidation / removal、local / upgrade系supportの先行開放。
- public runtime parser、route selection、default makepkg / pacman argv、current exit codeの変更。Slice 3.6のexplicit typed selected-provider capabilityが所有する`--hookdir` argvはこのdefault boundaryを変更しない。
- current lifecycle監査で確認した実装moduleや`-sc` argvを将来の恒久実装として固定すること。
- selected-provider以外のcausal proofを同じhook transportへ自動一般化すること、またはsnapshot形式、database API、rollback機構へ過剰に先決めすること。

## Compatibility

利用者向けの`--rmdeps` option分類、source-build fail-closed、pacman-only no-op、route matrix、pass-through policyは、[`COMPATIBILITY.md`の`--rmdeps` section](../COMPATIBILITY.md#compat-rmdeps)を参照する。
