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

### AUR dependency version constraints

`jpacker deps` / `jpacker plan` は、AUR dependency に含まれる `foo>=1.2` のような version constraint を検出し、表示に残します。ただし jpacker v1.x では、pacman / libalpm 相当の完全な version constraint 判定は行いません。

constraint 付き dependency は package name 部分での解決を試みますが、version 条件を満たしているとは表示しません。未検証の constraint は warning または unresolved reason として表示され、build plan 実行時には未解決依存として扱われます。

### AUR dependency providers

現時点では、dependency に exact package がなく provider 候補がある場合、`jpacker deps` / `jpacker plan` は候補数に応じて分類する方針です。provider が 1 件なら provided dependency として扱いますが、複数ある場合は ambiguous provider として候補を表示し、暗黙に最初の 1 件を選びません。

build / install / fetch 実行系では、ambiguous provider や unresolved dependency が残る plan は実行前に停止します。詳細は [docs/COMPATIBILITY.md](docs/COMPATIBILITY.md) の provider selection policy を参照してください。

### AUR split package install targets

AUR RPC の `PackageBase` は clone / fetch / build repository の単位で、package name は install 対象です。split package では、たとえば `Name=linux-mainline-headers` / `PackageBase=linux-mainline` のように一致しないことがあります。

`jpacker plan <pkg>` は、このような split package target を `Split package install targets` として表示し、install target selection が未実装であるため incomplete plan として扱います。`jpacker -S <pkg>` や `jpacker build <pkg>` の build/install 実行経路では、`PackageBase` と package name が異なる AUR target を clone / build / install 前に停止します。`jpacker fetch <pkg>` は PackageBase 単位の取得なので、PackageBase へ解決でき、他の unresolved / ambiguous / cyclic な問題が残らない場合は実行できます。

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
Preferred source builds は、PKGBUILD version が installed package より新しい場合に rebuild されます。
`jpacker -Syu` は pacman 互換の system upgrade であり、登録済み source build preferences の全体走査は行いません。登録済み source-build 設定を system upgrade 後に自動確認・再ビルドしたい場合は、`jpacker -Syu` ではなく `jpacker upgrade` を使ってください。

```bash
jpacker upgrade
```

