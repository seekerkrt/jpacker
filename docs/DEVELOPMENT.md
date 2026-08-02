# Development Workflow

Moguetは、`main` / `develop` / `feature/*` / `fix/*` / `docs/*` / `release/*`を使った軽量Git Flow型の運用を行う。canonical repositoryは[GitHub](https://github.com/seekerkrt/moguet)、backup mirrorは[GitLab](https://gitlab.com/seekerkrt/moguet)である。

branch / tag同期のownerはGitHub Actionsのmirror workflowとする。同じrefをGitHubとGitLabへ二重に手動pushせず、GitHubをauthority、GitLabをmirror destinationとして扱う。

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

- `release/v2.1.0`
- `release/v2.0.1`

`develop` から分岐し、以下を整える。

- `VERSION`
- generated man page
- `PKGBUILD` / package metadata
- English / Japanese sectionを持つtracked `RELEASE_NOTES.md`
- 必要な README / docs 更新

準備が完了したら`main`へPRする。merge後にrelease merge commitへannotated Git tagを作成し、tagをGitHubへだけpushする。GitHub Release本文はtracked `RELEASE_NOTES.md`をauthorityとし、GitLabのbranch / tag / Releaseは各mirror workflowで同期する。

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

GitLab側の同一refはmirror workflowの完了後に確認する。通常flowで手動pushしない。

### Release flow

    git switch develop
    git pull --ff-only origin develop
    git switch -c release/vX.Y.Z

リリース準備後:

    make clean
    make
    make test
    make release-check
    git diff --check

    git status --short

    git add -- \
        .github/workflows/mirror-gitlab-release.yml \
        .github/workflows/mirror-gitlab.yml \
        .gitlab/mirror-github-release.sh \
        AGENTS.md \
        CLAUDE.md \
        CONTRIBUTING.md \
        PKGBUILD \
        README.ja.md \
        README.md \
        RELEASE_NOTES.md \
        THIRD_PARTY_NOTICES.md \
        docs/CODING_CONVENTIONS.md \
        docs/COMPATIBILITY.md \
        docs/DECISIONS.md \
        docs/DEVELOPMENT.md \
        docs/LICENSING.md \
        docs/PROJECT_STANCE.md \
        docs/VERSIONING.md \
        docs/migration/v1-to-v2.ja.md \
        docs/migration/v1-to-v2.md \
        man/ja/moguet.1 \
        man/ja/moguet.1.in \
        man/moguet.1 \
        man/moguet.1.in \
        scripts/check-internal-identity.py \
        scripts/check-license-compliance.sh \
        scripts/check-packaging-metadata.sh \
        tests/test-package-transition.sh

    git diff --cached --name-only | LC_ALL=C sort
    git status --short
    git commit -m "vX.Y.Z release準備"
    git push -u origin release/vX.Y.Z

    gh pr create --base main --head release/vX.Y.Z

上記の`git add`は、#311で監査済みのv2.0.0 release diffを1 pathずつ明示したものです。
`git add .`や代表pathだけのpartial listへ置き換えません。commit前にcached path一覧を#311の
Known cutover patch ownership（承認済みのStage A finding修正を含む）と再照合し、release
scopeのunstaged / untracked pathやunrelatedなstaged pathがないことを確認します。将来の
releaseでは、このlistを流用せず、そのreleaseで監査済みのexact path setへ置き換えます。

merge 後:

    git switch main
    git pull --ff-only origin main

    git tag -a vX.Y.Z -m "vX.Y.Z"
    git push origin vX.Y.Z

    gh release create vX.Y.Z --title "vX.Y.Z" --notes-file RELEASE_NOTES.md

tag mirrorとGitLab Release mirrorの完了を確認する。uploaded assetがないreleaseではGitLab asset linkが0件でも正常とする。

その後、`main`から`develop`への回収PRを作成する。protected branchをlocal mergeやdirect pushで更新しない。

    gh pr create --base develop --head main

回収PRのmergeとmirror完了後、local branchを更新する。

    git switch develop
    git pull --ff-only origin develop

## Notes

- `main` は完成品を置く棚として扱う
- `develop` は普段の作業机として扱う
- `main` / `develop`はprotected branchとして扱い、更新はPR経由とする
- 実装後はPR作成・merge・branch削除・mirror結果・clean確認まで締める
- published tagは移動・再作成せず、通常recoveryでReleaseを削除・unpublishしない
- 大きなリネームや破壊的変更は major release で扱う
