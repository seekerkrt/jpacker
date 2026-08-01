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

Moguetはproject固有の造語です。
<code>MU<!-- rejected alternate spelling -->GUET</code>や
「ミュ<!-- rejected alternate reading -->ゲ」は別名・略称ではありません。

<!-- parity:status -->
## Project status

Moguet v2.0.0は、jpacker v1.16.0の実行基盤を土台に、identity、保存先、config、
localization、packagingを移行するbreaking releaseです。localの`moguet` binary、
XDG path、typed TOML config、gettextによる英日CLI surfaceは実装済みです。public
documentationとshell completionも最終local identityへ揃いました。packageとexternal
repositoryのcutoverは別のrelease gateです。

現在公開されているrepositoryやpackage endpointは、別途検証するpackaging / release
cutoverが完了するまで旧`jpacker`名を使う場合があります。この文書は、未公開package、
compatibility alias、repository rename、AUR endpointが既に存在すると断定しません。

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
- 未解決dependency、ambiguous provider、cycle、安全に解決できないconflicts /
  replaces、証明できないartifact identityは、対応するmutation前に拒否します。
- `--noconfirm`は対話停止を避ける指定であり、「すべてyes」ではありません。source
  selection、plan、identity、conflict、ownershipのguardを突破しません。
- 複数phaseのupgradeは単一atomic transactionではありません。failure時は後続処理を
  止めますが、完了済みpackage transactionをrollbackしません。install成功後にcleanup
  だけ失敗した場合、packageはinstall済みの可能性があるため、結果を確認せず再試行
  しないでください。

詳細なcompatibility / routing契約は
[docs/COMPATIBILITY.md](docs/COMPATIBILITY.md)、採用済み設計判断は
[docs/DECISIONS.md](docs/DECISIONS.md)を参照してください。

<!-- parity:installation -->
## インストール

### Build requirements

- C++20 compilerと`base-devel`
- `pacman`、`pacman-conf`、libalpm development metadata
- `pkgconf`
- `git`
- `curl`
- `nlohmann-json`
- `tomlplusplus`
- GNU gettext development tools

development treeは次のようにbuildして確認できます。

```bash
git clone https://github.com/seekerkrt/jpacker.git
cd jpacker
make
./moguet --help
```

live filesystemへ書き込まないpackaging用dry runは、payloadを一時directoryへstageします。

```bash
stage_dir=$(mktemp -d)
make PREFIX=/usr DESTDIR="$stage_dir" install
find "$stage_dir" -type f -print
```

v2.0.0のpackage名は`moguet`ですが、最終package metadata、jpackerとのconflict /
coexistence policy、公開download endpointは別途検証します。cutoverの公開前にAUR
package名を推測したり、development payloadを既存jpacker packageへ上書きしたり
しないでください。installed systemを変更する前に
[v1からv2へのMigration Guide](docs/migration/v1-to-v2.ja.md)を確認してください。

<!-- parity:usage -->
## 基本的な使い方

現在のcommand / option surfaceは`moguet --help`をruntime authorityとします。command
tokenとoption tokenはlocaleによって変わりません。

```bash
# packageのinstall、search、info表示
moguet -S <pkg>
moguet -Ss <query>
moguet -Si <pkg>

# pacman-compatible system upgrade
moguet -Syu

# configured source package、installed AUR package、または両方を更新
moguet upgrade
moguet upgrade-aur
moguet upgrade-all

# buildせずAUR dependencyとbuild orderを調査
moguet deps --recursive <pkg>
moguet plan <pkg>

# build / installせずbuild repositoryを取得
moguet fetch <pkg>

# PackageBase checkoutをexport、またはPKGBUILDだけを表示
moguet -G <pkg>
moguet -Gp <pkg>
```

`--aur`は対応する`-S`、`-Ss`、`-Si`をAURへ限定し、`--repo`はofficial binary
repositoryへ限定します。両selectorの併用はexternal commandやAUR queryより前に
失敗します。pacman-only routeではcompatibleなpacman optionを保持し、source-build
routeで意味を維持できないoptionは黙って無視せず拒否します。

