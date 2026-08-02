# Moguet v2.0.0

This tracked file is the source of truth for the GitHub Release body. The
English and Japanese sections describe the same release scope.

## English

Moguet v2.0.0 is the breaking successor to jpacker v1.16.0. It preserves the
pacman-first execution base while changing the project, command, package,
storage, configuration, localization, documentation, and repository identity.

### Highlights

- Renames the project and brand to **Moguet**, with `moguet` as the only
  command, binary, package, XDG application name, and gettext domain.
- Keeps package transactions with pacman, source package builds with makepkg,
  and repository transport with Git while retaining the existing fail-closed
  planning, artifact validation, and external-command visibility boundaries.
- Uses XDG user directories for Moguet config, state, and cache, with a typed
  read-only TOML configuration at `moguet/config.toml`.
- Provides English-authoritative CLI text and documentation with formal
  Japanese translations, locale catalogs, man pages, and shell completion.
- Ships a Moguet-only package payload with no `jpacker` command alias and no
  `provides`, `conflicts`, or `replaces` relationship with jpacker v1.16.0.

### Breaking migration

- The command and package name change from `jpacker` to `moguet`.
- Moguet does not read `/etc/jpacker/jpacker.conf` as its normal configuration
  and does not copy, rewrite, or delete `/etc/jpacker` automatically.
- The production source-preference compatibility store remains
  `/etc/jpacker/package.build/`; the Moguet package does not create or own it.
- Moguet and jpacker v1.16.0 may coexist for migration and rollback, but their
  mutating operations must not run concurrently.
- Follow the [English Migration Guide](https://github.com/seekerkrt/moguet/blob/v2.0.0/docs/migration/v1-to-v2.md)
  or [Japanese Migration Guide](https://github.com/seekerkrt/moguet/blob/v2.0.0/docs/migration/v1-to-v2.ja.md)
  before changing an installed system.

### Repository and v1 maintenance

- Canonical GitHub repository: <https://github.com/seekerkrt/moguet>
- GitLab mirror: <https://gitlab.com/seekerkrt/moguet>
- The former `seekerkrt/jpacker` slugs remain redirect-only and must not be
  reused for another repository.
- The `v1.16.0` tag and jpacker v1.16.0 Release remain available under their
  original identity. There is no permanent v1 maintenance branch; a critical
  fix branch, if needed, starts from the immutable `v1.16.0` tag.

### Not included

AUR package publication is not part of this release operation. No Moguet AUR
URL or old-package rename/deprecation action is implied by these notes.

## 日本語

Moguet v2.0.0は、jpacker v1.16.0の後継となるbreaking releaseです。pacman-firstな
実行基盤を維持しながら、project、command、package、storage、config、localization、
documentation、repositoryのidentityを変更します。

### 主な変更

- project / brandを **Moguet** へ変更し、command、binary、package、XDG application名、
  gettext domainを`moguet`へ統一しました。
- package transactionはpacman、source package buildはmakepkg、repository transportは
  Gitへ委ね、既存のfail-closed plan、artifact validation、external command可視化の境界を
  維持します。
- Moguetのconfig / state / cacheをXDG user directoryへ配置し、
  `moguet/config.toml`のtyped read-only TOML configを使用します。
- EnglishをCLI text / documentationのauthorityとし、日本語の正式翻訳、locale catalog、
  man page、shell completionを提供します。
- Moguet専用package payloadを提供します。`jpacker` command aliasはなく、jpacker v1.16.0
  に対する`provides`、`conflicts`、`replaces`も宣言しません。

### Breaking migration

- command / package名は`jpacker`から`moguet`へ変わります。
- Moguetは`/etc/jpacker/jpacker.conf`を通常configとして読まず、`/etc/jpacker`を自動で
  copy、rewrite、deleteしません。
- productionのsource-preference compatibility storeは
  `/etc/jpacker/package.build/`に残り、Moguet packageは作成・所有しません。
- migration / rollback中はMoguetとjpacker v1.16.0をcoexistできますが、両者のmutating
  operationを同時実行しないでください。
- installed systemを変更する前に
  [English Migration Guide](https://github.com/seekerkrt/moguet/blob/v2.0.0/docs/migration/v1-to-v2.md)または
  [日本語Migration Guide](https://github.com/seekerkrt/moguet/blob/v2.0.0/docs/migration/v1-to-v2.ja.md)を
  確認してください。

### Repositoryとv1 maintenance

- canonical GitHub repository: <https://github.com/seekerkrt/moguet>
- GitLab mirror: <https://gitlab.com/seekerkrt/moguet>
- 旧`seekerkrt/jpacker` slugはredirect専用として保持し、別repositoryへ再利用しません。
- `v1.16.0` tagとjpacker v1.16.0 Releaseは元のidentityで保持します。permanentなv1
  maintenance branchは作らず、重大修正が必要な場合だけimmutableな`v1.16.0` tagから
  branchを作成します。

### 含めないもの

AUR package publicationは今回のrelease operationに含めません。このrelease noteはMoguetの
AUR URL、旧packageのrename / deprecationを示すものではありません。
