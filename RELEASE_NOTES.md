# Moguet v2.0.1

This tracked file is the source of truth for the GitHub Release body. The
English and Japanese sections describe the same release scope.

## English

Moguet v2.0.1 is a narrow patch release that completes the XDG config storage
contract adopted for v2.0.0. It fixes the source-build preference storage issue
tracked in [#335](https://github.com/seekerkrt/moguet/issues/335).

### Fixed

- `add-src`, `edit-src`, `list-src`, `del-src`, and `revert`, together with the
  build and upgrade readers, now use
  `${XDG_CONFIG_HOME:-$HOME/.config}/moguet/source-build.d/` as their single
  authority.
- Source-build preference operations no longer require `sudo` or use a
  system-wide configuration store.
- An unset or empty `XDG_CONFIG_HOME` falls back to `$HOME/.config`. An explicit
  value must be an absolute, safe, existing base directory.
- Root execution uses root's own XDG context and never infers another user from
  `SUDO_USER`.
- The required Moguet directories are created safely when first needed, rather
  than during package installation.

### Migration

- `/etc/jpacker/package.build/` is now manual migration input only. Moguet does
  not create, read, write, fall back to, merge, copy, rewrite, or delete it at
  runtime.
- Existing preferences are not migrated automatically. Follow the
  [English Migration Guide](https://github.com/seekerkrt/moguet/blob/v2.0.1/docs/migration/v1-to-v2.md)
  or [Japanese Migration Guide](https://github.com/seekerkrt/moguet/blob/v2.0.1/docs/migration/v1-to-v2.ja.md).
- The published v2.0.0 tag, Release, and release body remain unchanged
  historical artifacts.

### Repositories

- Canonical GitHub repository: <https://github.com/seekerkrt/moguet>
- GitLab mirror: <https://gitlab.com/seekerkrt/moguet>

### Not included

Interactive package discovery, ambiguous AUR provider selection, shell
completion changes, and AUR package publication are outside this patch release.

## 日本語

Moguet v2.0.1は、v2.0.0で採用したXDG config storage契約を完成させる限定的な
patch releaseです。[#335](https://github.com/seekerkrt/moguet/issues/335)で追跡した
source-build preferenceの保存先問題を修正します。

### 修正内容

- `add-src`、`edit-src`、`list-src`、`del-src`、`revert`と、build / upgrade側の
  readerは、`${XDG_CONFIG_HOME:-$HOME/.config}/moguet/source-build.d/`だけを
  authorityとして使用します。
- source-build preference操作から`sudo`とsystem-wide config storeへの依存を
  撤去しました。
- `XDG_CONFIG_HOME`がunsetまたはemptyの場合は`$HOME/.config`へfallbackします。
  明示値にはabsoluteかつ安全で、既存のbase directoryであることを要求します。
- root実行時はroot自身のXDG contextを使い、`SUDO_USER`から別userを推測しません。
- 必要なMoguet directoryはpackage installation時ではなく、最初に必要とする実行時に
  安全に作成します。

### Migration

- `/etc/jpacker/package.build/`は手動migration専用のlegacy inputです。Moguetは
  runtimeでこれをcreate、read、write、fallback、merge、copy、rewrite、deleteしません。
- 既存preferenceは自動移行しません。
  [English Migration Guide](https://github.com/seekerkrt/moguet/blob/v2.0.1/docs/migration/v1-to-v2.md)または
  [日本語Migration Guide](https://github.com/seekerkrt/moguet/blob/v2.0.1/docs/migration/v1-to-v2.ja.md)を
  参照してください。
- 公開済みv2.0.0のtag、Release、release bodyは変更しないhistorical artifactとして
  維持します。

### Repository

- canonical GitHub repository: <https://github.com/seekerkrt/moguet>
- GitLab mirror: <https://gitlab.com/seekerkrt/moguet>

### 含めないもの

対話的package discovery、ambiguous AUR provider選択、shell completion変更、AUR package
publicationは、このpatch releaseのscope外です。
