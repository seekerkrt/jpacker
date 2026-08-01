# Migrating from jpacker v1 to Moguet v2

[日本語](v1-to-v2.ja.md)

<!-- parity:overview -->
## Overview

Moguet v2.0.0 is a breaking transition from jpacker v1.16.0. The execution
base remains pacman-first, but the public identity, user storage, configuration
format, and localized documentation change together.

This guide defines a non-destructive order:

```text
inspect and back up jpacker v1 data
-> remove the jpacker v1.16.0 package
-> install the validated Moguet v2 package
-> migrate only understood settings by hand
-> verify command, man, completion, locale, and paths
```

Moguet does not use `/etc/jpacker/jpacker.conf` as a normal configuration
layer. It does not automatically copy, rewrite, or delete `/etc/jpacker`, and
it does not guess which user should receive root-owned data. The existing
`/etc/jpacker/package.build/` source-preference store is a separate
compatibility boundary described below; it is not TOML configuration.

<!-- parity:preparation -->
## Before you begin

1. Finish or stop package operations. Do not migrate while pacman, makepkg, or
   another package helper is changing the system.
2. Confirm that the installed legacy package is jpacker v1.16.0, and record
   how it was installed.
3. Read the release-specific Moguet package instructions before removal. The
   package conflict/coexistence policy and any temporary command alias are
   packaging decisions; this guide does not assume them before publication.
4. Keep a trusted jpacker v1.16.0 package or source archive available for
   rollback.
5. Plan the migration separately for every local user. A root-owned legacy
   directory does not identify a destination user.

For example, record the installed package before making changes:

```bash
pacman -Q jpacker
pacman -Qi jpacker
```

If those commands report no package, determine the actual install method
before using package removal commands.

<!-- parity:identity -->
## Identity changes

| Surface | jpacker v1.16.0 | Moguet v2.0.0 |
| --- | --- | --- |
| Project / brand | jpacker | Moguet |
| Reading | — | モグエット |
| CLI / binary | `jpacker` | `moguet` |
| Package | `jpacker` | `moguet` |
| XDG application name | legacy jpacker paths | `moguet` |
| Project environment prefix | legacy names | `MOGUET_*` |
| Runtime localization | English-only legacy surface | English authority, Japanese formal translation |

<code>MU<!-- rejected alternate spelling -->GUET</code> and the Japanese
reading 「ミュ<!-- rejected alternate reading -->ゲ」 are not aliases. Command
and option tokens are not translated. The formal v2 command is `moguet`; do not
create a local `jpacker` symlink and assume that it has packaging support.

The source/repository URLs may continue to contain `jpacker` until the final
external identity cutover. Follow published release links rather than editing
remotes or package sources in advance.

<!-- parity:backup -->
## Back up v1 data

Create a private backup before removing the package. At minimum, preserve:

- the installed jpacker version and package metadata;
- `/etc/jpacker/jpacker.conf`, if present;
- `/etc/jpacker/package.build/`, including ownership and modes, if present;
- any custom log location referenced by `LOGFILE`;
- any package/source files that you changed outside the normal package
  payload.

One possible archive workflow is:

```bash
backup_root="$HOME/moguet-migration-backup-$(date +%Y%m%d-%H%M%S)"
install -d -m 700 "$backup_root"
pacman -Q jpacker > "$backup_root/jpacker-package.txt"
pacman -Qi jpacker > "$backup_root/jpacker-package-info.txt"
sudo tar --acls --xattrs -C /etc -cpf - jpacker \
    > "$backup_root/etc-jpacker.tar"
tar -tf "$backup_root/etc-jpacker.tar"
```

If `/etc/jpacker` does not exist, skip only that archive step. A failed or
empty archive is not a backup; inspect the command status and listing. Store a
copy outside the machine if losing the settings would be costly.

Also preserve any pre-existing Moguet XDG directories before testing a new
package. Their standard locations are:

```text
${XDG_CONFIG_HOME:-$HOME/.config}/moguet
${XDG_STATE_HOME:-$HOME/.local/state}/moguet
${XDG_CACHE_HOME:-$HOME/.cache}/moguet
```

Do not merge those directories with `/etc/jpacker` during backup.

<!-- parity:remove-v1 -->
## Remove jpacker v1.16.0

After the backup is verified, remove the package with the same package manager
that installed it. For a normal pacman-managed installation, the conservative
form is:

```bash
sudo pacman -R jpacker
```

Do not add recursive dependency cleanup merely for the rename. Inspect
pacman's proposed transaction before confirming it. If the package was
installed by another method, use that method's documented uninstall path
instead of pretending pacman owns it.

Package removal must not be used as a cleanup command for `/etc/jpacker`.
Keep the backup and any preserved `.pacsave` or preference files until both
migration and rollback validation are complete.

<!-- parity:install-v2 -->
## Install Moguet v2

Install Moguet only after the jpacker v1 package removal has completed and the
v2 package source, signature/checksum, dependencies, file conflicts, and
payload have been verified by the release-specific instructions.

The package identity is `moguet` and the executable is `/usr/bin/moguet`.
Until the package endpoint is officially published, this guide intentionally
does not invent an AUR URL or a `pacman -S` repository command. Do not stage a
development `make install` over the old package as a substitute for the
validated transition.

Installing the package must not create `/etc/moguet`, a user XDG config file,
or user XDG state/cache directories. Moguet creates only the user directories
needed by an actual command, under that executing user's XDG context.

<!-- parity:configuration -->
## Migrate configuration manually

Moguet uses this optional user-owned file:

```text
$XDG_CONFIG_HOME/moguet/config.toml
fallback: ~/.config/moguet/config.toml
```

