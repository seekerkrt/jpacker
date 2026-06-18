# AGENTS.md

このファイルは、Codex などの AI coding agent が jpacker を編集するときの作業方針をまとめる。

## 基本方針

- 日本語を主文として扱う。英語は必要に応じて補助として添える。
- Issue の目的とスコープを優先し、関係のない変更を混ぜない。
- 既存の `main` / `develop` / `feature/*` / `fix/*` 運用を尊重する。
- `main` は最新安定版、`develop` は次リリース向け統合ブランチとして扱う。
- 作業ブランチは原則として `develop` から切る。

## Coding style

実装前に `docs/CODING_CONVENTIONS.md` を読むこと。

既存コードを変更する場合は、まず変更対象の周辺コードのスタイルに合わせる。ここでいうスタイルには、命名、責務分割、コメント粒度、エラー処理、ログ出力の流儀を含む。

周辺コードのスタイルが不明瞭な場合、一貫していない場合、または判断材料が足りない場合は、`docs/CODING_CONVENTIONS.md` に従う。

Issue で明示されていない限り、広範囲のスタイル変更、大規模 rename、無関係な整形を混ぜないこと。周辺コードのスタイルと `docs/CODING_CONVENTIONS.md` が衝突して見える場合は、変更を最小限に留め、PR の説明で判断理由を明記する。

## Safety

- pacman / makepkg / git / AUR の責務を jpacker 側で抱え込みすぎない。
- `plan` / `deps` / `fetch` の安全境界を崩さない。
- `fetch` に build / install / pull / merge / reset を混ぜない。
- `--noconfirm` は pacman / makepkg の確認省略に限り、未解決依存や循環依存を自動突破させない。

## Verification

C++ やビルド関連に触れた場合は、少なくとも次を確認する。

```bash
make clean && make
git diff --check
```

CLI 表示や挙動を変えた場合は、対象コマンドを直接実行して確認する。

docs-only 変更の場合でも、少なくとも次を確認する。

```bash
git diff --check
```
