# jpacker licensing policy

この文書は、jpacker本体とthird-party componentのライセンス運用に関するsource of truthです。ライセンス本文そのものの代替ではありません。jpacker本体へ適用するGNU General Public License version 3の正式全文は、source treeではrepository rootの[`LICENSE`](../LICENSE)、install後は`${PREFIX}/share/licenses/jpacker/LICENSE`を参照してください。Arch packageでは`/usr/share/licenses/jpacker/LICENSE`へ配置します。

## Version boundary

jpackerのライセンス境界は次のとおりです。

> jpacker v1.14.0 and earlier releases were distributed under the MIT License.
> Those historical releases remain available under their original license.
> The current GPL-licensed development series and v1.15.0 or later releases are distributed under GPL-3.0-or-later.

- v1.14.0以前のtag、release、git historyは変更しません。既にMIT Licenseで提供したcopyのpermissionを取り消しません。
- 現在のGPLライセンス開発系列とv1.15.0以降のreleaseには`GPL-3.0-or-later`を適用します。
- v1.14.0以前に適用したMIT全文は、historical noticeとしてsource treeの[`LICENSES/jpacker-MIT-legacy.txt`](../LICENSES/jpacker-MIT-legacy.txt)へ改変せず保持します。install後は`${PREFIX}/share/licenses/jpacker/jpacker-MIT-legacy.txt`、Arch packageでは`/usr/share/licenses/jpacker/jpacker-MIT-legacy.txt`です。

## Project notice

```text
jpacker - an Arch Linux pacman wrapper with AUR and source-build support
Copyright (C) 2025-2026 seekerkrt

The current GPL-licensed development series and v1.15.0 or later releases are
free software: you can redistribute them and/or modify them under the terms
of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later
version.

These versions of jpacker are distributed in the hope that they will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
Public License for more details.

You should have received a copy of the GNU General Public License along with
jpacker. If not, see <https://www.gnu.org/licenses/>.
```

`or later`は、recipientがGPL version 3、またはFree Software Foundationが将来公開するlater versionのいずれかを選べる、という意味です。GPL本文だけではjpackerに`or later`を指定したことにならないため、上記project noticeとpackage metadataで明示します。

## Contributions

jpackerへ取り込まれたproject-authored contributionは、jpackerの一部として`GPL-3.0-or-later`で配布されます。contributorが保有するcopyrightをjpackerへ譲渡することは求めません。現時点ではDCOやCLAも要求しません。

詳しいcontribution processとthird-party materialの方針は、source treeの[`CONTRIBUTING.md`](../CONTRIBUTING.md)を参照してください。

## Why GPL-3.0-or-later

jpackerはv1.15.0開発系列から、Arch Linux `pacman` packageが提供する`libalpm` APIを直接利用し、動的にlinkしています。Arch package metadataは`libalpm`を含む`pacman`を`GPL-2.0-or-later`としており、GNU GPL FAQはGPL-covered libraryへlinkするprogramをcombined programとして扱う立場を示しています。

このためjpackerは、linked combination全体をGPL-covered workとして扱う保守的なproject compliance policyを採用し、later-version optionとの互換性と将来の明確さを優先して`GPL-3.0-or-later`を選択します。libalpmをread-only metadataへ限定し、transactionを`pacman`へ任せるarchitecture上のowner分離は維持しますが、その責務分離とlibrary linkのlicense分類は別の話です。

GPLは商用利用を禁止するlicenseではありません。実行、調査、改変、originalまたはmodified copyの再配布を認めつつ、covered copyを配布する場合のsource提供、license・copyright noticeの保持、同じlicense上の権利の継承、追加制限の禁止などを定めます。正確な条件は必ずsource treeの[`LICENSE`](../LICENSE)、またはinstall済みの`${PREFIX}/share/licenses/jpacker/LICENSE`にある正式全文で確認してください。

## Source distributions

v1.15.0開発系列以降のsource distributionには、少なくとも次を同じrevisionから含めます。

- `LICENSE`
- `docs/LICENSING.md`
- `THIRD_PARTY_NOTICES.md`
- `LICENSES/jpacker-MIT-legacy.txt`
- 監査で必要と確定したthird-party notice files
- buildとinstallに必要なMakefile、PKGBUILD、scripts、および対応するsource code

