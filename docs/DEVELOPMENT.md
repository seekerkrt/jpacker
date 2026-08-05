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

### Integration test build contract

Issue #380 Slice 1のpreflightでは、Makefileが直接生成する66個のtest binaryを棚卸しした。
このうち64 targetは複数の`.cpp`を1回のcompiler invocationへ渡してcompileとlinkをまとめて
実行している。production source数には、64個以上を含む次の10 targetと、次点の
`PRODUCTION_SOURCE_BUILD_TEST_TARGET`（33個）の間に明確な差がある。この10 targetを
Issue #380でobject分離する「重量級integration test target」とする。

| Make target variable | Binary | 現行source構成（production / test support） |
| --- | --- | --- |
| `APP_CONFIG_INTEGRATION_TEST_TARGET` | `build/tests/moguet-app-config-test` | `$(SRCS)`（71 / 0） |
| `AUR_RPC_VALIDATION_TEST_TARGET` | `build/tests/moguet-aur-rpc-validation-test` | `$(SRCS)` + package-metadata alpm stub（71 / 1） |
| `CLI_LOCALIZATION_TEST_TARGET` | `build/tests/moguet-cli-localization-test` | `$(SRCS)`（71 / 0） |
| `TEST_TARGET` | `build/tests/moguet-test` | `$(SRCS)`（71 / 0） |
| `UPGRADE_BASELINE_METADATA_TEST_TARGET` | `build/tests/moguet-upgrade-baseline-metadata-test` | `$(SRCS)` + package-metadata alpm stub（71 / 1） |
| `SOURCE_INSTALL_CHARACTERIZATION_TEST_TARGET` | `build/tests/moguet-source-install-characterization-test` | `$(SRCS)`から`moguet.cpp`を除外 + characterization test（70 / 1） |
| `UPGRADE_ALL_COMMAND_TEST_TARGET` | `build/tests/moguet-upgrade-all-command-test` | `$(SRCS)`から`upgrade_all_operation.cpp`を除外 + 2 stub（70 / 2） |
| `COMMANDS_INSPECT_TEST_TARGET` | `build/tests/moguet-commands-inspect-test` | `$(SRCS)`から`aur_rpc.cpp` / `repository_query.cpp`を除外 + 3 stub（69 / 3） |
| `COMMANDS_SYNC_TEST_TARGET` | `build/tests/moguet-commands-sync-test` | `$(SRCS)`から`aur_rpc.cpp` / `root_package_search.cpp`を除外 + 2 stub（69 / 2） |
| `AUR_UPDATE_COMMAND_TEST_TARGET` | `build/tests/moguet-aur-update-command-test` | `$(SRCS)`から7 operation TUを除外 + 3 stub（64 / 3） |

ここでtest supportには、`tests/*_test.cpp`だけでなくproduction symbolを所有するscenario / ABI
stubも含む。64 targetすべてを一度にobject化せず、上記10 targetだけをIssue #380の対象とする。
production sourceが33個以下の残り54 targetのobject化は、実測に基づく別の判断がない限り
Issue #380のscopeへ含めない。

#### 現行compile条件とstub ownership

重量級targetの現行compile共通部分は、順に`CPPFLAGS`、`LIBALPM_CPPFLAGS`、`CXXFLAGS`、
`MY_CXXFLAGS`である。既定値ではlibalpmの`-D_FILE_OFFSET_BITS=64`、`-O2 -pipe`、
`-std=c++20 -Wall -Wextra`、version macro、`-Ibuild/generated`を含む。target固有のmacro、
include path、link libraryは次のとおりであり、object分離後も落としてはならない。

