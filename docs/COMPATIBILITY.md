# Moguet 互換性と pass-through policy

MoguetはArch Linux向けの **pacman-first wrapper** として扱う。日常的な`pacman` workflowをできるだけ自然に通しつつ、AUR build / installとsource-build preferenceを補助する。MoguetはArch Linux / pacman / AURの公式toolではなく、pacmanや既存AUR helperとの完全互換を宣言しない。

この文書は、利用者がroute差分、pacman / makepkgとの差、pass-through、対応 / 非対応範囲を理解するためのcompatibility summaryである。Issue別production contractの詳細なnormative authorityは[`docs/contracts/`](contracts/README.md)に置き、この文書へ独立した全文contractを重複保持しない。

<a id="compat-route-overview"></a>
## 基本方針

- Moguetが明示的に扱うoperation / optionはMoguet側で解釈する。
- Moguet固有optionはpacman executionやreview前の`.SRCINFO`更新判定へ渡さない。
- pacmanが自然に扱えるoperation / optionは、Moguetがrouteの意味を安全に保てる範囲でpacmanへpass-throughする。
- AUR / source-build経路では、pacman transaction optionを無条件にmakepkg optionへ置き換えない。
- 未対応option、曖昧なselection、authoritative metadataのfailure、安全に意味を保てないpacman optionは黙って無視せず、external mutationより前に停止する。
- `--noconfirm`は「全部yes」ではなく、対応済みpromptの確認を省略する指定である。未解決dependency、provider、conflict / replacement、削除、source selection、artifact identityの曖昧さを承認しない。
- 値を取るoptionは、値をtargetと誤認しない。値が欠けている場合は停止する。

<a id="compat-general-route-matrix"></a>
## Route matrix

| Operation / route | Moguetのauthorityと動作 | pacman / makepkgとの差 |
| --- | --- | --- |
| plain `-S` Auto | official packageはbinary repository、source preferenceがあるofficial packageはofficial source-build、officialにないtargetはAURへ分類する | pacman単独のbinary repository searchにAUR/source-build分類を補う |
| `-S --aur` | AUR RPC / PackageBase / AUR build planだけを使い、official packageやsource preferenceへfallbackしない | `--aur`はMoguet selectorでありpacmanへ渡さない |
| `-S --repo` | official binary repositoryへ限定し、AUR / source-buildへfallbackしない | selectorを除いたargvをpacmanへ渡す |
| `-Ss` | official searchとAUR searchを組み合わせる。非対話でprovider / root selectionを開始しない | pacman searchを表示し、MoguetがAUR searchを補完する |
| `-Si` | officialを優先し、見つからない場合だけAUR metadataを表示する。`--aur` / `--repo`はsourceを限定する | AUR infoはpacman infoではなくtyped AUR metadataを表示する |
| `build` / `upgrade` | source preference、BuildPlan、makepkg、artifact validation、`pacman -U`を分離したsource lifecycleで扱う | `makepkg -sic`一括委譲へ戻さない |
| `deps` / `plan` | dependency inspection / plan表示だけを行い、build / install / cloneを開始しない | read-only observationとmutationを分ける |
| `fetch` | 未取得repositoryのclone、既存cloneの`git fetch origin`までに留める | working tree update、pull、merge、reset、build、installを行わない |
| `-G` / `-Gp` | root PackageBaseだけを一時cloneし、exportまたはPKGBUILD表示を行う | dependency plan、makepkg、pacman、sudo、persistent checkoutを行わない |

source routeのselection、preflight、partial completion、failureの詳細は各contractが正本である。routeの結果をpackage nameだけへflattenして別sourceを再推定しない。

<a id="compat-moguet-operations"></a>
## Moguet固有 operation

次のoperationはMoguet固有であり、pacmanへそのまま委譲しない。

