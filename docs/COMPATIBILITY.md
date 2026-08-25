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
- `--noconfirm`は「全部yes」ではなく、対応済みpromptの確認を省略する指定である。未解決dependency、provider、conflict / replacement、削除、source selection、local PKGBUILD metadata evaluation、artifact identityの曖昧さを承認しない。
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
| remote `build` / local `build --local` / `upgrade` | remote package routeとlocal PKGBUILD production routeを、source preference、BuildPlan、makepkg、artifact validation、`pacman -U`を分離したsource lifecycleで扱う | `makepkg -sic`一括委譲へ戻さない |
| `deps` / `plan` | dependency inspection / plan表示だけを行い、build / install / cloneを開始しない | read-only observationとmutationを分ける |
| `fetch` | 未取得repositoryのclone、既存cloneの`git fetch origin`までに留める | working tree update、pull、merge、reset、build、installを行わない |
| `-G` / `-Gp` | root PackageBaseだけを一時cloneし、exportまたはPKGBUILD表示を行う | dependency plan、makepkg、pacman、sudo、persistent checkoutを行わない |

source routeのselection、preflight、partial completion、failureの詳細は各contractが正本である。routeの結果をpackage nameだけへflattenして別sourceを再推定しない。

<a id="compat-moguet-operations"></a>
## Closed CLI grammar

Moguet-owned operationと、Moguetがinterceptするsource-aware `-S --select`のcanonical
grammarは次のとおりである。

<!-- CLI CANONICAL GRAMMAR BEGIN -->
```text
build <pkg> [V=K...]
build --local <directory> [V=K...]
upgrade
upgrade-aur
upgrade-all
clean
deps [--recursive] <pkg>...
plan <pkg>...
fetch <pkg>...
add-src <item>...
edit-src <pkg>...
list-src
del-src <pkg>...
revert <pkg>...
-G <pkg> [--output-dir=DIR]
-Gp <pkg>
-S --select [--needed] <query>
```
<!-- CLI CANONICAL GRAMMAR END -->

remote / local `build`はprimary operandをexactly oneだけ取り、その後には`V=K`
assignmentだけを許すため、extra bare operandを拒否する。`upgrade`、`upgrade-aur`、
`upgrade-all`、`clean`、`list-src`はtarget-lessであり、target operandを拒否する。一方、
`deps`、`plan`、`fetch`と`add-src`、`edit-src`、`del-src`、`revert`は表示どおり
multi-targetを維持する。`add-src`ではpackage itemが後続assignmentのscopeを開始する。

`--local`はlocal `build`、`--recursive`は`deps`、`--select`はplain `-S`だけが所有する。
`-S --select`の`--needed`はroute-owned final installationだけへ作用する。Moguet固有routeに
scope外optionを指定した場合は黙って無視せず停止する。上記以外のpacman operation formは
delegated open grammarであり、この一覧をpacman parserのclosed allowlistとして扱わない。

`deps`、`plan`、`fetch`、`-G`、`-Gp`は調査・表示・取得段階であり、build / installを
混ぜない。

<a id="compat-dry-run"></a>
## Unified dry-run compatibility

global `--dry-run`は、Moguet-owned supported `-S` install / system-update、`fetch`、remote `build`、local `build --local`、`upgrade`、`upgrade-aur`、`upgrade-all`だけを統一planとして観測する。nested `dry-run` commandやpacman自身の`--print`への委譲ではない。`deps`、`plan`、`-Ss`、`-Si`、`clean`、`-G` / `-Gp`、source-preference command、未裁定generic pacman pass-throughを含むその他のrouteでは明示的にnon-zeroで拒否する。

観測は各routeの既存production pre-mutation authorityを使い、root discovery、route projection、`BuildPlan`、provider selection、constraint evaluation、read-only local descriptor validation、AUR update preflight、required artifact、repository transaction intentをhuman-readableに表示する。read-only filesystem / network accessと、`pacman -Si` / `pacman -Qm`等のexact allowlist済みread-only discovery queryは実行し得る。statusは既存authorityから得た`Ready` / `NoOp`を終了code 0、`Blocked`をnon-zeroとし、renderer-local completenessから再計算しない。partial source failureやtyped blockerをreadyへflattenしない。