modified sourceを配布する場合は、GPLv3が求める変更表示、日付、notice、source提供条件を満たす必要があります。source archiveやtagを作る際は、license、notice、build/install scriptsが同じrevisionに揃っていることをrelease checkで確認します。

## Binary distributions and Corresponding Source

projectまたはdownstreamがbinaryを配布する場合は、そのbinaryと同じversionをrebuildできるCorresponding Sourceを、GPLv3 section 6に沿った方法で提供する必要があります。project policyとして、少なくともexact release tag、jpacker source、build/install scripts、package metadata、必要なnoticeへbinaryのrecipientが明確に到達できる状態を維持します。単に別versionのrepository先頭を案内するだけでは、exact binaryのsource確認として扱いません。

2026-07-23の監査時点では、GitHub Releaseにproject-built binary assetはなく、GitLab Release mirrorもtag由来のsource archiveだけです。将来binary assetやpackage registryを追加する場合は、公開前にCorresponding Sourceの提供方法を再監査します。

## Linked/compiled components and external commands

利用形態は次のように区別します。個別component、copyright notice、監査で除外したlicenseの理由は、source treeの[`THIRD_PARTY_NOTICES.md`](../THIRD_PARTY_NOTICES.md)、またはinstall済みの`${PREFIX}/share/doc/jpacker/THIRD_PARTY_NOTICES.md`を参照してください。Arch packageでのinstall先は`/usr/share/doc/jpacker/THIRD_PARTY_NOTICES.md`です。

| 分類 | 現在のcomponent | 境界 |
| --- | --- | --- |
| Direct dynamic link | libalpm、libcurl | jpackerと同じprocessでAPIを呼び、ELF runtime dependencyになる |
| Header-only compiled code | nlohmann-json、toml++ | system headerの実装がjpacker object codeへcompileされる |
| External command | pacman、pacman-conf、makepkg、git、vercmp、その他NOTICE記載のprogram | command line、stdin/stdout、exit statusを介する別processであり、jpackerへlinkしない |
| System/toolchain runtime | glibc、libstdc++、libgcc_s、libmなど | compiler/OSが提供するruntime。applicationが直接採用したlibraryと混同しない |

## System packages and bundling policy

現在のjpacker repositoryとpackageは、libalpm、libcurl、nlohmann-json、toml++のsource treeやlibrary binaryをbundleしていません。libalpmとlibcurlはbuild / runtime、nlohmann-jsonとtoml++はbuild時にArch system packagesから取得します。`ldd`に現れるtransitive dependencyを、jpackerが直接利用・再配布するcomponentとして無差別に列挙しません。

この前提が変わる場合はnoticeをそのまま流用しません。特に次の変更では、source提供、license compatibility、copyright notice、package layoutを改めて監査します。

- library sourceのvendoringまたはcopy
- static link
- shared library binaryの同梱
- generated/amalgamated headerのrepositoryへの追加
- 新しいlinked libraryまたはcompiled header dependencyの追加
- project-built binary assetやinstaller bundleの配布

## Scope and official sources

この文書は法律事務所によるformal legal opinionではなく、確認できた配布物、link形態、upstream noticeに基づくproject compliance policyです。疑義が生じた場合は、要約より正式license本文とupstream一次資料を優先し、必要に応じて専門家へ確認します。

- [GNU GPL version 3 full text](https://www.gnu.org/licenses/gpl-3.0.html)
- [GNU GPL FAQ](https://www.gnu.org/licenses/gpl-faq.html)
- [SPDX GPL-3.0-or-later](https://spdx.org/licenses/GPL-3.0-or-later.html)
- [Arch Linux pacman package](https://archlinux.org/packages/core/x86_64/pacman/)
- [curl license](https://curl.se/docs/copyright.html)
- [nlohmann-json v3.12.0 license information](https://github.com/nlohmann/json/tree/v3.12.0#license)
- [toml++ project and license](https://github.com/marzer/tomlplusplus#license)
- [Bjoern Hoehrmann UTF-8 decoder and license](https://bjoern.hoehrmann.de/utf-8/decoder/dfa/)