- `build <pkg> [VAR=VALUE...]`
- `upgrade`
- `upgrade-aur`
- `upgrade-all`
- `clean`
- `deps [--recursive] <pkg>`
- `plan <pkg>`
- `fetch <pkg>`
- `-G <pkg>` / `-Gp <pkg>`
- `add-src <pkg> [VAR=VALUE...]`
- `del-src <pkg>` / `edit-src <pkg>` / `list-src`
- `revert <pkg>`

`deps`、`plan`、`fetch`、`-G`、`-Gp`は調査・表示・取得段階であり、build / installを混ぜない。`deps`の`--recursive`を除き、Moguet固有operationに未対応optionを指定した場合は停止する。

<a id="compat-aur-update"></a>
## AUR update operation summary

`upgrade-aur`はinstalled foreign inventoryを起点に、AUR RPCでexact packageとして解決でき、installed versionより新しいpackageだけを対象にする。official repository package、AURに存在しないforeign package、source preferenceだけで選ばれるpackageはこのoperationの対象にしない。query、recursive plan、provider selection、conflicts / replaces metadata、preparationを全targetについて確認してからexecutionへ進み、blocking targetが1件でもあればgit checkout、makepkg、`pacman -U`、sudoを開始しない。

`upgrade-all`はsystem upgrade、registered source package、remaining installed AUR packageを`system → registered source → fresh foreign inventory → filtered AUR`のphase順で扱う。single atomic transactionやautomatic rollbackではなく、先行phaseの成功、現在のfailure、後続phaseの`NotAttempted`を区別する。`upgrade-aur`と`upgrade-all`の`--rmdeps`、package target、`--needed`、`--aur`、`--repo`はunsupportedであり、queryやcache mutationより前に停止する。

対象がない場合は成功とするが、query failure、preparation failure、cleanup failure、未実行targetを空の成功結果へ丸めない。partial completionはnon-zeroである。source preferenceで選ばれたPackageBaseとautomatic AUR targetが重複する場合はduplicate exclusion / external satisfactionとして扱い、同じsourceを二重buildしない。

<a id="compat-aur-export"></a>
## AUR PKGBUILD export summary

`-G <pkg>`と`-Gp <pkg>`はexactly oneのAUR root PackageBaseだけを扱う。official repository probe、source preference、repository fallback、dependency plan、dependency repository、makepkg、pacman、sudo、editor、build / installは行わない。

`-G`はcurrent directory直下の`./<PackageBase>`だけをdestinationとし、既存のdirectory、git repository、regular file、symlink、special fileがあればfail closedする。clone、PKGBUILD、`.git`、remote URL、containment、destination identityを検証してからno-replace publishする。`-Gp`はtemporary cloneからregular non-symlink PKGBUILD bytesだけをstdoutへ出し、通常成功・failureでpersistent checkoutやMoguet cacheを変更しない。identity replacementを証明できないtemporary artifactは手動確認用に保持し得る。

<a id="compat-conflicts-replaces"></a>
## AUR conflicts / replaces summary

AUR RPCの`Conflicts` / `Replaces`はdependency resolutionとは分離したmetadata riskとしてraw valueを保持する。`-Si`はmetadataとして表示し、`deps` / `plan`はdependency分類とは別のwarning / incomplete reasonとして表示する。`fetch`はunresolved dependency、未選択ambiguous provider、cycleがなければ取得を許可するが、build / install routeはriskをclone、fetch、makepkg、pacman transactionより前にblocking reasonとして扱う。Moguetはinstalled DBとmetadataを独自照合してconflictを解決せず、削除、置換、provider選択を自動実行しない。

<a id="compat-plan-size"></a>
## Planのofficial package size summary

`plan <pkg>`で表示するofficial repository dependencyのpackage sizeはpresentation metadataであり、BuildPlanのgraph safety、AUR build unitのsize、dependency resolution、provider selection、transactionを変更しない。configured repository orderとread-only sync metadataをauthorityとし、package absence、query failure、malformed metadata、configuration failure、0 bytesを区別する。size metadataが取得できなくても、既存のplan本文を表示できる場合はgraph statusやexit codeを不必要に変えない。

<a id="compat-aur-status"></a>
## AUR status display summary