absolute no-mutation boundaryとして、dry-runはstate log / persistent stateのwriteやdirectory作成、cache、workspace、worktree、Git clone / fetch / checkout mutation、`makepkg --printsrcinfo`その他のlocal metadata生成・評価、build output、sudo、pacman transactionの開始・mutation、pacman transaction lock、install、cleanup mutationへ到達しない。local routeは安全な既存descriptorだけを使い、metadata評価が必要ならreadyを推測せず`Blocked`とする。

dry-run observationはapproval token、prepared execution capability、cached provider choiceではない。後続のactual invocationへ渡さず、actual routeはcurrent stateからproduction validationとprovider selectionを再実行する。v2.2.0ではhuman-readable outputだけを提供し、JSON / machine-readable schemaは追加しない。

diagnostic presentationはtyped stateからlocalizeする一方向のprojectionであり、localized / raw
stringからclassificationを逆算しない。English / Japaneseともnormal summary、
attention-required detail、route-owned necessary detailの順を保つ。operation outcomeとpackage
state observation、plan constructionとcompletenessとexecution readiness、severityとblockingと
exit-status effectはそれぞれ独立したdimensionである。successful-unverifiedはrequired actionを
伴うsuccessとして保持し、failureへ丸めない。`Unknown`も`NoOp`へ丸めない。

<a id="compat-interactive-confirmation"></a>
## Interactive confirmation compatibility

Moguet-owned boolean confirmationは、`[Y/n]`をYes default、`[y/N]`をNo default、`[y/n]`をdefaultなしとして扱う。fixed ASCII / locale-neutral / case-insensitive tokenとして`y` / `yes`、`n` / `no`、`q` / `quit` / `cancel`だけを受理し、日本語response tokenは追加しない。q-familyは全boolean confirmationでformal cancellationであり、question固有のNoへflattenしない。invalid inputとdefaultなしのempty inputはwarning後に再promptする。

`--noconfirm`は宣言済みdefaultだけを利用し、`[y/n]`をapprovalへ変えない。non-TTY stdinは`[Y/n]`のYes defaultを選ばずUnavailableとなり、`[y/N]`だけがsafe Noへ進める。interactive clean EOFはCancelled / EndOfInput、actual stream read failureはInputFailureとして区別する。

Declinedはquestion固有のnegative answer、Cancelledはcurrent Moguet operationの停止であり、actual command / input / internal failureとも区別する。presentation classificationとprocess exit statusは別dimensionで、optional No、normal skip、inspection result等はroute contractに従ってexit 0となり得る一方、required operationを未完了にするDeclined / Cancelled / EOF / Unavailableとactual failureはnon-zeroとなる。cancellationはその時点以降を停止するだけで、既に完了したGit、editor、pacman、system等のphaseをrollbackしない。詳細は[interactive confirmation contract](contracts/interactive-confirmation.md)を正本とする。

<a id="compat-aur-update"></a>
## AUR update operation summary

`upgrade-aur`はinstalled foreign inventoryを起点に、AUR RPCでexact packageとして解決でき、installed versionより新しいpackageだけを対象にする。official repository package、AURに存在しないforeign package、source preferenceだけで選ばれるpackageはこのoperationの対象にしない。query、recursive plan、provider selection、conflicts / replaces metadata、preparationを全targetについて確認してからexecutionへ進み、blocking targetが1件でもあればgit checkout、makepkg、`pacman -U`、sudoを開始しない。

`upgrade-all`はsystem upgrade、registered source package、remaining installed AUR packageを`system → registered source → fresh foreign inventory → filtered AUR`のphase順で扱う。single atomic transactionやautomatic rollbackではなく、先行phaseの成功、現在のfailure、後続phaseの`NotAttempted`を区別する。`upgrade-aur`と`upgrade-all`の`--rmdeps`、package target、`--needed`、`--aur`、`--repo`はunsupportedであり、queryやcache mutationより前に停止する。

対象がない場合は成功とするが、query failure、preparation failure、cleanup failure、未実行targetを空の成功結果へ丸めない。partial completionはnon-zeroである。source preferenceで選ばれたPackageBaseとautomatic AUR targetが重複する場合はduplicate exclusion / external satisfactionとして扱い、同じsourceを二重buildしない。

<a id="compat-aur-export"></a>
## AUR PKGBUILD export summary

