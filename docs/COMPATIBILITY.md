# jpacker 互換性と pass-through policy

`jpacker` は Arch Linux 向けの **pacman-first wrapper** として扱う。

目的は、日常的な `pacman` workflow をできるだけ自然に通しつつ、AUR build / install と source build preference を補助することにある。jpacker は Arch Linux / pacman / AUR の公式ツールではなく、現時点では `pacman` や既存 AUR helper と同じ挙動をすべて提供するものでもない。pacman options / flags の対応範囲は段階的に実装・検証しており、完全互換も宣言しない。

この文書は、pacman / makepkg 由来の operation / option をどこまで pass-through するか、また jpacker 固有の operation / option とどう分けるかを固定するための方針である。実装がまだ追いついていない部分は「暫定」または「未整理」として扱う。

---

## 基本方針

- jpacker が明示的に扱う operation / option は、jpacker 側で解釈する。
- jpacker 固有 option は、pacman execution や `.SRCINFO` 読み取り用の `makepkg --printsrcinfo` へ渡さない。
- pacman が自然に扱える operation / option は、可能な範囲で pacman へ pass-through する。
- AUR / source build 経路では、pacman transaction option を無条件に makepkg へ置き換えない。
- `--noconfirm` は「全部 yes」ではなく、「対話で止まらない」指定として扱う。
- `--noconfirm` だけで rebuild / cleanbuild / provider selection / split package selection / conflicts / replaces / 未解決依存の突破を勝手に有効化しない。
- 値を取る option は、値を target と誤認しないように扱う。値が欠けている場合は停止する。

---

## jpacker 固有 operation

次の operation は jpacker 固有であり、pacman へそのまま委譲しない。

- `build <pkg> [VAR=VALUE...]`
- `upgrade`
- `clean`
- `deps [--recursive] <pkg>`
- `plan <pkg>`
- `fetch <pkg>`
- `add-src <pkg> [VAR=VALUE...]`
- `del-src <pkg>`
- `edit-src <pkg>`
- `list-src`
- `revert <pkg>`

`deps`、`plan`、`fetch` は調査・表示・取得段階の operation であり、build / install / pull / merge / reset を混ぜない。`fetch` は未取得 repository の clone と、既存 clone に対する `git fetch origin` までに留める。

---

## jpacker 固有 option

次の option は jpacker 固有として扱う。

- `--noedit`
- `--nodiff`
- `--rebuild`
- `--cleanbuild`

`--noedit` は PKGBUILD review / edit prompt を省略する。`--nodiff` は repository update 後の diff prompt を省略する。

`--rebuild` は AUR / source build の build/install 実行時に `makepkg -f` 相当として扱う。`--cleanbuild` は `makepkg -C` 相当として扱う。これらは pacman 由来 option ではないため、pacman execution へは渡さない。

`--rebuild` / `--cleanbuild` が未指定の場合、jpacker は既存の package artifact や `src/` directory がある場面で、必要に応じて default no の prompt で rebuild / cleanbuild を確認する。cleanbuild を有効にし、同じ package directory に既存 package artifact がある場合は、artifact 再利用を避けるため rebuild も有効にする。`--noconfirm` 指定時は prompt を出さず、未指定の rebuild / cleanbuild は no として扱う。

---

## pacman 由来として pass-through する operation

次の pacman operation は、jpacker が AUR / source build のために介入しない場合、基本的に pacman へ委譲する。

- `-S` 系
- `-R` 系
- `-Q` 系
- `-U` 系
- `-D` 系

ただし、次のように jpacker が補完する operation がある。

- `-S <target>`: official repository package は pacman へ渡し、official repository にない target または source build preference がある target は AUR / source build 経路へ進める。
- `-Ss <query>`: pacman search のあと AUR search を補完する。
- `-Si <target>`: official repository にあれば pacman info を使い、なければ AUR metadata を表示する。
- `-Qua`: foreign packages を見て AUR update を確認する。
- `-Syu` / `-Sy` / `-Su`: pacman-compatible system upgrade として扱い、登録済み source build preferences の全体走査は行わない。source build preferences も確認したい場合は `upgrade` を使う。

---

## pacman / makepkg 由来 option の pass-through

pacman へ直接委譲する経路では、jpacker が明示的に消費しない pacman-compatible option を pacman へ渡す。

AUR / source build 経路では、pacman option をそのまま makepkg option とみなさない。現時点で makepkg build/install execution へ明示的に反映するのは次の範囲に留める。

- `--noconfirm`: pacman / makepkg execution へ渡す。
- `--rebuild`: jpacker 固有 option として `makepkg -f` 相当へ変換する。
- `--cleanbuild`: jpacker 固有 option として `makepkg -C` 相当へ変換する。

それ以外の pacman transaction option は、official repository target へは pass-through してよいが、AUR / source build target に対して同じ意味で効くとは宣言しない。

---

## pass-through するが jpacker 側でも意味を見る option

### `--noconfirm`

`--noconfirm` は pacman 由来の互換 option として扱い、pacman execution と makepkg build/install execution へ pass-through する。

同時に、jpacker 自身の prompt 制御にも関係する。ただし意味は「全部 yes」ではなく「対話で止まらない」指定である。

そのため、`--noconfirm` 指定時でも次は自動承認しない。

- 未解決依存を含む AUR build plan の実行
- 循環依存を含む AUR build plan の実行
- 未実装の provider selection
- 未実装の split package selection
- conflicts / replaces の自動判断
- 明示されていない rebuild
- 明示されていない cleanbuild
- 危険な削除や reset

