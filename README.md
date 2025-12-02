# jpacker

A robust, C++20 AUR helper and Pacman wrapper for Arch Linux.

`jpacker` is designed to be a seamless replacement for `pacman` commands while adding powerful AUR support and **Gentoo-like source-based package management** capabilities.

## Features

* **Pacman Wrapper**: Supports standard syntax (`-S`, `-Syu`, `-R`, `-Q`, etc.) and passes unknown commands directly to `pacman`.
* **AUR Support**: Search and install AUR packages with dependency handling (via `makepkg`).
* **Source-Based Optimization**: Mark specific packages to always build from source with custom environment variables (e.g., `CFLAGS="-O3 -march=native"`).
* **Safe & Robust**: Written in C++20 using RAII patterns for resource management (network, directories).
* **Review First**: Prompts to edit/review `PKGBUILD` by default before building.
* **One-Shot Build**: Easily build official repository packages from source for testing optimizations.
* **Logging**: Keeps track of activities in a log file.

## Installation

### Requirements
* `base-devel`
* `git`
* `curl`
* `nlohmann-json`

### Build from source

You can install `jpacker` using `makepkg`:

```bash
git clone [https://gitlab.com/seekerkrt/jpacker.git](https://gitlab.com/seekerkrt/jpacker.git)
cd jpacker
makepkg -si
This will install the binary, configuration files, man page (man jpacker), and bash completion script.

Usage
Basic Operations (Pacman Compatible)
jpacker accepts standard pacman flags.

Bash

# Install packages (Repo or AUR)
jpacker -S firefox google-chrome

# Search packages
jpacker -Ss "visual studio code"

# Remove packages
jpacker -Rns google-chrome

# Update system (Sync database & Upgrade)
jpacker -Syu
Advanced: Source-Based Management
jpacker allows you to manage specific packages as "Source Builds", enabling custom optimization flags (similar to Gentoo's package.env).

1. Mark a package for source build:

Bash

# Add 'neofetch' to source-build list with custom CFLAGS
jpacker add-src neofetch CFLAGS="-O3 -march=native"
This creates a config file at /etc/jpacker/package.build/neofetch.

2. List registered source packages:

Bash

jpacker list-src
3. Edit build configuration:

Bash

jpacker edit-src neofetch
4. Install/Update: When you run -S or upgrade, jpacker will detect the configuration and build it from source (git pull + makepkg) instead of installing the binary.

Bash

# Installs from source with your CFLAGS
jpacker -S neofetch
5. Full System Upgrade: Updates official repos (pacman) and rebuilds all AUR/Source-marked packages.

Bash

jpacker upgrade
6. Revert to Official Binary: If you want to stop building from source and go back to the standard official binary immediately:

Bash

jpacker revert neofetch
This removes the build config and runs sudo pacman -S neofetch.

One-Off Build
Build a package (Official Repo or AUR) from source once, without registering it.

Bash

jpacker build coreutils CFLAGS="-O3 -march=native"
Configuration
The configuration file is located at /etc/jpacker/jpacker.conf.

Ini, TOML

# /etc/jpacker/jpacker.conf

# Skip PKGBUILD review prompt (default: false)
# NOEDIT=true

# Preferred editor (default: $EDITOR or nano)
# EDITOR=vim

# Log file path (default: ~/.cache/jpacker/jpacker.log)
# LOGFILE=~/logs/jpacker.log
You can also skip review temporarily using the command line flag:

Bash

jpacker -S google-chrome --noedit
Logs
By default, logs are stored in ~/.cache/jpacker/jpacker.log.

License
MIT License
