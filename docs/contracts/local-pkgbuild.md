# Local PKGBUILD contract

## 文書の位置づけ

この文書は、local PKGBUILDをroot sourceとして扱う`build --local` routeのidentity、metadata authority、dependency projection、artifact lifecycle、安全境界を定めるnormative production contractである。文書の規範上の正本は日本語本文であり、現在のCLIで未接続のsliceを実装済みとみなすものではない。

- Origin Issue: [#271](https://github.com/seekerkrt/moguet/issues/271)
- Related Issues: [#217](https://github.com/seekerkrt/moguet/issues/217)、[#268](https://github.com/seekerkrt/moguet/issues/268)、[#272](https://github.com/seekerkrt/moguet/issues/272)、[#86](https://github.com/seekerkrt/moguet/issues/86)、[#96](https://github.com/seekerkrt/moguet/issues/96)、[#97](https://github.com/seekerkrt/moguet/issues/97)、[#151](https://github.com/seekerkrt/moguet/issues/151)、[#152](https://github.com/seekerkrt/moguet/issues/152)
- Related PRs: #368（slice 1）、#369（slice 2）、#370（slice 3）、#371（slice 4）
- Update history: Issue #373で旧decision 15の本文から安定contractへ分離。
- Related upper decisions: [decision 1](../DECISIONS.md#decision-1)、[decision 2](../DECISIONS.md#decision-2)、[decision 4](../DECISIONS.md#decision-4)、[decision 5](../DECISIONS.md#decision-5)、[decision 6](../DECISIONS.md#decision-6)、[decision 7](../DECISIONS.md#decision-7)

## Contract本文（日本語normative source of truth）

### CLI入口とroot identity

正式入口は`moguet build --local <directory> [V=K...]`とする。既存の`build <pkg> [V=K...]`はremote package name routeとして維持し、pathらしい文字列をlocal rootへ暗黙変換しない。`--local`は`build`だけが所有するoperation-local source selectorであり、`-Bi`、`build-local`、`-S --local`などのaliasは追加しない。directory operandはexactly oneとし、missing / multiple operand、invalid `V=K`、package targetとの併記、opaque operandはfilesystem accessより前に拒否する。

directoryは1つのlocal PackageBase sourceを選ぶ。採用metadata snapshotに宣言されたvalidかつuniqueな全`pkgname` childをfirst-seen orderのrequired root targetとする。初期routeは先頭child、PackageBase名、またはproduced artifact全体をinstall targetとして推測しない。

local rootはrepository / AUR rootと異なるtyped identityである。descriptor-firstで開いたdirectory、descriptorから導出したcanonical display path、filesystem identity、root `PKGBUILD` content snapshot、metadata provenanceを保持する。Git repositoryであることを要求せず、Moguetはlocal rootへclone、fetch、pull、reset、clean、merge、overwrite、reclone、delete、cache cleanupを行わない。

root directory、root `PKGBUILD`、optional `.SRCINFO`はeffective user所有で、group / other writableではないことを要求する。final symlink、non-directory、special file、unsafe owner / permissionをfollowまたは黙って受け入れない。fileはdirectory descriptor相対のregular non-symlinkとして開き、device / inode、size、high-resolution mtime、content snapshotをprovenanceとして保持する。

### Metadata authorityとPKGBUILD評価

既存の安全な`.SRCINFO`をread-only metadataの第一候補とする。少なくともPackageBase、ordered children、version、architecture、architecture-qualified fields、depends / makedepends / checkdepends / optdepends、provides、conflicts、replaces、各version constraintとchild scopeを保持する。qualified fieldをunqualifiedへflattenせず、local metadataをAUR RPC用identityへ変換しない。

`.SRCINFO` stateは次を区別する。

- `Missing`: `.SRCINFO`がENOENTの場合だけ。
- `Unsafe`: symlink、non-regular、root外参照、owner / mode違反、permission / I/O、read中のidentity replacement。
- `Invalid`: safeに読めたがgrammar、required field、identifier、section、duplicate / conflicting identity、constraintをtyped metadataへ変換できない。
- `UsableUnverified`: validだがPKGBUILDとのsemantic freshnessを証明していない、採用可能なmetadata。
- `KnownStale`: PKGBUILDのmtimeが`.SRCINFO`より新しい、review後にidentityが変わった、one-off environmentが指定された、またはmetadata identityが一致しない状態。

mtimeが逆順または同時刻であることはfreshnessの証明にならない。`Unsafe`をPKGBUILD評価へfallbackせずhard failureとする。`Missing`、`Invalid`、`KnownStale`をempty dependencyや正常metadataへflattenしない。

mutation可能な`build --local`だけが、source rootとreasonを表示し、PKGBUILD review後にdefaultを持たない明示確認を受けて、normal userで表示した`makepkg --printsrcinfo`を実行できる。evaluation outputはinvocation-owned metadataとしてparseし、user-owned `.SRCINFO`をcreate、truncate、rewriteしない。`--noedit`はeditor skipでありevaluation consentではない。`--noconfirm`、non-TTY、cancel、EOFはevaluationを自動承認しない。read-onlyの`deps` / `plan`境界はPKGBUILD evaluationを開始しない。

one-off `V=K`がある場合、existing `.SRCINFO`はeffective environmentを証明できないため`KnownStale`とする。同じordered environment snapshotを`makepkg --printsrcinfo`、`makepkg --packagelist`、build-only makepkgへ渡す。inherited environmentまたは`V=K`が`PKGDEST`を定義する場合はemptyでもall-target preflightで拒否し、fresh `PKGDEST`のownershipを利用者入力へ渡さない。

### Dependency planとsource selection

local root directory、PKGBUILD path、PackageBase、root child nameをAUR root queryへ渡さない。同名AUR packageが存在してもlocal rootを置換せず、local metadata missing / invalid / stale / evaluation failureをAUR absence、RPC failure、empty dependencyへfallbackしない。

local PackageBase、children、provides、version constraints、self-returnを含むback-edgeをtyped identityのままremote candidate queryより前に保持する。同じPackageBaseのedgeをconstraint matching前にsatisfiedへ丸めず、cycle情報を捨てない。effective architecture、version / provides constraint、required child集合に基づくinternal edge、unresolved dependency、real cycleの分類を行ってから、unresolved dependencyだけを既存のrepo exact / AUR exact / provider policyへprojectする。

ambiguous providerは[ambiguous provider contract](ambiguous-provider-selection.md)へ渡す。selected AUR providerのPackageBaseはdependency build unit、selected repository providerはexact `repository/package` dependency transactionとする。provider選択のcancel、non-TTY、`--noconfirm`、constraint conflictはlocal metadata failureへ混ぜず、既存typed reasonのままmutation前に停止する。

### Source snapshot、artifact、install

user-owned local source treeを直接build lifecycleへ渡さない。source entriesを検証しながら一つのinvocation-owned source snapshotへmaterializeし、snapshot前後と最初のexternal mutation前にroot identity、content provenance、containmentを再検証する。観測した追加、削除、replacement、content change、partial generationではpartial snapshotを公開せず停止する。

source snapshotへ取り込むentryはrootと同じfilesystem上にあり、effective user所有であることを要求する。regular directory / fileはgroup / other writableでないこと、symlinkはroot内へlexically収まるrelative targetだけを許可すること、absolute target、`..`によるroot escape、special file、mount境界、unsafe owner / modeを拒否することを要求する。source treeのmodeは変更せず、hardlink identityをsnapshotへ持ち越さない。

artifact phaseでは[PackageBase / required-child selection contract](packagebase-child-selection.md)を再利用する。fresh `PKGDEST`はartifact outputだけをisolateし、makepkgが作るsource treeの`src/` / `pkg/`をlocal rootへ戻すものではない。expected / actual artifact identity、freshness、containment、ownership、required child selectionをmutation前に証明する。

local root childrenはExplicit、dependency planが要求するchildrenはDependencyとしてtyped install planへ渡す。existing Explicit packageをDependencyへ降格しない。unexpected sibling / debug artifactはunselectedとし、missing / duplicate / unknown identity、mixed reason、unproven artifactで推測して続行しない。build failure、artifact validation failure、install failureのいずれでもuser-owned local source treeをreset、clean、overwrite、deleteしない。

source workspaceのcleanupとartifact workspaceのdiagnostic retentionは別lifecycleである。cleanup failureやpartial completionをprimary build / install failureへflattenせず、completed child、failed target、unattempted targetを区別する。unsafe identity replacementを観測した場合はnamed replacementをcleanupせず、manual inspection用artifactを保持し得る。

### Current production connection

このcontractは#271のproduction contractを固定するが、`--local` routeが現在のCLIで実装済みであることを意味しない。LocalSourceRoot、read-only metadata、dependency-plan projection、user-owned treeを変更しないsource workspace、artifact / install execution、help / man / completion / localizationを揃えるまで、不完全なlocal-build routeを有効化しない。

## Non-scope / implementationを固定しない範囲

- local rootをAUR RPC packageとして扱うこと、AUR fallback、arbitrary source tree import。
- provider choiceの永続化、complete solver、conflicts / replaces自動解決、cross-source transaction。
- local repository database、clean chroot、package signing、automatic VCS update。
- descriptor、snapshot、workspace、PKGBUILD parser、metadata evaluationの具体的なmodule / type / syscallを恒久固定すること。
- incomplete routeのhelp、man、completion、gettext接続。これらはproduction接続時に別途同期する。

## Compatibility

正式CLI入口、local root identity、`.SRCINFO` state、PKGBUILD evaluation gate、AUR fallback禁止、source snapshot、artifact、install reason、user-owned tree非変更の要約は、[`COMPATIBILITY.md`のlocal PKGBUILD section](../COMPATIBILITY.md#compat-local-pkgbuild)を参照する。
