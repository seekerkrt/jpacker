# jpacker v1からMoguet v2への移行

[English](v1-to-v2.md)

<!-- parity:overview -->
## 概要

Moguet v2.0.0はjpacker v1.16.0からのbreaking transitionです。pacman-firstな実行
基盤を維持しながら、public identity、user storage、config形式、localized
documentationをまとめて変更します。

このGuideは次の非破壊な順序を定めます。

```text
jpacker v1 dataを調査・backup
-> jpacker v1.16.0 packageをremove
-> 検証済みMoguet v2 packageをinstall
-> 理解できる設定だけを手動移行
-> command、man、completion、locale、pathを検証
```

Moguetは`/etc/jpacker/jpacker.conf`を通常config layerとして使用しません。
`/etc/jpacker`を自動copy・rewrite・deleteせず、root-owned dataを受け取るuserを
推測しません。既存の`/etc/jpacker/package.build/` source-preference storeは後述する
別のcompatibility境界であり、TOML configではありません。

<!-- parity:preparation -->
## 始める前に

1. package操作を完了または停止します。pacman、makepkg、他のpackage helperがsystemを
   変更している間にmigrationを始めないでください。
2. installed legacy packageがjpacker v1.16.0であることと、そのinstall方法を記録します。
3. remove前にrelease固有のMoguet package手順を確認します。packageのconflict /
   coexistence policyと一時的command aliasの有無はpackaging decisionであり、公開前に
   このGuideから仮定しません。
4. rollback用に、信頼できるjpacker v1.16.0 packageまたはsource archiveを確保します。
5. local userごとにmigrationを分けます。root-owned legacy directoryだけでは移行先userを
   特定できません。

変更前にinstalled packageを記録する例です。

```bash
pacman -Q jpacker
pacman -Qi jpacker
```

これらがpackageなしと報告した場合は、package removal commandを使う前に実際のinstall
方法を特定してください。

<!-- parity:identity -->
## Identityの変更

| Surface | jpacker v1.16.0 | Moguet v2.0.0 |
| --- | --- | --- |
| Project / brand | jpacker | Moguet |
| 読み | — | モグエット |
| CLI / binary | `jpacker` | `moguet` |
| Package | `jpacker` | `moguet` |
| XDG application名 | legacy jpacker path | `moguet` |
| Project environment prefix | legacy name | `MOGUET_*` |
| Runtime localization | legacy English-only surface | English authority、日本語正式翻訳 |

<code>MU<!-- rejected alternate spelling -->GUET</code>と
「ミュ<!-- rejected alternate reading -->ゲ」はaliasではありません。command / option
tokenは翻訳しません。正式なv2 commandは`moguet`です。localで`jpacker` symlinkを作り、
packaging supportがあると仮定しないでください。

source / repository URLは最終external identity cutoverまで`jpacker`を含む場合があります。
remoteやpackage sourceを先行変更せず、公開済みrelease linkに従ってください。

<!-- parity:backup -->
## v1 dataをbackupする

packageをremoveする前にprivate backupを作成します。最低でも次を保持してください。

- installed jpacker versionとpackage metadata
- `/etc/jpacker/jpacker.conf`（存在する場合）
- `/etc/jpacker/package.build/`の内容・ownership・mode（存在する場合）
- `LOGFILE`が指定するcustom log
- 通常のpackage payload外で変更したpackage / source file

archive方法の一例です。

```bash
backup_root="$HOME/moguet-migration-backup-$(date +%Y%m%d-%H%M%S)"
install -d -m 700 "$backup_root"
pacman -Q jpacker > "$backup_root/jpacker-package.txt"
pacman -Qi jpacker > "$backup_root/jpacker-package-info.txt"
sudo tar --acls --xattrs -C /etc -cpf - jpacker \
    > "$backup_root/etc-jpacker.tar"
tar -tf "$backup_root/etc-jpacker.tar"
```

`/etc/jpacker`が存在しない場合だけ、そのarchive stepをskipします。失敗したarchiveや
empty archiveはbackupではありません。command statusとlistingを確認してください。
設定を失う影響が大きい場合はmachine外にもcopyを保管します。