Create it only for a specific user who wants non-default values. The minimal
schema is:

```toml
schema_version = 1

[review]
pkgbuild = "prompt"
diff = "prompt"

[build]
mode = "normal"
```

Map only understood jpacker v1 keys:

| jpacker v1 setting | Moguet v2 action |
| --- | --- |
| `NOEDIT=true` | Set `review.pkgbuild = "skip"` |
| `NOEDIT=false` | Omit the key or use `review.pkgbuild = "prompt"` |
| `NODIFF=true` | Set `review.diff = "skip"` |
| `NODIFF=false` | Omit the key or use `review.diff = "prompt"` |
| `EDITOR=...` | Do not copy to TOML; set `VISUAL`, then `EDITOR`, in the user's environment |
| `LOGFILE=...` | No v2.0.0 config key; use the fixed XDG state log |
| `RMDEPS=true` | Do not migrate; separated source-build dependency cleanup remains unsupported |

The editor resolution order is `VISUAL -> EDITOR -> nano`. The default log is:

```text
$XDG_STATE_HOME/moguet/moguet.log
fallback: ~/.local/state/moguet/moguet.log
```

Moguet reads configuration as strict, read-only input. An existing file needs
`schema_version = 1`; unknown keys, invalid types or enum values, and future
schema versions fail before external mutation. Moguet does not rewrite the
file or silently fall back from a broken file.

<!-- parity:legacy-data -->
## Handle legacy preferences and data

Do not copy `/etc/jpacker` wholesale into a Moguet XDG directory. In this
implementation, `/etc/jpacker/package.build/` remains the source-build
preference store and source operations read it directly. Its files are not
TOML config tables. The `moguet add-src <pkg> [V=K]` interface writes to that
same store, so do not replay existing entries through it as a migration step.
Back up the store, preserve it unchanged, and follow the release-specific
package transition instructions for its package ownership and coexistence
handling.

Moguet does not automatically:

- read `/etc/jpacker/jpacker.conf` as a config layer;
- copy `/etc/jpacker` into one or more users' homes;
- delete or rewrite legacy files;
- create or read `/etc/moguet`;
- migrate `LOGFILE`, `RMDEPS`, arbitrary editor commands, credentials, shell
  fragments, or unknown uppercase keys;
- infer a destination user from root ownership, `sudo`, or `SUDO_USER`.

Config, state, and cache have separate v2 responsibilities:

```text
config: $XDG_CONFIG_HOME/moguet/  (fallback ~/.config/moguet/)
state:  $XDG_STATE_HOME/moguet/   (fallback ~/.local/state/moguet/)
cache:  $XDG_CACHE_HOME/moguet/   (fallback ~/.cache/moguet/)
```

Cache is reproducible and is not a backup location. Uninstalling Moguet must
not be treated as permission to remove user config, state, or cache.

<!-- parity:verification -->
## Verify the migration

Run Moguet as the target normal user, not through `sudo`:

```bash
command -v moguet
moguet --version
LC_ALL=C moguet --help
```

Then verify documentation and completion files supplied by the package:

```bash
man -w moguet
LC_ALL=C man moguet
LANG=ja_JP.UTF-8 man moguet
```

Expected standard paths are:

```text
/usr/share/man/man1/moguet.1
/usr/share/man/ja/man1/moguet.1
/usr/share/bash-completion/completions/moguet
/usr/share/zsh/site-functions/_moguet
/usr/share/fish/vendor_completions.d/moguet.fish
/usr/share/locale/ja/LC_MESSAGES/moguet.mo
```

Start a fresh shell, or reload only the completion mechanism documented by
your shell. If the system uses a man-page cache, use its normal package hook or
administrator procedure to refresh it. Confirm that completion proposes
`moguet` commands and the final options such as `--edit`, `--diff`, and
`--build-mode=normal|rebuild|clean`, not a legacy `jpacker` command.

Finally, inspect the resolved XDG paths as the intended user. A help/version
check alone should not create config, state, or cache directories. Test an
operation only after reviewing its external commands and package effects.

<!-- parity:rollback -->
## Roll back to jpacker v1.16.0

Rollback is an explicit package transition, not an automatic transaction
rollback.

1. Stop Moguet and finish any active package operation.
2. Back up the user's Moguet config and state. Keep cache only if it is useful
   for diagnostics.
3. Remove the Moguet package using the package manager that installed it.
   Do not delete user XDG directories as part of package removal.
4. Reinstall the trusted jpacker v1.16.0 package or source archive recorded
   before migration.
5. Restore `/etc/jpacker` only from the verified backup and only after checking
   the current destination. Do not overwrite a newer file blindly.
6. Verify `jpacker --version`, the v1 man/completion surface, and a read-only
   operation before performing a package transaction.

Moguet's XDG data is not interpreted by jpacker v1 and may be kept for a later
retry. A completed pacman transaction is not undone by switching helpers;
compare actual package database state rather than assuming the helper rollback
changed installed packages.

<!-- parity:maintenance -->
## v1 maintenance and external cutover

jpacker v1 remains under the `jpacker` identity and its v1 release tags. It is
not relabeled as Moguet, and v1.16.0 does not receive the v2 XDG/config format
through this migration guide.

At the time this guide is prepared, the public repository has no dedicated v1
maintenance branch; `develop` is the Moguet v2 integration branch and must not
be treated as a v1 maintenance source. The final public location for v1
maintenance packages/branches and any old-URL redirects is part of the
external release cutover. Use the location named in the published v2.0.0
release notes rather than guessing a branch or endpoint.

For the current source contracts, see [README.md](../../README.md),
[README.ja.md](../../README.ja.md), and
[COMPATIBILITY.md](../COMPATIBILITY.md).
