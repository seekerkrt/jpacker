# Live validation lane contract

このディレクトリは、Issue #372 Slice 1で固定した **live** lane契約と、
Slice 2で実装したprovider-selection laneを収める。

- live laneは既存のoffline validation lane (`containers/arch-validation`) と**完全に別入口**で運用する。
- ベースイメージは `archlinux:latest` を利用する。
- 実行時ネットワークは**有効**。
- ホスト `worktree`、`.git`、credentials、`/var/run/docker.sock`、XDG state、pacman DB/config/cacheを**共有しない**。
- `--mount` などの bind mount は**必須化しない**。
- `docker run --privileged` を使用しない。
- source build は、非 root の検証ユーザーで実行する。
- sudo は将来、`pacman` バイナリへの限定的使用に切り分ける。
- ライフサイクル終了時（成功/失敗含む）はコンテナを破棄する。
- live case については暗黙のフォールバックを許容せず、外部レビューを要求する。
- live lane は `make test` / `release-check` を再帰的に起動しない。

## Slice 2: live provider selection

`make test-container-live-provider`は、`archlinux:latest`をpullして独立imageを
buildし、production `./moguet`、real PTY、real Arch repository metadataを使って、
local PKGBUILDの`cargo` dependencyに対するambiguous provider selectionを検証する。
candidate identityは`extra/rust`と`extra/rustup`のexact集合として解釈し、表示順や
versionは固定しない。集合、origin、repository、重複、またはpresentationがdriftした
場合は、別dependencyや先頭候補へfallbackせずreview-required failureとして停止する。

実package transactionは行わない。imageのprovisioningとMoguet buildが完了した後、
container内のcanonical `/usr/bin/pacman`だけをroot-owned・non-writableなsentinelへ
置き換える。sentinelはproduction Moguetが生成したexactな
`sudo pacman -S --asdeps --needed [--noconfirm] -- extra/{rust,rustup}` argvを
byte-safeに記録し、固定statusで停止する。元のreal pacmanはvalidation userから
実行不能なroot-only pathへ隔離されるため、rust / rustupのreal install、`pacman -U`、
remove、upgrade、複数target、unqualified targetは実行されない。

各caseはtracked fixtureをfresh user-owned directoryへcopyし、そのcopyだけで
unprivileged `makepkg --printsrcinfo`を実行する。tracked fixtureへ`.SRCINFO`を追加せず、
before / after checksum、package database manifest、sentinel log、source / artifact
workspace不在を照合する。Moguetとmakepkgはいずれもvalidation userで動作する。

runtime networkは有効だが、host worktreeや`.git`、credentials、Docker socket、XDG
state、pacman DB/config/cacheをmountしない。containerは`--rm`で破棄する。Dockerの
image / layer cacheはhost localに残り得るが、host systemへpackage mutationは行わない。

残る責務は次のとおり。

- Slice 3: real AUR packageのRPC / source取得 / build / install lane
- Slice 4: real dependency transactionとlocal build / artifact install、およびrelease gate

Slice 2のnon-goalはproduction C++、CLI、help、man、completion、gettextの変更、
rust / rustupのreal install、real AUR packageのbuild / install、local rootのbuild / artifact
install、release docs / checklist、既存offline laneの変更である。
