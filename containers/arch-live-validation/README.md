# Live validation lane contract

このディレクトリは、Issue #372 Slice 1で導入する **live** laneの固定契約を収める。

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
- later slice responsibility:
  - Slice 2: provider selection
  - Slice 3: real AUR
  - Slice 4: local build/install と release gate