注意: 現在の upgrade / source update 系の一部では、更新判定用の `.SRCINFO` を得るために `makepkg --printsrcinfo` を実行する場合があり、後続の review prompt より前に PKGBUILD が評価されうる。これは既知の制限で、[#134](https://github.com/seekerkrt/jpacker/issues/134) で `.SRCINFO` 優先の更新判定へ整理する予定です。

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

command line から一時的に review step を skip することもできます。

```bash
jpacker -S google-chrome --noedit
```

`--noconfirm` を指定すると、pacman execution と makepkg execution に `--noconfirm` を渡します。jpacker では「全部 yes」ではなく「対話で止まらない」指定として扱う方針です。ただし、unresolved dependencies や cyclic dependencies が残る AUR build plan は `--noconfirm` 指定時でも実行前に停止します。provider selection、split package selection、conflicts / replaces などの未実装判断を自動で進めるものではありません。option pass-through policy の詳細は [docs/COMPATIBILITY.md](docs/COMPATIBILITY.md) を参照してください。

```bash
jpacker --noconfirm -S google-chrome
```

AUR / source build の build/install 時に、`--rebuild` を指定すると `makepkg -f` 相当、`--cleanbuild` を指定すると `makepkg -C` 相当を渡します。両方を指定した場合は `makepkg -f -C` 相当として扱います。未指定の場合、既存の package artifact や `src/` directory があるときは、必要に応じて default no の確認 prompt で rebuild / cleanbuild を選べます。cleanbuild を有効にし、同じ package directory に既存 package artifact がある場合は、artifact 再利用を避けるため rebuild も有効にします。`--noconfirm` 指定時はこの prompt を出さず、未指定の rebuild / cleanbuild は no 扱いにします。これらは jpacker 固有の option であり、pacman execution や `.SRCINFO` 読み取り用の `makepkg --printsrcinfo` には渡しません。

```bash
jpacker --rebuild --cleanbuild -S google-chrome
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

### AUR dependency version constraints

`jpacker deps` / `jpacker plan` detects version constraints in AUR dependencies such as `foo>=1.2` and keeps them visible in output. jpacker v1.x does not implement a complete pacman/libalpm-compatible version constraint solver.

Dependencies with constraints are still resolved by their package name when possible, but jpacker does not report the version condition as satisfied. Unverified constraints are shown as warnings or unresolved reasons, and build plan execution treats them as unresolved dependencies.

### AUR dependency providers

In jpacker v1.x, when a dependency has no exact package match but has provider candidates, `jpacker deps` / `jpacker plan` classifies it by the number of providers. A single provider may be treated as a provided dependency, but multiple providers are reported as ambiguous provider candidates; jpacker does not implicitly pick the first one.

Build / install / fetch execution stops before running a plan that still has ambiguous providers or unresolved dependencies. See the provider selection policy in [docs/COMPATIBILITY.md](docs/COMPATIBILITY.md) for details.

### AUR split package install targets

In AUR metadata, `PackageBase` is the clone / fetch / build repository unit, while the package name is the install target. For split packages, they may differ, such as `Name=linux-mainline-headers` / `PackageBase=linux-mainline`.

`jpacker plan <pkg>` shows such split package targets under `Split package install targets` and reports the plan as incomplete because install target selection is not implemented. Build/install execution paths such as `jpacker -S <pkg>` and `jpacker build <pkg>` stop before clone / build / install when the requested AUR target has a package name different from its `PackageBase`. `jpacker fetch <pkg>` operates by PackageBase, so it can still run when the package name resolves to a PackageBase and no other unresolved, ambiguous, or cyclic plan issue remains.

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
Preferred source builds are rebuilt when their PKGBUILD version is newer than the installed package.
`jpacker -Syu` is a pacman-compatible system upgrade and does not scan all registered source-build preferences. If you want registered source-build settings to be checked automatically after a system upgrade and rebuilt when needed, use `jpacker upgrade` instead of `jpacker -Syu`.

```bash
jpacker upgrade
```

Note: some upgrade/source-update checks currently use `makepkg --printsrcinfo` to obtain `.SRCINFO`, which may evaluate the PKGBUILD before the later review prompt. This is a known limitation tracked in [#134](https://github.com/seekerkrt/jpacker/issues/134) and will be moved toward `.SRCINFO`-first checks.

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

You can also skip the review step temporarily from the command line:

```bash
jpacker -S google-chrome --noedit
```

`--noconfirm` passes `--noconfirm` to pacman and makepkg execution. jpacker treats it as a request to avoid interactive blocking, not as "yes to everything". It does not bypass unresolved dependency or cyclic dependency checks in AUR build plans, and it does not automatically decide unsupported provider selection, split package selection, conflicts, or replaces cases. See [docs/COMPATIBILITY.md](docs/COMPATIBILITY.md) for the option pass-through policy.

```bash
jpacker --noconfirm -S google-chrome
```

For AUR/source build install execution, `--rebuild` passes the equivalent of `makepkg -f`, and `--cleanbuild` passes the equivalent of `makepkg -C`. When both are specified, jpacker passes the equivalent of `makepkg -f -C`. When they are not specified, jpacker may ask with a default-no prompt before rebuilding an existing package artifact or cleaning an existing `src/` directory. If cleanbuild is enabled and a package artifact exists in the same package directory, jpacker also enables rebuild to avoid reusing that artifact. With `--noconfirm`, these prompts are skipped and unspecified rebuild/cleanbuild choices default to no. These are jpacker-specific options; they are not forwarded to pacman execution or to `makepkg --printsrcinfo` metadata reads.

```bash
jpacker --rebuild --cleanbuild -S google-chrome
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
