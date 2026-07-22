# jpacker 設計ポリシー / Design Policy

[日本語](#日本語) | [English](#english)

---

## 日本語

### 文書の位置づけ

この文書は、現在の jpacker、および将来 pactune へ発展する場合に共通して適用する上位設計ポリシーの詳細な正本である。CLI 挙動、provider 選択、solver の利用、fallback、自動化、安全境界について新しい判断を行うときは、このポリシーを基準にする。

現在の project 名は jpacker であり、pactune は将来の発展先としてのみ記載する。この文書は rename の決定や実施を意味しない。

この文書は、現在の全 CLI 挙動を列挙する互換性仕様でも、未実装機能を実装済みとみなす保証でもない。現在の command routing と個別の互換性契約は [docs/COMPATIBILITY.md](docs/COMPATIBILITY.md) を参照する。個別仕様を追加・変更するときは、この上位ポリシーに沿って、対象となる behavior と検証範囲を別途明示する。

### 1. 一貫性

同じ種類の状態、結果、失敗は、経路や内部実装が違っても同じ境界と規則で扱う。

* package absence、metadata query failure、invalid input、unsupported decision は意味が異なる。これらを単一の bool、empty result、または success へ flatten しない。
* 観測できなかった状態と、観測した結果として存在しない状態を区別する。判断できない状態を「対象なし」や「処理済み」に置き換えない。
* 同じ option や operation は、pacman-only、AUR、source-build など経路が違っても、対応可能な範囲で同じ意味を保つ。
* 経路ごとに同じ意味を安全に保てない場合は、黙って無視したり別の意味へ変換したりせず、未対応であることを示して実行前に停止する。

### 2. 透明性

何を観測し、何を判断し、どの command を実行し、なぜ停止したかを、利用者が追える形にする。

* query や外部 command の failure を package absence、empty result、または正常終了として隠さない。
* partial completion が可能な処理では、成功した対象、失敗した対象、未実行の対象を区別し、diagnostic と exit status に反映する。
* 利用者に影響する主要な外部 command と副作用は、実行前に見える形にする。特に pacman / makepkg に渡す主要 option を隠さない。
* 安全境界で停止した場合は、拒否した判断や不足している情報を示し、利用者が次に確認すべき対象を分かるようにする。

### 3. 既存操作と挙動の尊重

pacman、makepkg、git、および既存 jpacker CLI の操作、責務、挙動は、明確な理由と独立した設計判断なしに変更しない。

* package metadata の内部取得を libalpm などへ移行しても、利用者から見た operation、transaction owner、build owner、判断結果は可能な限り維持する。
* behavior change を refactor、metadata migration、内部 API の置換へ暗黙に混ぜない。変更する場合は、目的、互換性への影響、失敗時の扱い、検証方法を独立して説明する。
* wrapper として扱える operation は、元 command の操作感、引数の意味、主要な副作用、終了状態を可能な限りなぞる。
* この原則は完全互換の宣言ではない。現在未対応の command や edge case を保証するのではなく、差異を意図せず増やさないための判断基準である。

### 4. 任せる部分は任せる

jpacker、または将来の pactune は、既存 component が所有する package-management 機能の不完全な再実装を増やさない。各 component の authoritative な結果と既存の責務境界を利用し、その上で必要な orchestration を行う。

* Arch 固有の package metadata、dependency、provider、conflict などは、対応可能な範囲で libalpm を authoritative source とする。
* system package transaction は pacman へ任せる。
* source package の build と artifact install は makepkg へ任せる。
* source repository の取得と更新は git へ任せる。
* jpacker / future pactune は、orchestration、source-build policy、execution order、安全境界、diagnostic、および user intent の保全を所有する。

重要: 現在採用している libalpm の scope は read-only package metadata に限る。jpacker は libalpm transaction を開始、準備、commit せず、system package transaction の owner は引き続き pacman である。metadata の authority を libalpm へ寄せることは、transaction ownership の移行を意味しない。

### 5. ユーザーが自然に想像する意図

これは、利便性の好みではなく、自動化と安全境界を決める独立した中核原則である。

> jpacker / future pactune は、command 名、引数、既存 tool の慣習から利用者が自然に想像する目的と結果を尊重する。内部実装の都合や過剰な自動化によって、意外な選択、副作用、fallback、挙動変更を持ち込まない。

user intent は、command 名、指定された target と option、元 tool の慣習、文書化済みの既存挙動から判断する。内部で実装しやすいことや、技術的に自動化できることだけを根拠に拡張しない。

* 最終的な判断基準は、その command を実行した利用者が、自然にどの対象、結果、副作用を期待するかである。
* `--noconfirm` は、対応する経路で確認を省略し非対話で処理するための option であり、「すべてを自動承認する」という意味ではない。未解決依存、provider 選択、conflict、replacement、削除、source selection などの曖昧さや危険を承認する根拠にしない。
* 複数 provider、conflict、replacement、削除、source selection など、利用者の意図が一意に決まらないものを、候補順や内部都合だけで勝手に選ばない。
* authoritative な判断材料を取得できない場合は、failure を absence に置き換えたり推測で補ったりして mutation へ進まず、安全に停止して理由を示す。
* 対象同士が独立し、安全境界を保ったまま処理できる場合は、失敗対象を隔離して残りを継続してよい。その場合は partial failure として、成功、失敗、未実行を区別して示す。
* 「驚きが少ないこと」と「元 command から自然に予想できること」を、compatibility と UX の一部として扱う。

### 6. 責務境界

| Component | 所有する責務 |
| --- | --- |
| libalpm | 対応範囲内の authoritative な Arch package metadata と package relationships。現在は read-only metadata に限り、transaction は所有しない |
| pacman | system package transactions |
| makepkg | source package build と artifact installation |
| git | source repository retrieval と update |
| jpacker / future pactune | orchestration、source-build policy、execution order、safety boundaries、diagnostics、user intent の保全 |

jpacker / future pactune が外部 component を呼び出すための順序、事前条件、停止条件、表示を設計することは orchestration の責務である。ただし、それを理由に各 component の solver、transaction、build、repository operation を独自実装へ置き換えない。

### 7. 判断ルール

新しい自動化、fallback、solver 利用、または behavior change を検討するときは、最低限、次を確認する。

* 既存 command の意味を変えないか。
* command 名、引数、既存 tool の慣習から利用者が自然に予想する対象、結果、副作用と一致するか。
* authoritative component へ任せられる判断や処理を、jpacker / future pactune 側で再実装していないか。
* failure と absence、および unsupported decision を区別できるか。
* 実行する command、判断理由、副作用、partial completion を説明できるか。
* 判断が曖昧な場合や authoritative な情報を取得できない場合に、mutation より前に安全に停止できるか。
* behavior change を独立した decision として説明し、既存挙動との差分を検証できるか。

これらを満たす説明や検証方法がない場合は、自動化を既定動作へ組み込まない。read-only の観測や plan と、build、install、remove、repository update などの mutation を分け、必要な判断材料を利用者へ示すことを優先する。

---

## English

### Status and authority

This document is the detailed canonical source for the high-level design policy shared by the current jpacker project and any future evolution into pactune. New decisions about CLI behavior, provider selection, solver use, fallback, automation, and safety boundaries must be evaluated against this policy.

The current project name is jpacker. pactune is mentioned only as a possible future evolution; this document neither decides nor performs a rename.

This document is not a compatibility specification enumerating all current CLI behavior, and it does not claim that unimplemented features already exist. See [docs/COMPATIBILITY.md](docs/COMPATIBILITY.md) for current command-routing details and specific compatibility contracts. When a specific contract is added or changed, the affected behavior and verification scope must be stated separately in accordance with this policy.

### 1. Consistency

States, results, and failures of the same kind must follow the same boundaries and rules even when they pass through different routes or internal implementations.

* Package absence, metadata query failure, invalid input, and an unsupported decision have different meanings. They must not be flattened into a single boolean, an empty result, or success.
* A state that could not be observed must remain distinct from a state observed to be absent. An undecidable state must not be rewritten as “no target” or “completed.”
* The same option or operation should retain the same meaning, where supported, across pacman-only, AUR, and source-build routes.
* If a route cannot safely preserve that meaning, jpacker must not silently ignore the option or translate it into a different meaning. It must report the unsupported case and stop before execution.

### 2. Transparency

Users must be able to follow what was observed, what was decided, which command will run, and why processing stopped.

* A query or external-command failure must not be hidden as package absence, an empty result, or normal completion.
* When partial completion is possible, successful, failed, and unattempted targets must be distinguished and reflected in diagnostics and exit status.
* Major external commands and side effects that affect users must be visible before execution. In particular, important options passed to pacman or makepkg must not be hidden.
* When a safety boundary stops processing, diagnostics must identify the rejected decision or missing information so users know what to inspect next.

### 3. Preserve existing commands and behavior

The operations, responsibilities, and behavior of pacman, makepkg, git, and the existing jpacker CLI must not change without a clear reason and a separate design decision.

* Moving internal package-metadata access to libalpm or another component should preserve the user-visible operation, transaction owner, build owner, and decision result wherever possible.
* A behavior change must not be hidden inside a refactor, metadata migration, or internal API replacement. Its purpose, compatibility impact, failure handling, and verification method must be explained independently.
* An operation that can remain a wrapper should follow the original command's interaction model, argument meaning, major side effects, and completion status wherever possible.
* This principle is not a declaration of complete compatibility. It does not guarantee commands or edge cases that are currently unsupported; it is a rule against introducing unintended differences.

### 4. Delegate to authoritative components

jpacker, or a future pactune, must not accumulate incomplete reimplementations of package-management capabilities owned by existing components. It should use their authoritative results and established responsibility boundaries, then provide the necessary orchestration around them.

* Arch-specific package metadata, dependencies, providers, conflicts, and related information use libalpm as the authoritative source where supported.
* System package transactions remain delegated to pacman.
* Source package builds and artifact installation remain delegated to makepkg.
* Source repository retrieval and updates remain delegated to git.
* jpacker / future pactune owns orchestration, source-build policy, execution order, safety boundaries, diagnostics, and preservation of user intent.

Important: the currently adopted libalpm scope is limited to read-only package metadata. jpacker does not initiate, prepare, or commit libalpm transactions; pacman remains the owner of system package transactions. Treating libalpm as the metadata authority does not transfer transaction ownership to libalpm.

### 5. Natural user intent

This is an independent core principle for automation and safety boundaries, not merely a preference about convenience.

> jpacker / future pactune respects the purpose and result that users would naturally expect from a command name, its arguments, and existing tool conventions. Internal implementation convenience or excessive automation must not introduce surprising selections, side effects, fallback, or behavior changes.

User intent is inferred from the command name, explicit targets and options, conventions of the original tool, and documented existing behavior. An implementation must not expand behavior merely because an action is technically automatable or internally convenient.

* The final criterion is which targets, results, and side effects a user running that command would naturally expect.
* `--noconfirm` suppresses confirmation and enables non-interactive handling only within a supported route; it does not mean “approve everything automatically.” It does not authorize ambiguity or risk involving unresolved dependencies, provider selection, conflicts, replacements, removals, or source selection.
* Multiple providers, conflicts, replacements, removals, source selection, and other decisions for which user intent is not unique must not be resolved arbitrarily from candidate order or implementation convenience.
* If authoritative decision inputs cannot be obtained, jpacker must not replace failure with absence or guess its way into mutation. It must stop safely and explain why.
* When targets are independent and can be processed without weakening safety boundaries, a failed target may be isolated while the remaining targets continue. The result must be reported as a partial failure that distinguishes success, failure, and unattempted work.
* “Low surprise” and behavior naturally predictable from the original command are part of compatibility and user experience.

### 6. Responsibility boundary

| Component | Owned responsibility |
| --- | --- |
| libalpm | Authoritative Arch package metadata and package relationships where supported. Its current scope is read-only metadata; it does not own transactions |
| pacman | System package transactions |
| makepkg | Source package builds and artifact installation |
| git | Source repository retrieval and updates |
| jpacker / future pactune | Orchestration, source-build policy, execution order, safety boundaries, diagnostics, and preservation of user intent |

Designing the order, preconditions, stop conditions, and presentation around calls to external components is part of jpacker / future pactune orchestration. It is not a reason to replace each component's solver, transaction, build, or repository operations with a custom implementation.

### 7. Decision rule

Before introducing new automation, fallback, solver use, or a behavior change, verify at least the following:

* Does it preserve the meaning of existing commands?
* Does it match the targets, results, and side effects users would naturally expect from the command name, its arguments, and existing tool conventions?
* Could an authoritative component own the decision or operation instead of jpacker / future pactune reimplementing it?
* Can it distinguish failure, absence, and an unsupported decision?
* Can it explain the command to run, the reason for the decision, side effects, and any partial completion?
* Can it stop safely before mutation when a decision is ambiguous or authoritative information cannot be obtained?
* Can the behavior change be explained as an independent decision and verified against existing behavior?

If these questions cannot be answered with a clear explanation and verification method, the automation must not become default behavior. Prefer separating read-only observation and planning from mutations such as build, install, removal, and repository update, and expose the decision inputs users need.
