# jpacker バージョン運用方針

このドキュメントでは、jpacker のバージョン番号の付け方とリリース範囲の判断基準を定義する。

jpacker は `MAJOR.MINOR.PATCH` 形式のバージョン番号を使う。ただし、厳密な Semantic Versioning
準拠とは名乗らない。基本判断は Semantic Versioning に近づけ、破壊的変更は `MAJOR`、後方互換な
機能追加や対応範囲の拡大は `MINOR`、bug fix や小さな補正は `PATCH` として扱う。

バージョンの正本はリポジトリルートの `VERSION` ファイルとする。Git tag は `v1.4.0` のように
`v` prefix を付ける。

## 互換性の判断対象

jpacker は CLI ツールなので、互換性は内部 C++ API ではなく、ユーザーから見える挙動を基準に判断する。

互換性の判断対象には、次を含める。

- コマンド名、オプション、引数、その意味
- ユーザーやスクリプトが依存しうる出力形式
- 終了コード
- `/etc/jpacker/jpacker.conf` などの config file format
- `/etc/jpacker/package.build/` などの config / state directory
- pacman、makepkg、git、AUR repository を使う package build の期待挙動
- inspection-only command と build / install / update / reset などを行う command の安全境界

内部の C++ class、関数、ファイル分割は、安定した公開 API としては扱わない。

## MAJOR

`MAJOR` は、後方互換性のない breaking release で上げる。

例:

- ユーザーが CLI の形や通常 workflow を覚え直す必要がある
- 既存コマンドや option を削除する
- 既存コマンドや option の意味を大きく変える
- 既存利用を壊しうる出力形式や終了コードの変更を行う
- config 形式を非互換に変える
- config directory や state directory の場所を変える
- 安全境界を大きく変える
- 新しい大テーマや product direction へ移行する

`v2.0.0` は、`pactune` rename など、identity や workflow に関わる breaking release 候補を扱う
場所として想定する。

## MINOR

`MINOR` は、後方互換な機能追加、機能拡張、対応範囲の拡大で上げる。

例:

- 既存利用を壊さずに新しいコマンドや option を追加する
- 既存コマンドに後方互換な機能的変更を加える
- AUR 対応範囲を広げる
- dependency や PackageBase 処理に、後方互換な能力を追加する
- 既存挙動を保ったまま、新しい workflow を追加する

AUR build plan execution のように、build / install の実行範囲や対応 workflow を広げる変更は、後方互換で
あっても `MINOR` 相当として扱う。

## PATCH

`PATCH` は、bug fix、docs、build / packaging fix、表示改善、エラー文改善、既存機能の小さな補正で上げる。

例:

- bug fix
- docs update
- build / packaging fix
- 既存の意味を保つ表示改善
- エラー文の改善
- 既存機能の小さな補正
- ユーザーから見える挙動を変えない内部整理

PATCH は後方互換な変更だけを含める。ただし、後方互換であっても、新しい command / option、既存 command の
機能的変更、対応 workflow の拡大は原則として `MINOR` として扱う。

## PATCH に含めない変更

次の変更は PATCH release に含めない。

- CLI を覚え直す必要がある変更
- config 形式変更
- config directory 変更
- 安全境界の大きな変更
- 既存コマンドの意味を大きく変える変更
- 新しい大テーマへの移行
- 新しい command / option
- 既存 command の機能的変更
- 対応 workflow の拡大
- 将来の `pactune` transition のような breaking rename や identity change

これらは、後方互換であれば新しい MINOR series、breaking change であれば MAJOR release として扱う。

## 迷った場合

ユーザー影響を正直に伝える、もっとも小さい version bump を選ぶ。

- 既存 workflow が壊れるなら `MAJOR`
- 後方互換な機能追加、機能拡張、対応範囲の拡大なら `MINOR`
- bug fix、docs、build / packaging fix、表示改善、エラー文改善、既存機能の小さな補正なら `PATCH`

判断が難しい場合は、GitHub Issue または Pull Request で議論し、判断理由を残す。