`-Ss`は軽いsearch / discovery表示として、AUR resultの状態tagを`[installed]`、`[out-of-date]`、`[orphaned]`の順に表示する。`-Si`はAUR metadataの`Maintainer`、`Installed`、`Orphaned`、`Out of Date`を表示する。repository packageの`-Si`はpacmanへ委譲し、AUR metadata表示と混ぜない。status表示はselectionやbuild executionを開始しない。

<a id="compat-split-package"></a>
## AUR split package summary

PackageBaseはclone / fetch / build repositoryの単位であり、package nameはinstall targetである。`deps` / `plan` / `fetch` / `-G` / `-Gp`はPackageBaseとrequested packageの違いを表示・取得のidentityとして保持するだけで、splitであることだけを理由にincomplete扱いしたり全artifactをinstallしたりしない。build / install routeはBuildPlanのrequired childとartifact metadata identityがexactly one一致する場合だけselected childを渡し、sibling / debug outputを暗黙にinstallしない。詳細なselection、transaction、partial completionは[PackageBase contract](contracts/packagebase-child-selection.md)を参照する。

<a id="compat-contract-summary"></a>
## Production contract summary

各contract本文の日本語がnormative source of truthである。ここでは利用者がroute差分を判断するための要約だけを示す。

| Behavior / safety contract | User-visible compatibility summary | Normative contract |
| --- | --- | --- |
| PackageBase / child selection | PackageBase単位でbuildするが、installするのはBuildPlanが要求しmetadata identityで選択したchildだけ。sibling / debugは暗黙installしない | [PackageBase / required-child selection](contracts/packagebase-child-selection.md) |
| separated source-build `--rmdeps` | source-buildではownershipを証明できないためmutation前に拒否。pacman-onlyではMoguetが消費するが作用させず、pacmanへ転送しない | [source-build `--rmdeps`](contracts/source-build-rmdeps.md) |
| XDG cache cutover | trusted root、filesystem identity、symlink、root escape、legacy cache非変更を守る。implementation moduleは固定しない | [XDG cache safety](contracts/xdg-cache-safety.md) |
| source-build preference | `${XDG_CONFIG_HOME:-$HOME/.config}/moguet/source-build.d/`をreader / writer共通のauthorityとする。legacy storeへfallbackしない | [source-build preference XDG authority](contracts/source-build-preference-xdg.md) |
| ambiguous provider | exact / unique provider以外は候補順で選ばず、interactive TTYの明示selectionだけを受け付ける。non-TTY / `--noconfirm`は停止 | [ambiguous provider selection](contracts/ambiguous-provider-selection.md) |
| root package selection | `-S --select`だけが対話root selection。`-Ss`は非対話search。候補が1件でもdefault選択せず、source identityを保持する | [root package selection](contracts/root-package-selection.md) |
| local PKGBUILD | `build --local <directory>`を明示入口とし、local treeをAUR rootへfallbackせず、metadata / source identity / artifactをfail closedで検証する | [local PKGBUILD](contracts/local-pkgbuild.md) |

<a id="compat-packagebase-child-selection"></a>
## PackageBase / required-child compatibility

PackageBaseはrepository / build / workspace / package transactionの単位、required childはinstall-selectionの単位である。1 PackageBaseを1 fresh workspaceで1回buildし、`makepkg --packagelist`のexpected aggregateとbuild後package metadata identityを照合する。required childがexactly one選択できない場合、filename、先頭artifact、PackageBase名、`--noconfirm`で補わずfail closedする。

selected childだけがinstall input、install reason、installed / skipped-as-needed outcome、target attributionを持つ。unselected sibling / debug artifactはresult dataとして保持する。transaction failureでchild successを推測せず、cleanup failureはcompleted childを保持するpartial successとして扱う。詳細は[contract](contracts/packagebase-child-selection.md)を参照する。

<a id="compat-rmdeps"></a>
## `--rmdeps` compatibility

