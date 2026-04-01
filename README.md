# jpacker

A robust, C++20 AUR helper and Pacman wrapper for Arch Linux.

`jpacker` is designed to be a seamless replacement for `pacman` commands while adding powerful AUR support and **Gentoo-like source-based package management** capabilities.

Written in C++20.

## Features

- **Pacman Wrapper**: Supports standard syntax (`-S`, `-Syu`, `-R`, `-Q`, etc.) and passes unknown commands directly to `pacman`.
- **AUR Support**: Search and install AUR packages with dependency handling via `makepkg`.
- **Source-Based Optimization**: Mark specific packages to always build from source with custom environment variables, for example `CFLAGS="-O3 -march=native"`.
- **Safe & Robust**: Written in C++20 using RAII patterns for resource management such as network operations and directory handling.
- **Review First**: Prompts to edit or review `PKGBUILD` by default before building.
- **One-Shot Build**: Easily build official repository packages from source for testing optimizations.
- **Logging**: Keeps track of activities in a log file.

## Installation

### Requirements

- `base-devel`
- `git`
- `curl`
- `nlohmann-json`

### Build from source

You can install `jpacker` using `makepkg`:

```bash
git clone https://gitlab.com/seekerkrt/jpacker.git
cd jpacker
makepkg -si
```

This installs the binary, configuration files, man page (`man jpacker`), and bash completion script.

## Usage

### Basic operations (Pacman-compatible)

`jpacker` accepts standard `pacman` flags.

```bash
# Install packages (repo or AUR)
jpacker -S firefox google-chrome

# Search packages
jpacker -Ss "visual studio code"

# Remove packages
jpacker -Rns google-chrome

# Update system (sync database and upgrade)
jpacker -Syu
```

### Advanced: Source-based management

`jpacker` allows you to manage specific packages as source builds, enabling custom optimization flags similar to Gentoo's `package.env`.

#### 1. Mark a package for source build

```bash
# Add 'neofetch' to the source-build list with custom CFLAGS
jpacker add-src neofetch CFLAGS="-O3 -march=native"
```

This creates a config file at:

```text
/etc/jpacker/package.build/neofetch
```

#### 2. List registered source packages

```bash
jpacker list-src
```

#### 3. Edit build configuration

```bash
jpacker edit-src neofetch
```

#### 4. Install or update

When you run `-S` or perform an upgrade, `jpacker` detects the configuration and builds the package from source (`git pull + makepkg`) instead of installing the binary package.

```bash
# Install from source with your CFLAGS
jpacker -S neofetch
```

#### 5. Full system upgrade

Updates official repositories via `pacman` and rebuilds all AUR and source-marked packages.

```bash
jpacker upgrade
```

#### 6. Revert to the official binary package

If you want to stop building from source and immediately return to the standard official binary:

```bash
jpacker revert neofetch
```

This removes the build config and runs:

```bash
sudo pacman -S neofetch
```

### One-off build

Build a package from source once, whether it is from the official repositories or the AUR, without registering it.

```bash
jpacker build coreutils CFLAGS="-O3 -march=native"
```

## Configuration

The configuration file is located at:

```text
/etc/jpacker/jpacker.conf
```

Example:

```ini
# /etc/jpacker/jpacker.conf

# Skip PKGBUILD review prompt (default: false)
# NOEDIT=true

# Preferred editor (default: $EDITOR or nano)
# EDITOR=vim

# Log file path (default: ~/.cache/jpacker/jpacker.log)
# LOGFILE=~/logs/jpacker.log
```

You can also skip review temporarily using the command-line flag:

```bash
jpacker -S google-chrome --noedit
```

## Logs

By default, logs are stored at:

```text
~/.cache/jpacker/jpacker.log
```

## License

MIT License
