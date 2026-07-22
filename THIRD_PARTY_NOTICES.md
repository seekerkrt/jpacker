# Third-Party Notices

This document records components used by the current jpacker v1.15.0 development series. It distinguishes code linked or compiled into jpacker from programs invoked across a process boundary. The project licensing policy is in [`docs/LICENSING.md`](docs/LICENSING.md) in the source tree and `${PREFIX}/share/doc/jpacker/LICENSING.md` after installation (`/usr/share/doc/jpacker/LICENSING.md` in the Arch package).

## Linked or compiled components

### libalpm

- **Purpose:** Read-only access to installed Arch package metadata.
- **Relationship:** Direct API use through dynamic linking. jpacker explicitly links with `-lalpm`, and the built ELF records `libalpm.so.16` as a `DT_NEEDED` entry on the audited system.
- **License:** `GPL-2.0-or-later`.
- **Provider:** The Arch Linux `pacman` package.
- **Bundled:** No. jpacker does not copy or distribute the libalpm source tree or library binary.

jpacker conservatively treats the dynamically linked result as a GPL-covered combined work and licenses the current project under `GPL-3.0-or-later`. libalpm remains limited to read-only metadata in jpacker's architecture; `pacman` remains the owner of system package transactions. That transaction ownership boundary does not change the license classification of direct library linking.

