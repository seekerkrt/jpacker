# Moguet

[日本語](README.ja.md)

<!-- parity:overview -->
## Overview

Moguet is a pacman-first AUR helper for Arch Linux with verified source
builds and per-package build preferences. It keeps package transactions with
`pacman`, package builds with `makepkg`, and repository transport with `git`,
while Moguet owns planning, review, artifact validation, and the safe hand-off
between those tools.

Moguet is not an official Arch Linux, pacman, or AUR project. It is not an
independent package manager, a complete clone of another AUR helper, or a
replacement for the contracts of pacman and makepkg.

<!-- parity:name -->
## Name and identity

The project and brand name is **Moguet**, pronounced **モグエット**. The
command, binary, package, and XDG application name is
`moguet`; environment variables owned by the project use the `MOGUET_*`
prefix.

The name starts from the Japanese expression *mogu mogu* (もぐもぐ) and the
English diminutive suffix *-let*. The resulting *Mogulet* was shaped toward
French *muguet*, meaning lily of the valley. The flower is small and modest
but poisonous; that combination reflects a small helper that warns about
danger or ambiguity and stops before external mutation when it cannot make an
authoritative decision.

Moguet is a project-specific coined name. Its formal project spelling is
**Moguet**, and its formal Japanese reading is **モグエット**.

<!-- parity:status -->
## Project status

Moguet v2.0.0 is a breaking identity, storage, configuration, localization,
and packaging transition built on the jpacker v1.16.0 execution base. The
local `moguet` binary, XDG paths, typed TOML configuration, and gettext-based
English/Japanese CLI surface are implemented. The local package identity,
payload, dependency metadata, documentation, and non-destructive transition
from jpacker v1.16.0 form the v2 release contract.

Moguet v2.0.1 completes the source-preference part of that adopted XDG storage
contract. It corrects an implementation omission in v2.0.0 rather than adding
a new storage direction: source-build preferences now use only the executing
user's XDG config context, while the published v2.0.0 tag, Release, and release
notes remain historical records.

The canonical repository identity is Moguet on GitHub, with a GitLab mirror.
The Moguet package does not provide a `jpacker` command alias. AUR publication
is a separate future decision; this document does not claim that an AUR
endpoint exists.

<!-- parity:safety -->
## Design and safety boundaries

- Run `moguet` as a normal user. It invokes `sudo pacman` only for operations
  that require a system package transaction; AUR source retrieval, review, and
  builds do not run as root.
- `pacman` and libalpm remain the authorities for package database state and
  package transactions. `makepkg` builds packages, and `git` retrieves AUR
  repositories. Moguet does not reimplement those tools.
- `deps` and `plan` only inspect and present information. They do not clone,
  build, or install. `fetch` clones missing repositories or runs only
  `git fetch origin` for an existing clone; it does not pull, merge, reset,
  advance the working tree, build, or install.
- Moguet rejects unresolved dependencies, ambiguous providers, cycles,
  conflicts/replacements that it cannot safely resolve, and unprovable
  artifact identities before the corresponding mutation.
- `--noconfirm` avoids interactive blocking. It is not “yes to everything” and
  does not bypass source selection, planning, identity, conflict, or ownership
  guards.
- Multi-phase upgrades are not one atomic transaction. A failure stops later
  work but does not roll back an already completed package transaction. If
  cleanup fails after installation succeeds, inspect the result before
  retrying; the package may already be installed.

