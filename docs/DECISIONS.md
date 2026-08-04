# Moguet 設計ポリシー / Design Policy

[日本語](#日本語) | [English](#english)

---

## 日本語

### 文書の位置づけ

この文書は、現在のMoguetへ適用する上位設計ポリシーの詳細な正本である。CLI挙動、provider選択、solverの利用、fallback、自動化、安全境界について新しい判断を行うときは、このポリシーを基準にする。

現在のproject名はMoguetである。Moguet v2.0.0はjpacker v1.16.0の実行基盤を継承するが、current identityはMoguetとし、旧名称はversion、migration、storage等の明示されたlegacy contextだけで使用する。

この文書は、現在の全 CLI 挙動を列挙する互換性仕様でも、未実装機能を実装済みとみなす保証でもない。現在の command routing と個別の互換性契約は [docs/COMPATIBILITY.md](COMPATIBILITY.md) を参照する。個別仕様を追加・変更するときは、この上位ポリシーに沿って、対象となる behavior と検証範囲を別途明示する。

### 1. 一貫性

同じ種類の状態、結果、失敗は、経路や内部実装が違っても同じ境界と規則で扱う。

* package absence、metadata query failure、invalid input、unsupported decision は意味が異なる。これらを単一の bool、empty result、または success へ flatten しない。
* 観測できなかった状態と、観測した結果として存在しない状態を区別する。判断できない状態を「対象なし」や「処理済み」に置き換えない。
* 同じ option や operation は、pacman-only、AUR、source-build など経路が違っても、対応可能な範囲で同じ意味を保つ。
* 経路ごとに同じ意味を安全に保てない場合は、黙って無視したり別の意味へ変換したりせず、未対応であることを示して実行前に停止する。

### 2. 透明性

何を観測し、何を判断し、どの command を実行し、なぜ停止したかを、利用者が追える形にする。

* query や外部 command の failure を package absence、empty result、または正常終了として隠さない。
* partial completion が可能な処理では、成功した対象、失敗した対象、未実行の対象を区別し、diagnostic と exit status に反映する。
* 利用者に影響する主要な外部 command と副作用は、実行前に見える形にする。特に pacman / makepkg に渡す主要 option を隠さない。
* 安全境界で停止した場合は、拒否した判断や不足している情報を示し、利用者が次に確認すべき対象を分かるようにする。

### 3. 既存操作と挙動の尊重

pacman、makepkg、git、および既存 Moguet CLI の操作、責務、挙動は、明確な理由と独立した設計判断なしに変更しない。

* package metadata の内部取得を libalpm などへ移行しても、利用者から見た operation、transaction owner、build owner、判断結果は可能な限り維持する。
* behavior change を refactor、metadata migration、内部 API の置換へ暗黙に混ぜない。変更する場合は、目的、互換性への影響、失敗時の扱い、検証方法を独立して説明する。
* wrapper として扱える operation は、元 command の操作感、引数の意味、主要な副作用、終了状態を可能な限りなぞる。
* この原則は完全互換の宣言ではない。現在未対応の command や edge case を保証するのではなく、差異を意図せず増やさないための判断基準である。

### 4. 任せる部分は任せる

Moguetは、既存componentが所有するpackage-management機能の不完全な再実装を増やさない。各componentのauthoritativeな結果と既存の責務境界を利用し、その上で必要なorchestrationを行う。

* Arch 固有の package metadata、dependency、provider、conflict などは、対応可能な範囲で libalpm を authoritative source とする。
* system package transaction と検証済みsource artifactのinstall transactionは pacman へ任せる。
* source package artifact の build は makepkg へ任せる。
* source repository の取得と更新は git へ任せる。
* Moguet は、orchestration、source-build policy、execution order、安全境界、diagnostic、および user intent の保全を所有する。

重要: 現在採用している libalpm の scope は read-only package metadata に限る。Moguet は libalpm transaction を開始、準備、commit せず、system package transaction の owner は引き続き pacman である。metadata の authority を libalpm へ寄せることは、transaction ownership の移行を意味しない。

### 5. ユーザーが自然に想像する意図

これは、利便性の好みではなく、自動化と安全境界を決める独立した中核原則である。

> Moguet は、command 名、引数、既存 tool の慣習から利用者が自然に想像する目的と結果を尊重する。内部実装の都合や過剰な自動化によって、意外な選択、副作用、fallback、挙動変更を持ち込まない。

user intent は、command 名、指定された target と option、元 tool の慣習、文書化済みの既存挙動から判断する。内部で実装しやすいことや、技術的に自動化できることだけを根拠に拡張しない。

* 最終的な判断基準は、その command を実行した利用者が、自然にどの対象、結果、副作用を期待するかである。
* `--noconfirm` は、対応する経路で確認を省略し非対話で処理するための option であり、「すべてを自動承認する」という意味ではない。未解決依存、provider 選択、conflict、replacement、削除、source selection などの曖昧さや危険を承認する根拠にしない。
* 複数 provider、conflict、replacement、削除、source selection など、利用者の意図が一意に決まらないものを、候補順や内部都合だけで勝手に選ばない。
* authoritative な判断材料を取得できない場合は、failure を absence に置き換えたり推測で補ったりして mutation へ進まず、安全に停止して理由を示す。
* 対象同士が独立し、安全境界を保ったまま処理できる場合は、失敗対象を隔離して残りを継続してよい。その場合は partial failure として、成功、失敗、未実行を区別して示す。
* 「驚きが少ないこと」と「元 command から自然に予想できること」を、compatibility と UX の一部として扱う。

### 6. 責務境界

| Component | 所有する責務 |
| --- | --- |
| libalpm | 対応範囲内の authoritative な Arch package metadata と package relationships。現在は read-only metadata に限り、transaction は所有しない |
| pacman | system package transactionsと検証済みsource artifactのinstall transactions |
| makepkg | source package artifactsのbuild |
| git | source repository retrieval と update |
| Moguet | orchestration、source-build policy、execution order、safety boundaries、diagnostics、user intent の保全 |

Moguet が外部 component を呼び出すための順序、事前条件、停止条件、表示を設計することは orchestration の責務である。ただし、それを理由に各 component の solver、transaction、build、repository operation を独自実装へ置き換えない。

### 7. 判断ルール

新しい自動化、fallback、solver 利用、または behavior change を検討するときは、最低限、次を確認する。

* 既存 command の意味を変えないか。
* command 名、引数、既存 tool の慣習から利用者が自然に予想する対象、結果、副作用と一致するか。
* authoritative component へ任せられる判断や処理を、Moguet 側で再実装していないか。
* failure と absence、および unsupported decision を区別できるか。
* 実行する command、判断理由、副作用、partial completion を説明できるか。
* 判断が曖昧な場合や authoritative な情報を取得できない場合に、mutation より前に安全に停止できるか。
* behavior change を独立した decision として説明し、既存挙動との差分を検証できるか。

これらを満たす説明や検証方法がない場合は、自動化を既定動作へ組み込まない。read-only の観測や plan と、build、install、remove、repository update などの mutation を分け、必要な判断材料を利用者へ示すことを優先する。

### 8. Licenseとthird-party compliance

Moguet releaseとjpacker v1.15.0以降は`GPL-3.0-or-later`で提供する。jpacker v1.14.0以前のreleaseはMIT Licenseのまま維持し、過去のtag、release、permissionを書き換えない。

licenseとnoticeの監査では、同じprogramへ組み込まれるdirect linked / header-compiled componentと、command line・stdin/stdout・exit statusを介するexternal programを分離して扱う。libraryのvendor、static link、binary bundle、新規linked/compiled dependencyを追加する場合は、配布前にlicense、notice、Corresponding Sourceを再監査する。

version boundary、配布policy、component別の詳細は[docs/LICENSING.md](LICENSING.md)をsource of truthとする。

### 9. PackageBase buildとrequired child selectionの分離

AUR source buildでは、PackageBaseをrepository / build / workspace / package transactionの単位、BuildPlanが必要とするpackage childをinstall-selectionの単位とする。この2つのidentityを単一のpackage nameへflattenしない。

* 1 PackageBaseは1つのfresh artifact workspaceで1回buildする。expected outputは`makepkg --packagelist`からordered aggregateとして取得する。
* required childのartifactはfilenameの推測ではなく、build後のpackage metadata identityでexactly one選択する。selected childの順序はBuildPlanのrequired-target orderに従う。
* expectedだがrequiredでないsibling / debug artifactはunselected result dataとして保持する。install input、update target attribution、install outcomeは付与しない。
* selected childrenは1 PackageBaseにつき1回のpacman transactionへ渡す。childごとのdesired install reasonからそのtransactionで表現できるpolicyを作れない場合は、部分的にinstallせずfail closedとする。
* transaction failureはpackageごとのpartial successを証明しないため、child successを推測しない。safeなattempt identityはfailure evidenceとして成功outcomeから分離する。
* workspace cleanupはtransaction成功後に限る。cleanup failureはtransaction failureへflattenせず、すべてのcompleted childの正確なoutcomeとunselected identityを保つpartial successとする。

### 10. separated source-build上の`--rmdeps`はunsupportedとする

#123の旧combined lifecycleでは、`makepkg -sicr`がdependency同期、source artifactのbuild、package install、dependency cleanupを一続きで所有していた。#242ではこの責務を、build-only makepkg、invocation-ownedのfresh `PKGDEST`、検証済みartifactを扱うtyped `pacman -U` install transactionへ分離した。Moguetはjpacker v1.16.0からこのseparated lifecycleを継承しており、今回のinvocationだけが新規導入したmake / check dependencyの集合をauthoritativeに所有していない。

build前後のinstalled package差分だけでは、並行package transaction、pre-existing dependency、Explicit package、install reasonの変化、`base-devel`、およびinvocation外で導入または変更されたpackageを安全に区別できない。`pacman -Qdt`や`pacman -Rns`によるsystem-wideなorphan cleanupは、このoptionの責務でもない。そのためMoguetでは、separated AUR / source-build lifecycle上の`--rmdeps`を正式にunsupportedとする。

このunsupported decisionはsilent ignoreではない。source-build routeでは、cleanup ownershipを証明できないまま削除へ進む代わりに、各callerの既存preflight contractに従ってexternal mutationより前にfailureとする。`makepkg -r`、pacman removal、独自orphan cleanup、automatic rollbackへ変換しない。`--noconfirm`はpackage削除を暗黙に許可せず、このfailureを突破しない。pacman-only routeではMoguet global optionとして消費するが、作用させずpacmanへも転送しない。

将来dependency cleanupを実装する場合は、少なくとも次を満たす設計が必要である。これは実装方法を確定するものではなく、安全にsupportできると判断するための必要条件である。

* authoritativeなbuild前installed package / install reason snapshotを持つこと。
* dependency installation transactionが今回のinvocationに所有されていることを証明できること。
* 今回新規導入されたDependency-reason packageのexact setを特定できること。
* concurrent package transactionを排除または検出できること。
* pre-existing package、Explicit package、`base-devel`、およびinvocation外で導入または変更されたpackageをcleanup対象から保護すること。
* cleanup planを実行前にpreviewし、必要なconfirmationを得ること。
* build、install、cleanupの成功または失敗を別resultとして保持すること。
* cleanup failure後も、すでに成功したpackage installを失敗へflattenしたり無条件に再実行したりしないこと。
* package削除を実systemへ向けずに検証できるstrict stub / isolated testを備えること。

### 11. XDG cache cutoverの安全契約と実装の比例性

#302のMoguet v2 roadmapにおける#305は、XDG準拠のcache rootへの切替とlegacy jpacker cacheの非破壊を当初の中心としていた。一方、対象operationにはpersistent checkout、artifact workspace、rollback、reclone、recursive cleanupが含まれ、利用者所有のfilesystem entryを作成、置換、削除する。

MoguetはOSSであり、maintainer自身が把握する環境だけでなく、未知の利用者と実行環境でも利用され得る。#75のdecision authorityの下で、decision 4 / 6の責務境界とdecision 5 / 7のmutation前停止をこのfilesystem / Git execution境界へ適用し、最小限のpath cutoverより保守的に、現実的に到達可能な脆弱性をfail-closedに扱う設計を採用する。これは安全性を優先した意図的なtrade-offである。

将来implementationを統合、縮小、または置換する場合も、少なくとも次の契約を維持する。

* destructive operationをtrusted root内へ限定する。
* symlinkまたはroot escapeをfollowしない。
* 通常のidentity replacementを検出し、fail-closedとする。
* rollbackはownershipとidentityを証明できるentryだけを対象にする。
* cache cleanupは全targetのpreflightが完了する前に削除を開始しない。
* legacy jpacker cacheを自動的に読み込み、移行、変更、削除しない。
* Git executionは、危険なparent-process routingまたはconfig environmentを暗黙に継承しない。

このdecisionが固定するのは上記の安全契約であり、現在のmodule、type、capability plumbing、trusted Git policy、removal planningを恒久的architectureとして固定するものではない。現在のproject規模に対して、実装、理解、test、将来追従のcostが不釣り合いになる可能性を認識する。実際の保守でその負担が明らかになった場合は、安全契約を維持したまま、より小さく規模に比例したarchitectureへ統合、縮小、または置換してよい。その簡素化はこのdecisionの撤回ではなく、安全性と保守性を両立するための正当な調整である。

### 12. source-build preferenceのXDG authorityとv2.0.1 PATCH例外

#335は、v2.0.0のXDG移行でsource-build preferenceだけがlegacy system storeに残った実装漏れを修正する。canonical authorityは`${XDG_CONFIG_HOME:-$HOME/.config}/moguet/source-build.d/<package-name>`とし、add / edit / list / delete / revertとbuild / upgrade側のreaderを原子的に同じauthorityへ切り替える。unsetまたはemptyな`XDG_CONFIG_HOME`は`$HOME/.config`へfallbackし、明示値はabsoluteかつ安全で既存のbase directoryでなければfail-closedとする。root実行でもroot自身のXDG contextを使い、`SUDO_USER`から別userを推測しない。

readとmissing entryに対するdelete / revertはdirectoryを作らない。最初にstorageを必要とするadd / editだけが安全なcreation boundaryを通り、managed directoryをmode `0700`、entryをmode `0600`で作成する。source preferenceのfilesystem操作はdescriptor基準とし、final symlinkを拒否し、write / renameをatomicに行う。missing store / entryだけをabsenceとして扱い、invalid name、symlink、non-regular file、owner / mode違反、permission、I/O、raceはhard errorとする。listはsnapshot全体を検証してから出力する。filesystem操作から`sudo`を撤去するが、revert後のpacman transactionに必要な`sudo`は維持する。

同じstoreを使うMoguet process同士のconcurrency contractは、directory descriptorへのcooperative flockで固定する。writerはmutation全体で`LOCK_EX`、strict single-entry readとsnapshot / list readerはreadとvalidationの全期間で`LOCK_SH`を保持する。これにより、正常なMoguet readerが別のMoguet writerのinternal temporary / tombstoneを観測しない。crash等でlock ownerが消えた後も残るinternal artifactはskipせず、invalid entryとしてhard errorにする。

storeへaccessできる非協調same-euid processまたはrootが、最終identity checkとpathname syscallの間でentryを敵対的に置換する場合まで完全なrace-free保証は行わない。ただし、Moguetがidentity mismatchやexternal replacementを観測した後は、その正体を証明できないnameをunlink、exchange、restoreしない。cleanup / rollbackを安全に証明できない場合はmanual inspection用artifactを保持し、typed hard errorを返す。Linuxにinode条件付きunlinkがないことを過剰なrecheck chainで模倣しない。

`/etc/jpacker`と`/etc/moguet`はruntimeで作成・参照せず、legacy storeへのfallback、merge、自動copy / rewrite / deleteを行わない。migrationは利用者がMigration Guideに従ってuserごとに手動実行する。package install / reinstall / uninstallはuser XDG directoryを作成・削除せず、canonical entryとlegacy entryの双方を保持する。

通常、config directoryの変更はPATCHに含めない。しかしこれはv2.0.0で承認済みのuser XDG storage contractから漏れた一領域を、その同じauthorityへ揃える限定的なv2.0.1のbug fixとして扱う。新しいstorage移行をPATCHへ許可する一般的precedentにはしない。v2.0.0のtag、GitHub Release、公開済みrelease bodyはhistorical artifactとして変更しない。

### 13. ambiguous providerはinvocation-localな明示選択とする

#272では、pacman-firstのexact package / unique provider優先順位を維持したまま、複数providerが残る場合だけ利用者の明示選択を受け付ける。選択はinteractive TTYの番号入力に限定し、defaultを設けない。non-TTY、`--noconfirm`、取消、EOFでは候補を自動選択せず、mutation可能な経路をfail closedとする。choiceはcanonical dependencyごとにinvocation内だけで共有し、configやcacheへ永続化しない。

selected AUR providerはPackageBase identityをBuildPlanのdependency edgeとbuild / fetch orderへ渡す。selected repository providerはAUR sourceとして扱わず、source checkout / buildより前にexactな`repository/package`を`pacman -S --asdeps --needed` transactionへ渡す。Moguetが所有するのは候補提示、choice保持、順序、preflight、diagnosticであり、dependency transaction自体はpacmanが所有する。

対応phaseの全provider choiceとstatic preflightが完了するまで、このdependency transaction、Git checkout、makepkg、source artifact installを開始しない。transaction failure後はsource mutationへ進まず、すでに完了した別phaseやpackage transactionをrollbackしたとは扱わない。provider choiceの永続化、non-interactive自動選択、root discovery、完全なversion / conflict / replaces solverはこのdecisionへ含めない。

### 14. root package discoveryは`-S --select`でsource-awareな明示選択とする

#217では、通常のpacman-compatible `-Ss`を非対話の検索・表示として維持し、installを伴うroot package discoveryを専用の`moguet -S --select <query>`入口へ分離する。operation省略構文は採用せず、未知のbare tokenをunknown operationとして拒否する既存契約を保つ。`-S`がinstall intent、`--select`がexact targetではなく検索候補から選ぶintentを表し、候補が1件でもdefault選択しない。

repository / AUR candidateとselected rootはpackage名へflattenせず、source kind、package名、repository packageのrepository名、AUR packageのPackageBase、root roleを保持する。同名packageもsource identityが異なれば別候補として提示し、候補順だけでsourceを決めない。official searchとArch package groupはread-only libalpm metadata、AUR searchはtyped AUR responseをauthorityとし、pacmanのhuman-readable search outputやsync database形式をroot candidateへparseしない。

interactive stdinでだけ、番号、複数番号、inclusive range、および表示済みofficial groupを表す`@group` selectorを受け付ける。empty input、`q`、`quit`、`cancel`、EOF、non-TTY、`--noconfirm`では選択せずnon-zeroで停止する。invalidなselection expressionは一部だけを採用せず、同じcandidate snapshotに対して再入力を求める。#272と共有するのはTTY gate、cancel / retry / EOF、no-default、selection-before-mutationのinteraction contractであり、dependency provider固有のexactly-one sessionやchoice cacheをroot selectionへ混ぜない。

selectionと全selected rootのstatic preflightが完了するまで、pacman、sudo、clone、build、install等のexternal mutationを開始しない。selected repository rootはexactな`repository/package`のbinary route、selected AUR rootはPackageBase identityを保持したAUR routeへ明示的にprojectし、Auto routingへpackage名だけを戻してsourceを再推定しない。transactionは引き続きpacman、source buildはmakepkgが所有し、cross-source atomic transactionやrollbackを新設しない。

このdecisionは#217のproduction contractを固定する。#217 Slice 5でpure model、typed adapter、interaction / routing、help / man / completion / localizationを揃え、production CLIの`--select`入口へ接続した。

---

## English

### Status and authority

This document is the detailed canonical source for the high-level design policy applied to the current Moguet project. New decisions about CLI behavior, provider selection, solver use, fallback, automation, and safety boundaries must be evaluated against this policy.

The current project name is Moguet. Moguet v2.0.0 inherits the jpacker v1.16.0 execution base, but Moguet is the current identity; the former name is used only in explicit legacy contexts such as versions, migration, and storage.

This document is not a compatibility specification enumerating all current CLI behavior, and it does not claim that unimplemented features already exist. See [docs/COMPATIBILITY.md](COMPATIBILITY.md) for current command-routing details and specific compatibility contracts. When a specific contract is added or changed, the affected behavior and verification scope must be stated separately in accordance with this policy.

### 1. Consistency

States, results, and failures of the same kind must follow the same boundaries and rules even when they pass through different routes or internal implementations.

* Package absence, metadata query failure, invalid input, and an unsupported decision have different meanings. They must not be flattened into a single boolean, an empty result, or success.
* A state that could not be observed must remain distinct from a state observed to be absent. An undecidable state must not be rewritten as “no target” or “completed.”
* The same option or operation should retain the same meaning, where supported, across pacman-only, AUR, and source-build routes.
* If a route cannot safely preserve that meaning, Moguet must not silently ignore the option or translate it into a different meaning. It must report the unsupported case and stop before execution.

### 2. Transparency

Users must be able to follow what was observed, what was decided, which command will run, and why processing stopped.

* A query or external-command failure must not be hidden as package absence, an empty result, or normal completion.
* When partial completion is possible, successful, failed, and unattempted targets must be distinguished and reflected in diagnostics and exit status.
* Major external commands and side effects that affect users must be visible before execution. In particular, important options passed to pacman or makepkg must not be hidden.
* When a safety boundary stops processing, diagnostics must identify the rejected decision or missing information so users know what to inspect next.

### 3. Preserve existing commands and behavior

The operations, responsibilities, and behavior of pacman, makepkg, git, and the existing Moguet CLI must not change without a clear reason and a separate design decision.

* Moving internal package-metadata access to libalpm or another component should preserve the user-visible operation, transaction owner, build owner, and decision result wherever possible.
* A behavior change must not be hidden inside a refactor, metadata migration, or internal API replacement. Its purpose, compatibility impact, failure handling, and verification method must be explained independently.
* An operation that can remain a wrapper should follow the original command's interaction model, argument meaning, major side effects, and completion status wherever possible.
* This principle is not a declaration of complete compatibility. It does not guarantee commands or edge cases that are currently unsupported; it is a rule against introducing unintended differences.

### 4. Delegate to authoritative components

Moguet must not accumulate incomplete reimplementations of package-management capabilities owned by existing components. It should use their authoritative results and established responsibility boundaries, then provide the necessary orchestration around them.

* Arch-specific package metadata, dependencies, providers, conflicts, and related information use libalpm as the authoritative source where supported.
* System package transactions and validated source-artifact installation transactions remain delegated to pacman.
* Source package artifact builds remain delegated to makepkg.
* Source repository retrieval and updates remain delegated to git.
* Moguet owns orchestration, source-build policy, execution order, safety boundaries, diagnostics, and preservation of user intent.

Important: the currently adopted libalpm scope is limited to read-only package metadata. Moguet does not initiate, prepare, or commit libalpm transactions; pacman remains the owner of system package transactions. Treating libalpm as the metadata authority does not transfer transaction ownership to libalpm.

### 5. Natural user intent

This is an independent core principle for automation and safety boundaries, not merely a preference about convenience.

> Moguet respects the purpose and result that users would naturally expect from a command name, its arguments, and existing tool conventions. Internal implementation convenience or excessive automation must not introduce surprising selections, side effects, fallback, or behavior changes.

User intent is inferred from the command name, explicit targets and options, conventions of the original tool, and documented existing behavior. An implementation must not expand behavior merely because an action is technically automatable or internally convenient.

* The final criterion is which targets, results, and side effects a user running that command would naturally expect.
* `--noconfirm` suppresses confirmation and enables non-interactive handling only within a supported route; it does not mean “approve everything automatically.” It does not authorize ambiguity or risk involving unresolved dependencies, provider selection, conflicts, replacements, removals, or source selection.
* Multiple providers, conflicts, replacements, removals, source selection, and other decisions for which user intent is not unique must not be resolved arbitrarily from candidate order or implementation convenience.
* If authoritative decision inputs cannot be obtained, Moguet must not replace failure with absence or guess its way into mutation. It must stop safely and explain why.
* When targets are independent and can be processed without weakening safety boundaries, a failed target may be isolated while the remaining targets continue. The result must be reported as a partial failure that distinguishes success, failure, and unattempted work.
* “Low surprise” and behavior naturally predictable from the original command are part of compatibility and user experience.

### 6. Responsibility boundary

| Component | Owned responsibility |
| --- | --- |
| libalpm | Authoritative Arch package metadata and package relationships where supported. Its current scope is read-only metadata; it does not own transactions |
| pacman | System package transactions and validated source-artifact installation transactions |
| makepkg | Source package artifact builds |
| git | Source repository retrieval and updates |
| Moguet | Orchestration, source-build policy, execution order, safety boundaries, diagnostics, and preservation of user intent |

Designing the order, preconditions, stop conditions, and presentation around calls to external components is part of Moguet orchestration. It is not a reason to replace each component's solver, transaction, build, or repository operations with a custom implementation.

### 7. Decision rule

Before introducing new automation, fallback, solver use, or a behavior change, verify at least the following:

* Does it preserve the meaning of existing commands?
* Does it match the targets, results, and side effects users would naturally expect from the command name, its arguments, and existing tool conventions?
* Could an authoritative component own the decision or operation instead of Moguet reimplementing it?
* Can it distinguish failure, absence, and an unsupported decision?
* Can it explain the command to run, the reason for the decision, side effects, and any partial completion?
* Can it stop safely before mutation when a decision is ambiguous or authoritative information cannot be obtained?
* Can the behavior change be explained as an independent decision and verified against existing behavior?

If these questions cannot be answered with a clear explanation and verification method, the automation must not become default behavior. Prefer separating read-only observation and planning from mutations such as build, install, removal, and repository update, and expose the decision inputs users need.

### 8. Licensing and third-party compliance

Moguet releases and jpacker v1.15.0 or later are distributed under `GPL-3.0-or-later`. jpacker releases through v1.14.0 remain under the MIT License; their historical tags, releases, and granted permissions are not rewritten.

License and notice audits distinguish direct linked or header-compiled components incorporated into the program from external programs communicating through command-line arguments, stdin/stdout, and exit status. Adding vendored libraries, static links, binary bundles, or a new linked/compiled dependency requires a new license, notice, and Corresponding Source audit before distribution.

[docs/LICENSING.md](LICENSING.md) is the source of truth for the version boundary, distribution policy, and component-level details.

### 9. Separate PackageBase builds from required-child selection

For AUR source builds, a PackageBase is the repository, build, workspace, and package-transaction unit. A package child required by the BuildPlan is the installation-selection unit. These two identities must not be flattened into one package name.

* One PackageBase is built once in one fresh artifact workspace. Ordered expected outputs come from `makepkg --packagelist` as an aggregate.
* A required child's artifact is selected exactly once by post-build package metadata identity, never by guessing from a filename. Selected-child order follows BuildPlan required-target order.
* Expected but unrequired sibling or debug artifacts remain unselected result data. They receive no install input role, update-target attribution, or install outcome.
* Selected children enter one pacman transaction per PackageBase. If the child-specific desired install reasons cannot form a policy representable by that transaction, processing fails closed instead of partially installing them.
* A failed transaction does not prove per-package partial success, so no child success is inferred. Safe attempted identities remain failure evidence separate from successful outcomes.
* Workspace cleanup occurs only after transaction success. Cleanup failure is not flattened into transaction failure: it is partial success retaining every completed child's exact outcome and all unselected identities.

### 10. `--rmdeps` is unsupported on separated source builds

Under the former combined lifecycle from #123, `makepkg -sicr` owned dependency synchronization, source-artifact building, package installation, and dependency cleanup as one continuous operation. #242 separated those responsibilities into build-only makepkg, an invocation-owned fresh `PKGDEST`, and a typed `pacman -U` installation transaction over validated artifacts. Moguet inherits this separated lifecycle from jpacker v1.16.0 and does not authoritatively own the set of make and check dependencies introduced only by the current invocation.

A pre/post installed-package difference alone cannot safely distinguish concurrent package transactions, pre-existing dependencies, Explicit packages, install-reason changes, `base-devel`, or packages introduced or changed outside the invocation. System-wide orphan cleanup through `pacman -Qdt` or `pacman -Rns` is also outside this option's responsibility. Moguet therefore formally treats `--rmdeps` as unsupported on the separated AUR/source-build lifecycle.

This unsupported decision is not silent ignore. On a source-build route, each caller follows its existing preflight contract and fails before external mutation instead of deleting packages whose cleanup ownership is unproven. The option is not translated into `makepkg -r`, pacman removal, custom orphan cleanup, or automatic rollback. `--noconfirm` does not implicitly authorize package removal and cannot bypass this failure. On a pacman-only route, Moguet consumes the global option but gives it no effect and does not forward it to pacman.

Any future dependency-cleanup implementation would need a design that satisfies at least the following conditions. These are necessary conditions for deciding that support is safe, not a commitment to a particular implementation:

* An authoritative pre-build snapshot of installed packages and install reasons.
* Proof that the dependency installation transaction is owned by the current invocation.
* The exact set of Dependency-reason packages newly introduced by this invocation.
* A mechanism that excludes or detects concurrent package transactions.
* Protection for pre-existing packages, Explicit packages, `base-devel`, and packages introduced or changed outside the invocation.
* Preview and required confirmation of the cleanup plan before execution.
* Separate results for build, installation, and cleanup success or failure.
* Preservation of an already successful package installation after cleanup failure, without flattening it into failure or blindly retrying it.
* Strict stubs and isolated tests that verify removal behavior without targeting the real system.

### 11. Safety contract and implementation proportionality for the XDG cache cutover

Under the Moguet v2 roadmap in #302, #305 originally centered on moving the cache root to an XDG-compliant location while leaving the legacy jpacker cache untouched. The affected operations also include persistent checkouts, artifact workspaces, rollback, reclone, and recursive cleanup, all of which create, replace, or remove user-owned filesystem entries.

Moguet is open source and may be used by unknown users in execution environments beyond those known to the maintainer. Under the decision authority in #75, the responsibility boundaries in decisions 4 and 6 and the pre-mutation stop rules in decisions 5 and 7 are applied to this filesystem and Git execution boundary. The adopted design is more conservative than the minimum path cutover and fails closed for realistically reachable vulnerabilities. This is a deliberate safety-first trade-off.

Any future consolidation, reduction, or replacement of the implementation must preserve at least these contracts:

* Destructive operations remain confined to a trusted root.
* Symlinks and root escapes are not followed.
* Ordinary identity replacement is detected and fails closed.
* Rollback targets only entries whose ownership and identity can be proven.
* Cache cleanup does not begin deletion until every target has completed preflight.
* The legacy jpacker cache is never read, migrated, modified, or removed automatically.
* Git execution does not implicitly inherit dangerous parent-process routing or configuration environment.

This decision fixes those safety contracts, not the current modules, types, capability plumbing, trusted Git policy, or removal-planning structure as permanent architecture. Their implementation, comprehension, testing, and future adaptation costs may prove disproportionate to the current project scale. If maintenance demonstrates that burden, the implementation may be consolidated, reduced, or replaced with a smaller architecture proportional to the project, provided the safety contracts remain intact. Such simplification is a legitimate adjustment that balances safety and maintainability, not a reversal of this decision.

### 12. XDG authority for source-build preferences and the v2.0.1 PATCH exception

#335 fixes an implementation omission that left source-build preferences in a legacy system store during the v2.0.0 XDG transition. The canonical authority is `${XDG_CONFIG_HOME:-$HOME/.config}/moguet/source-build.d/<package-name>`, and add, edit, list, delete, revert, and the build/upgrade readers move atomically to that one authority. An unset or empty `XDG_CONFIG_HOME` falls back to `$HOME/.config`; an explicit value fails closed unless it is an absolute, safe, existing base directory. Root execution uses root's own XDG context and never infers another user from `SUDO_USER`.

Reads and deletion/revert of a missing entry do not create directories. Only the first add/edit that needs storage crosses the safe creation boundary, creating managed directories with mode `0700` and entries with mode `0600`. Source-preference filesystem operations are descriptor-based, reject a final symlink, and use atomic write/rename. Only a missing store or entry means absence; an invalid name, symlink, non-regular file, ownership/mode violation, permission or I/O error, or race is a hard error. Listing validates the complete snapshot before output. Filesystem operations no longer use `sudo`, while `sudo` remains for the pacman transaction after revert.

The concurrency contract between Moguet processes using the same store is cooperative directory-descriptor flocking. A writer holds `LOCK_EX` for the complete mutation, while strict single-entry reads and snapshot/list readers hold `LOCK_SH` for their complete read and validation. A normal Moguet reader therefore does not observe another Moguet writer's internal temporary file or tombstone. An internal artifact left after its lock owner exits, for example after a crash, is not skipped and remains a hard invalid-entry error.

Moguet does not promise complete race freedom against a non-cooperating same-euid process or root that can replace an entry between the final identity check and a pathname syscall. However, after Moguet observes an identity mismatch or external replacement, it does not unlink, exchange, or restore a name whose identity it cannot prove. If cleanup or rollback cannot be proven safe, Moguet retains the artifact for manual inspection and returns a typed hard error. It does not add an excessive recheck chain to imitate an inode-conditional unlink operation that Linux does not provide.

Moguet neither creates nor reads `/etc/jpacker` or `/etc/moguet` at runtime, and it does not fall back to, merge, automatically copy, rewrite, or delete the legacy store. Migration is a per-user manual operation governed by the Migration Guide. Package installation, reinstallation, and removal neither create nor delete user XDG directories and preserve both canonical and legacy entries.

Config-directory changes are normally excluded from PATCH releases. This is a narrow v2.0.1 bug-fix exception that completes one omitted part of the user-XDG storage contract already approved for v2.0.0; it is not a general precedent for storage migrations in PATCH releases. The v2.0.0 tag, GitHub Release, and published release body remain unchanged historical artifacts.

### 13. Ambiguous providers require invocation-local explicit selection

#272 preserves the pacman-first priority of exact packages and unique providers and asks for user intent only when multiple providers remain. Selection is limited to numbered input on an interactive TTY and has no default. Non-TTY use, `--noconfirm`, cancellation, and EOF never auto-select a candidate; mutation-capable routes fail closed. A choice is shared by canonical dependency only within the invocation and is not persisted in configuration or cache.

A selected AUR provider contributes its PackageBase identity to the BuildPlan dependency edge and build/fetch order. A selected repository provider is not treated as AUR source: before source checkout or build, its exact `repository/package` target enters a `pacman -S --asdeps --needed` transaction. Moguet owns candidate presentation, retained user choice, ordering, preflight, and diagnostics; pacman continues to own the dependency transaction itself.

The corresponding phase does not begin that dependency transaction, Git checkout, makepkg, or source-artifact installation until all provider choices and static preflight for the phase are complete. A transaction failure stops source mutation and is not reported as rolling back package transactions or earlier phases that already completed. Persisted provider choices, non-interactive auto-selection, root discovery, and complete version/conflict/replaces solvers remain outside this decision.

### 14. Root package discovery uses source-aware explicit selection through `-S --select`

#217 keeps ordinary pacman-compatible `-Ss` as non-interactive search and presentation, and separates root package discovery that may install packages behind the dedicated `moguet -S --select <query>` entry point. Moguet does not adopt operation omission, so an unknown bare token remains an unknown-operation error. `-S` expresses install intent, `--select` expresses selection from search candidates rather than an exact target, and even a single candidate has no default.

Repository and AUR candidates and selected roots are not flattened to package names. They retain the source kind, package name, repository name for repository packages, PackageBase for AUR packages, and the root role. Same-name packages with different source identities remain separate candidates, and candidate order never chooses a source. Read-only libalpm metadata is authoritative for official search and Arch package groups, while typed AUR responses are authoritative for AUR search. Moguet does not parse pacman's human-readable search output or the sync-database file format into root candidates.

Only interactive stdin accepts package numbers, multiple numbers, inclusive ranges, and an `@group` selector for a displayed official group. Empty input, `q`, `quit`, `cancel`, EOF, non-TTY input, and `--noconfirm` make no selection and stop with a non-zero status. An invalid selection expression is rejected atomically and retried against the same candidate snapshot. #217 shares the TTY gate, cancellation/retry/EOF, no-default, and selection-before-mutation interaction contract with #272; it does not mix the dependency-provider-specific exactly-one session or choice cache into root selection.

Pacman, sudo, clone, build, install, and other external mutation do not begin until selection and static preflight for every selected root complete. A selected repository root is projected explicitly to the exact `repository/package` binary route, while a selected AUR root is projected to the AUR route with its PackageBase identity intact. Moguet does not return package names to Auto routing and infer the source again. Pacman continues to own transactions and makepkg continues to own source builds; this decision adds no cross-source atomic transaction or rollback.

This decision fixes the production contract for #217. Slice 5 of #217 completed the pure model, typed adapters, interaction/routing, help, man pages, completion, and localization, and connected the `--select` entry point to the production CLI.
