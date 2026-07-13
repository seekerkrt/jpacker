# jpacker

[日本語](#日本語) | [English](#english)

---

## 日本語

`jpacker` は Arch Linux 向けの C++20 製 `pacman` wrapper です。

日常的な `pacman` のコマンドライン操作をなるべくそのまま使えるようにしつつ、AUR package の build / install と、Gentoo の `package.env` に近い source build preference を追加することを目指しています。

> [!NOTE]
> jpacker は Arch Linux / pacman / AUR の公式ツールではありません。
> pacman / makepkg / AUR の既存の流儀を尊重しながら、日常的なパッケージ操作を補助するためのツールです。

### 開発背景

jpacker は、既存 AUR helper と同じ方向を目指すために作り始めたものではありません。

Arch Linux の優れた `pacman` / `makepkg` の仕組みをそのまま活かしつつ、そこに Gentoo のようなソースコードビルド系ディストリビューションが持つ「自分の環境に合わせてビルドを調整する楽しさ」を少し取り入れたら、面白いツールになるのではないか。そんな発想から作り始めた実験的なプロジェクトです。

まずは小さく AUR サポートを試しながら、しっかりとした `pacman` / `makepkg` wrapper かつ AUR helper として育てつつ、独自機能として source build 管理を少しずつ重ねていくことを目指しています。

作者本人も、完成済みの常用ツールを急いで作るというよりは、Arch Linux のパッケージ管理を題材に、`pacman` / `makepkg` / AUR / source build の境界を学びながら、「こういうものがあったら面白そうだ」という感覚で育てています。

### プロジェクト状態

jpacker は現在も開発中です。

現時点では `pacman` や既存 AUR helper と同じ挙動をすべて提供するものではありません。よく使う happy path は通り始めていますが、AUR support はまだ発展途上であり、未実装の command / option / edge case が残っています。AUR 対応や互換性の改善にあわせて、挙動が変わる可能性もあります。

詳細な互換性目標と command routing の仕様は [docs/COMPATIBILITY.md](docs/COMPATIBILITY.md) を参照してください。

安全のため、重要な package 操作は実行前に内容を確認してください。

### リポジトリ情報

* Canonical repository: [GitHub](https://github.com/seekerkrt/jpacker)
* Backup mirror: [GitLab](https://gitlab.com/seekerkrt/jpacker)
* Issue と pull request は GitHub で管理しています。

### Contributing

Issue と pull request は GitHub で受け付けています。

この project は、今のところ solo developer による個人開発です。OSS project としての外部 contribution 受け入れや pull request 運用にはまだ慣れていない部分がありますが、bug report、提案、改善案は歓迎します。

提案や pull request は、jpacker の方針や保守負担との兼ね合いを見ながら判断します。

返答や確認に時間がかかる場合があります。

### 主な機能

* **Pacman wrapper**: `-S`, `-Syu`, `-R`, `-Q` などの標準的な `pacman` syntax を扱い、jpacker が明示的に処理しない command は `pacman` に渡します。
* **AUR support**: `makepkg` based workflow で AUR package を検索し、build / install します。
* **AUR PKGBUILD export**: AUR PackageBase repository を current directory へ取得し、または root `PKGBUILD` だけを標準出力へ表示できます。
* **Source-based optimization**: 特定 package を常に source build するように登録し、`CFLAGS="-O3 -march=native"` のような custom environment variables を適用できます。
* **Safe and robust implementation**: C++20 で実装し、networking や temporary directory handling などに RAII based resource management を使います。
* **Review-first workflow**: default では build 前に `PKGBUILD` の review / edit を促します。
* **One-off source builds**: official repository package も AUR package も、永続登録なしで一度だけ source build できます。
* **Logging**: 操作ログを log file に記録します。

### インストール方法

#### Requirements

* `base-devel`
* `git`
* `curl`
* `nlohmann-json`

#### Build from source

`makepkg` で `jpacker` を install できます。

```bash
git clone https://github.com/seekerkrt/jpacker.git
cd jpacker
makepkg -si
```

これにより binary、configuration files、man page (`man jpacker`)、Bash / zsh / fish completion scripts が install されます。

補完ファイルは次の場所に install されます:

* Bash: `/usr/share/bash-completion/completions/jpacker`
* zsh: `/usr/share/zsh/site-functions/_jpacker`
* fish: `/usr/share/fish/vendor_completions.d/jpacker.fish`

### 基本的な使い方

`jpacker` 自体は `sudo` や root で起動せず、通常ユーザーで実行してください。`pacman` が必要な操作では、`jpacker` が必要に応じて `sudo pacman` を呼び出します。

`jpacker` は標準的な `pacman` flags を受け付けます。ただし、すべての `pacman` options / flags に対応しているわけではありません。対応範囲は段階的に実装・検証しています。

pacman だけで完結する経路では、jpacker が消費しない pacman-compatible option を pacman へ渡します。一方、`-S` が AUR package または source-build preference のある package へ分岐する場合、makepkg build/install に同じ意味で反映できない pacman option は黙って無視せず、外部コマンドの実行前に unsupported として停止します。必要な場合は official repository target と AUR / source build target を別 invocation に分けてください。

operation 確定後、値を取る pacman option の次の token は、その綴りにかかわらず option value として優先します。option value として消費されていない最初の `--` は end-of-options marker として pacman へ保持し、それ以降の token は opaque operand として扱います。`--noedit`、`--rmdeps`、`--aur`、`--repo` などの jpacker global option は、option value 位置でも `--` 後でもない通常位置でのみ認識します。

`-F` / `-Fl` などの file database query は通常ユーザーの `pacman` へ委譲し、`-Fy` / `-Fyy` / `-F -y` / `-F --refresh` のように refresh を含む場合だけ `sudo pacman` を使います。selector 未指定の `-Ss` に refresh を組み合わせた場合も official repository search 側を `sudo pacman` で実行したあと、通常どおり AUR search を行います。

selector 未指定の `-Si` に refresh を組み合わせる場合、AUR fallback の判定前に official database refresh だけが先行しないよう、target は `core/filesystem` のような repository-qualified form に限定します。unqualified target が 1 件でもあれば pacman / sudo / AUR query の前に停止するため、必要なら refresh と `-Si` を別 invocation に分けてください。refresh なしの通常の `-Si <pkg>` は従来どおり official repository を優先し、見つからなければ AUR metadata を表示します。

#### Package source selection

`-S`、`-Ss`、`-Si` では、jpacker 固有の `--aur` / `--repo` で、この invocation が使う package source を明示的に限定できます。selector を指定しない場合は従来の Auto routing を維持し、official repository package は binary repository、source build preference がある official package は source build、official repository にない package は AUR へ進みます。`-Ss` は repository と AUR の両方を検索し、`-Si` は repository を優先して、見つからない場合だけ AUR へ fallback します。

* `--aur`: root target を AUR package に限定し、official repository へ fallback しません。official repository に同名 package がある場合も AUR を選び、source build preference と official source-build route は使いません。AUR build plan 内の official dependency は、既存の dependency 処理に委ねます。`core/filesystem` のような repository-qualified target は AUR package name として扱わず、失敗します。
* `--repo`: target を official binary repository に限定し、AUR や source build へ fallback しません。source build preference がある package でも、この invocation だけ binary package を選びます。preference file 自体は変更・削除しません。repository-qualified target も指定できます。

同じ selector の重複指定は idempotent として許可します。`--aur` と `--repo` を同時に指定すると、外部コマンドや AUR query の前に失敗します。`--noconfirm` は source の限定や、この conflict を突破しません。selector を使えるのは plain `-S` install、`-Ss` search、`-Si` info だけで、`upgrade` / `-Syu` / `-Qua` や `build` / `fetch` / `deps` / `plan` などでは未対応です。`--aur` と refresh の組み合わせも、official database refresh を AUR-only operation へ混ぜないため拒否します。`--repo` の `-Ss` / `-Si` は refresh を指定でき、従来どおり `sudo pacman` へ委譲します。

```bash
# AUR だけから install / search / info
jpacker -S --aur google-chrome
jpacker -Ss --aur browser
jpacker -Si --aur google-chrome

# official binary repositories だけから install / search / info
jpacker -S --repo firefox
jpacker -Ss --repo browser
jpacker -Si --repo firefox
```

`jpacker` が利用者に影響する主要な外部コマンドを実行する場合は、実行前に対象のコマンドを表示します。

```bash
# Install packages from the official repositories or the AUR
jpacker -S firefox google-chrome

# Search for packages
jpacker -Ss "visual studio code"

# Remove packages
jpacker -Rns google-chrome

# Synchronize package databases and upgrade the system
jpacker -Syu
```

`jpacker -Syu` は pacman 互換の system upgrade として扱われ、登録済み source build preferences の全体走査は行いません。system upgrade 後に `/etc/jpacker/package.build/` の設定を確認し、必要な package を自動で rebuild したい場合は `jpacker upgrade` を使ってください。

`jpacker -Sc` は `sudo pacman -Sc` へ委譲され、pacman cache のみを対象にします。jpacker の build/cache も削除したい場合は `jpacker clean` を使ってください。jpacker の cache directory は、`XDG_CACHE_HOME` が設定されていれば `$XDG_CACHE_HOME/jpacker`、未設定なら `$HOME/.cache/jpacker` です。`jpacker clean` は実行時に解決した jpacker cache directory を表示し、確認後に cached build files を削除します。`jpacker.log` は削除対象から除外されます。

### AUR build repository inspection / `fetch`

`jpacker fetch <pkg>` は、build や install の前に AUR build repository を確認するための安全な retrieval stage です。

```bash
# Clone or fetch the AUR build repositories needed for the package
jpacker fetch spotify
```

未取得の AUR repositories は jpacker cache に clone されます。既存 clone では `git fetch origin` だけを実行し、working tree は更新しません。この command は `git pull`、merge、reset、build、install を行いません。

working tree を進める将来の挙動は `fetch` には含めません。必要な場合は `sync`、`update`、`fetch --update` のような明示的な別 operation として扱うべきです。

### AUR PKGBUILD export / `-G` / `-Gp`

`jpacker -G <pkg>` と `jpacker -Gp <pkg>` は、exactly one AUR package の build files を build/install と切り離して確認する AUR-only operation です。official repository probe、source-build preference、repository fallback は使いません。repository-qualified target や、複数・欠落 target、global/pacman option は AUR RPC や filesystem mutation より前に拒否します。

```bash
# requested package の PackageBase repository 一式を ./<PackageBase> へ export
jpacker -G google-chrome

# PackageBase repository root の PKGBUILD 本文だけを stdout へ表示
jpacker -Gp google-chrome
```

`-G` は command 開始時の current directory 直下へ `./<PackageBase>` を作ります。split package を指定した場合も directory 名は requested package 名ではなく AUR RPC の `PackageBase` です。destination に directory、file、git repository、symlink、dangling symlink、その他の path がすでに存在すれば失敗し、fetch / pull / reset / overwrite / cleanup の対象にしません。`--noconfirm` はこの operation では unsupported であり、既存 path 拒否を突破しません。clone は invocation が所有する temporary directory で行い、expected AUR remote、`.git`、regular non-symlink `PKGBUILD` を確認してから、既存 path を置換しない形で publish します。command 開始時の current directory と temporary directory は directory descriptor と device/inode で固定し、検証・cleanup・publish は descriptor 相対で行います。

`-Gp` は secure temporary checkout から regular non-symlink `PKGBUILD` を descriptor 相対かつ no-follow で読み、成功時の stdout へその bytes だけを出します。diagnostic、command 表示、PackageBase mapping は stderr に分離し、通常成功時と通常の failure 時には persistent checkout を残しません。どちらも dependency repository を取得せず、jpacker internal build cache や既存 cache repository を変更しません。PKGBUILD を評価せず、`.install` / `.SRCINFO` の生成・表示、makepkg、pacman、build、install は行いません。cleanup 前に temporary path の identity mismatch を検出した場合は、その replacement を削除せず fail-closed で停止し、手動確認用の temporary artifact を残し得ます。

### AUR dependency version constraints

`jpacker deps` / `jpacker plan` は、AUR dependency に含まれる `foo>=1.2` のような version constraint を検出し、表示に残します。ただし jpacker v1.x では、pacman / libalpm 相当の完全な version constraint 判定は行いません。

constraint 付き dependency は package name 部分での解決を試みますが、version 条件を満たしているとは表示しません。未検証の constraint は warning または unresolved reason として表示され、build plan 実行時には未解決依存として扱われます。

### AUR conflicts / replaces metadata

`jpacker -Si <aur-pkg>` は AUR RPC の `Conflicts` / `Replaces` を従来どおり package metadata として表示します。`jpacker deps <pkg>` は対象 package の metadata warning を dependency 分類とは別に表示し、`jpacker plan <pkg>` は recursive plan に含まれる各 AUR package の metadata を package 名付きで表示します。この metadata が 1 件でも残る plan は incomplete です。

jpacker v1.x は installed package や official repository package との実際の衝突判定、replacement による install target 選択、package の自動削除・置換を行いません。そのため AUR build/install 経路は clone / fetch / build / makepkg / pacman transaction より前に停止し、`--noconfirm` でも突破しません。`jpacker fetch` は read-only retrieval stage なので、metadata risk を表示したうえで clone または `git fetch origin` を許可します。

### AUR dependency providers

現時点では、dependency に exact package がなく provider 候補がある場合、`jpacker deps` / `jpacker plan` は候補数に応じて分類する方針です。provider が 1 件なら provided dependency として扱いますが、複数ある場合は ambiguous provider として候補を表示し、暗黙に最初の 1 件を選びません。

build / install / fetch 実行系では、ambiguous provider や unresolved dependency が残る plan は実行前に停止します。詳細は [docs/COMPATIBILITY.md](docs/COMPATIBILITY.md) の provider selection policy を参照してください。

### AUR split package install targets

AUR RPC の `PackageBase` は clone / fetch / build repository の単位で、package name は install 対象です。split package では、たとえば `Name=linux-mainline-headers` / `PackageBase=linux-mainline` のように一致しないことがあります。

`jpacker plan <pkg>` は、このような split package target を `Split package install targets` として表示し、install target selection が未実装であるため incomplete plan として扱います。`jpacker -S <pkg>` や `jpacker build <pkg>` の build/install 実行経路では、`PackageBase` と package name が異なる AUR target を clone / build / install 前に停止します。`jpacker fetch <pkg>` は dependency plan の PackageBase 単位で internal cache へ取得します。read-only inspection の `-G` は root PackageBase repository を `./<PackageBase>` へ export し、`-Gp` はその root `PKGBUILD` だけを表示するため、requested name と PackageBase の相違自体では停止しません。

### Source build preferences

`jpacker` は per-package source build preferences を管理できます。これにより、選択した package を source から build し、Gentoo の `package.env` に近い形で custom build flags を適用できます。

#### 1. Source build preference を有効化する

```bash
# Enable source builds for 'fastfetch' with custom CFLAGS
jpacker add-src fastfetch CFLAGS="-O3 -march=native"
```

これにより source build preference file が次の path に作られます。

```text
/etc/jpacker/package.build/fastfetch
```

#### 2. Source build preferences を一覧する

```bash
jpacker list-src
```

#### 3. Source build preference を編集する

```bash
jpacker edit-src fastfetch
```

#### 4. Package を install / update する

package の install / upgrade 時に、`jpacker` は source build preference の有無を確認します。preference がある場合は、prebuilt binary package を install する代わりに、package build files を更新して `makepkg` を実行するなど、source から build します。

```bash
# Install from source using your custom CFLAGS
jpacker -S fastfetch
```

#### 5. Full system upgrade を実行する

official repositories の packages を更新したあと、source build preferences がある packages を確認します。
Preferred source builds は、既存 `.SRCINFO` の version が installed package より新しい場合に rebuild されます。
`jpacker -Syu` は pacman 互換の system upgrade であり、登録済み source build preferences の全体走査は行いません。登録済み source-build 設定を system upgrade 後に自動確認・再ビルドしたい場合は、`jpacker -Syu` ではなく `jpacker upgrade` を使ってください。

```bash
jpacker upgrade
```

`jpacker upgrade` の review 前更新判定では、working tree にある既存 `.SRCINFO` を使います。`.SRCINFO` がない、または version 情報が不完全な場合、review 前に `makepkg --printsrcinfo` は実行せず、対話実行では続行確認を行い、`--noconfirm` または非対話実行では対象 package を skip します。
既存 cache repository の更新では、reset 前に `HEAD..origin/<branch>` の git diff を確認できます。これは現在 cache にある checkout から、取得した remote branch へ進めた場合の変更です。初回 clone では比較元がないため diff prompt は出ず、build 前の review prompt で `PKGBUILD` と作業ツリー直下の `*.install` を確認します。

#### 6. Official binary package に戻す

source build をやめて standard binary package に戻したい場合は、次を実行します。

```bash
jpacker revert fastfetch
```

これは build configuration を削除し、その後に次を実行します。

```bash
sudo pacman -S fastfetch
```

### One-off builds

official repositories または AUR の package を、永続的な source-build package として登録せずに一度だけ source build できます。

```bash
jpacker build coreutils CFLAGS="-O3 -march=native"
```

### Configuration

main configuration file は次の path にあります。

```text
/etc/jpacker/jpacker.conf
```

Example:

```ini
# /etc/jpacker/jpacker.conf

# Skip the PKGBUILD review prompt (default: false)
# NOEDIT=true

# Skip the diff prompt after fetching repository updates (default: false)
# NODIFF=true

# Preferred editor (priority: $EDITOR > this setting > nano)
# Accepts a simple editor command and simple options only.
# Complex shell syntax such as shell expansion, nested quoting, pipes,
# redirects, or command chains is intentionally unsupported for safety.
# Examples:
# EDITOR=nano
# EDITOR=vim
# EDITOR=code --wait

# Log file path (default: ~/.cache/jpacker/jpacker.log)
# LOGFILE=~/logs/jpacker.log
```

command line から一時的に review step を skip することもできます。default では build/install 前に `PKGBUILD` を review でき、作業ツリー直下に `*.install` がある場合は存在を表示して個別に edit できます。これは PKGBUILD を評価して `install=` を解決するものではなく、maintainer install script の見落としを減らすための案内です。

```bash
jpacker -S google-chrome --noedit
```

`--noedit` は `PKGBUILD` / `*.install` の review / edit prompt を skip します。`--nodiff` は既存 cache repository 更新時の git diff prompt を skip します。初回 clone には update diff がないため、`--nodiff` の有無に関わらず diff prompt は出ません。

`--noconfirm` を指定すると、pacman execution と makepkg execution に `--noconfirm` を渡します。jpacker では「全部 yes」ではなく「対話で止まらない」指定として扱う方針です。ただし、unresolved dependencies や cyclic dependencies が残る AUR build plan は `--noconfirm` 指定時でも実行前に停止します。provider selection、split package selection、conflicts / replaces などの未実装判断を自動で進めるものではありません。option pass-through policy の詳細は [docs/COMPATIBILITY.md](docs/COMPATIBILITY.md) を参照してください。

```bash
jpacker --noconfirm -S google-chrome
```

`--needed` は jpacker global option ではなく、pacman-compatible option です。pacman-only 経路では argv を保ったまま pacman へ渡します。対応済みの `-S` install が AUR または source build preference 経路へ進む場合も、jpacker は `--needed` を理由とする build skip 判定を追加せず、最終 package install の要否だけを makepkg / pacman の `--needed` policy に委ねます。AUR と official source build preference で意味は同じです。

そのため `--needed` 自体を理由に、validation、AUR RPC / build plan、provider・split package・conflicts/replaces などの guard、clone/fetch、PKGBUILD / `.install` review、makepkg execution までの経路を省略しません。jpacker は installed version や local artifact を見た独自の build skip 判定を追加せず、既存 artifact の再利用と `--rebuild` の契約は従来どおり維持します。mixed official/AUR invocation では official pacman argv にも `--needed` を保持し、重複指定は pacman argv では保持、makepkg 側では 1 回だけ渡します。

`--rebuild` / `--cleanbuild` は build 方針、`--rmdeps` は makepkg が導入した dependency の cleanup 方針、`--noconfirm` は prompt suppression であり、`--needed` の install-only policy と独立して併用できます。既存の plan / review / safety guard はどの組み合わせでも維持します。target なしの pacman-compatible `jpacker -Syu --needed` はそのまま pacman へ渡し、target 付きの既存対応形で source route が生じる場合は同じ install-only policy を適用します。jpacker 固有の `upgrade --needed` は未対応です。

AUR / source build の build/install は `makepkg -sic` を基本形とします。`--rebuild` を指定すると `-f`、`--cleanbuild` を指定すると `-C`、`--rmdeps` を指定すると `-r` を追加し、`--noconfirm` は makepkg にも渡します。未指定の場合、既存の package artifact や `src/` directory があるときは、必要に応じて default no の確認 prompt で rebuild / cleanbuild を選べます。cleanbuild を有効にし、同じ package directory に既存 package artifact がある場合は、artifact 再利用を避けるため rebuild も有効にします。`--noconfirm` 指定時はこの prompt を出さず、未指定の rebuild / cleanbuild は no 扱いにします。`--noedit` / `--nodiff` / `--rebuild` / `--cleanbuild` / `--rmdeps` は jpacker 固有の option であり、そのまま pacman へは渡しません。

`--rmdeps` は明示 opt-in です。未指定時や `--noconfirm` だけを指定した場合は依存削除を有効にしません。`--rmdeps --noconfirm` を両方指定した場合は、makepkg に `-r` と `--noconfirm` の両方を渡します。削除対象の判断と実行は `makepkg -s/-r` に委ね、jpacker 自身は `pacman -Rns`、`pacman -Qdt`、orphan cleanup を実行しません。この option は pacman-only install には作用せず、pacman にも渡しません。

```bash
jpacker --rebuild --cleanbuild --rmdeps -S google-chrome
```

### Logs

default では logs は次の path に保存されます。

```text
~/.cache/jpacker/jpacker.log
```

### Versioning

この project は `MAJOR.MINOR.PATCH` 形式の version numbers を使いますが、strict Semantic Versioning 準拠ではなく SemVer-like policy として運用します。詳細な versioning policy と compatibility は [docs/VERSIONING.md](docs/VERSIONING.md) を参照してください。

### License

MIT License

---

## English

`jpacker` is a C++20 `pacman` wrapper for Arch Linux with AUR support and source build preference features.

It aims to provide a familiar command-line workflow for everyday `pacman` usage while adding AUR build support and Gentoo-like source build preferences.

> [!NOTE]
> jpacker is not an official Arch Linux, pacman, or AUR tool.
> It aims to assist day-to-day package operations while respecting existing pacman, makepkg, and AUR workflows.

### Motivation

jpacker was not started to follow the same direction as existing AUR helpers.

The project began from a simple idea: Arch Linux already has an excellent `pacman` / `makepkg` ecosystem, and it might be interesting to keep that foundation while adding a small amount of the flexibility found in source-based distributions such as Gentoo — especially the ability to tune builds for your own environment.

jpacker aims to grow step by step into a solid `pacman` / `makepkg` wrapper and AUR helper, while experimenting with source build management as its own additional feature.

The author is not trying to rush this into a finished daily-driver tool. Instead, jpacker is being developed as a project for learning and experimenting with the boundaries between `pacman`, `makepkg`, AUR workflows, and source-based package customization — guided by the feeling that “this might be an interesting tool to have.”

### Project status

jpacker is still under active development.

It does not provide the full behavior surface of `pacman` or existing AUR helpers at this stage. Common happy paths are starting to work, but AUR support is still evolving. Some commands, options, and edge cases are not implemented yet, and behavior may change as AUR support and compatibility are improved.

For detailed compatibility goals and command routing specifications, see [docs/COMPATIBILITY.md](docs/COMPATIBILITY.md).

For safety, important package operations should still be reviewed carefully before execution.

### Repository

* Canonical repository: [GitHub](https://github.com/seekerkrt/jpacker)
* Backup mirror: [GitLab](https://gitlab.com/seekerkrt/jpacker)
* Issues and pull requests are managed on GitHub.

### Contributing

Issues and pull requests are accepted on GitHub.

This project is currently a solo developer project. I am still learning how to handle external contributions and pull request workflows as an OSS project, but bug reports, suggestions, and improvement ideas are welcome.

Suggestions and pull requests will be considered in light of jpacker's direction and maintenance cost.

Responses and reviews may take some time.

### Features

* **Pacman wrapper**: Supports standard `pacman` syntax such as `-S`, `-Syu`, `-R`, and `-Q`, and forwards unknown commands to `pacman`.
* **AUR support**: Search for and build/install AUR packages using workflows based on `makepkg`.
* **AUR PKGBUILD export**: Export an AUR PackageBase repository into the current directory or print only its root `PKGBUILD` to stdout.
* **Source-based optimization**: Mark selected packages to always be built from source with custom environment variables such as `CFLAGS="-O3 -march=native"`.
* **Safe and robust implementation**: Written in C++20 and designed with RAII-based resource management for tasks such as networking and temporary directory handling.
* **Review-first workflow**: Prompts you to review or edit `PKGBUILD` files before building by default.
* **One-off source builds**: Build official repository packages from source for testing or local optimization without permanently registering them.
* **Logging**: Records operations in a log file.

### Installation

#### Requirements

* `base-devel`
* `git`
* `curl`
* `nlohmann-json`

#### Build from source

You can install `jpacker` with `makepkg`:

```bash
git clone https://github.com/seekerkrt/jpacker.git
cd jpacker
makepkg -si
```

This installs the binary, configuration files, man page (`man jpacker`), and Bash / zsh / fish completion scripts.

Completion files are installed to:

* Bash: `/usr/share/bash-completion/completions/jpacker`
* zsh: `/usr/share/zsh/site-functions/_jpacker`
* fish: `/usr/share/fish/vendor_completions.d/jpacker.fish`

### Usage

#### Basic operations

Do not run jpacker itself with sudo or as root. Run it as a normal user. For operations that need pacman, jpacker will invoke sudo pacman when needed.

jpacker accepts standard pacman flags where supported. Not all pacman options / flags are implemented yet; support is added and verified incrementally.

On routes handled entirely by pacman, jpacker forwards pacman-compatible options that it does not consume. If `-S` routes any target to AUR or a source-build preference, however, a pacman option that cannot retain its meaning during makepkg build/install is rejected before any external transaction starts. Split official repository and AUR/source-build targets into separate invocations when such an option is needed.

After the operation is identified, the token following a value-taking pacman option is treated as its value regardless of its spelling. The first `--` not consumed as an option value is preserved as the end-of-options marker, and later tokens are treated as opaque operands. jpacker global options such as `--noedit`, `--rmdeps`, `--aur`, and `--repo` are recognized only in normal option positions, not as pacman option values or after `--`.

Read-only file database queries such as `-F` and `-Fl` are delegated to plain `pacman`. Forms that request a refresh, including `-Fy`, `-Fyy`, `-F -y`, and `-F --refresh`, use `sudo pacman`. When refresh is combined with `-Ss` without a source selector, the official repository search runs through `sudo pacman` before the usual AUR search.

When refresh is combined with `-Si` without a source selector, every target must use a repository-qualified form such as `core/filesystem`. This prevents an official database refresh from running before jpacker discovers that an unqualified target needs AUR fallback. If any target is unqualified, jpacker stops before pacman, sudo, or an AUR query; split refresh and `-Si` into separate invocations when needed. Normal `-Si <pkg>` without refresh keeps the existing repository-first AUR fallback behavior.

#### Package source selection

For `-S`, `-Ss`, and `-Si`, the jpacker-specific `--aur` / `--repo` options explicitly limit the package source used by that invocation. Without a selector, the existing Auto routing is unchanged: official repository packages use the binary repository, official packages with a source build preference use the source-build route, and packages absent from the official repositories use AUR. `-Ss` searches both repositories and AUR, while `-Si` prefers repository information and falls back to AUR only when the package is not found there.

* `--aur`: Limit root targets to AUR, with no official repository fallback. AUR is selected even when an official package has the same name, and source build preferences and the official source-build route are ignored. Official dependencies in the AUR build plan remain handled by the existing dependency path. A repository-qualified target such as `core/filesystem` is not converted into an AUR package name and fails.
* `--repo`: Limit targets to official binary repositories, with no AUR or source-build fallback. For a package with a source build preference, this selects the binary package for this invocation only; the preference file is not modified or removed. Repository-qualified targets are accepted.

Repeating the same selector is idempotent and allowed. Combining `--aur` and `--repo` fails before any external command or AUR query. `--noconfirm` does not bypass source selection or this conflict. Selectors are initially supported only for plain `-S` installs, `-Ss` searches, and `-Si` information queries; they are not supported for `upgrade` / `-Syu` / `-Qua` or jpacker-specific operations such as `build` / `fetch` / `deps` / `plan`. Combining `--aur` with refresh is also rejected so an official database refresh is not mixed into an AUR-only operation. `--repo` searches and information queries may use refresh and retain the existing `sudo pacman` routing.

```bash
# Install / search / show information using only AUR
jpacker -S --aur google-chrome
jpacker -Ss --aur browser
jpacker -Si --aur google-chrome

# Install / search / show information using only official binary repositories
jpacker -S --repo firefox
jpacker -Ss --repo browser
jpacker -Si --repo firefox
```

When jpacker runs major external commands that affect the user, it prints the command before executing it.

```bash
# Install packages from the official repositories or the AUR
jpacker -S firefox google-chrome

# Search for packages
jpacker -Ss "visual studio code"

# Remove packages
jpacker -Rns google-chrome

# Synchronize package databases and upgrade the system
jpacker -Syu
```

`jpacker -Syu` is treated as a pacman-compatible system upgrade and does not scan all registered source-build preferences. Use `jpacker upgrade` instead when you want jpacker to check `/etc/jpacker/package.build/` after the system upgrade and rebuild configured source packages when needed.

`jpacker -Sc` is passed through to `sudo pacman -Sc` and cleans pacman caches only. Use `jpacker clean` when you also want to remove jpacker build/cache files. The jpacker cache directory is `$XDG_CACHE_HOME/jpacker` when `XDG_CACHE_HOME` is set, otherwise `$HOME/.cache/jpacker`. `jpacker clean` shows the resolved jpacker cache directory at runtime and removes cached build files after confirmation. `jpacker.log` is excluded.

### AUR build repository inspection / `fetch`

`jpacker fetch <pkg>` is a safe retrieval stage for inspecting AUR build repositories before building or installing anything.

```bash
# Clone or fetch the AUR build repositories needed for the package
jpacker fetch spotify
```

Missing AUR repositories are cloned into the jpacker cache. Existing cloned repositories run only `git fetch origin`; the working tree is not updated. This command does not run `git pull`, merge, reset, build, or install operations.

Future behavior that advances a working tree is not implemented by `fetch`; it should be handled in a separate issue as an explicit operation such as `sync`, `update`, or `fetch --update`.

### AUR PKGBUILD export / `-G` / `-Gp`

`jpacker -G <pkg>` and `jpacker -Gp <pkg>` are AUR-only operations for inspecting the build files of exactly one AUR package without building or installing it. They do not probe official repositories, consult source-build preferences, or fall back to a repository package. Repository-qualified targets, missing or multiple targets, and global/pacman options are rejected before an AUR request or filesystem mutation.

```bash
# Export the requested package's complete PackageBase repository to ./<PackageBase>
jpacker -G google-chrome

# Print only the PackageBase repository's root PKGBUILD bytes to stdout
jpacker -Gp google-chrome
```

`-G` creates `./<PackageBase>` directly below the current directory fixed at command start. For a split package, the directory uses the AUR RPC `PackageBase`, not the requested package name. It fails if any directory, file, Git repository, symlink, dangling symlink, or other path already exists at the destination, and never fetches, pulls, resets, overwrites, or cleans that path. `--noconfirm` is unsupported for this operation and cannot bypass the existing-path guard. The clone is made in an invocation-owned temporary directory; jpacker validates the expected AUR remote, `.git`, and a regular non-symlink `PKGBUILD` before publishing it without replacing an existing path. The command-start current directory and temporary directory are anchored by directory descriptors plus device/inode identity, and validation, cleanup, and publication are descriptor-relative.

`-Gp` reads a regular non-symlink `PKGBUILD` from a secure temporary checkout with descriptor-relative, no-follow access and writes only those bytes to stdout on success. Diagnostics, command display, and PackageBase mapping go to stderr, and no persistent checkout remains after normal success or ordinary failure. Neither operation retrieves dependency repositories or changes the jpacker internal build cache or an existing cached repository. They do not evaluate PKGBUILD, generate or print `.install` / `.SRCINFO`, invoke makepkg or pacman, build, or install anything. If a temporary-path identity mismatch is detected before cleanup, jpacker fails closed without deleting that replacement and may leave temporary artifacts for manual inspection.

### AUR dependency version constraints

`jpacker deps` / `jpacker plan` detects version constraints in AUR dependencies such as `foo>=1.2` and keeps them visible in output. jpacker v1.x does not implement a complete pacman/libalpm-compatible version constraint solver.

Dependencies with constraints are still resolved by their package name when possible, but jpacker does not report the version condition as satisfied. Unverified constraints are shown as warnings or unresolved reasons, and build plan execution treats them as unresolved dependencies.

### AUR conflicts / replaces metadata

`jpacker -Si <aur-pkg>` continues to show the AUR RPC `Conflicts` / `Replaces` fields as package metadata. `jpacker deps <pkg>` reports metadata warnings for the target separately from dependency classification, while `jpacker plan <pkg>` shows the metadata for every AUR package in the recursive plan. A plan containing any such metadata is incomplete.

jpacker v1.x does not determine whether an installed or official repository package actually conflicts, select install targets through replacements, or remove/replace packages automatically. AUR build/install paths therefore stop before clone, fetch, build, makepkg, or a pacman transaction, and `--noconfirm` does not bypass the guard. Since `jpacker fetch` is a read-only retrieval stage, it reports the metadata risk but still allows clone or `git fetch origin`.

### AUR dependency providers

In jpacker v1.x, when a dependency has no exact package match but has provider candidates, `jpacker deps` / `jpacker plan` classifies it by the number of providers. A single provider may be treated as a provided dependency, but multiple providers are reported as ambiguous provider candidates; jpacker does not implicitly pick the first one.

Build / install / fetch execution stops before running a plan that still has ambiguous providers or unresolved dependencies. See the provider selection policy in [docs/COMPATIBILITY.md](docs/COMPATIBILITY.md) for details.

### AUR split package install targets

In AUR metadata, `PackageBase` is the clone / fetch / build repository unit, while the package name is the install target. For split packages, they may differ, such as `Name=linux-mainline-headers` / `PackageBase=linux-mainline`.

`jpacker plan <pkg>` shows such split package targets under `Split package install targets` and reports the plan as incomplete because install target selection is not implemented. Build/install execution paths such as `jpacker -S <pkg>` and `jpacker build <pkg>` stop before clone / build / install when the requested AUR target has a package name different from its `PackageBase`. `jpacker fetch <pkg>` retrieves the dependency plan by PackageBase into the internal cache. For read-only inspection, `-G` exports only the root PackageBase repository to `./<PackageBase>`, while `-Gp` prints its root `PKGBUILD`; neither stops merely because the requested name and PackageBase differ.

### Source build preferences

`jpacker` can manage per-package source build preferences, allowing you to force selected packages to build from source and apply custom build flags in a way similar to Gentoo's `package.env`.

#### 1. Enable a source-build preference

```bash
# Enable source builds for 'fastfetch' with custom CFLAGS
jpacker add-src fastfetch CFLAGS="-O3 -march=native"
```

This creates a source-build preference file at:

```text
/etc/jpacker/package.build/fastfetch
```

#### 2. List source-build preferences

```bash
jpacker list-src
```

#### 3. Edit a source-build preference

```bash
jpacker edit-src fastfetch
```

#### 4. Install or update the package

When you install or upgrade packages, `jpacker` checks whether a package has a source-build preference. If it does, `jpacker` builds it from source, for example by updating the package build files and running `makepkg`, instead of installing the prebuilt binary package.

```bash
# Install from source using your custom CFLAGS
jpacker -S fastfetch
```

#### 5. Perform a full system upgrade

This updates packages from the official repositories and then checks packages with source-build preferences.
Preferred source builds are rebuilt when the version in the existing `.SRCINFO` is newer than the installed package.
`jpacker -Syu` is a pacman-compatible system upgrade and does not scan all registered source-build preferences. If you want registered source-build settings to be checked automatically after a system upgrade and rebuilt when needed, use `jpacker upgrade` instead of `jpacker -Syu`.

```bash
jpacker upgrade
```

`jpacker upgrade` uses the existing `.SRCINFO` in the working tree for pre-review update checks. If `.SRCINFO` is missing or incomplete, jpacker does not run `makepkg --printsrcinfo` before review; interactive runs ask whether to continue, while `--noconfirm` or non-interactive runs skip the package.
When an existing cache repository is updated, jpacker can show `HEAD..origin/<branch>` before resetting the working tree. This diff means "changes from the currently cached checkout to the fetched remote branch". Initial clones have no previous checkout to compare against, so there is no update diff prompt; the pre-build review prompt covers `PKGBUILD` and any top-level `*.install` files.

#### 6. Revert to the official binary package

If you want to stop building a package from source and immediately switch back to the standard binary package:

```bash
jpacker revert fastfetch
```

This removes the build configuration and then runs:

```bash
sudo pacman -S fastfetch
```

### One-off builds

You can build a package from source once, whether it comes from the official repositories or the AUR, without registering it as a permanent source-build package.

```bash
jpacker build coreutils CFLAGS="-O3 -march=native"
```

### Configuration

The main configuration file is located at:

```text
/etc/jpacker/jpacker.conf
```

Example:

```ini
# /etc/jpacker/jpacker.conf

# Skip the PKGBUILD review prompt (default: false)
# NOEDIT=true

# Skip the diff prompt after fetching repository updates (default: false)
# NODIFF=true

# Preferred editor (priority: $EDITOR > this setting > nano)
# Accepts a simple editor command and simple options only.
# Complex shell syntax such as shell expansion, nested quoting, pipes,
# redirects, or command chains is intentionally unsupported for safety.
# Examples:
# EDITOR=nano
# EDITOR=vim
# EDITOR=code --wait

# Log file path (default: ~/.cache/jpacker/jpacker.log)
# LOGFILE=~/logs/jpacker.log
```

You can also skip the review step temporarily from the command line. By default, jpacker lets you review `PKGBUILD` before build/install and, when top-level `*.install` files exist, shows them and offers to edit each one. This does not evaluate PKGBUILD or resolve `install=`; it is guidance so maintainer install scripts are harder to miss.

```bash
jpacker -S google-chrome --noedit
```

`--noedit` skips the `PKGBUILD` / `*.install` review and edit prompts. `--nodiff` skips the git diff prompt for existing cache repository updates. Initial clones do not have an update diff to show, regardless of `--nodiff`.

`--noconfirm` passes `--noconfirm` to pacman and makepkg execution. jpacker treats it as a request to avoid interactive blocking, not as "yes to everything". It does not bypass unresolved dependency or cyclic dependency checks in AUR build plans, and it does not automatically decide unsupported provider selection, split package selection, conflicts, or replaces cases. See [docs/COMPATIBILITY.md](docs/COMPATIBILITY.md) for the option pass-through policy.

```bash
jpacker --noconfirm -S google-chrome
```

`--needed` is a pacman-compatible option, not a jpacker global option. Pacman-only routes preserve it in the pacman argument vector. When a supported `-S` install uses AUR or an official source-build preference, jpacker does not add a build-skip decision for `--needed`; only final package installation is delegated to makepkg/pacman's `--needed` policy. AUR and official source-build preference routes use the same meaning.

`--needed` itself does not skip validation, AUR RPC and build planning, provider/split-package/conflicts/replaces guards, clone/fetch, PKGBUILD / `.install` review, or the path to makepkg execution. jpacker does not add an installed-version or local-artifact heuristic for skipping builds; existing artifact reuse and `--rebuild` behavior remain unchanged. In a mixed official/AUR invocation, `--needed` remains in the official pacman arguments. Duplicates remain in pacman's ordered arguments but produce one `--needed` on the makepkg side.

`--rebuild` / `--cleanbuild` control the build, `--rmdeps` controls cleanup of dependencies installed by makepkg, and `--noconfirm` suppresses prompts; each remains independent of the install-only `--needed` policy. Existing plan, review, and safety guards remain in force. Target-less pacman-compatible `jpacker -Syu --needed` is passed through to pacman; if an existing supported target-bearing form selects a source route, the same install-only policy applies there. The jpacker-specific `upgrade --needed` remains unsupported.

AUR/source build installation uses `makepkg -sic` as its baseline. `--rebuild` adds `-f`, `--cleanbuild` adds `-C`, `--rmdeps` adds `-r`, and `--noconfirm` is also passed to makepkg. When rebuild/cleanbuild are not specified, jpacker may ask with a default-no prompt before rebuilding an existing package artifact or cleaning an existing `src/` directory. If cleanbuild is enabled and a package artifact exists in the same package directory, jpacker also enables rebuild to avoid reusing that artifact. With `--noconfirm`, these prompts are skipped and unspecified rebuild/cleanbuild choices default to no. `--noedit`, `--nodiff`, `--rebuild`, `--cleanbuild`, and `--rmdeps` are jpacker-specific and are not passed through unchanged to pacman.

`--rmdeps` is explicit opt-in. Omitting it, including when using `--noconfirm` alone, does not enable dependency removal. When both `--rmdeps --noconfirm` are explicit, jpacker passes both `-r` and `--noconfirm` to makepkg. Dependency selection and removal remain makepkg's `-s/-r` responsibility; jpacker does not run its own `pacman -Rns`, `pacman -Qdt`, or orphan cleanup. The option has no effect on pacman-only installs and is not forwarded to pacman.

```bash
jpacker --rebuild --cleanbuild --rmdeps -S google-chrome
```

### Logs

By default, logs are stored at:

```text
~/.cache/jpacker/jpacker.log
```

### Versioning

This project uses MAJOR.MINOR.PATCH version numbers with a SemVer-like policy. For details on versioning policy and compatibility, see [docs/VERSIONING.md](docs/VERSIONING.md).

### License

MIT License