| Target | Target固有macro | Target固有include | Link library |
| --- | --- | --- | --- |
| `APP_CONFIG_INTEGRATION_TEST_TARGET` | `MOGUET_ENABLE_TEST_OVERRIDES`, `MOGUET_ENABLE_TEST_CONFIG_PATH`, `MOGUET_ENABLE_APP_CONFIG_TEST_HOOKS` | なし | `MY_LDLIBS`, `LIBALPM_LDLIBS` |
| `AUR_RPC_VALIDATION_TEST_TARGET` | `MOGUET_ENABLE_TEST_OVERRIDES`, `MOGUET_ENABLE_AUR_RPC_TEST_HOOKS` | `src`, `tests/stubs/package-metadata` | `MY_LDLIBS` |
| `CLI_LOCALIZATION_TEST_TARGET` | target専用locale directory, `MOGUET_ENABLE_TEST_OVERRIDES` | なし | `MY_LDLIBS`, `LIBALPM_LDLIBS` |
| `TEST_TARGET` | `MOGUET_ENABLE_TEST_OVERRIDES` | なし | `MY_LDLIBS`, `LIBALPM_LDLIBS` |
| `UPGRADE_BASELINE_METADATA_TEST_TARGET` | `MOGUET_ENABLE_TEST_OVERRIDES`, `MOGUET_ENABLE_TEST_CONFIG_PATH`, `MOGUET_ENABLE_APP_CONFIG_TEST_HOOKS` | `src`, `tests/stubs/package-metadata` | `MY_LDLIBS` |
| `SOURCE_INSTALL_CHARACTERIZATION_TEST_TARGET` | `MOGUET_ENABLE_TEST_OVERRIDES` | `src` | `MY_LDLIBS`, `LIBALPM_LDLIBS` |
| `UPGRADE_ALL_COMMAND_TEST_TARGET` | `MOGUET_ENABLE_TEST_OVERRIDES`, `MOGUET_ENABLE_TEST_CONFIG_PATH` | `src`, `tests/stubs/package-metadata` | `MY_LDLIBS` |
| `COMMANDS_INSPECT_TEST_TARGET` | `MOGUET_ENABLE_TEST_OVERRIDES` | `src`, `tests/stubs/package-metadata` | `MY_LDLIBS` |
| `COMMANDS_SYNC_TEST_TARGET` | `MOGUET_ENABLE_TEST_OVERRIDES`, `MOGUET_ENABLE_TEST_CONFIG_PATH` | `src` | `MY_LDLIBS`, `LIBALPM_LDLIBS` |
| `AUR_UPDATE_COMMAND_TEST_TARGET` | `MOGUET_ENABLE_TEST_OVERRIDES`, `MOGUET_ENABLE_TEST_CONFIG_PATH` | `src`, `tests/stubs/package-metadata` | `MY_LDLIBS` |

stub ownershipは次のsource境界を正とする。

- `AUR_RPC_VALIDATION_TEST_TARGET`と`UPGRADE_BASELINE_METADATA_TEST_TARGET`では
  `tests/stubs/package-metadata/alpm_stub.cpp`がlibalpm ABIを所有し、`LIBALPM_LDLIBS`をlinkしない。
- `UPGRADE_ALL_COMMAND_TEST_TARGET`では
  `tests/stubs/upgrade-all-command/operation_stub.cpp`が除外した
  `src/upgrade_all_operation.cpp`のoperation APIを所有し、package-metadata alpm stubがlibalpm ABIを
  所有する。
- `COMMANDS_INSPECT_TEST_TARGET`では`tests/commands_inspect_aur_stub.cpp`がAUR client、
  `tests/stubs/commands-inspect/repository_query_stub.cpp`がrepository query、package-metadata alpm stubが
  libalpm ABIを所有し、対応するproduction transportを同時にlinkしない。
- `COMMANDS_SYNC_TEST_TARGET`では`tests/stubs/commands-sync/aur_rpc_stub.cpp`と
  `tests/stubs/commands-sync/root_package_search_stub.cpp`が、除外した2つのproduction transportを
  それぞれ所有する。package metadataはproductionのままなので`LIBALPM_LDLIBS`は維持する。