source-build preferenceは`add-src`、`edit-src`、`list-src`、`del-src`、`revert`で
管理します。一時的な`build <pkg> [V=K]`はpreferenceを保存しません。runtime stateを
使うpackage-name completion等の高度な補完はfuture workであり、同梱completionは
public CLI schemaに限定します。

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

source-build preferenceは、この実装では`/etc/jpacker/package.build/`に残る独立した
compatibility境界です。これらのfileはTOML configではなく、XDG config directoryへ
copyされません。source preference commandは既存storeを操作するため、v2 config
migrationで保存済みentryが移動したものとして再登録しないでください。

<!-- parity:xdg -->
## XDG config・state・cache

| 責務 | XDG path | fallback |
| --- | --- | --- |
| user config | `$XDG_CONFIG_HOME/moguet/` | `~/.config/moguet/` |
| 永続runtime stateとlog | `$XDG_STATE_HOME/moguet/` | `~/.local/state/moguet/` |
| 再生成可能cache | `$XDG_CACHE_HOME/moguet/` | `~/.cache/moguet/` |

default logはstate directory内の`moguet.log`です。cacheはauthorityではなく再生成可能で、
cache削除によってconfigやpersistent stateを失ってはいけません。directoryはcommandが
必要としたときだけ作成し、help / version表示では作成しません。

relative path等の安全でないXDG pathはfail closedとします。rootでMoguetを実行した
場合はroot自身のXDG contextを使い、`SUDO_USER`等から別userを推測してそのhomeへ
書き込みません。このpath規則はAUR source buildのroot実行を許可するものではありません。

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

Moguet v2.0.0は`/etc/jpacker/jpacker.conf`を通常config layerとして読まず、
`/etc/jpacker`を自動copy・rewrite・deleteしません。root-owned legacy dataの移行先userを
推測しません。[English Migration Guide](docs/migration/v1-to-v2.md)または
[日本語Migration Guide](docs/migration/v1-to-v2.ja.md)に従い、v1 stateをbackupして
manual mappingとrollbackを行ってください。

正式なv2 commandは`moguet`です。packagingが一時的な`jpacker` aliasを提供するか、
2 packageがcoexistできるかはpackaging decisionとして残っているため、公開前にどちらの
behaviorもscriptから仮定しないでください。

<!-- parity:development -->
## 開発

canonical development repositoryは現在
[GitHub](https://github.com/seekerkrt/jpacker)、backup mirrorは
[GitLab](https://gitlab.com/seekerkrt/jpacker)です。最終identity cutoverの検証完了まで、
external nameを意図的に変更していません。Issueとpull requestはGitHubで管理します。

active integration branchは`develop`、stable releaseは`main`です。
[CONTRIBUTING.md](CONTRIBUTING.md)、
[docs/DEVELOPMENT.md](docs/DEVELOPMENT.md)、
[docs/VERSIONING.md](docs/VERSIONING.md)を参照してください。Moguet v2.xではAUR helper
機能を段階的に追加し、高度なruntime-aware completionと将来のbuild profile systemは
別作業として扱います。

<!-- parity:license -->
## License

現在のGPLライセンス開発系列とv1.15.0以降のjpackerは、`GPL-3.0-or-later`で提供します。
v1.14.0以前のreleaseはMIT Licenseで提供されました。これらhistorical releaseは元の
licenseのまま利用でき、Moguet renameによってtag、release、付与済みpermissionは変わりません。

- GNU GPL version 3全文: [LICENSE](LICENSE)
- version境界と配布方針: [docs/LICENSING.md](docs/LICENSING.md)
- link / compile対象とexternal program: [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)
- v1.14.0以前のhistorical MIT本文: [LICENSES/jpacker-MIT-legacy.txt](LICENSES/jpacker-MIT-legacy.txt)

Moguetはlibalpmとlibcurlへ直接dynamic linkし、systemのnlohmann-jsonとtoml++ headerを
binaryへcompileします。pacman、pacman-conf、makepkg、git、vercmp、およびnoticeに記載した
他のprogramはprocess boundary越しに実行する別programです。