`-G <pkg>`と`-Gp <pkg>`はexactly oneのAUR root PackageBaseだけを扱う。official repository probe、source preference、repository fallback、dependency plan、dependency repository、makepkg、pacman、sudo、editor、build / installは行わない。

`-G`は`--output-dir`未指定時、command開始時current directory直下の`./<PackageBase>`をdestinationとする。`--output-dir=DIR`は`-G`専用のoperation-local attached-value optionであり、指定した既存parent直下の`<PackageBase>`へexportする。relative valueはcommand開始時current directory基準で解決し、parentを自動作成せず、指定pathのfinal / intermediate symlink componentをfollowしない。Moguet独自のtilde expansionは行わないため、HOME配下には`--output-dir="$HOME/..."`またはabsolute pathを使う。space-separated value、empty value、duplicate、`-Gp`、他operationでは拒否する。

export parentはdirectory fdと`st_dev` / `st_ino` identityでanchorし、publish直前にnamed pathをnofollowで再openして同じfilesystem objectであることを確認する。rename、replacement、symlink replacement、identity driftはfail closedとし、replacement側へredirectしない。destination leafはvalidated PackageBaseのdirect childへ固定する。既存のdirectory、git repository、regular file、symlink、special fileがあればfail closedし、preflight後にdestinationが現れた場合も`renameat2(..., RENAME_NOREPLACE)`相当で置換しない。clone、PKGBUILD、`.git`、remote URL、containment、temporary checkout identityを検証してからpublishする。

`-Gp`はtemporary cloneからregular non-symlink PKGBUILD bytesだけをstdoutへ出し、通常成功・failureでpersistent checkoutやMoguet cacheを変更しない。`--output-dir=DIR`を受理せず、stdout lifecycleも変更しない。identity replacementを証明できないtemporary artifactは手動確認用に保持し得る。

<a id="compat-conflicts-replaces"></a>
## AUR conflicts / replaces summary

AUR RPC、`.SRCINFO`等から得た`Conflicts` / `Replaces`宣言は、dependency resolutionとは分離したtyped metadataとして保持する。Moguetはread-onlyなinstalled package databaseとplanned targetを観測し、package name、PackageBase、source / root attribution、version、provided componentを保ったままversion付きrelationをtransaction前に分類する。public diagnostic、build / install readiness、execution preflightはこのtyped assessmentを共通authorityとし、rendererやrouteごとにraw declarationを再parse・再判定しない。

classificationは、installed packageとのconfirmed conflict、planned targetとのconfirmed conflict、reviewが必要なpotential replacement impact、judgment不能な`Unknown`、invalid metadata / observation、complete observationによるconfirmed no matching current / planned targetを区別する。replacement matchはautomatic replacementの予告や許可ではない。`Unknown`とinvalid result、およびdeclarationはあるがassessment未完了のfallbackはfail closedとし、「一致対象なし」へ丸めない。completeな観測がpackageとprovided componentのいずれにも一致しないと確認した場合だけrelation guardを解除する。この結果もdeclaration自体が存在しないという意味ではない。

`-Si`はsource metadataとして`Conflicts` / `Replaces`を表示し、installed / planned stateを必要とする判定はplan / build preflightへ延期したことを明示する。`deps` / `plan`はtyped assessmentとreadinessをread-onlyに表示し、既存inspection commandのstatus contractへ新しいexit codeを追加しない。standalone `fetch`はfetch readinessが満たされる限りsource取得だけを行えるが、relation assessmentはBuild / Install readinessを許可しない。blocking conflict、potential replacement、`Unknown`、invalid、assessment未完了の各resultは、source install、local source route、AUR updateを含むexecutionをmakepkg、sudo、pacman install transactionより前に停止する。dry-run / unified planは同じblocking truthを`Blocked`とnon-zero statusへ投影する。complete no-matchだけならrelationを理由にblockせず、他のguardがなければ`Ready` / successを維持する。

Moguetが所有するのはmetadata observation、typed classification、pre-transaction diagnostic、safety stopまでである。automatic package removal、automatic replacement、automatic conflict resolution、replacement targetやproviderのimplicit selection、full dependency / conflict solverの置換、libalpm transaction prepare / commitは行わない。`pacman` / libalpmが最終transaction authorityであり、Moguetのpreflight successはtransaction successを意味しない。`--noconfirm`もrelation guardをbypassせず、自動削除・自動置換を許可しない。