test対象userに既存Moguet XDG directoryがある場合は、それも先に保持します。standard
locationは次のとおりです。

```text
${XDG_CONFIG_HOME:-$HOME/.config}/moguet
${XDG_STATE_HOME:-$HOME/.local/state}/moguet
${XDG_CACHE_HOME:-$HOME/.cache}/moguet
```

backup時にこれらを`/etc/jpacker`とmergeしないでください。

<!-- parity:remove-v1 -->
## jpacker v1.16.0をremoveする

backupを検証したら、installに使ったpackage managerでpackageをremoveします。通常の
pacman-managed installでは、慎重な形式は次です。

```bash
sudo pacman -R jpacker
```

renameだけを理由にrecursive dependency cleanupを追加しないでください。confirm前にpacmanが
提案するtransactionを確認します。別の方法でinstallしたpackageは、pacmanがownerであると
見なさず、その方法のdocumented uninstall pathを使ってください。

package removalを`/etc/jpacker`のcleanup commandとして使ってはいけません。migrationと
rollback validationの両方が完了するまで、backup、preserved `.pacsave`、preference fileを
保持します。

<!-- parity:install-v2 -->
## Moguet v2をinstallする

jpacker v1 packageのremoveが完了し、release固有手順でv2 package source、signature /
checksum、dependency、file conflict、payloadを検証してからMoguetをinstallします。

package identityは`moguet`、executableは`/usr/bin/moguet`です。package endpointが正式公開
されるまで、このGuideはAUR URLや`pacman -S`のrepository commandを作りません。検証済み
transitionの代わりにdevelopment treeの`make install`を旧packageへ重ねないでください。

package installは`/etc/moguet`、user XDG config file、user XDG state / cache directoryを
作成してはいけません。Moguetは実際のcommandが必要とするuser directoryだけを、その
実行userのXDG context内へ作成します。

<!-- parity:configuration -->
## Configを手動移行する

Moguetは次のoptionalなuser-owned fileを使います。

```text
$XDG_CONFIG_HOME/moguet/config.toml
fallback: ~/.config/moguet/config.toml
```

non-default値を必要とする特定userについてだけ作成します。minimal schemaは次です。

```toml
schema_version = 1

[review]
pkgbuild = "prompt"
diff = "prompt"

[build]
mode = "normal"
```

理解できるjpacker v1 keyだけを対応付けます。

| jpacker v1 setting | Moguet v2 action |
| --- | --- |
| `NOEDIT=true` | `review.pkgbuild = "skip"`を設定 |
| `NOEDIT=false` | keyを省略、または`review.pkgbuild = "prompt"` |
| `NODIFF=true` | `review.diff = "skip"`を設定 |
| `NODIFF=false` | keyを省略、または`review.diff = "prompt"` |
| `EDITOR=...` | TOMLへcopyせず、user environmentの`VISUAL`、次に`EDITOR`を設定 |
| `LOGFILE=...` | v2.0.0 config keyなし。固定XDG state logを使用 |
| `RMDEPS=true` | 移行しない。separated source-build dependency cleanupはunsupported |

editor解決順は`VISUAL -> EDITOR -> nano`です。default logは次です。

```text
$XDG_STATE_HOME/moguet/moguet.log
fallback: ~/.local/state/moguet/moguet.log
```

Moguetはconfigをstrictなread-only inputとして読みます。fileが存在すれば
`schema_version = 1`が必須で、unknown key、invalid type / enum、future schema versionは
external mutation前に失敗します。broken fileをrewriteしたり、黙ってdefaultへfallback
したりしません。

<!-- parity:legacy-data -->
## Legacy preferenceとdataを扱う

`/etc/jpacker`全体をMoguet XDG directoryへcopyしないでください。この実装では
`/etc/jpacker/package.build/`がsource-build preference storeとして残り、source operationが
直接読みます。そのfileはTOML config tableではありません。
`moguet add-src <pkg> [V=K]` interfaceも同じstoreへ書くため、既存entryをmigration手順として
再登録しないでください。storeをbackupして変更せずに保持し、package ownershipと
coexistenceの扱いはrelease固有のpackage transition手順に従ってください。

Moguetは次を自動実行しません。

