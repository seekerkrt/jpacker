# XDG cache cutover safety contract

## 文書の位置づけ

この文書は、Moguet cacheをXDG user cacheへ切り替える際のfilesystem identity、destructive operation、legacy cache、Git executionの安全契約を定めるnormative production contractである。文書の規範上の正本は日本語本文である。

- Origin Issue: [#305](https://github.com/seekerkrt/moguet/issues/305)
- Related Issues: [#75](https://github.com/seekerkrt/moguet/issues/75)、[#302](https://github.com/seekerkrt/moguet/issues/302)、[#304](https://github.com/seekerkrt/moguet/issues/304)
- Related PRs: #315〜#318（#305 path、directory safety、state / cache cutover）、#312〜#314（identity boundary）
- Update history: Issue #373で旧decision 11の本文から安定contractへ分離。
- Related upper decisions: [decision 1](../DECISIONS.md#decision-1)、[decision 2](../DECISIONS.md#decision-2)、[decision 4](../DECISIONS.md#decision-4)、[decision 5](../DECISIONS.md#decision-5)、[decision 6](../DECISIONS.md#decision-6)、[decision 7](../DECISIONS.md#decision-7)

## Contract本文（日本語normative source of truth）

cache rootはXDG user cache authorityへ切り替える。legacy cacheを正本として扱わず、自動的に読み込み、移行、変更、削除しない。destructive operation、persistent checkout、artifact workspace、rollback、reclone、recursive cleanupは、利用者所有のfilesystem entryへ作用し得るため、最小のpath cutoverだけで安全性を判断してはならない。

将来のimplementationの統合、縮小、置換にかかわらず、少なくとも次を維持する。

- destructive operationをtrusted root内へ限定する。
- symlinkまたはroot escapeをfollowしない。
- 通常のfilesystem identity replacementを検出し、fail closedする。
- rollbackはownershipとidentityを証明できるentryだけを対象にする。
- cache cleanupは全targetのpreflightが完了する前に削除を開始しない。mutation-before-preflightを許可しない。
- legacy cacheを自動的に読み込み、移行、変更、削除しない。
- Git executionは、危険なparent-process routingまたはconfig environmentを暗黙に継承しない。

安全境界を証明できないpath、owner、type、identity、containment、cleanup対象は、利用者の明示的な別操作へ曖昧に委譲せず、external mutation前に停止する。既に完了したpackage transactionやbuildの結果を、cleanup failureや未実行の後続対象と混同しない。

このcontractが固定するのは上記の安全契約であり、現在のmodule、type、capability plumbing、trusted Git policy、removal planningを恒久的architectureとして固定するものではない。現在のproject規模に対してcostが不釣り合いになった場合、安全契約を維持したまま、より小さく比例したarchitectureへ統合、縮小、置換してよい。その簡素化は安全契約の撤回ではない。

## Non-scope / implementationを固定しない範囲

- legacy cacheの自動migration、dual-read / dual-write、削除。
- cacheからのpackage transaction、build、Git operationの独自再実装。
- 特定のdirectory descriptor API、module分割、capability type、cleanupアルゴリズムの恒久固定。
- XDG config / stateやsource-build preferenceの個別authority。これらは各contractを参照する。

## Compatibility

XDG cache、legacy path非変更、filesystem identity、symlink / root escape、preflight summaryは、[`COMPATIBILITY.md`のcache / source-build compatibility section](../COMPATIBILITY.md#compat-xdg-cache-safety)を参照する。