<a id="compat-plan-size"></a>
## Planのofficial package size summary

`plan <pkg>...`で表示するofficial repository dependencyのpackage sizeはpresentation metadataであり、BuildPlanのgraph safety、AUR build unitのsize、dependency resolution、provider selection、transactionを変更しない。configured repository orderとread-only sync metadataをauthorityとし、package absence、query failure、malformed metadata、configuration failure、0 bytesを区別する。size metadataが取得できなくても、既存のplan本文を表示できる場合はgraph statusやexit codeを不必要に変えない。

dependency edgeはmetadata trust boundaryで構成したtyped requirement、installed / configured repository / AUR / local / providerのsource-aware candidate、`ConstraintEvaluation`を保持し、production downstreamでraw constraintを再parseしない。`deps`は`Satisfied` / `Unconstrained`を通常表示し、`Unsatisfied` / `Unknown`をresult / reason付きwarningとして継続する。`plan`は同じ2状態をincompleteとする。`Invalid` / `Conflicting`はread-only plan constructionでもfail-closedとする。`fetch`、build、install、upgrade、local buildは`Unsatisfied` / `Unknown`を含め、成功を証明できないconstraint resultをclone、fetch、source mutation、build、sudo、pacman、transaction開始前に拒否する。preflight successはtransaction successを意味しない。

<a id="compat-aur-status"></a>
## AUR status display summary

`-Ss`は軽いsearch / discovery表示として、AUR resultの状態tagを`[installed]`、`[out-of-date]`、`[orphaned]`の順に表示する。`-Si`はAUR metadataの`Maintainer`、`Installed`、`Orphaned`、`Out of Date`を表示する。repository packageの`-Si`はpacmanへ委譲し、AUR metadata表示と混ぜない。status表示はselectionやbuild executionを開始しない。

<a id="compat-split-package"></a>
## Remote source-build PackageBase summary

PackageBaseはclone / fetch / build repositoryの単位であり、package nameはinstall targetである。official repositoryでは、requested childとPackageBaseの対応をconfigured repository順のstrict libalpm exact snapshotから取得する。`Present`だけをrepository sourceとして採用し、confirmed `NotFound`だけをAUR fallbackへ渡す。query / config / metadata failureをabsenceへflattenせず停止し、requested name、filename、URL、artifact pathからPackageBaseを推測しない。

`deps` / `plan` / `fetch` / `-G` / `-Gp`はPackageBaseとrequested packageの違いを表示・取得のidentityとして保持するだけで、splitであることだけを理由にincomplete扱いしたり全artifactをinstallしたりしない。build / install routeはsource-build upper projectionが確定したrequired childとartifact metadata identityがexactly one一致する場合だけselected childを渡し、sibling / debug outputを暗黙にinstallしない。official repositoryのstandalone / registered routeではrequested `Explicit` childだけをinstallし、全unselected sibling / debugをresultへ保持する。詳細なselection、transaction、partial completionは[PackageBase contract](contracts/packagebase-child-selection.md)を参照する。

<a id="compat-contract-summary"></a>
## Production contract summary

各contract本文の日本語がnormative source of truthである。ここでは利用者がroute差分を判断するための要約だけを示す。