- `AUR_UPDATE_COMMAND_TEST_TARGET`では
  `aur_update_query.cpp`、`aur_update_execution_preflight.cpp`、
  `aur_update_execution_preparation.cpp`、`aur_update_execution_runner.cpp`、
  `aur_update_operation_result.cpp`、`filtered_aur_update_operation.cpp`、
  `upgrade_all_operation.cpp`を除外する。aur-update / upgrade-all scenario stubと
  package-metadata alpm stubだけがそのtest seamを所有し、`LIBALPM_LDLIBS`をlinkしない。

#### Object・dependency・ccache契約

Slice 2以降のobject分離は次を満たす。

- objectは`build/tests/obj/<binary-name>/`以下のtarget専用directoryへ置く。`src/`、`tests/`、
  `tests/stubs/`以下の相対pathをobject pathにも残し、同名fileの衝突を避ける。異なるtest target間で
  Make objectを直接共有しない。
- 1 sourceにつき1回のcompile invocationとし、`-MMD -MP`でobjectと同じdirectoryへ`.d`を生成する。
  移行済みtargetの`.d`だけを`-include`し、header変更時は依存するobjectだけを再compileする。
- targetごとにeffective `CXX`、`CPPFLAGS`、`LIBALPM_CPPFLAGS`、`CXXFLAGS`、`MY_CXXFLAGS`、
  target固有macro / include pathを記録したcompile signatureを持つ。内容が変わった場合だけstampを
  更新し、そのtargetのobjectを再compileする。command line overrideによるflag変更もtimestampに
  関係なく検出する。
- linkにもordered object set、effective `CXX`、`LDFLAGS`、`MY_LDLIBS`、
  `LIBALPM_LDLIBS`を記録したsignatureを持つ。objectまたはlink条件が変わった場合だけ再linkする。
  現行の重量級recipeが参照していない`LDFLAGS`は、分離後のlink stepで正式に適用する。
- `CCACHE ?=`を空の既定値として定義し、compile recipeだけを`$(CCACHE) $(CXX) ... -c`とする。
  `make CCACHE=ccache ...`で有効化し、未導入環境、通常の`make`、または`CCACHE=`ではwrapperなしの
  buildを維持する。`CCACHE`は生成物のcompile signatureへ含めず、compiler identityと全compile
  inputのcache identityはccacheへ委ねる。link recipeへccacheを付けない。
- `CXX`、`CPPFLAGS`、`CXXFLAGS`、`LDFLAGS`のoverrideを維持する。ccacheをruntime / packageの
  必須dependencyにせず、mold等のlinkerも既定または必須にしない。

#### Link firewall

source listはobject mapping後もtest compositionのauthorityである。各entryをexactly oneのobjectへ
写像し、最終link inputはそのordered object listだけから作る。wildcardでobject directory全体を
linkしてはならない。既存のrequired production source、required test support、forbidden production
sourceのcheckはsource listに対して維持し、source-to-object mappingと最終object listにも重複・欠落が
ないことをstatic checkする。production implementationと同じsymbolを所有するstubを同じbinaryへ
混在させない。

特に`check-upgrade-all-command-link-firewall`と`check-commands-sync-link-firewall`を各test targetの
prerequisiteとして維持する。明示的なfirewallをまだ持たないstub使用targetでは、上記stub ownershipを
required / forbidden source setとして固定し、object化と同時に同等以上のstatic checkを追加する。

#### Slice scopeとbaseline

Slice 2は次の2 targetだけをobject化する。

- `UPGRADE_ALL_COMMAND_TEST_TARGET`
- `AUR_UPDATE_COMMAND_TEST_TARGET`

Slice 3は共通Make helperを最小限に整理しながら、次の残り8 targetをobject化する。

- `COMMANDS_SYNC_TEST_TARGET`
- `COMMANDS_INSPECT_TEST_TARGET`
- `TEST_TARGET`
- `CLI_LOCALIZATION_TEST_TARGET`
- `APP_CONFIG_INTEGRATION_TEST_TARGET`
- `AUR_RPC_VALIDATION_TEST_TARGET`
- `SOURCE_INSTALL_CHARACTERIZATION_TEST_TARGET`
- `UPGRADE_BASELINE_METADATA_TEST_TARGET`

