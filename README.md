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

Moguet v2.1.0 is the latest release. It adds interactive handling for
source-aware package discovery and ambiguous AUR dependency providers, expands
local `PKGBUILD` builds, and adds Arch Linux container validation coverage. See
the [v2.1.0 release](https://github.com/seekerkrt/moguet/releases/tag/v2.1.0)
for the complete user-visible changes.

The canonical repository identity is Moguet on GitHub, with a GitLab mirror.
The Moguet package does not provide a `jpacker` command alias. AUR publication
is a separate future decision; this document does not claim that an AUR
endpoint exists.

Moguet v2.x is published and usable, but it remains a development-phase
product rather than a finished, general-purpose AUR helper. Basic pacman
wrapping, AUR source builds, updates, and per-package source-build
preferences already work today, while the wider AUR-support surface — a full
dependency solver, provider/conflict/replaces/version-constraint handling,
and edge-case coverage — is still being implemented incrementally and its UX
is still maturing. Moguet does not promise the same automatic-resolution
completeness as established AUR helpers: unsupported or ambiguous cases stop
fail-closed instead of guessing. v2.x is the public development period that
builds Moguet's source-aware entry points, safety boundaries, and validation
infrastructure; v3.0.0 is the point where Moguet-specific build-profile and
PKGBUILD-diff workflows come together, which the project treats internally
as Moguet's full commissioning. See the release roadmap
([issue #344](https://github.com/seekerkrt/moguet/issues/344)) for the
detailed plan.

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
- When multiple provider candidates remain, an interactive TTY lists
  source-aware candidates by number and requires exactly one explicit choice;
  there is no default. Empty input, `q`, `quit`, `cancel`, or EOF cancels the
  choice, while invalid or out-of-range input retries.
- Non-TTY use and `--noconfirm` do not read provider input from stdin or
  auto-select a candidate. Unselected ambiguity fails closed.
- `moguet -S --select <query>` discovers source-aware root package candidates
  from official repositories and AUR. An interactive TTY accepts package
  numbers, multiple numbers, inclusive ranges, and an `@group` selector for a
  displayed official group; there is no default, even for one candidate.
  Empty input, `q`, `quit`, `cancel`, or EOF cancels, and invalid input retries
  against the same candidate list.
- Root package discovery does not query candidates or prompt on non-TTY stdin
  or with `--noconfirm`. After selection and static preflight finish, selected
  repository roots run first in one exact `repository/package` transaction;
  only its success allows selected AUR roots to enter the existing source-build
  lifecycle. A later AUR failure does not roll back the completed repository
  transaction.
- Provider choices belong only to the current invocation. `deps` and `plan`
  distinguish selected and ambiguous providers; a selected AUR provider's
  PackageBase flows into fetch/build planning, while a selected repository
  provider is introduced as an official `repository/package` dependency. In
  `deps --recursive`, among provided dependencies, only a user-selected AUR
  provider is traversed; unique providers and selected repository providers
  remain terminal.
- The registered-source phase keeps its singular source lifecycle. It offers
  provider selection only when every candidate is from an official repository;
  a candidate set containing an AUR provider remains ambiguous and stops before
  system or source execution because that phase cannot schedule the provider's
  PackageBase.
- Moguet rejects unresolved dependencies, unselected ambiguous providers,
  cycles, conflicts/replacements that it cannot safely resolve, and
  unprovable artifact identities before the corresponding mutation.
- `--noconfirm` avoids interactive blocking. It is not “yes to everything” and
  does not bypass source selection, planning, identity, conflict, or ownership
  guards.
- Multi-phase upgrades are not one atomic transaction. A failure stops later
  work but does not roll back an already completed package transaction. If
  cleanup fails after installation succeeds, inspect the result before
  retrying; the package may already be installed. In `upgrade-all`, provider
  selection for the filtered AUR phase occurs before clone, build, pacman, or
  sudo work in that phase, but earlier phases may already have completed.

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

### Package installation with `PKGBUILD`

The repository root ships a `PKGBUILD` that packages a published release
rather than the checked-out working tree. Its `pkgver()` reads the version
from the repository's own `VERSION` file and fetches the matching published
Git tag (`v<version>`) as the build source, so it never packages unreleased
working-tree commits.

```bash
git clone https://github.com/seekerkrt/moguet.git
cd moguet
makepkg -si
```

`makepkg -si` builds that tagged release and installs it onto the live
system with `pacman -U` in the same step. This differs from `make` and
`./moguet --help` above, which only build and inspect the development tree
in place and install nothing. This `PKGBUILD` is a repository-provided
packaging path, not an AUR submission; Moguet still has no published AUR
page.

<!-- parity:usage -->
## Basic usage

`moguet --help` is the runtime authority for the current command and option
surface. Command and option tokens never change with the selected locale.

```bash
# Install, search, or inspect packages
moguet -S <pkg>
moguet -S --select <query>
moguet -Ss <query>
moguet -Si <pkg>

# Pacman-compatible system upgrade
moguet -Syu

# Update configured source packages, installed AUR packages, or both
moguet upgrade
moguet upgrade-aur
moguet upgrade-all

# Build and install one remote package or one local PKGBUILD root
moguet build <pkg> [V=K...]
moguet build --local <directory> [V=K...]

# Inspect AUR dependencies and build order without building
moguet deps --recursive <pkg>
moguet plan <pkg>

# Retrieve build repositories without building or installing
moguet fetch <pkg>

# Export one PackageBase checkout or print only its PKGBUILD
moguet -G <pkg>
moguet -Gp <pkg>
```

**Choosing an upgrade command:** For ordinary package installation, search,
and system upgrades, use pacman-compatible operations such as `-S`, `-Ss`,
and `-Syu`, as with pacman and other AUR helpers. Use the corresponding
`upgrade`, `upgrade-aur`, or `upgrade-all` command when you intentionally want
a Moguet-specific multi-phase workflow, such as applying saved source-build
preferences or rebuilding installed AUR packages from source. These commands
are not aliases for an ordinary `-Syu`.

`--aur` limits supported `-S`, `-Ss`, and `-Si` forms to AUR. `--repo`
limits them to official binary repositories. Combining the selectors is an
error before an external command or AUR query. Pacman-only routes preserve
compatible pacman options; a source-build route rejects options whose meaning
cannot be preserved instead of silently ignoring them.

`-S --select <query>` is the interactive source-aware discovery form. Without
a source selector it searches both official repositories and AUR; `--aur` or
`--repo` limits the candidate source. Only `--needed` has a shared meaning on
both selected routes. Non-TTY use and `--noconfirm` fail without querying or
choosing a package.

Source-build preferences are managed with `add-src`, `edit-src`, `list-src`,
`del-src`, and `revert`. A one-off `build <pkg> [V=K...]` resolves a remote
package and does not save a preference. `build --local <directory> [V=K...]`
instead treats exactly one user-owned directory as a local PackageBase source;
it does not infer a local root from a path-like package operand or query AUR for
that root.

The local route reads a safe `.SRCINFO` without modifying it. Missing, invalid,
or known-stale metadata requires PKGBUILD review and explicit no-default consent
before `makepkg --printsrcinfo`; non-TTY input and `--noconfirm` stop instead of
authorizing evaluation. Moguet builds from an invocation-owned source snapshot,
leaves the user-owned tree unchanged, and installs every valid unique `pkgname`
child declared by the accepted metadata as an explicit root. Dependency
artifacts retain dependency install reasons, and an already explicit installed
package is never demoted. Runtime-aware package-name completion and more
advanced completion are future work; the shipped completion is limited to the
public CLI schema.

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
