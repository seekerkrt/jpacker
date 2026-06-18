# jpacker

[日本語](#日本語) | [English](#english)

---

## 日本語

`jpacker` は Arch Linux 向けの C++20 製 `pacman` wrapper です。

日常的な `pacman` のコマンドライン操作をなるべくそのまま使えるようにしつつ、AUR package の build / install と、Gentoo の `package.env` に近い source build preference を追加することを目指しています。

### プロジェクト状態

jpacker は現在も開発中です。

現時点では `pacman` や `yay` の完全な drop-in replacement ではありません。よく使う happy path は通り始めていますが、AUR support はまだ発展途上であり、未実装の command / option / edge case が残っています。AUR 対応や互換性の改善にあわせて、挙動が変わる可能性もあります。

詳細な互換性目標と command routing の仕様は [docs/COMPATIBILITY.md](docs/COMPATIBILITY.md) を参照してください。

安全のため、重要な package 操作は実行前に内容を確認してください。

### リポジトリ情報

* Canonical repository: [GitHub](https://github.com/seekerkrt/jpacker)
* Backup mirror: [GitLab](https://gitlab.com/seekerkrt/jpacker)
* Issue と pull request は GitHub で管理しています。

### Contributing

Issue と pull request は GitHub で受け付けています。

この project は solo developer が管理しており、pull request や外部 contribution の運用にもまだ慣れている途中です。返答に時間がかかる場合があります。

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

これにより binary、configuration files、man page (`man jpacker`)、Bash completion script が install されます。

### 基本的な使い方

`jpacker` は標準的な `pacman` flags を受け付けます。

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

### AUR build repository inspection / `fetch`

`jpacker fetch <pkg>` は、build や install の前に AUR build repository を確認するための安全な retrieval stage です。

```bash
# Clone or fetch the AUR build repositories needed for the package
jpacker fetch spotify
```

未取得の AUR repositories は jpacker cache に clone されます。既存 clone では `git fetch origin` だけを実行し、working tree は更新しません。この command は `git pull`、merge、reset、build、install を行いません。

working tree を進める将来の挙動は `fetch` には含めません。必要な場合は `sync`、`update`、`fetch --update` のような明示的な別 operation として扱うべきです。

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

```bash
jpacker upgrade
```

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

# Preferred editor (default: $EDITOR or nano)
# EDITOR=vim

# Log file path (default: ~/.cache/jpacker/jpacker.log)
# LOGFILE=~/logs/jpacker.log
```

command line から一時的に review step を skip することもできます。

```bash
jpacker -S google-chrome --noedit
```

`--noconfirm` を指定すると、pacman execution と makepkg execution に `--noconfirm` を渡します。ただし、unresolved dependencies や cyclic dependencies が残る AUR build plan は `--noconfirm` 指定時でも実行前に停止します。provider selection、split package selection、conflicts / replaces などの未実装判断を自動で進めるものではありません。

```bash
jpacker --noconfirm -S google-chrome
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

### Project status

jpacker is still under active development.

It is not intended to be a complete drop-in replacement for `pacman` or `yay` at this stage. Common happy paths are starting to work, but AUR support is still evolving. Some commands, options, and edge cases are not implemented yet, and behavior may change as AUR support and compatibility are improved.

For detailed compatibility goals and command routing specifications, see [docs/COMPATIBILITY.md](docs/COMPATIBILITY.md).

For safety, important package operations should still be reviewed carefully before execution.

### Repository

* Canonical repository: [GitHub](https://github.com/seekerkrt/jpacker)
* Backup mirror: [GitLab](https://gitlab.com/seekerkrt/jpacker)
* Issues and pull requests are managed on GitHub.

### Contributing

Issues and pull requests are welcome on GitHub.

This project is maintained by a solo developer, and I am still getting used to managing pull requests and external contributions. Responses may take some time.

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

This installs the binary, configuration files, man page (`man jpacker`), and Bash completion script.

### Usage

#### Basic operations

`jpacker` accepts standard `pacman` flags.

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

### AUR build repository inspection / `fetch`

`jpacker fetch <pkg>` is a safe retrieval stage for inspecting AUR build repositories before building or installing anything.

```bash
# Clone or fetch the AUR build repositories needed for the package
jpacker fetch spotify
```

Missing AUR repositories are cloned into the jpacker cache. Existing cloned repositories run only `git fetch origin`; the working tree is not updated. This command does not run `git pull`, merge, reset, build, or install operations.

Future behavior that advances a working tree is not implemented by `fetch`; it should be handled in a separate issue as an explicit operation such as `sync`, `update`, or `fetch --update`.

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

```bash
jpacker upgrade
```

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

# Preferred editor (default: $EDITOR or nano)
# EDITOR=vim

# Log file path (default: ~/.cache/jpacker/jpacker.log)
# LOGFILE=~/logs/jpacker.log
```

You can also skip the review step temporarily from the command line:

```bash
jpacker -S google-chrome --noedit
```

`--noconfirm` passes `--noconfirm` to pacman and makepkg execution. It does not bypass unresolved dependency or cyclic dependency checks in AUR build plans, and it does not automatically decide unsupported provider selection, split package selection, conflicts, or replaces cases.

```bash
jpacker --noconfirm -S google-chrome
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