| Behavior / safety contract | User-visible compatibility summary | Normative contract |
| --- | --- | --- |
| common source-aware identity | package child、PackageBase、source、revision、release、architectureを別fieldで保持するinternal foundation。既存routeを置換せず、incomplete evidenceをcomplete identityへ推測しない | [source-aware package identity](contracts/source-package-identity.md) |
| reviewed AUR source state | AUR PackageBaseごとにexplicit accept済みexact revisionを保持し、previous reviewed revisionからexact targetまでをreviewする。skipではstateを進めず、accepted targetだけをpinned build authorityにする | [reviewed AUR source state](contracts/reviewed-source-state.md) |
| PackageBase / child selection | PackageBase単位でbuildするが、installするのはsource-build upper projectionが要求しmetadata identityで選択したchildだけ。sibling / debugは暗黙installしない | [PackageBase / required-child selection](contracts/packagebase-child-selection.md) |
| separated source-build `--rmdeps` | source-buildではownershipを証明できないためmutation前に拒否。pacman-onlyではMoguetが消費するが作用させず、pacmanへ転送しない | [source-build `--rmdeps`](contracts/source-build-rmdeps.md) |
| XDG cache cutover | trusted root、filesystem identity、symlink、root escape、legacy cache非変更を守る。implementation moduleは固定しない | [XDG cache safety](contracts/xdg-cache-safety.md) |
| source-build preference | `${XDG_CONFIG_HOME:-$HOME/.config}/moguet/source-build.d/`をreader / writer共通のauthorityとする。legacy storeへfallbackしない | [source-build preference XDG authority](contracts/source-build-preference-xdg.md) |
| interactive confirmation | `[Y/n]` / `[y/N]` / `[y/n]`、fixed yes / no / cancel token、non-TTY / `--noconfirm` gate、Declined / Cancelled / failure、route-owned exit、non-rollbackを統一する | [interactive confirmation](contracts/interactive-confirmation.md) |
| ambiguous provider | exact / unique provider以外は候補順で選ばず、interactive TTYの明示selectionだけを受け付ける。non-TTY / `--noconfirm`は停止 | [ambiguous provider selection](contracts/ambiguous-provider-selection.md) |
| root package selection | `-S --select`だけが対話root selection。`-Ss`は非対話search。候補が1件でもdefault選択せず、source identityを保持する | [root package selection](contracts/root-package-selection.md) |
| local PKGBUILD | `build --local <directory>`を明示入口とし、local treeをAUR rootへfallbackせず、metadata / source identity / artifactをfail closedで検証するproduction接続済みroute | [local PKGBUILD](contracts/local-pkgbuild.md) |

<a id="compat-reviewed-source-state"></a>
## Reviewed AUR source state compatibility

AUR Git source-buildでは、最後に利用者が明示acceptしたcomplete commit OIDをPackageBase単位で
XDG stateへ保持する。fetch / clone後にexact targetをpinし、reviewed continuationのreview、
acceptance、detached checkout、state publication、build continuationへ同じOIDを渡す。cache
checkoutのHEAD、branch、`origin/<branch>`等のmutable refをreview baselineやreviewed build
authorityとして再利用しない。

reviewed stateがない場合はempty treeからexact targetまでのinitial full review、valid baselineが
targetと異なる場合はprevious reviewed revisionからexact targetまでのupdate review、同じ場合は
prompt / rewriteなしのalready-reviewed continuationとなる。baseline objectが利用不能ならcache HEADへ
fallbackせずfull rebaseline reviewを行う。current-schemaのinvalid / corrupted / source-mismatched
stateはreason付きfull rebind reviewを要求し、unsupported future schema、unsafe history、store /
observation failureはoverwriteせずfail closedとする。

review inventoryはAUR Git treeのtracked file全体であり、added / modified / deleted / renamed /
type-changedを保持する。root `PKGBUILD`とtop-level `*.install`をreview-sensitiveとして強調するが、
patch、service unit、helper script、config / local source、binary / non-text change等をextensionで除外
しない。`.SRCINFO`はgenerated metadataとして表示に残すが、source-review authorityにはしない。

review表示とacceptanceは別eventである。defaultなしのinteractive promptへ明示入力した`y` /
`yes`だけがstate advancementを許可する。`--nodiff`、review decline、safe default No、
`--noconfirm`、non-TTYはreviewed authorityを作らず、compatibility buildを継続し得る場合もstateを
進めない。q-family、EOF、input failure、materialization / presentation failure、manual inspection
required、review-sensitive source unrenderable等のstop outcomeではbuildへ進まない。unsafe / future /
inconsistent stateをcompatibility routeで迂回しない。

compatibility-only buildはreviewed authorityを持たず、legacy checkout continuationをreview済みまたは
pinned reviewed buildとして表示しない。この経路からreviewed revision provenanceやstate publicationを
作らない。

state publicationはreview開始時のexact record identityとraw contentsをguardにするCAS semanticsを
持つ。並行processが別targetへ進めたstateをstale writerが上書きせず、same exact targetだけを
idempotent successとして扱う。acceptance後もexact checkout、editor boundary、既存preflightを
完了してからmakepkg前にpublishする。正常にpublishしたreviewed revisionは、後続build / install /
cleanup failureでrollbackしない。reviewed、built、installed outcomeは別々に表示する。