`--rmdeps`はpacman optionではなく、makepkg由来のMoguet global optionである。separated source-buildでは、今回のinvocationが導入したdependency集合をMoguetがauthoritativeに所有できないため、意味のあるcleanup要求をsilent ignoreせず、mutation前にfail closedする。pre/post installed package差分だけではpre-existing / Explicit / `base-devel`、reason変化、並行transaction、invocation外の変更を安全に区別できない。

source-build routeでは`makepkg -r`、`pacman -Rns`、`pacman -Qdt`、独自orphan cleanup、automatic rollbackへ変換しない。`--noconfirm`でも拒否を突破しない。

pacman-only routeでは、Moguetがmakepkg dependency installation lifecycleを実行しない。そのためcleanup対象となるinvocation-owned dependency集合自体が発生せず、Moguetはoptionを消費するが作用させず、pacmanへ転送しない。このno-opはsource-build routeで意味のあるcleanupを黙って無視することとは異なる。pacman-onlyでは安全に作用させるcleanup lifecycleが存在しないからである。decision 1の「黙って無視せず、意味を安全に維持できない場合は停止する」とも矛盾しない。

| Route | `--rmdeps` contract |
| --- | --- |
| 明示的なAUR / source-build install、`build` | source resolutionより前、またはroute probe後でcheckout mutation、workspace、makepkg、metadata query、pacman / sudoより前に拒否 |
| singular / PackageBase separated lifecycle | workspace / process / metadata / transactionより前に拒否。既存preflight orderを維持 |
| `upgrade-aur` | update query、default log / cache初期化より前に拒否。target 0件でもno-op成功へ変換しない |
| `upgrade-all` | log / cache、source preparation、system upgrade、foreign inventory、AUR queryより前に拒否 |
| registered `upgrade`にvalid source targetがある | source preparation、source mutation、system mutationより前に拒否 |
| registered `upgrade`にsource targetがなくpacman-onlyへ縮退 | Moguetが消費するが作用させず、system `pacman -Syu`へ転送しない |
| その他のpacman-only route | Moguetが消費するが作用させず、pacmanへ転送しない |

`--rmdeps`のauthority、source-build fail-closed、pacman-only no-opの理由は[専用contract](contracts/source-build-rmdeps.md)を正本とする。

<a id="compat-xdg-cache-safety"></a>
## XDG cache compatibility

cacheのdestructive operationはtrusted root内へ限定し、symlink / root escapeをfollowせず、identity replacement、ownership不明、preflight不足をfail closedとする。cache cleanupは全targetのpreflight前に開始しない。legacy cacheを自動read / migrate / modify / deleteしない。Git executionも親processの危険なroutingやconfig environmentを暗黙継承しない。

このsectionはuser-visibleな安全要約であり、filesystem identity、rollback、implementation proportionalityの正本は[XDG cache safety contract](contracts/xdg-cache-safety.md)である。

<a id="compat-source-preference-xdg"></a>
## Source-build preference authority compatibility

canonical rootは次である。

```text
${XDG_CONFIG_HOME:-$HOME/.config}/moguet/source-build.d/<package-name>
```

unset / emptyの`XDG_CONFIG_HOME`は`$HOME/.config`へfallbackし、明示値はabsoluteかつ安全で既存base directoryでなければfail closedとする。root実行時もroot自身のXDG contextを使い、`SUDO_USER`から別userを推測しない。add / editだけが必要なdirectoryをsafe creation boundary経由で作成し、read / list / build / upgrade / missing delete / revertはdirectoryを作成しない。

`/etc/jpacker`と`/etc/moguet`をruntimeで作成・参照せず、legacy storeへのfallback、merge、自動copy / rewrite / deleteを行わない。source preference filesystem操作はsudoを使わず、revert後のpacman transactionのsudoとは分離する。詳細は[source-build preference contract](contracts/source-build-preference-xdg.md)を参照する。

<a id="compat-ambiguous-provider"></a>
## Dependency provider compatibility