2026-08-06のSlice 1 baselineは、16 logical CPU、GCC 16.1.1、GNU Make 4.4.1、ccache 4.13.6で、
上記10 binary targetを`make -j8 --output-sync=target`へ同時指定して測定した。

| Build mode | Elapsed | ccache result |
| --- | ---: | --- |
| clean、ccacheなし | 233.49 s | 未使用 |
| 変更なしincremental | 0.02 s | compiler invocationなし |
| isolated cold ccache | 232.60 s | cacheable 0 / 10、`Called for linking` 10 / 10 |
| isolated warm ccache + clean rebuild | 233.06 s | 累計cacheable 0 / 20、hit 0、cache file 0 |

cold / warm測定はtemporary `CCACHE_DIR`だけを使用し、global ccacheを参照・消去・変更していない。
現行recipeではccacheが全callをlink invocationと判定するため、warm cacheの効果はない。Slice 2 / 3では
同じtarget集合についてdefault clean、incremental、isolated cold / warm ccache、test behavior、
link firewallを再測定し、このbaselineと比較する。

### Arch Linux container validation

実機Arch Linuxでの最終smoke testより前に、official `archlinux:latest`を使う隔離laneを
明示的に実行できる。Docker CLI、起動中のDocker daemon、およびcommitとして解決できる
local `v1.16.0` tagを用意し、repository rootで次を実行する。

    make test-container

image buildではnetwork利用を許可し、`--pull`でbase imageを確認し、cache miss時はArch
repositoryからdependency packageを取得する。Dockerのlayer cacheは再利用し得るため、このlaneは
base imageとpackage repositoryの現在状態に対するvalidationであり、長期固定された再現imageでは
ない。test containerの実行時は`--network=none`とし、public AUR、real Git clone、actual package
transactionを行わない。

default build contextからsource snapshotだけをcontainer内へcopyし、host worktreeをbind mount
しない。`.git`、host build artifact、XDG data、credential、Docker socket、host pacman database /
config / cacheは共有せず、`--privileged`も使用しない。package transition testのlegacy sourceは
local `v1.16.0` tagからtemporary archiveとして生成し、Git metadataとは分離したnamed build
contextで渡す。build、test、release validationはcontainer固有のHOME / XDG directoryを使う
一般userとして、次の順で実行する。

    env -u MAKEFLAGS -u MFLAGS make clean
    env -u MAKEFLAGS -u MFLAGS make -j8 --output-sync=target
    env -u MAKEFLAGS -u MFLAGS make -j8 --output-sync=target test
    env -u MAKEFLAGS -u MFLAGS make -j8 --output-sync=target release-check

Docker command、daemon、image build、またはvalidation stepが失敗した場合、diagnosticとnon-zero
statusをhostへ返す。実行containerは成功時・失敗時とも`--rm`で破棄し、temporary legacy archiveも
削除する。build cacheとlocal image `moguet-arch-validation:local`は後続実行で再利用できるよう残す。

このlaneはhostの通常build、`make test`、`make release-check`を置き換えず、それらから再帰的に
呼び出さない。release前にはhost validationとcontainer validationを別々に確認する。

Issue #372のlive aggregate gateは、provider selection、real AUR install、real local
PKGBUILD build / installを単一のfail-fast recipeから別containerで順に実行する。parallel makeの
contextでも後続laneを並行開始せず、providerまたはAUR failure後は残りのlaneを開始しない。
current Arch repository、public AUR、
container内のactual package transactionを使うため、`make test` / `make release-check`へ
actual executionを混ぜない。release candidateでは通常のhost / offline validation後に、
明示的に次を実行し、three live laneの結果を個別に確認する。

    make test-container-live

`release-check`はlive targetのisolation、standalone Dockerfile、aggregate compositionを
static `test-live-contract`として確認するが、networkやcontainer runtimeが必要なこのgateの
成功を代替しない。

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
