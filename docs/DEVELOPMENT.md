# Development Workflow

jpacker は、v1.4.0 以降、`main` / `develop` / `feature/*` / `fix/*` / `docs/*` / `release/*` を使った軽量 Git Flow 型の運用を行う。

バージョン番号の付け方は [VERSIONING.md](VERSIONING.md) を参照する。

## Branches

### main

`main` は最新安定版を表す。

- GitHub の default branch とする
- `VERSION` / `PKGBUILD` / man page / Git tag / GitHub Release と整合している状態を保つ
- 通常の機能追加や修正作業は直接取り込まない
- release branch からの PR のみを取り込む

### develop

`develop` は次リリース候補を集約する integration branch とする。

- 普段の Issue 対応や小修正は `develop` から分岐する
- feature / fix / docs branch は原則 `develop` へ PR する
- 未リリース変更を含んでよい
- 次の release branch は `develop` から分岐する

### feature / fix / docs branches

Issue ごとの作業ブランチ。

例:

- `feature/issue-XX-aur-deps`
- `fix/issue-XX-search-exit-code`
- `docs/issue-XX-branch-workflow`

原則として `develop` から分岐し、`develop` へ PR する。

### release branches

リリース準備用ブランチ。

例:

- `release/v1.5.0`
- `release/v1.4.1`

`develop` から分岐し、以下を整える。

- `VERSION`
- generated man page
- `PKGBUILD` / package metadata
- release notes
- 必要な README / docs 更新

準備が完了したら `main` へ PR する。merge 後に Git tag と GitHub Release を作成し、GitLab mirror へ push する。

## Typical flow

### Normal development

    git switch develop
    git pull --ff-only origin develop
    git switch -c feature/issue-XX-topic

作業後:

    make clean && make
    git diff --check
    git status --short

    git add .
    git commit -m "..."
    git push -u origin feature/issue-XX-topic

    gh pr create --base develop --head feature/issue-XX-topic

PR merge 後:

    git switch develop
    git pull --ff-only origin develop
    git push gitlab develop

### Release flow

    git switch develop
    git pull --ff-only origin develop
    git switch -c release/vX.Y.Z

リリース準備後:

    make clean && make
    git diff --check

    git add VERSION jpacker.8 README.md docs/
    git commit -m "vX.Y.Z release準備"
    git push -u origin release/vX.Y.Z

    gh pr create --base main --head release/vX.Y.Z

merge 後:

    git switch main
    git pull --ff-only origin main
    git push gitlab main

    git tag -a vX.Y.Z -m "vX.Y.Z"
    git push origin vX.Y.Z
    git push gitlab vX.Y.Z

    gh release create vX.Y.Z --title "vX.Y.Z" --notes-file RELEASE_NOTES.md

その後、`main` の release commit を `develop` に取り込む。

    git switch develop
    git pull --ff-only origin develop
    git merge --ff-only main || git merge main
    git push origin develop
    git push gitlab develop

## Notes

- `main` は完成品を置く棚として扱う
- `develop` は普段の作業机として扱う
- 実装後は PR 作成・merge・branch 削除・mirror push・clean 確認まで締める
- 大きなリネームや破壊的変更は major release で扱う