official exact、AUR exact、unique providerを先に扱い、複数providerはambiguousとして扱う。候補identityはsource kind、package、repositoryまたはPackageBase、provided dependency、available constraint metadataを保持する。interactive TTYの番号選択以外ではdefaultを設けない。

non-TTY、`--noconfirm`、cancel、EOFではpromptや自動選択を開始しない。choiceはinvocation-localであり、config / cacheへ保存しない。selected repository providerはexact `repository/package`のofficial dependency、selected AUR providerはPackageBase build unitとして扱う。selectionとstatic preflight前にclone、build、pacman、sudoを開始しない。詳細は[ambiguous provider contract](contracts/ambiguous-provider-selection.md)を参照する。

<a id="compat-root-package-selection"></a>
## Root package selection compatibility

正式入口は`moguet -S --select <query>`であり、`-Ss`は非対話search / presentationのままである。repository / AUR candidateはsource identityを保持し、同名packageでもsourceが違えば別候補とする。official searchはread-only libalpm metadata、AUR searchはtyped AUR responseをauthorityとし、pacmanのhuman-readable search outputをparseしない。

interactive stdinで番号、複数番号、inclusive range、表示済みofficial groupの`@group` selectorを扱う。empty、cancel、EOF、non-TTY、`--noconfirm`はnon-zeroで停止し、invalid lineはatomically retryする。selection、identity validation、全static preflightが終わるまでpacman、sudo、clone、build、install、cache / workspace mutationを開始しない。selected repository rootとAUR rootは明示routeへprojectし、package nameからsourceを再推定しない。詳細は[root package selection contract](contracts/root-package-selection.md)を参照する。

<a id="compat-local-pkgbuild"></a>
## Local PKGBUILD compatibility（production connection boundary）

正式入口は`moguet build --local <directory> [V=K...]`であり、`build <pkg>`はremote package routeとして維持する。local directory、root `PKGBUILD`、`.SRCINFO`のfilesystem identity、owner、mode、containmentをdescriptor-firstで検証し、unsafe stateはfail closedとする。local rootをAUR RPCへqueryせず、metadata failureをAUR absenceやempty dependencyへfallbackしない。

safe `.SRCINFO`をread-only authorityの第一候補とし、missing / invalid / known-staleとPKGBUILD evaluationを区別する。`--noedit`はevaluation consentではなく、`--noconfirm`、non-TTY、cancel、EOFはevaluationを自動承認しない。local source treeをreset、clean、overwrite、deleteせず、artifactはPackageBase / required-child contractへ接続する。なお、metadata、dependency plan、source workspace、artifact / install、public surfaceの全sliceが揃うまで不完全な`--local` routeをproductionへ接続しない。詳細は[local PKGBUILD contract](contracts/local-pkgbuild.md)を参照する。

<a id="compat-source-selection"></a>
## Package source selection policy

source selectionは排他的な3状態である。

- Auto: selector未指定時のdefault。既存の分類を維持する。
- AurOnly (`--aur`): root targetをAURへ限定し、officialへfallbackしない。
- RepoOnly (`--repo`): targetをofficial binary repositoryへ限定し、AUR / source-buildへfallbackしない。

`--aur`と`--repo`の同時指定はconflictとして、pacman、sudo、AUR RPC、git、makepkg、cache mutationより前に停止する。scope外のoperationでselectorを認識した場合も黙って無視しない。selectorはpacman option value待ち、`--`後のopaque operand、`--` markerより優先されず、通常位置のtokenだけを消費する。

## pacman由来 operationのpass-through

MoguetがAUR / source-buildへ介入しない場合、次のoperationは基本的にpacmanへ委譲する。

- `-S`系（sourceへ分岐しない場合）
- `-R`系
- `-Q`系
- `-U`系
- `-D`系
- `-F`系
- `-T`系

`-Sc`は`sudo pacman -Sc`へ委譲し、Moguet build/cacheは削除しない。Moguetのbuild/cacheをcleanしたい場合は`clean`を使う。`-Syu` / `-Sy` / `-Su`はpacman-compatible system upgradeとし、registered source preferenceの全体走査は`upgrade`へ混ぜない。