- `/etc/jpacker/jpacker.conf`をconfig layerとして読む
- `/etc/jpacker`を1人以上のuser homeへcopyする
- legacy fileをdelete / rewriteする
- `/etc/moguet`を作成・参照する
- `LOGFILE`、`RMDEPS`、arbitrary editor command、credential、shell fragment、unknown
  uppercase keyを移行する
- root ownership、`sudo`、`SUDO_USER`から移行先userを推測する

config、state、cacheはv2で別の責務を持ちます。

```text
config: $XDG_CONFIG_HOME/moguet/  (fallback ~/.config/moguet/)
state:  $XDG_STATE_HOME/moguet/   (fallback ~/.local/state/moguet/)
cache:  $XDG_CACHE_HOME/moguet/   (fallback ~/.cache/moguet/)
```

cacheは再生成可能でありbackup先ではありません。Moguet uninstallをuser config、state、
cacheの削除許可として扱わないでください。

<!-- parity:verification -->
## Migrationを検証する

対象の通常userとして、`sudo`を付けずに確認します。

```bash
command -v moguet
moguet --version
LC_ALL=C moguet --help
```

次にpackageが提供するdocumentationとcompletion fileを確認します。

```bash
man -w moguet
LC_ALL=C man moguet
LANG=ja_JP.UTF-8 man moguet
```

期待するstandard pathは次です。

```text
/usr/share/man/man1/moguet.1
/usr/share/man/ja/man1/moguet.1
/usr/share/bash-completion/completions/moguet
/usr/share/zsh/site-functions/_moguet
/usr/share/fish/vendor_completions.d/moguet.fish
/usr/share/locale/ja/LC_MESSAGES/moguet.mo
```

新しいshellを起動するか、使用shellがdocumentするcompletion mechanismだけをreloadします。
systemがman page cacheを使う場合は、通常のpackage hookまたはadministrator procedureで
更新します。completionがlegacy `jpacker` commandではなく、`moguet` commandと
`--edit`、`--diff`、`--build-mode=normal|rebuild|clean`等のfinal optionを提示することを
確認してください。

最後に、意図したuserのresolved XDG pathを確認します。help / version確認だけではconfig、
state、cache directoryは作られません。operationのexternal commandとpackage effectを
reviewしてから実行testへ進んでください。

<!-- parity:rollback -->
## jpacker v1.16.0へrollbackする

rollbackは明示的なpackage transitionであり、自動transaction rollbackではありません。

1. Moguetを停止し、activeなpackage operationを完了します。
2. userのMoguet configとstateをbackupします。cacheは診断に必要な場合だけ保持します。
3. installに使ったpackage managerでMoguet packageをremoveします。package removalの一部
   としてuser XDG directoryを削除しません。
4. migration前に記録した信頼できるjpacker v1.16.0 package / source archiveをreinstallします。
5. current destinationを確認したうえで、verified backupからだけ`/etc/jpacker`をrestore
   します。新しいfileへ盲目的にoverwriteしないでください。
6. package transaction前に`jpacker --version`、v1 man / completion、read-only operationを
   検証します。

MoguetのXDG dataをjpacker v1は解釈しないため、後の再試行用に保持できます。完了済み
pacman transactionはhelperを切り替えても戻りません。helper rollbackがinstalled packageを
変更したと仮定せず、実際のpackage database stateを比較してください。

<!-- parity:maintenance -->
## v1 maintenanceとexternal cutover

jpacker v1は`jpacker` identityとv1 release tagの下で維持します。Moguetへ名称変更せず、
v1.16.0へこのGuideだけでv2 XDG / config形式を導入しません。

このGuide作成時点のpublic repositoryには専用v1 maintenance branchがありません。
`develop`はMoguet v2 integration branchであり、v1 maintenance sourceとして扱わないで
ください。v1 maintenance package / branchの最終公開位置とold URL redirectはexternal
release cutoverで確定します。branchやendpointを推測せず、公開済みv2.0.0 release noteが
指定する位置を使用してください。

現在のsource contractは[README.md](../../README.md)、
[README.ja.md](../../README.ja.md)、
[COMPATIBILITY.md](../COMPATIBILITY.md)を参照してください。
