# jpacker バージョン運用方針

このドキュメントでは、jpacker のバージョン番号の付け方とリリース範囲の判断基準を定義する。

jpacker は `MAJOR.MINOR.PATCH` 形式のバージョン番号を使う。ただし、厳密な Semantic Versioning
としては運用しない。jpacker では release series based versioning として、各 release series に
開発テーマを持たせ、そのテーマに属する後方互換な変更は同じ `MAJOR.MINOR.x` series の中で継続して
取り込めるものとして扱う。

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

`MINOR` は、新しい後方互換な release series を始める場合、または後方互換なまとまった拡張を行う場合に
上げる。

例:

- 新しい主テーマを持つ release series を始める
- 既存利用を壊さずに新しいコマンドや option を追加する
- AUR 対応範囲を広げ、対応 workflow の面を大きく増やす
- dependency や PackageBase 処理に、後方互換な大きめの能力を追加する
- 既存挙動を保ったまま、新しい実験領域を追加する

## PATCH

`PATCH` は、同一 release series 内の後方互換な変更で上げる。

PATCH release には bug fix だけでなく、小〜中規模の後方互換な機能追加を含める場合がある。ただし、その変更は
進行中の release series のテーマに属しており、ユーザーに CLI の覚え直しや config migration を要求しない
ものに限る。

例:

- bug fix
- documentation update
- build / packaging fix
- 既存の意味を保つ表示の明確化
- ユーザーから見える挙動を変えない内部整理
- 現在の release series 内に収まる、小〜中規模の後方互換な機能追加

`v1.4.x` は AUR build/install completion series として扱う。AUR dependency planning、safe fetch、
build/install plan execution の段階導入のように、このテーマを完成させる後方互換な変更は、既存 CLI と
安全境界を維持する限り `v1.4.x` の PATCH release に含めてよい。

## PATCH に含めない変更

次の変更は PATCH release に含めない。

- CLI を覚え直す必要がある変更
- config 形式変更
- config directory 変更
- 安全境界の大きな変更
- 既存コマンドの意味を大きく変える変更
- 新しい大テーマへの移行
- 将来の `pactune` transition のような breaking rename や identity change

これらは、後方互換であれば新しい MINOR series、breaking change であれば MAJOR release として扱う。

## 迷った場合

ユーザー影響を正直に伝える、もっとも小さい version bump を選ぶ。

- 既存 workflow が壊れるなら `MAJOR`
- 後方互換な新テーマや大きめの能力追加なら `MINOR`
- 現在の release series を完成させる後方互換な変更なら `PATCH`

判断が難しい場合は、GitHub Issue または Pull Request で議論し、判断理由を残す。