`--edit` / `--noedit`と`review.pkgbuild`はinvocation-localなPKGBUILD / detected top-level
`*.install` editor policyであり、upstream reviewed-source acceptanceではない。editor changeは
reviewed exact commit上のoverlayとしてcurrent buildにだけ作用し、persistent reviewed revision、
将来invocationのpatch、generic source identityへ昇格しない。

Issue #411より前から存在するAUR cacheにmanual migrationは不要である。reviewed stateが本当に
存在しない状態だけをnormalな`Missing`として扱い、最初に対象となるPackageBaseを一度full review
する。legacy checkout HEAD、branch、remote ref、artifactからreviewed revisionを捏造しない。
Invalid / Corrupted / SourceMismatch / UnsupportedFutureやunsafe stateをMissingへ丸めない。

official repository source-buildとlocal PKGBUILD routeはreviewed-source stateをread / writeしない。
official routeはconfigured repository / libalpm snapshot、local routeはuser-owned filesystem /
content provenanceをそれぞれ維持する。詳細は
[reviewed AUR source state contract](contracts/reviewed-source-state.md)を正本とする。

<a id="compat-common-source-identity"></a>
## Common source-aware identity compatibility

Issue #355のcommon identityは、後続profile / snapshot / patch workflow向けのinternal foundationであり、現時点のpublic CLIやproduction selection / build / install semanticsを変更しない。package child、PackageBase、repository / AUR / local source、source location、source revision、package release、architectureを別fieldで保持し、package名またはderived string keyへflattenしない。

Issue #355のgeneric current repository / AUR modelはexact source commitを保持しないためrevisionは`Unknown`であり、known commitとして推測しない。Issue #411のreviewed-source lifecycleはAUR review / build用のexact OIDを別のpersistent / capability authorityとして保持するが、そのOIDをgeneric `source_package_identity_projection`へ注入して`Known`へ昇格させない。generic source identity projectionとreviewed-source persistent / build authorityは同じものではない。current local routeはGit repositoryをauthorityにせずfilesystem / content provenanceを使うためrevisionは`Inapplicable`である。`Unknown`、`Absent`、`Unavailable`、`Inapplicable`をknown matchやabsenceへ丸めない。

repository root candidateはPackageBaseを、actual artifactはPackageBase / sourceを単体では保持しない。internal read-only adapterは既存typed contextと相関できる場合だけcomplete common identityを返し、filename、package name、URL leaf、canonical source keyから不足fieldを逆算しない。PackageBaseを持たないrepository root、source contextを持たないartifact、installed-only dependency等はtyped failureであり、partial identityを公開しない。

internal compatibility evaluatorはsource、PackageBase、child、revision、release、architectureをdimension別に判定し、`ExactMatch`、`SamePackageChild`、`SamePackageBase`、`Incompatible`、`Indeterminate`を区別する。structurally equalな`Unknown` / `Absent` / `Unavailable` / `Inapplicable`も`ExactMatch`へ昇格しない。これらのadapter / evaluatorは後続v3 model向けで、current production routeのdecisionには未接続である。詳細なstate、equality、compatibility、projection contractは[source-aware package identity contract](contracts/source-package-identity.md)を正本とする。

<a id="compat-packagebase-child-selection"></a>
## PackageBase / required-child compatibility

PackageBaseはrepository / build / workspace / package transactionの単位、required childはinstall-selectionの単位である。1 PackageBaseを1 fresh workspaceで1回buildし、`makepkg --packagelist`のexpected aggregateとbuild後package metadata identityを照合する。required childがexactly one選択できない場合、filename、先頭artifact、PackageBase名、`--noconfirm`で補わずfail closedする。

remote source-buildのcurrent route matrixは次である。

| Route | PackageBase lifecycle / preparation |
| --- | --- |
| standalone repository | `PackageBaseSet` |
| registered repository | `OnlyIfUpdated` preparation後の`PackageBaseSet` |
| sync repository | `SingularCompatibility` + existing `--needed` |
| registered AUR | `SingularCompatibility` + existing provider / split guard |

official repositoryのrequested child / PackageBase authorityはstrict libalpm exact snapshotである。confirmed `NotFound`だけがAUR fallbackを許し、metadata query / config / malformed identityは停止する。standalone / registered repository routeはsingle / multiple outputに関係なくrequested `Explicit` childだけをinstallし、sibling / debug artifactはunselectedかつnot installedとして保持する。sync / registered AUR routeの既存capabilityを、このSet対応から推測して拡張しない。

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

