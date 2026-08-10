# Moguet v2.2.0

This tracked file is the source of truth for release bodies. The English and
Japanese sections for each release describe the same scope.

## English

Moguet v2.2.0 extends package planning and execution safety while preserving
its pacman-first and fail-closed boundaries. It makes ambiguity visible,
validates version constraints across source observations, and provides one
human-readable plan for supported no-mutation workflows.

### #388 — Ambiguous provider installed-state visibility

- Ambiguous provider candidates now show `[installed]` when a same-name
  installed package is observed.
- If installed-state lookup is unavailable or cannot be trusted, the candidate
  shows `[installed state unknown]` with a warning; an authoritative absence
  remains unmarked.
- Installed-state text is presentation-only. Provider selection policy,
  candidate order, numbering, explicit choice, and routing are unchanged; no
  provider is selected automatically.

### #351 — Version constraint satisfiability

- Consumer requirements and provider capabilities are kept separate and
  evaluated as typed results: `Unconstrained`, `Satisfied`, `Unsatisfied`,
  `Unknown`, `Invalid`, or `Conflicting`.
- Repository, AUR, provider, and local observations retain their source-aware
  identity so version compatibility is not inferred from a name alone.
- Mutation routes perform the same production preflight before clone, fetch,
  build, install, or transaction work. Constraints that cannot be proven safe
  fail closed instead of being guessed or silently routed elsewhere.

### #352 — Unified plan and global dry-run

- A unified human-readable plan describes the observations and decisions for
  supported Moguet-owned routes.
- Global `--dry-run` is supported for:
  `-S` install / system-update, `fetch`, remote build, local build, `upgrade`,
  `upgrade-aur`, and `upgrade-all`.
- Dry-run is no-mutation and unsupported routes fail closed. An actual
  execution revalidates the current state instead of reusing an observation as
  approval or execution capability.

## 日本語

Moguet v2.2.0は、pacman-firstかつfail-closedの境界を保ったまま、package
planningとexecution safetyを拡張するreleaseです。曖昧さを表示し、source
observationをまたぐversion constraintを検証し、supportedなno-mutation workflowを
1つのhuman-readable planで確認できるようにしました。

### #388 — ambiguous providerのinstalled state表示

- ambiguous provider candidateと同名のinstalled packageが観測された場合、候補に
  `[installed]`を表示します。
- installed-state lookupが利用できない、または信頼できない場合は、候補に
  `[installed state unknown]`とwarningを表示します。authoritativeに未installと判定できる
  場合はsuffixを付けません。
- installed-state表示はpresentation-onlyです。provider selection policy、候補順、番号、
  明示選択、routingは変更せず、providerを自動選択しません。

### #351 — version constraint satisfiability

- consumer requirementとprovider capabilityを分離し、`Unconstrained`、`Satisfied`、
  `Unsatisfied`、`Unknown`、`Invalid`、`Conflicting`のtyped resultとして評価します。
- repository、AUR、provider、localのobservationはsource-awareなidentityを保ち、nameだけ
  からversion compatibilityを推測しません。
- mutation routeではclone、fetch、build、install、transactionへ進む前に同じproduction
  preflightを実行します。安全を証明できないconstraintは推測や別経路への黙ったfallbackをせず、
  fail-closedになります。

### #352 — unified planとglobal dry-run

- supportedなMoguet-owned routeのobservationとdecisionを、統一human-readable planで表示します。
- global `--dry-run`は、次のrouteに対応します。
  - `-S` install / system-update
  - `fetch`
  - remote build
  - local build
  - `upgrade`
  - `upgrade-aur`
  - `upgrade-all`
- dry-runはno-mutationで、unsupported routeはfail-closedです。actual executionはobservationを
  approvalやexecution capabilityとして再利用せず、current stateから再validationします。

# Moguet v2.1.0

The following historical notes describe the published v2.1.0 release.

## English

Moguet v2.1.0 expands Moguet's source-aware AUR workflows while preserving its
pacman-first and fail-closed boundaries. It makes ambiguous choices explicit,
adds a local `PKGBUILD` entry point, and strengthens the validation lanes that
protect these workflows.

### Package workflows

- When an AUR dependency has several providers, an interactive TTY now lists
  the source-aware candidates by number and requires one explicit choice. No
  provider is selected by default; non-TTY, EOF, `--noconfirm`, and cancellation
  remain fail-closed.
