# Moguet

[English](README.md)

<!-- parity:overview -->
## 概要

Moguetは、検証済みsource buildとpackageごとのbuild preferenceを提供する、
Arch Linux向けのpacman-first AUR helperです。package transactionは`pacman`、
package buildは`makepkg`、repository取得は`git`へ委ね、Moguetはplan、review、
artifact validationと各tool間の安全な引き渡しを担います。

MoguetはArch Linux、pacman、AURの公式projectではありません。独立したpackage
manager、既存AUR helperの完全なclone、pacmanやmakepkgの契約を置き換えるtoolでも
ありません。

<!-- parity:name -->
## 名称とidentity

project / brandの正式名称は **Moguet**、読みは **モグエット** です。command、
binary、package、XDG application名は`moguet`、projectが所有するenvironment
variableは`MOGUET_*` prefixを使います。

名称は、日本語の「もぐもぐ」と、小さなものを表す英語の接尾辞`-let`を出発点に
しています。そこから生まれた*Mogulet*を、フランス語で「すずらん」を意味する
*muguet*へ寄せました。小さく慎ましい姿でありながら毒を持つ花の性質を、危険や
曖昧さを警告し、authoritativeな判断ができなければexternal mutation前に停止する
小さなhelperへ重ねています。

Moguetはproject固有の造語です。正式なproject表記は **Moguet**、正式な読みは
**モグエット** です。

<!-- parity:status -->
## Project status

Moguet v2.0.0は、jpacker v1.16.0の実行基盤を土台に、identity、保存先、config、
localization、packagingを移行するbreaking releaseです。localの`moguet` binary、
XDG path、typed TOML config、gettextによる英日CLI surfaceは実装済みです。local
package identity、payload、dependency metadata、documentation、jpacker v1.16.0からの
非破壊transitionをv2 release contractとして確定しました。

Moguet v2.0.1は、採用済みXDG storage契約のうちsource-preference部分を完成させます。
新しいstorage方針の追加ではなく、v2.0.0で欠けた実装の修正です。source-build
preferenceは実行user自身のXDG config contextだけを使い、公開済みv2.0.0のtag、Release、
release noteは歴史的記録のまま変更しません。