constraint resultはcandidateのfilter、sort、番号、default、recommend、auto-selection、choice reuseを変更しない。`Unsatisfied` / `Unknown`はprompt上のpresentation-only warningであり、`Invalid` / `Conflicting`だけをprompt前にfail-closedとする。constraintによるrepository / AUR / local source fallbackは行わない。provider metadata refresh後はcurrent matching capabilityで再評価し、古いprovided version / resultを再利用しない。

interactive candidate listには、read-only local package databaseにcandidateの`package_name`と同名packageがある場合だけlocalizedな`[installed]`を末尾へ付ける。authoritativeなabsenceはsuffixなし、configuration / local DB / query / malformed metadata failureはlocalizedな`[installed state unknown]`と別warningで表示する。これはname-only observationであり、source provenance、PackageBase、version / constraint、install reasonを証明しない。state表示はcandidate identity、順序、番号、選択、choice reuse、BuildPlan、routingを変更せず、non-TTY、`--noconfirm`、candidate数1以下、reuse、cancelled dependencyではlookupを開始しない。

<a id="compat-root-package-selection"></a>
## Root package selection compatibility

正式入口は`moguet -S --select [--needed] <query>`であり、`-Ss`は非対話search / presentationのままである。repository / AUR candidateはsource identityを保持し、同名packageでもsourceが違えば別候補とする。official searchはread-only libalpm metadata、AUR searchはtyped AUR responseをauthorityとし、pacmanのhuman-readable search outputをparseしない。

interactive stdinで番号、複数番号、inclusive range、表示済みofficial groupの`@group` selectorを扱う。empty、cancel、EOF、non-TTY、`--noconfirm`はnon-zeroで停止し、invalid lineはatomically retryする。selection、identity validation、全static preflightが終わるまでpacman、sudo、clone、build、install、cache / workspace mutationを開始しない。selected repository rootとAUR rootは明示routeへprojectし、package nameからsourceを再推定しない。詳細は[root package selection contract](contracts/root-package-selection.md)を参照する。

<a id="compat-local-pkgbuild"></a>
## Local PKGBUILD compatibility（production接続済み）

正式入口は`moguet build --local <directory> [V=K...]`であり、`build <pkg>`はremote package routeとして維持する。local PKGBUILD routeはproduction CLIへ接続済みである。local directory、root `PKGBUILD`、`.SRCINFO`のfilesystem identity、owner、mode、containmentをdescriptor-firstで検証し、unsafe stateはfail closedとする。local rootをAUR RPCへqueryせず、metadata failureをAUR absenceやempty dependencyへfallbackしない。

safe `.SRCINFO`をread-only authorityの第一候補とし、missing / invalid / known-staleとPKGBUILD evaluationを区別する。`--noedit`はevaluation consentではなく、`--noconfirm`、non-TTY、cancel、EOFはevaluationを自動承認しない。local source treeをreset、clean、overwrite、deleteせず、local rootはExplicit、dependency artifactsはDependencyとして扱い、existing Explicitを降格しない。artifactはPackageBase / required-child contractへ接続する。Issue #271 Slice 2〜5でmetadata、dependency plan、source workspace、artifact / install、public surfaceを揃え、production CLIへ接続済みである。詳細なfilesystem、execution、cleanup contractは[local PKGBUILD contract](contracts/local-pkgbuild.md)を参照する。

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

- `--edit` / `--noedit`: PKGBUILD / detected top-level `.install`のinvocation-local editor prompt policyを選ぶ。upstream reviewed-source acceptanceではない。
- `--diff`: previous reviewed revisionからexact targetまでのreviewed source change、またはinitial / rebaseline full reviewのprompt policyを選ぶ。state advancementには別のexplicit acceptanceが必要である。
- `--nodiff`: reviewed source changeのreview / acceptance導線を省略し、reviewed stateを進めない。
- `--rebuild`: build-only makepkgの`-f`へ変換する。
- `--cleanbuild`: build-only makepkgの`-C`へ変換する。
- `--rmdeps`: source-buildでは下記contractに従い拒否し、pacman-onlyでは消費する。
- `--aur` / `--repo`: Moguetのsource selectorであり、pacman / makepkgへ渡さない。
- `--output-dir=DIR`: `-G`だけが消費するoperation-local export parentであり、configやpacman / makepkgへ渡さない。

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