read-only queryのpacman標準出力・標準エラーはできるだけ保ち、Moguetが主要なexternal commandを実行する場合はcommandを実行前に表示する。pacmanのtransaction ownerはpacman、source artifact build ownerはmakepkg、source repository retrieval ownerはgitである。

<a id="compat-pacman-options"></a>
## pacman / makepkg由来 option

pacmanへ直接委譲する経路では、Moguetが明示的に消費しないpacman-compatible optionをpacmanへ渡す。AUR / source-build経路では、pacman optionをそのままmakepkg optionとはみなさない。

### Moguet固有 option

- `--noedit`: PKGBUILD / `.install` review / edit promptを省略する。
- `--nodiff`: cache repository update後のdiff promptを省略する。
- `--rebuild`: build-only makepkgの`-f`へ変換する。
- `--cleanbuild`: build-only makepkgの`-C`へ変換する。
- `--rmdeps`: source-buildでは下記contractに従い拒否し、pacman-onlyでは消費する。
- `--aur` / `--repo`: Moguetのsource selectorであり、pacman / makepkgへ渡さない。

### `--noconfirm`

pacman-onlyではpacmanへ、separated source-buildではbuild-only makepkgとtyped `sudo pacman -U`へ渡す。ただし、未解決依存、循環依存、ambiguous provider、root candidate、unknown / duplicate artifact identity、mixed install reason、conflicts / replaces、未指定のrebuild / cleanbuild、危険な削除 / reset、PKGBUILD evaluationを自動承認しない。

### `--needed`

pacman-onlyではordered pacman argvへ保持する。対応済み`-S`でsource routeが生じる場合は、build skipではなく検証済みartifactへ渡すtyped `pacman -U`のinstall-only policyとして1回だけ扱い、makepkgへ渡さない。selection、metadata、provider、artifact、safety guardは省略しない。

### AUR / source-buildで単純pass-throughできないoption

`--asdeps`、`--asexplicit`、`--ignore`、`--ignoregroup`、`--overwrite`、`--config`、`--dbpath`、`--root`、`--sysroot`、`--cachedir`、`--gpgdir`、`--hookdir`、`--logfile`、`--print-format`、`--nodeps` / `--assume-installed`、`--dbonly` / `--noscriptlet`、`--downloadonly`、`--print`は、source-buildで同じ意味を保てないため、対応範囲外ではmutation前に停止する。pacmanが見ているworldとMoguetのmetadata / installed state / cacheがずれるoptionを黙って変換しない。

### 値を取るoption

`--arch`、`--assume-installed`、`--cachedir`、`--color`、`--config`、`--dbpath`、`--gpgdir`、`--hookdir`、`--ignore`、`--ignoregroup`、`--logfile`、`--overwrite`、`--print-format`、`--root`、`--sysroot`、および`-b`、`-r`はvalueとの組として扱う。option value待ちのtokenや`--`後のopaque operandをMoguet global optionとして再解釈しない。

## Exit code、partial completion、failure

- pacmanへ直接委譲したcommandはpacmanの終了codeを返す。
- integrated searchはofficialまたはAURのmatchを表示できた場合に成功とするが、query failureをempty resultへflattenしない。
- `plan` / inspectionのwarningと、plan作成自体のfailureを区別する。
- build / install / cleanup、package transaction、source phaseのfailureはpartial completionとunattempted targetを保持し、successへ丸めない。
- 先行phaseの成功をautomatic rollbackや後続phaseの成功と解釈しない。

## Out of scope

この方針はpacman完全互換、provider choiceの永続化、arbitrary multiple-outputの全自動install、debug package default install、conflicts / replacesの自動解決、dependency solver強化、pacman database write、package verificationの独自再実装を宣言しない。詳細なproduction safety contractは[`docs/contracts/`](contracts/README.md)と[`DECISIONS.md`](DECISIONS.md)へ分離している。
