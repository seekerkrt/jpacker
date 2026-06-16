# Compatibility and Operation Support Policy

`jpacker` is designed as a **pacman-first wrapper** for Arch Linux. Its primary goal is to forward standard official repository operations directly to `pacman` while complementing them with AUR building and source-based package optimization.

It is **not** intended to be a complete, drop-in replacement for `yay` or `pacman` in every edge case, but rather to match the common command-line workflow for daily package management.

---

## Command Delegation & Fallback Policy

### 1. Direct Pacman Delegation
Operations that do not involve AUR or source-build package management are forwarded directly to `pacman` (with `sudo` prepended if root privileges are required):
* **System Upgrades (Official)**: `jpacker -Syu`, `jpacker -Sy`, `jpacker -Su` (for official repos).
* **Package Removal**: `jpacker -R`, `jpacker -Rs`, `jpacker -Rns`.
* **Local Package Operations**: `jpacker -U`.
* **Database Operations**: `jpacker -D`.
* **Queries (Official/Local)**: `jpacker -Q`, `jpacker -Qi`, `jpacker -Qs`, `jpacker -Qm`.

### 2. AUR Complementary Operations
For commands where official repository and AUR targets can be combined, `jpacker` intercepts and handles them:
* **Installation (`jpacker -S <pkg>`)**:
  - Checks if `<pkg>` is registered as a source-build package or exists in the official repositories.
  - If a package is found in official repositories (and is not forced to build from source), `jpacker` delegates the installation to `pacman`.
  - Otherwise, it falls back to AUR and builds the package from source using `makepkg`.
* **Search (`jpacker -Ss <query>`)**:
  - Executes `pacman -Ss <query>` first, then queries the AUR API and displays AUR search results below.
* **Sync Info (`jpacker -Si <pkg>`)**:
  - Displays official package info via `pacman -Si` if it exists.
  - If not found, falls back to displaying AUR metadata.
* **AUR/Foreign Upgrades (`jpacker -Qua`)**:
  - Scans for foreign packages via `pacman -Qm` and queries the AUR API to check for updates.

### 3. Unique jpacker Features
Commands that are specific to `jpacker`'s source-based management features:
* `build <pkg> [V=K]`: Run a one-off build from source.
* `upgrade`: Check and rebuild source-marked or AUR packages.
* `clean`: Clean package caches.
* `deps [--recursive] <pkg>`: Classify AUR dependencies.
* `plan <pkg>`: Show the build order plan for an AUR package.
* `fetch <pkg>`: Safely clone or fetch AUR build repositories for inspection without building or installing.
* `add-src <pkg> [V=K]`: Mark a package for source-based builds.
* `del-src <pkg>`: Unmark a package.
* `edit-src <pkg>`: Edit custom environment variables/makeopts for the package.
* `list-src`: List registered source packages.
* `revert <pkg>`: Unmark and reinstall the official binary package.

`jpacker fetch <pkg>` is a safe retrieval stage for inspecting AUR build repositories, not an execution stage for build/install. It clones missing AUR repositories and, for existing clones, runs only `git fetch origin`. It does not update the working tree and does not run `git pull`, merge, reset, build, or install operations. Future behavior that advances a working tree is not implemented by `fetch` and should be considered in a separate issue as `sync`, `update`, `fetch --update`, or another explicitly named operation.

---

## Yay Compatibility Alignment

We aim to align `jpacker` with the basic, high-frequency operations of `yay` to ensure a smooth transition for users:
* **Interactive updates**: Providing an easy mechanism to update both official repositories and AUR packages.
* **Seamless lookup**: Unified output for searches and package information queries.

---

## Out of Scope (Intentional Non-goals)
* **Reimplementing Pacman**: `jpacker` will not reimplement dependency resolution, pacman database writing, or package verification. It relies on the official `pacman` and `makepkg` utilities.
* **Full yay Feature parity**: Complex developmental flag support (like `--devel` tracking for VCS packages, `--cleanafter` options, etc.) is out of scope for the initial versions.
* **Non-Arch Linux support**: Compatibility is strictly limited to Arch Linux and environments using `pacman`/`makepkg`.

---

## Exit Codes, AUR Fallback, and Output Format

### Exit Codes
* Delegated commands return the exact exit code of `pacman`.
* For unified operations like search (`-Ss`), `jpacker` returns `0` if a match is found in either official repositories or AUR, and `1` if no match is found.
* For sync info (`-Si`) and AUR update checks (`-Qua`), critical failures should return a non-zero exit code. Per-package lookup failures may be reported as warnings when the rest of the operation can continue.

### AUR Fallback
* When a package query fails against official repos, `jpacker` will automatically query the AUR.
* Network timeouts or invalid packages are logged as errors, and the program will terminate with a non-zero exit code if critical steps fail.

### Output Format
* Standard pacman outputs are preserved and printed directly to stdout/stderr.
* AUR and source build outputs are formatted with `jpacker`'s own CLI theme (e.g., using styled headers for AUR search results).

---

## Future Roadmap & Tasks

The compatibility features will be implemented iteratively via the following small tasks:
1. **Task 1**: Investigate no-argument behavior.
   Decide whether `jpacker` with no arguments should show help, run a safe status check, or optionally provide a `yay`-like interactive update mode. Automatic updates by default should be treated carefully.
2. **Task 2**: Enhance the integrated upgrade flow (run `pacman -Syu` followed automatically by AUR/source-build upgrades).
3. **Task 3**: Refine dependency handling when repo packages and AUR packages are mixed in a single transaction.
4. **Task 4**: Unify `-Ss` and `-Si` output styles to make AUR results visually consistent with pacman.
5. **Task 5**: Add automated unit and integration tests for argument parsing and routing.
