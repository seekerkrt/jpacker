# AGENTS.md

このファイルは、Codex などの AI coding agent が jpacker を編集するときの作業方針をまとめる。

## 基本方針

- 既存の Issue / PR / commit message の言語・文体に合わせる。
- Issue の目的とスコープを優先し、関係のない変更を混ぜない。
- 既存の main / develop / feature/* / fix/* 運用を尊重する。
- main は最新安定版、develop は次リリース向け統合ブランチとして扱う。
- 作業ブランチは原則として develop から切る。

## 言語方針

このリポジトリ内の contributor / AI coding agent 向け指示は、日本語で記述する。

Issue、PR、commit message、merge message、release note などのプロジェクト記録は、日本語を主文・原典として扱う。必要に応じて、英語の要約や補足を追加してよい。

既存の日本語中心の記録を、Issue で明示されていない限り英語のみへ置き換えないこと。

commit message や merge message は、まず日本語で変更内容が分かるように書く。英語は補足として扱う。

## Coding style

実装前に docs/CODING_CONVENTIONS.md を読むこと。

実装コードは、docs/CODING_CONVENTIONS.md に記載された C++ 標準とコーディング方針に従う。

現時点では C++20 をプロジェクトの基準とし、Issue で明示されていない限り、より新しい標準機能やコンパイラ固有拡張へ寄せる変更を混ぜないこと。

既存コードを変更する場合は、まず変更対象の周辺コードのスタイルに合わせる。ここでいうスタイルには、命名、責務分割、コメント粒度、エラー処理、ログ出力の流儀を含む。

周辺コードのスタイルが不明瞭な場合、一貫していない場合、または判断材料が足りない場合は、docs/CODING_CONVENTIONS.md に従う。

Issue で明示されていない限り、広範囲のスタイル変更、大規模 rename、無関係な整形を混ぜないこと。周辺コードのスタイルと docs/CODING_CONVENTIONS.md が衝突して見える場合は、変更を最小限に留め、PR の説明で判断理由を明記する。

## Safety

- pacman / makepkg / git / AUR の責務を jpacker 側で抱え込みすぎない。
- plan / deps / fetch の安全境界を崩さない。
- fetch に build / install / pull / merge / reset を混ぜない。
- `--noconfirm` は pacman / makepkg の確認省略に限り、未解決依存や循環依存を自動突破させない。
- jpacker 本体は通常ユーザーで起動し、必要な pacman 操作だけ sudo 経由で行う方針を尊重する。

## Commit message

commit message は、後で release summary / changelog の素材になる前提で書く。

subject は `git log --oneline` で読んでも変更内容が分かる程度に具体的にする。

必要に応じて、軽量な Conventional Commits 風の分類 prefix を使ってよい。

- `feat:` 新機能・機能追加
- `fix:` バグ修正
- `docs:` ドキュメント更新
- `build:` ビルド設定・PKGBUILD・Makefile など
- `test:` テスト追加・修正
- `refactor:` 挙動を変えない内部整理
- `chore:` 雑務・メンテナンス
- `release:` リリース準備

分類に迷う場合は、無理に厳密化せず、変更内容が伝わる日本語 subject を優先する。

英語の説明を追加する場合も、日本語の subject / body を原典として扱い、英語は補足に留める。

## GitHub workflow

GitHub を canonical repository として扱い、GitLab は backup mirror として扱う。

Issue / PR / Release は GitHub 側を正とする。

PR 作成、merge、tag 作成、release 作成は、明示的に指示された場合のみ行う。

作業完了後は、実装内容、確認したコマンド、未確認事項を PR または作業ログに残す。

GitLab mirror への push は、GitHub 側の状態を確認したうえで、必要な場合のみ行う。

## Verification

C++ やビルド関連に触れた場合は、少なくとも次を確認する。

    make clean && make
    git diff --check

CLI 表示や挙動を変えた場合は、対象コマンドを直接実行して確認する。

docs-only 変更の場合でも、少なくとも次を確認する。

    git diff --check
