# jpacker

`jpacker` is a C++20 `pacman` wrapper for Arch Linux with AUR support and source-based package management features.

It aims to provide a familiar command-line workflow for everyday `pacman` usage while adding AUR build support and Gentoo-like source-based package management features.

## Project status

jpacker is still under active development.

It is not intended to be a complete drop-in replacement for `pacman` or `yay` at this stage. Some commands, options, and edge cases are not implemented yet, and behavior may change as AUR support and compatibility are improved.

For detailed compatibility goals and command routing specifications, see [docs/COMPATIBILITY.md](docs/COMPATIBILITY.md).

For safety, important package operations should still be reviewed carefully before execution.

## Repository

* Canonical repository: [GitHub](https://github.com/seekerkrt/jpacker)
* Backup mirror: [GitLab](https://gitlab.com/seekerkrt/jpacker)
* Issues and pull requests are managed on GitHub.

## Contributing

Issues and pull requests are welcome on GitHub.

This project is maintained by a solo developer, and I am still getting used to managing pull requests and external contributions. Responses may take some time.

## Features

* **Pacman wrapper**: Supports standard `pacman` syntax such as `-S`, `-Syu`, `-R`, and `-Q`, and forwards unknown commands to `pacman`.
* **AUR support**: Search for and build/install AUR packages using workflows based on `makepkg`.
* **Source-based optimization**: Mark selected packages to always be built from source with custom environment variables such as `CFLAGS="-O3 -march=native"`.
* **Safe and robust implementation**: Written in C++20 and designed with RAII-based resource management for tasks such as networking and temporary directory handling.
* **Review-first workflow**: Prompts you to review or edit `PKGBUILD` files before building by default.
* **One-off source builds**: Build official repository packages from source for testing or local optimization without permanently registering them.
* **Logging**: Records operations in a log file.

## Installation

### Requirements

* `base-devel`
* `git`
* `curl`
* `nlohmann-json`

### Build from source

You can install `jpacker` with `makepkg`:

```bash
git clone https://github.com/seekerkrt/jpacker.git
cd jpacker
makepkg -si
```

This installs the binary, configuration files, man page (`man jpacker`), and Bash completion script.

## Usage

### Basic operations

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

### Source-based package management

`jpacker` can manage selected packages as source builds, allowing you to apply custom build flags in a way similar to Gentoo's `package.env`.

#### 1. Mark a package for source builds

```bash
# Add 'fastfetch' to the source-build list with custom CFLAGS
jpacker add-src fastfetch CFLAGS="-O3 -march=native"
```

This creates a configuration file at:

```text
/etc/jpacker/package.build/fastfetch
```

#### 2. List registered source packages

```bash
jpacker list-src
```

#### 3. Edit the build configuration

```bash
jpacker edit-src fastfetch
```

#### 4. Install or update the package

When you install or upgrade packages, `jpacker` checks whether a package has a source-build configuration. If it does, `jpacker` builds it from source, for example by updating the package build files and running `makepkg`, instead of installing the prebuilt binary package.

```bash
# Install from source using your custom CFLAGS
jpacker -S fastfetch
```

#### 5. Perform a full system upgrade

This updates packages from the official repositories and then checks source-marked packages.
Marked packages are rebuilt from source when their PKGBUILD version is newer than the installed package.

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

## Configuration

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

## Logs

By default, logs are stored at:

```text
~/.cache/jpacker/jpacker.log
```

## Versioning

This project follows Semantic Versioning. For details on versioning policy and compatibility, see [docs/VERSIONING.md](docs/VERSIONING.md).

## License

MIT License