この方針は #83 の prompt helper 実装前提でもある。prompt helper では、単純に `--noconfirm` を「yes」として扱うのではなく、非対話時に安全に停止するもの、default を選べるもの、明示 option が必要なものを分ける。

現時点の jpacker 独自 prompt は、prompt ごとに default selection を持つ。`Updates detected. View diff?`、`Edit PKGBUILD?`、`Rebuild package?`、`Clean build existing build directory?`、`Clean jpacker build cache?` は default no とし、`Proceed with build?` は default yes とする。`--noconfirm` 指定時は prompt を表示せず、この default selection を採用する。ただし EOF や入力読み取り失敗は Enter と同一扱いにしない。stdin が TTY でない場合も、危険側へ進まないように扱う。

### `--needed`

`--needed` は pacman 由来 option として、pacman execution へ pass-through する。

AUR / source build 経路では、現時点で「既に入っているなら build/install を省略する」という完全な意味は宣言しない。将来的には build plan と installed package state の扱いとして整理する。

### `--asdeps` / `--asexplicit`

`--asdeps` と `--asexplicit` は pacman 由来 option として、pacman execution へ pass-through する。

AUR / source build 経路では、最終的な install reason と関係するため、単純な pass-through だけでは足りない。将来的には build plan / install reason の契約として扱う。現時点では、AUR / source build target に対する完全な反映は宣言しない。

---

## 値を取る option の扱い

値を取る pacman option は、次のどちらの形式も option と値の組として扱う。

- `--option value`
- `--option=value`

現在 jpacker が値を取るものとして追跡する pacman long option は次の通り。

- `--arch`
- `--assume-installed`
- `--cachedir`
- `--color`
- `--config`
- `--dbpath`
- `--gpgdir`
- `--hookdir`
- `--ignore`
- `--ignoregroup`
- `--logfile`
- `--overwrite`
- `--print-format`
- `--root`
- `--sysroot`

現在 jpacker が値を取るものとして追跡する pacman short option は次の通り。

- `-b`
- `-r`

この追跡は、option value を package target と誤認しないための最小限の parsing である。pacman option の意味検証そのものは pacman に委ねる。値が必要な形式で値が欠けている場合、jpacker は pacman 実行前に停止する。

---

## AUR / source build 経路では単純 pass-through できない option

次の option は pacman へ渡すことはできても、AUR / source build 経路で同じ意味を保つには追加設計が必要である。

- `--needed`
- `--asdeps`
- `--asexplicit`
- `--ignore`
- `--ignoregroup`
- `--overwrite`
- `--config`
- `--dbpath`
- `--root`
- `--sysroot`
- `--cachedir`
- `--gpgdir`
- `--hookdir`
- `--logfile`
- `--print-format`

特に database path、root、sysroot、config を変える option は、pacman の見ている world と jpacker の AUR metadata / installed package state / build cache の見ている world がずれる可能性がある。AUR / source build 経路で意味を持たせる場合は、別 Issue で安全境界を整理する。

---

## 未整理・慎重扱いの option / 状態

次は未整理または慎重扱いとする。

- provider selection
- split package selection
- conflicts / replaces
- dependency solver 的な自動解決
- `--overwrite`
- `--ignore` / `--ignoregroup`
- `--config` / `--dbpath` / `--root` / `--sysroot`
- makepkg と pacman の両方に似た名前がある option
- pacman / makepkg の hook や database state に影響する option

これらは、jpacker 側で意味を再実装するのではなく、pacman / makepkg に任せる範囲と jpacker が安全に停止する範囲を分けてから扱う。

---

## Exit codes, AUR fallback, and output format

- pacman へ直接委譲した command は、pacman の終了コードを返す。
- `-Ss` のような統合表示では、official repository または AUR のどちらかで match すれば成功として扱う。
- `-Si` や `-Qua` では、処理継続できる per-package failure は警告に留め、critical failure は non-zero exit code にする。
- pacman の標準出力・標準エラーはできるだけそのまま保つ。
- AUR / source build の表示は jpacker 側の format を使う。

---

## 外部コマンド表示

jpacker が利用者に影響する主要な外部コマンドを実行する場合は、実行前に対象のコマンドを表示する。

ただし、metadata 確認や内部判定のための小さな確認コマンドまで、通常ログにすべて列挙する方針ではない。

---

## Out of scope

次はこの方針文書では扱わない。

- pacman / 既存 AUR helper 完全互換の宣言
- provider selection の本格実装
- split package selection の本格実装
- conflicts / replaces の自動解決
- dependency solver の強化
- pacman database 書き込みや package verification の再実装

---

## Future roadmap

この方針は #56 の親 Issue として v2.0.0 まで段階的に整理する。

v1.x では、`--noconfirm`、`--needed`、`--asdeps`、`--asexplicit` など必要性の高い option から個別に扱う。#56 は v2.0.0 時点の compatibility policy を整理する親 Issue であり、個別 PR では `Refs #56` として参照する。

## Related future topics

次の話題は、この方針の周辺で今後整理する。

- no-argument behavior: `jpacker` 単体実行時に help 表示、safe status check、interactive update のどれを採るか。
- integrated upgrade flow: official repository update と AUR / source-build update のつなぎ方。
- mixed dependency handling: official repository package と AUR package が混ざる transaction / build plan の扱い。
- search / info output alignment: `-Ss` / `-Si` の official repository 結果と AUR 結果の見せ方。
- argument parsing tests: operation / option routing の regression test。