The detailed compatibility and routing contract is in
[docs/COMPATIBILITY.md](https://github.com/seekerkrt/moguet/blob/develop/docs/COMPATIBILITY.md),
and adopted design decisions are recorded in
[docs/DECISIONS.md](https://github.com/seekerkrt/moguet/blob/develop/docs/DECISIONS.md).

<!-- parity:installation -->
## Installation

### Build requirements

- An Arch build environment with `base-devel` preinstalled; its current
  members provide the C++ toolchain, `pkgconf`, and GNU gettext development
  tools
- `pacman`, `pacman-conf`, and libalpm development metadata
- `git`
- `curl`
- `nlohmann-json`
- `tomlplusplus`

Build and inspect the development tree with:

```bash
git clone https://github.com/seekerkrt/moguet.git
cd moguet
make
./moguet --help
```

For a packaging-safe dry run, stage the payload outside the live filesystem:

```bash
stage_dir=$(mktemp -d)
make PREFIX=/usr DESTDIR="$stage_dir" install
find "$stage_dir" -type f -print
```

The v2.0.0 package and its only executable are named `moguet`; it does not
install `/usr/bin/jpacker`. Its payload is disjoint from the jpacker v1.16.0
package, so the metadata intentionally declares no `provides`, `conflicts`, or
`replaces` relationship with `jpacker`. The packages may coexist while the
manual migration and rollback are verified. Their source-preference stores are
separate: Moguet uses its user-owned XDG config authority and does not read or
modify the legacy `/etc/jpacker/package.build/` store. The Moguet package
creates or owns neither user XDG data nor the legacy directory.

The package runtime dependencies are `curl`, `git`, `libalpm.so`, `libarchive`,
`nano`, `pacman`, and `sudo`. The exact `makedepends` set recorded by the
package is `nlohmann-json` and `tomlplusplus`. Arch package builds assume
`base-devel` is preinstalled; its current membership supplies GNU gettext and
`pkgconf`, so `base-devel`, `gettext`, and `pkgconf` are not listed in
`makedepends`. `git` remains a runtime dependency and is not duplicated there.
gettext supplies the catalog build tools; the runtime binary has no separate
libintl dependency.

Moguet v2.0.0 does not include AUR publication. Do not invent an AUR URL or
install a development payload on the live system. See the
[v1 to v2 Migration Guide](docs/migration/v1-to-v2.md) before changing an
installed system.

<!-- parity:usage -->
## Basic usage

`moguet --help` is the runtime authority for the current command and option
surface. Command and option tokens never change with the selected locale.

```bash
# Install, search, or inspect packages
moguet -S <pkg>
moguet -Ss <query>
moguet -Si <pkg>

# Pacman-compatible system upgrade
moguet -Syu

# Update configured source packages, installed AUR packages, or both
moguet upgrade
moguet upgrade-aur
moguet upgrade-all

# Inspect AUR dependencies and build order without building
moguet deps --recursive <pkg>
moguet plan <pkg>

# Retrieve build repositories without building or installing
moguet fetch <pkg>

# Export one PackageBase checkout or print only its PKGBUILD
moguet -G <pkg>
moguet -Gp <pkg>
```

`--aur` limits supported `-S`, `-Ss`, and `-Si` forms to AUR. `--repo`
limits them to official binary repositories. Combining the selectors is an
error before an external command or AUR query. Pacman-only routes preserve
compatible pacman options; a source-build route rejects options whose meaning
cannot be preserved instead of silently ignoring them.

Source-build preferences are managed with `add-src`, `edit-src`, `list-src`,
`del-src`, and `revert`. A one-off `build <pkg> [V=K]` does not save a
preference. Runtime-aware package-name completion and more advanced completion
are future work; the shipped completion is limited to the public CLI schema.

<!-- parity:configuration -->
## Configuration

Moguet reads one optional, user-owned TOML file:

```text
$XDG_CONFIG_HOME/moguet/config.toml
fallback: ~/.config/moguet/config.toml
```

The minimal v2.0.0 schema and built-in defaults are:

```toml
schema_version = 1

[review]
pkgbuild = "prompt"
diff = "prompt"

[build]
mode = "normal"
```

Values are composed in this order:

```text
built-in defaults -> user config -> CLI override
```

The canonical CLI overrides are `--edit` / `--noedit`, `--diff` /
`--nodiff`, and `--build-mode=normal|rebuild|clean`. `--rebuild` and
`--cleanbuild` are compatibility aliases for the corresponding build modes.
Conflicting overrides fail before external mutation rather than using
last-one-wins behavior.

A missing config file is normal. An existing file must contain
`schema_version = 1`; invalid TOML, unknown keys, type errors, invalid enum
values, and unsupported future schema versions stop the invocation. Moguet
does not create, rewrite, or migrate this file automatically, and it does not
use `/etc/moguet` as a system-wide configuration layer.

Source-build preferences are separate per-package files under:

```text
${XDG_CONFIG_HOME:-$HOME/.config}/moguet/source-build.d/<package-name>
```

They are not TOML config. `add-src`, `edit-src`, `list-src`, `del-src`,
`revert`, and every build or upgrade reader use this one authority. A missing
store or entry means no saved preference; an invalid name, unsafe entry,
permission error, or I/O failure is a hard error. Read and list operations do
not create directories. Only an `add-src` or `edit-src` that first needs
storage creates the managed directories with mode `0700` and the entry with
mode `0600`. Package install, reinstall, and uninstall do not create,
migrate, or remove either XDG preferences or legacy data.

<!-- parity:xdg -->
## XDG config, source preferences, state, and cache

| Responsibility | XDG path | Fallback |
| --- | --- | --- |
| User configuration | `$XDG_CONFIG_HOME/moguet/` | `~/.config/moguet/` |
| Source-build preferences | `$XDG_CONFIG_HOME/moguet/source-build.d/` | `~/.config/moguet/source-build.d/` |
| Persistent runtime state and log | `$XDG_STATE_HOME/moguet/` | `~/.local/state/moguet/` |
| Reproducible cache | `$XDG_CACHE_HOME/moguet/` | `~/.cache/moguet/` |

The default log is `moguet.log` in the state directory. Cache contents are not
authoritative and may be regenerated; deleting cache must not delete config or
persistent state. Directories are created only when a command needs them.
Help and version output do not create XDG directories.

For source-preference access, an unset or empty `XDG_CONFIG_HOME` uses the HOME
fallback. An explicit `XDG_CONFIG_HOME` must be absolute, already exist, and
pass the ownership, type, and permission checks; Moguet creates only the
managed `moguet/source-build.d` children when a write command first needs
them. With the HOME fallback, the same source-preference safety boundary may
create the required config hierarchy. Relative or otherwise unsafe XDG paths
fail closed. When Moguet is run as root, root's own XDG context is used;
Moguet does not infer a different user from `SUDO_USER` or write into that
user's home. This path rule does not make AUR source builds as root acceptable.

<!-- parity:localization -->
## Localization

English text is the source authority and built-in fallback. Japanese is a
formally supported translation. Moguet follows the process locale through
standard GNU gettext behavior (`LC_ALL`, `LC_MESSAGES`, `LANG`, and
`LANGUAGE`) and does not implement `--lang` or a language setting in TOML.

Use `LC_ALL=C` when an English reproduction is useful. If a catalog or entry
is unavailable, the complete English message remains visible. Command and
option tokens, package and repository identities, paths, TOML keys and enum
values, machine-readable fields, and output produced by external programs are
not translated.

English and Japanese man pages use the standard locale-specific layout, so
`man moguet` selects the Japanese page when appropriate and otherwise falls
back to English.

<!-- parity:compatibility -->
## Compatibility and migration

Moguet is pacman-first, not fully pacman-compatible in every source-build
route. Operations handled entirely by pacman pass through options that Moguet
does not consume. When Moguet takes responsibility for an AUR or source-build
route, it preserves only options with an explicitly defined equivalent and
fails before mutation for the rest.

Moguet does not read `/etc/jpacker/jpacker.conf` as a normal config layer or
use `/etc/jpacker/package.build/` as a source-preference fallback. It does not
automatically copy, merge, rewrite, or delete `/etc/jpacker`. Root-owned legacy
data is not assigned to a user by guesswork. Back up the v1 state and follow
the [English migration guide](docs/migration/v1-to-v2.md) or [Japanese
migration guide](docs/migration/v1-to-v2.ja.md) to recreate understood entries
manually for each target user and to preserve rollback data.

The formal and only packaged v2 command is `moguet`; no `jpacker` binary alias
is supplied. Moguet and jpacker v1.16.0 have disjoint package files and may be
installed together for transition and rollback. Do not run mutating operations
from both helpers concurrently. Their preference stores are not shared. Moguet
ignores and preserves the legacy store; only an explicit per-user migration
changes the XDG authority.

<!-- parity:development -->
## Development

The canonical development repository is
[GitHub](https://github.com/seekerkrt/moguet), with a
[GitLab mirror](https://gitlab.com/seekerkrt/moguet). Issues and pull requests
are managed on GitHub.

The active integration branch is `develop`; stable releases are on `main`.
See
[CONTRIBUTING.md](https://github.com/seekerkrt/moguet/blob/develop/CONTRIBUTING.md),
[docs/DEVELOPMENT.md](https://github.com/seekerkrt/moguet/blob/develop/docs/DEVELOPMENT.md),
and
[docs/VERSIONING.md](https://github.com/seekerkrt/moguet/blob/develop/docs/VERSIONING.md).
Moguet v2.x will add AUR-helper
capabilities incrementally; advanced runtime-aware completion and the later
build-profile system are separate work.

<!-- parity:license -->
## License

Moguet releases and jpacker v1.15.0 through v1.16.0 are distributed under `GPL-3.0-or-later`.
jpacker v1.14.0 and earlier releases were distributed under the MIT License.
Those historical releases remain
available under their original license; their tags, releases, and granted
permissions are unchanged by the Moguet rename.

- GNU GPL version 3 full text: [LICENSE](https://github.com/seekerkrt/moguet/blob/develop/LICENSE)
- Version boundary and distribution policy: [docs/LICENSING.md](docs/LICENSING.md)
- Linked/compiled components and external programs: [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)
- Historical MIT text for v1.14.0 and earlier: [LICENSES/jpacker-MIT-legacy.txt](https://github.com/seekerkrt/moguet/blob/develop/LICENSES/jpacker-MIT-legacy.txt)

Moguet directly and dynamically links libalpm and libcurl and compiles the
system nlohmann-json and toml++ headers into its binary. pacman, pacman-conf,
makepkg, git, vercmp, and the other programs listed in the notices remain
separate programs invoked across a process boundary.