Because no libalpm source or binary copy is included in the jpacker source or package, jpacker does not install a duplicate GPLv2 text as a third-party copy. The providing Arch package and [pacman upstream](https://gitlab.archlinux.org/pacman/pacman) remain the sources for the library itself and its notices. jpacker's applicable GPLv3 text is installed as `LICENSE`.

### libcurl

- **Purpose:** HTTP requests to the AUR RPC service.
- **Relationship:** Direct API use through dynamic linking. jpacker explicitly links with `-lcurl`, and the built ELF records `libcurl.so.4` as a `DT_NEEDED` entry on the audited system.
- **License:** `curl` (SPDX identifier).
- **Provider:** The Arch Linux `curl` package.
- **Bundled:** No. jpacker does not copy or distribute the libcurl source tree or library binary.
- **Required notice:** [`LICENSES/curl.txt`](LICENSES/curl.txt) in the source tree; `${PREFIX}/share/licenses/jpacker/curl.txt` after installation (`/usr/share/licenses/jpacker/curl.txt` in the Arch package).

`LICENSES/curl.txt` is an exact copy of the copyright and permission notice installed by Arch `curl` 8.21.0-1 and matches the [curl upstream license](https://curl.se/docs/copyright.html) for the audited release.

### nlohmann-json

- **Purpose:** Parse and serialize AUR RPC JSON data.
- **Relationship:** The system `<nlohmann/json.hpp>` header is used at build time. Its header-only implementation is compiled into the jpacker binary.
- **License for the compiled header closure:** MIT.
- **Provider:** The Arch Linux `nlohmann-json` package at build time.
- **Bundled source tree:** No.
- **Required notice:** [`LICENSES/nlohmann-json-MIT.txt`](LICENSES/nlohmann-json-MIT.txt) in the source tree; `${PREFIX}/share/licenses/jpacker/nlohmann-json-MIT.txt` after installation (`/usr/share/licenses/jpacker/nlohmann-json-MIT.txt` in the Arch package).

The audit used Arch `nlohmann-json` 3.12.0-2 and the matching upstream v3.12.0 tag. The C++20 include closure contained 46 nlohmann headers; every actual file carried `SPDX-License-Identifier: MIT`. The notice preserves the following copyright provenance found in compiled headers:

- Copyright (c) 2013-2025 Niels Lohmann
- Copyright (c) 2008-2009 Björn Hoehrmann
- Copyright (c) 2009 Florian Loitsch
- Copyright (c) 2016-2021 Evan Nemerson

The upstream README records the Hedley origin as CC0-1.0. The actual v3.12.0 `thirdparty/hedley/hedley.hpp` file compiled by jpacker carries the Evan Nemerson copyright and an MIT SPDX identifier, and it matches the installed Arch header. This provenance is retained here; jpacker does not bundle a separate Hedley source copy or rely on a separately redistributed CC0 work, so a CC0 full text is not installed.

`detail/meta/cpp_future.hpp` also contains an Apache-2.0-attributed Abseil fallback for pre-C++14 builds. jpacker is built as C++20, where that fallback branch is excluded from the preprocessed source. No Abseil fallback code or nlohmann upstream test/tool component is compiled or bundled, so an Apache-2.0 full text is not installed for the current build. Changing the C++ baseline, vendoring headers, or using an amalgamated header requires this decision to be re-audited.

## External programs invoked

Every entry below is a separately installed program invoked through command-line arguments, stdin/stdout, and/or an exit status. It is not linked into jpacker and is not bundled with jpacker.

| Program | Purpose and provider | Process boundary | Bundled |
| --- | --- | --- | --- |
| `pacman` | Repository queries and system package transactions; Arch `pacman` package | Separate process; not linked | No |
| `pacman-conf` | Resolve pacman repository configuration and libalpm `RootDir`/`DBPath`; Arch `pacman` package | Separate process; not linked | No |
| `makepkg` | Build and install source packages; Arch `pacman` package | Separate process; not linked | No |
| `vercmp` | Compare Arch package versions; Arch `pacman` package | Separate process; not linked | No |
| `git` | Clone, inspect, fetch, diff, and update source repositories | Separate process; not linked | No |
| `bsdtar` | Read repository metadata from pacman sync databases; Arch `libarchive` package | Separate process; not linked | No |
| `sudo` | Enter the privilege boundary for pacman and protected file operations | Separate process; not linked | No |
| `touch`, `tee`, `install`, `rm` | Maintain source-build preference files; Arch `coreutils` package | Separate processes; not linked | No |
| `/bin/sh` | Execute constructed command lines. `printf` is used as a shell builtin on the audited system | Separate shell process; not linked | No |
| User-selected editor (default `nano`) | Review PKGBUILD, install scripts, and preference files; selected by the user or configuration | Separate process; no specific editor implementation is linked | No |

These programs are independently installed and distributed, and jpacker includes no copy of their executables or source. Their license texts are therefore not duplicated into the jpacker package. The program/package distributor remains responsible for the copies it provides.

Build tools such as the C++ compiler, `make`, `pkg-config`, and the linker are also separate toolchain programs. They build jpacker but are not invoked by the production binary.

## System/toolchain runtime

On the audited Arch system, the jpacker ELF had direct `DT_NEEDED` entries for `libstdc++.so.6`, `libm.so.6`, `libgcc_s.so.1`, and `libc.so.6` in addition to libcurl and libalpm. The C++ compiler driver supplies these system/toolchain runtimes; jpacker does not select them as application libraries or bundle their binaries.

`ldd` also expands dependencies required by system libcurl and libalpm packages, including protocol, TLS, compression, archive, and package-signing libraries. Those are transitive system dependencies, not direct jpacker API dependencies. The kernel-provided vDSO and ELF interpreter are likewise not third-party components bundled by jpacker. This notice intentionally does not turn every `ldd` line into a direct-component notice.

## Distribution notes

- The jpacker repository contains no vendored third-party library source, library archive, shared library binary, or third-party test fixture.
- libalpm and libcurl are obtained as dynamic dependencies from Arch system packages. nlohmann-json is obtained as a system build dependency.
- For v1.15.0 and later, `make install` is the single owner of the audited license and notice layout used by the Arch `PKGBUILD`. When packaging a pre-v1.15 tag whose Makefile predates that layout, `PKGBUILD` installs only the tag's legacy MIT `LICENSE` as a compatibility fallback.
- External program license texts are not duplicated because no copy of those programs is distributed with jpacker.
- Vendoring, static linking, binary bundling, a new linked/compiled dependency, a different nlohmann header form, or a project-built binary distribution requires a fresh notice and source-availability audit.