Moguet v2.1.0は最新releaseです。source-awareなpackage discoveryとambiguous AUR
dependency providerの対話処理、local `PKGBUILD` buildを拡張し、Arch Linux container
validationも追加しました。利用者から見える変更の全体は
[v2.1.0 release](https://github.com/seekerkrt/moguet/releases/tag/v2.1.0)を参照してください。

canonical repository identityはGitHub上のMoguetで、GitLab mirrorを持ちます。Moguet
packageは`jpacker` command aliasを提供しません。AUR publicationは将来の別判断であり、
この文書はAUR endpointが存在すると断定しません。

Moguet v2.xは公開済みで利用できますが、完成済みの一般向けAUR helperではなく、
development-phaseのproductのままです。basicなpacman wrapper、AUR source build、
update、package別のsource-build preferenceは現在すでに動作しますが、AUR support全体を
構成するdependency solver、provider / conflict / replaces / version constraint対応、
edge case対応といったより広い範囲は段階的に実装中で、UXも成熟途上です。Moguetは既存AUR
helperと同等の自動解決能力・完成度を約束しません。unsupportedまたはambiguousなcaseは、
推測せずfail-closedで停止します。v2.xは、Moguetのsource-aware入口、安全境界、検証基盤を
築く公開開発期です。v3.0.0は、Moguet固有のbuild-profileとPKGBUILD差分workflowが揃う
地点であり、projectは内部的にこれをMoguetの本格的な正式就役と位置付けています。詳細な
計画はrelease roadmap（[issue #344](https://github.com/seekerkrt/moguet/issues/344)）
を参照してください。

<!-- parity:safety -->
## 設計と安全境界

- `moguet`は通常ユーザーで実行します。system package transactionが必要な操作だけ
  `sudo pacman`を呼び、AUR sourceの取得・review・buildをrootでは実行しません。
- package database stateとpackage transactionのauthorityは`pacman` / libalpmです。
  package buildは`makepkg`、AUR repository取得は`git`が所有し、Moguetはこれらを
  再実装しません。
- `deps`と`plan`は調査・表示だけを行い、clone、build、installしません。`fetch`は
  未取得repositoryをcloneし、既存cloneでは`git fetch origin`だけを実行します。
  pull、merge、reset、working tree更新、build、installは行いません。
- dependency edgeはtyped requirement、source-aware candidate、constraint resultを保持します。
  `deps`は`Unsatisfied` / `Unknown`をwarning付きで継続し、`plan`はincompleteとして表示します。
  `Invalid` / `Conflicting`はfail-closedです。`fetch`、build、install、upgrade、local buildは
  `Unsatisfied` / `Unknown`の場合、clone、fetch、source mutation、build、sudo、pacman、transaction
  より前に停止します。
- 複数provider candidateが残る場合、interactive TTYではsource-aware candidateを番号付きで
  表示し、exactly oneの明示選択を要求します。defaultはありません。empty input、`q`、
  `quit`、`cancel`、EOFは選択を取り消し、invalid / out-of-range inputは再入力します。
- interactive provider一覧では、candidateのpackage名と同名のpackageがread-only local
  package databaseにあればlocalizedな`[installed]`を末尾へ付けます。未install candidateには
  suffixを付けず、lookup不能時はwarningと`[installed state unknown]`を表示します。この
  name-only observationはprovenanceやversion compatibilityを証明せず、candidate順、番号、
  明示選択の必要性を変更しません。
- non-TTYと`--noconfirm`ではprovider inputをstdinから読まず、candidateを自動選択しません。
  未選択のambiguous providerはfail-closedで停止します。
- `moguet -S --select <query>`はofficial repositoryとAURからsource-awareなroot
  package candidateを検索します。interactive TTYではpackage番号、複数番号、inclusive range、
  表示済みofficial groupの`@group` selectorを受理します。candidateが1件でもdefaultは
  ありません。empty input、`q`、`quit`、`cancel`、EOFは取消とし、invalid inputは同じ
  candidate一覧に対して再入力します。
- root package discoveryはnon-TTY stdinまたは`--noconfirm`ではcandidate queryもpromptも
  開始しません。selectionとstatic preflightの完了後、selected repository rootをexactな
  `repository/package`の1 transactionとして先に実行し、成功した場合だけselected AUR
  rootを既存source-build lifecycleへ渡します。後続AUR failureは完了済みrepository
  transactionをrollbackしません。
- provider choiceは現在のinvocation内だけで所有します。`deps` / `plan`はselectedと
  ambiguous providerを区別し、selected AUR providerのPackageBaseはfetch / build planへ
  渡し、selected repository providerはofficial `repository/package` dependencyとして
  導入します。`deps --recursive`ではprovided dependencyのうちuser-selected AUR
  providerだけをさらに辿り、unique providerとselected repository providerは終端の
  まま表示します。
- constraint resultはprovider candidateのfilter、sort、番号変更、recommend、default、
  auto-selectを行わず、source fallbackの根拠にもなりません。selected AUR provider metadataを
  refreshした場合はcurrent matching capabilityを再評価し、古いresultを再利用しません。
- registered source phaseはsingular source lifecycleを維持します。candidateがすべて
  official repository由来の場合だけprovider selectionを行い、AUR providerを含む
  candidate setは、そのPackageBaseをこのphaseでscheduleできないためambiguousのまま
  system / source execution前に停止します。
- 未解決dependency、未選択のambiguous provider、cycle、安全に解決できないconflicts /
  replaces、証明できないartifact identityは、対応するmutation前に拒否します。
- `--noconfirm`は対話停止を避ける指定であり、「すべてyes」ではありません。source
  selection、plan、identity、conflict、ownershipのguardを突破しません。
- 複数phaseのupgradeは単一atomic transactionではありません。failure時は後続処理を
  止めますが、完了済みpackage transactionをrollbackしません。install成功後にcleanup
  だけ失敗した場合、packageはinstall済みの可能性があるため、結果を確認せず再試行
  しないでください。`upgrade-all`のprovider selectionはfiltered AUR phaseのclone、build、
  pacman、sudoより前に行いますが、それ以前のphaseは完了済みの場合があります。

詳細なcompatibility / routing契約は
[docs/COMPATIBILITY.md](https://github.com/seekerkrt/moguet/blob/develop/docs/COMPATIBILITY.md)、
採用済み設計判断は
[docs/DECISIONS.md](https://github.com/seekerkrt/moguet/blob/develop/docs/DECISIONS.md)を
参照してください。

<!-- parity:installation -->
## インストール

### Build requirements

- `base-devel`を事前導入したArch build環境。現在の構成packageがC++ toolchain、
  `pkgconf`、GNU gettext development toolを提供します
- `pacman`、`pacman-conf`、libalpm development metadata
- `git`
- `curl`
- `nlohmann-json`
- `tomlplusplus`

development treeは次のようにbuildして確認できます。

```bash
git clone https://github.com/seekerkrt/moguet.git
cd moguet
make
./moguet --help
```

live filesystemへ書き込まないpackaging用dry runは、payloadを一時directoryへstageします。

```bash
stage_dir=$(mktemp -d)
make PREFIX=/usr DESTDIR="$stage_dir" install
find "$stage_dir" -type f -print
```

v2.0.0のpackage名と唯一のexecutableは`moguet`で、`/usr/bin/jpacker`をinstall
しません。payloadはjpacker v1.16.0 packageと重複しないため、metadataには
`jpacker`への`provides`、`conflicts`、`replaces`を意図的に設定しません。manual
migrationとrollbackを検証する間、両packageはcoexistできます。source-preference
storeは共有せず、Moguetはuser所有のXDG config authorityだけを使い、legacy
`/etc/jpacker/package.build/` storeを参照・変更しません。Moguet packageはuser XDG dataも
legacy directoryも作成・所有しません。

package runtime dependencyは`curl`、`git`、`libalpm.so`、`libarchive`、`nano`、
`pacman`、`sudo`です。package metadataへ記録するexactな`makedepends` setは
`nlohmann-json`と`tomlplusplus`です。Arch package buildは`base-devel`の事前導入を
前提とし、現在の構成packageがGNU gettextと`pkgconf`を提供するため、`base-devel`、
`gettext`、`pkgconf`は`makedepends`へ含めません。`git`はruntime dependencyのまま
とし、重複して列挙しません。gettextはcatalog build toolを提供し、runtime binaryは
独立したlibintl dependencyを持ちません。

Moguet v2.0.0にはAUR publicationを含めません。AUR URLを作り上げたり、development
payloadをlive systemへinstallしたりしないでください。installed systemを変更する前に
[v1からv2へのMigration Guide](docs/migration/v1-to-v2.ja.md)を確認してください。

### `PKGBUILD`によるpackage installation

repository rootの`PKGBUILD`は、checkout済みのworking treeではなく公開済みreleaseを
package化します。`pkgver()`はrepository自身の`VERSION` fileからversionを読み取り、
対応する公開済みGit tag（`v<version>`）をbuild sourceとして取得するため、未release
のworking tree commitをpackage化することはありません。

```bash
git clone https://github.com/seekerkrt/moguet.git
cd moguet
makepkg -si
```

`makepkg -si`は、そのtag付きreleaseをbuildし、同じ操作で`pacman -U`によってlive
systemへinstallします。これは、development treeをその場でbuild・確認するだけで
何もinstallしない、上記の`make`や`./moguet --help`とは異なります。この`PKGBUILD`は
repository同梱のpackaging経路であり、AUR submissionではありません。Moguetはまだ
AUR pageを公開していません。

<!-- parity:usage -->
## 基本的な使い方

現在のcommand / option surfaceは`moguet --help`をruntime authorityとします。command
tokenとoption tokenはlocaleによって変わりません。

```bash
# packageのinstall、search、info表示
moguet -S <pkg>
moguet -S --select <query>
moguet -Ss <query>
moguet -Si <pkg>

# pacman-compatible system upgrade
moguet -Syu

# configured source package、installed AUR package、または両方を更新
moguet upgrade
moguet upgrade-aur
moguet upgrade-all

# remote package 1件、またはlocal PKGBUILD root 1件をbuild・install
moguet build <pkg> [V=K...]
moguet build --local <directory> [V=K...]

# buildせずAUR dependencyとbuild orderを調査
moguet deps --recursive <pkg>
moguet plan <pkg>

# build / installせずbuild repositoryを取得
moguet fetch <pkg>

# PackageBase checkoutをexport、またはPKGBUILDだけを表示
moguet -G <pkg>
moguet -Gp <pkg>

# persistent stateを変更せず、対応する全mutating routeを観測
moguet --dry-run -S <pkg>
moguet --dry-run -Syu
moguet --dry-run fetch <pkg>
moguet --dry-run build <pkg>
moguet --dry-run build --local <directory>
moguet --dry-run upgrade
moguet --dry-run upgrade-aur
moguet --dry-run upgrade-all
```

`--dry-run`は、Moguet-owned `-S` install / system-update route、`fetch`、remote / local
`build`、`upgrade`、`upgrade-aur`、`upgrade-all`のglobal observation modifierです。
`deps`、`plan`、`-Ss`、`-Si`、`clean`、generic pacman pass-through routeではpacmanへ
転送せず明示的に拒否します。rendererは`Ready` / `NoOp`を終了code 0、`Blocked`を
non-zeroで報告します。

dry-runはproduction preflightと同じread-only filesystem / network discoveryと、exact
allowlist済みのpacman discovery queryを実行し得ますが、state logやpersistent stateを書かず、
cache、workspace、worktreeを作成せず、Git clone / fetch / checkout mutation、
`makepkg --printsrcinfo`その他のlocal metadata評価、build output、sudo、pacman transactionの
開始・mutation、pacman transaction lock、package install、cleanup mutationへ進みません。そのためlocal
buildでmetadata評価が必要な場合はreadyを推測せず`Blocked`になります。観測結果をapproval
token、execution capability、cached provider choiceとして再利用せず、後のactual invocationは
current stateを再validationします。v2.2.0のsurfaceはhuman-readableだけで、JSONその他の
machine-readable plan schemaは追加しません。

**upgrade commandの選択:** 通常のpackage install、search、system upgradeでは、
pacmanや他のAUR helperと同様に`-S`、`-Ss`、`-Syu`等のpacman-compatible
operationを使用してください。保存済みsource-build preferenceの適用、installed AUR
packageのsource build、またはそれらを組み合わせたMoguet固有のmulti-phase upgradeを
明示的に実行する場合は、対応する`upgrade`、`upgrade-aur`、`upgrade-all`を使用します。
これらは通常の`-Syu`の別名ではありません。

`--aur`は対応する`-S`、`-Ss`、`-Si`をAURへ限定し、`--repo`はofficial binary
repositoryへ限定します。両selectorの併用はexternal commandやAUR queryより前に
失敗します。pacman-only routeではcompatibleなpacman optionを保持し、source-build
routeで意味を維持できないoptionは黙って無視せず拒否します。

`-S --select <query>`はinteractiveなsource-aware discovery形式です。source selectorを
指定しなければofficial repositoryとAURの両方を検索し、`--aur`または`--repo`でcandidate
sourceを限定します。両selected routeで同じ意味を持つoptionは`--needed`だけです。
non-TTYと`--noconfirm`ではqueryやpackage選択を行わず失敗します。

source-build preferenceは`add-src`、`edit-src`、`list-src`、`del-src`、`revert`で
管理します。一時的な`build <pkg> [V=K...]`はremote packageを解決し、preferenceを
保存しません。`build --local <directory> [V=K...]`は、代わりにuser所有directoryを
exactly oneのlocal PackageBase sourceとして扱います。pathらしいpackage operandからlocal
rootを推測せず、そのrootをAURへqueryしません。

local routeは安全な`.SRCINFO`を変更せずに読みます。metadataがmissing、invalid、または
known-staleの場合、PKGBUILD reviewとdefaultなしの明示同意を終えてから
`makepkg --printsrcinfo`を実行します。non-TTY inputや`--noconfirm`は評価を許可せず停止
します。Moguetはinvocation-owned source snapshotからbuildし、user-owned treeを変更せず、
採用metadataが宣言するvalidかつuniqueな全`pkgname` childをexplicit rootとしてinstall
します。dependency artifactはdependency install reasonを保持し、既にexplicitなinstalled
packageをdependencyへ降格しません。runtime stateを使うpackage-name completion等の高度な
補完はfuture workであり、同梱completionはpublic CLI schemaに限定します。

<!-- parity:configuration -->
## 設定

Moguetはuser所有のoptionalなTOML fileを1つ読みます。

```text
$XDG_CONFIG_HOME/moguet/config.toml
fallback: ~/.config/moguet/config.toml
```

v2.0.0の最小schemaとbuilt-in defaultは次のとおりです。

```toml
schema_version = 1

[review]
pkgbuild = "prompt"
diff = "prompt"

[build]
mode = "normal"
```

値は次の順で合成します。

```text
built-in default -> user config -> CLI override
```

canonical CLI overrideは`--edit` / `--noedit`、`--diff` / `--nodiff`、
`--build-mode=normal|rebuild|clean`です。`--rebuild`と`--cleanbuild`は対応する
build modeのcompatibility aliasです。conflicting overrideはlast-one-winsにせず、
external mutation前に失敗します。

config fileがない状態は正常です。fileが存在する場合は`schema_version = 1`が必須で、
invalid TOML、unknown key、type error、invalid enum、未対応future schema versionは
invocationを停止します。Moguetはfileを自動作成・rewrite・migrationせず、
`/etc/moguet`をsystem-wide config layerとして使用しません。

source-build preferenceは、次のpackage別fileです。

```text
${XDG_CONFIG_HOME:-$HOME/.config}/moguet/source-build.d/<package-name>
```

TOML configではありません。`add-src`、`edit-src`、`list-src`、`del-src`、`revert`と、
build / upgrade側の全readerがこの1つのauthorityを使います。storeまたはentryがない場合
だけを保存済みpreferenceなしとして扱い、invalid name、unsafe entry、permission error、
I/O failureはhard errorです。read / list operationはdirectoryを作成しません。storageを
最初に必要とする`add-src`または`edit-src`だけがmanaged directoryをmode `0700`、entryを
mode `0600`で作成します。package install / reinstall / uninstallはXDG preferenceも
legacy dataもcreate、migrate、removeしません。

<!-- parity:xdg -->
## XDG config・source preference・state・cache

| 責務 | XDG path | fallback |
| --- | --- | --- |
| user config | `$XDG_CONFIG_HOME/moguet/` | `~/.config/moguet/` |
| source-build preference | `$XDG_CONFIG_HOME/moguet/source-build.d/` | `~/.config/moguet/source-build.d/` |
| 永続runtime stateとlog | `$XDG_STATE_HOME/moguet/` | `~/.local/state/moguet/` |
| 再生成可能cache | `$XDG_CACHE_HOME/moguet/` | `~/.cache/moguet/` |

default logはstate directory内の`moguet.log`です。cacheはauthorityではなく再生成可能で、
cache削除によってconfigやpersistent stateを失ってはいけません。directoryはcommandが
必要としたときだけ作成し、help / version表示では作成しません。

source-preference accessでは、`XDG_CONFIG_HOME`がunsetまたはemptyならHOME fallbackを
使います。明示的な`XDG_CONFIG_HOME`はabsoluteで、既存かつownership、type、permissionの
検証に成功する必要があり、Moguetはwrite commandが最初に必要としたときだけ配下のmanaged
`moguet/source-build.d`を作成します。HOME fallbackでは同じsource-preference safety
boundaryが必要なconfig階層を作成できます。relative path等の安全でないXDG pathはfail
closedとします。rootでMoguetを実行した場合はroot自身のXDG contextを使い、`SUDO_USER`等
から別userを推測してそのhomeへ書き込みません。このpath規則はAUR source buildのroot実行を
許可するものではありません。

<!-- parity:localization -->
## Localization

英語textをsource authorityかつbuilt-in fallbackとし、日本語を正式対応します。
Moguetはstandard GNU gettext behaviorによりprocess locale（`LC_ALL`、
`LC_MESSAGES`、`LANG`、`LANGUAGE`）へ従い、`--lang`やTOMLのlanguage settingを
独自実装しません。

英語で再現したい場合は`LC_ALL=C`を使えます。catalogやentryが欠けても、意味が
完結した英語messageへfallbackします。command / option token、package / repository
identity、path、TOML key / enum、machine-readable field、external programの出力は
翻訳しません。

英語manと日本語manはstandard locale-specific layoutへ配置され、適切なlocaleでは
`man moguet`が日本語pageを選び、存在しないlocaleでは英語へfallbackします。

<!-- parity:compatibility -->
## Compatibilityとmigration

Moguetはpacman-firstですが、すべてのsource-build routeで完全なpacman互換を宣言しません。
pacmanだけで完結するoperationはMoguetが消費しないoptionをpass-throughします。AUR /
source-build routeをMoguetが所有する場合は、対応関係を明示したoptionだけを保持し、
意味を維持できないものはmutation前に拒否します。

Moguetは`/etc/jpacker/jpacker.conf`を通常config layerとして読まず、
`/etc/jpacker/package.build/`をsource-preference fallbackとして使用しません。
`/etc/jpacker`を自動copy・merge・rewrite・deleteせず、root-owned legacy dataの移行先userを
推測しません。[English Migration Guide](docs/migration/v1-to-v2.md)または
[日本語Migration Guide](docs/migration/v1-to-v2.ja.md)に従い、v1 stateをbackupし、対象user
ごとに理解したentryだけを手動で再作成してrollback dataを保持してください。

正式かつpackageが提供する唯一のv2 commandは`moguet`で、`jpacker` binary aliasは
ありません。Moguetとjpacker v1.16.0のpackage fileは重複せず、transition / rollback用に
同時installできます。preference storeは共有しませんが、両helperのpackage-mutating
operationを同時実行しないでください。Moguetはlegacy storeを無視して保持し、明示的な
per-user migrationだけがXDG authorityを変更します。

<!-- parity:development -->
## 開発

canonical development repositoryは
[GitHub](https://github.com/seekerkrt/moguet)、backup mirrorは
[GitLab](https://gitlab.com/seekerkrt/moguet)です。Issueとpull requestはGitHubで管理します。

active integration branchは`develop`、stable releaseは`main`です。
[CONTRIBUTING.md](https://github.com/seekerkrt/moguet/blob/develop/CONTRIBUTING.md)、
[docs/DEVELOPMENT.md](https://github.com/seekerkrt/moguet/blob/develop/docs/DEVELOPMENT.md)、
[docs/VERSIONING.md](https://github.com/seekerkrt/moguet/blob/develop/docs/VERSIONING.md)を
参照してください。Moguet v2.xではAUR helper
機能を段階的に追加し、高度なruntime-aware completionと将来のbuild profile systemは
別作業として扱います。

<!-- parity:license -->
## License

Moguet releaseとjpacker v1.15.0からv1.16.0は、`GPL-3.0-or-later`で提供します。
jpacker v1.14.0以前のreleaseはMIT Licenseで提供されました。これらhistorical releaseは
元のlicenseのまま利用でき、Moguet renameによってtag、release、付与済みpermissionは
変わりません。

- GNU GPL version 3全文: [LICENSE](https://github.com/seekerkrt/moguet/blob/develop/LICENSE)
- version境界と配布方針: [docs/LICENSING.md](docs/LICENSING.md)
- link / compile対象とexternal program: [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)
- v1.14.0以前のhistorical MIT本文: [LICENSES/jpacker-MIT-legacy.txt](https://github.com/seekerkrt/moguet/blob/develop/LICENSES/jpacker-MIT-legacy.txt)

Moguetはlibalpmとlibcurlへ直接dynamic linkし、systemのnlohmann-jsonとtoml++ headerを
binaryへcompileします。pacman、pacman-conf、makepkg、git、vercmp、およびnoticeに記載した
他のprogramはprocess boundary越しに実行する別programです。