- `moguet -S --select <query>` provides interactive package discovery across
  official repositories and AUR. It supports explicit package numbers,
  multiple selections, inclusive ranges, and displayed official groups without
  guessing a default choice.
- Selected repository roots are installed first through one exact pacman
  transaction. Only then do selected AUR roots enter the source-build workflow,
  so source-aware install routing stays visible and a later AUR failure does
  not hide an already completed repository transaction.
- `moguet build --local <directory> [V=K...]` builds an explicitly selected
  local PackageBase without treating a path-like remote package operand as a
  local source. It preserves the user-owned source tree and validates the local
  metadata before build and installation.
- Local `PKGBUILD` builds can resolve their AUR dependencies, build the
  required PackageBases, and install the validated dependency and local package
  artifacts through the established build/install boundaries.

### Validation and maintenance

- The Arch Docker offline lane validates a clean source snapshot without
  runtime network access. The live lane separately exercises real provider
  selection, AUR build/install, and local `PKGBUILD` end-to-end flows.
- The heavyweight integration-test binaries now use target-isolated object
  builds with dependency tracking and link firewalls, improving incremental
  validation without changing runtime behavior.
- Developers can opt into `ccache` for compilation and an optional linker such
  as mold through existing Make overrides; neither is a runtime or package
  requirement.
- TTY and locale authority handling is aligned across interactive selection,
  diagnostics, and gettext fallback, so non-interactive calls do not consume
  input intended for prompts.
- Obsolete production artifacts from the jpacker v1.16.0 transition have been
  removed. Historical migration guidance, fixtures, and license evidence remain
  available where they document the supported transition.

### Repositories

- Canonical GitHub repository: <https://github.com/seekerkrt/moguet>
- GitLab mirror: <https://gitlab.com/seekerkrt/moguet>

## 日本語

Moguet v2.1.0は、pacman-firstかつfail-closedの境界を保ったまま、source-awareな
AUR workflowを拡張するreleaseです。曖昧な選択を明示化し、local `PKGBUILD`の入口を追加し、
これらのworkflowを守るvalidation laneを強化しました。

### Package workflow

- AUR dependencyに複数providerがある場合、interactive TTYはsource-awareなcandidateを
  番号付きで表示し、1つの明示選択を求めます。default選択はなく、non-TTY、EOF、
  `--noconfirm`、cancelは引き続きfail-closedです。
- `moguet -S --select <query>`はofficial repositoryとAURをまたぐinteractive package
  discoveryを提供します。package番号、複数選択、inclusive range、表示済みofficial groupを
  明示的に選択でき、defaultを推測しません。
- 選択したrepository rootは、まず正確に1つのpacman transactionでinstallします。その後に
  選択したAUR rootだけがsource-build workflowへ入るため、source-awareなinstall routingは
  見通しを保ち、後続AUR failureが完了済みrepository transactionを隠すことはありません。
- `moguet build --local <directory> [V=K...]`は、pathのように見えるremote package operandを
  local sourceとして扱わず、明示したlocal PackageBaseをbuildします。user所有のsource treeを
  保持し、local metadataを検証してからbuild / installします。
- local `PKGBUILD` buildではAUR dependencyを解決し、必要なPackageBaseをbuildし、確立済みの
  build / install boundaryに従って検証済みのdependencyとlocal package artifactをinstallできます。

### Validationとmaintenance

- Arch Docker offline laneはruntime network accessなしでcleanなsource snapshotを検証します。
  live laneはreal provider selection、AUR build / install、local `PKGBUILD`のend-to-end flowを
  個別に実行します。
- 重量級integration test binaryは、targetごとに分離したobject build、dependency tracking、
  link firewallを使用するようになり、runtime behaviorを変えずにincremental validationを改善しました。
- developerは既存のMake overrideにより、compileへ`ccache`、linkerへmoldなどを任意で使用できます。
  どちらもruntime / package requirementではありません。
- TTYとlocaleのauthorityをinteractive selection、diagnostic、gettext fallbackで整合させ、
  non-interactive callがprompt用inputを消費しないようにしました。
- jpacker v1.16.0移行に由来するobsoleteなproduction artifactを削除しました。supported
  transitionを説明するhistorical migration guidance、fixture、license evidenceは必要な範囲で
  保持しています。

### Repository

- canonical GitHub repository: <https://github.com/seekerkrt/moguet>
- GitLab mirror: <https://gitlab.com/seekerkrt/moguet>
