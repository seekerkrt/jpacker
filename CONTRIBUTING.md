# Contributing to Moguet

[日本語](#日本語) | [English](#english)

---

## 日本語

Moguetは、maintainerが個人で育てている開発中のprojectです。bug report、要望、設計相談、改善提案は歓迎します。共同開発の進め方もまだ手探りなので、projectの状況に合わせて少しずつ整えていくつもりです。

### Issueとpull request

typoやdocumentationの修正など、動作を変えない明白な小修正は、Issueなしでそのままpull requestを送ってもらって構いません。

一方、次のような変更は、実装前に軽く[Issue](https://github.com/seekerkrt/moguet/issues)で相談してもらえると、お互いの手戻りを減らせます。

- user-visibleなbehaviorの変更
- architectureやdependencyの変更
- CLI、設定、file layoutなどpublic contractに関わる変更
- 複数の責務にまたがる変更や大きめの実装

相談したからといって、必ず取り込まれる・すぐ返信が来るとは限りません。projectの方向性と合わない場合は、動くcodeでも見送ることがありますし、いただいた報告や提案を参考にmaintainerが別の形で実装することもあります。roadmapやscope、design、merge、releaseの最終判断はmaintainerが行います。

### 変更を送るときにお願いしたいこと

確認できた範囲でよいので、試した内容や気づいた懸念点を教えてください。全部を検証していなくても大丈夫です。「ここは試した」「ここはまだ未確認」が分かれば十分助かります。reviewで気になる点が見つかったら、可能な範囲で相談に付き合ってもらえると嬉しいです。

使ったtoolや作り方よりも、確認したこと・試したこと・まだ未確認のことが分かるほうを大切にしています。

### Licenseとthird-party material

Moguetに取り込まれたproject-authored contributionは、現在のMoguetと同じ`GPL-3.0-or-later`で配布されます。contributorが保有するcopyrightをMoguetへ譲渡することは求めません。

提出するのは、自分が提出してよい内容に限ってください。third-partyのcodeや文章を含む場合は、出典とlicenseが分かるようにしておいてください。

### 個人projectとしての運用

この文書は、official Moguet repositoryでのcontributionの受け入れ方についてのものです。GPLが認めるfork・改変・再配布の自由を制限するものではありません。

外部からのcontribution実績がまだ少ないprojectなので、この案内はprojectの成長や経験に合わせて今後見直していきます。迷ったことがあれば、Issueで気軽に聞いてください。

---

## English

Moguet is a personal project that the maintainer develops on their own, and it's still under active development. Bug reports, feature requests, design discussions, and improvement ideas are all welcome. The collaboration process itself is still taking shape and will be adjusted as the project and its contributions grow.

### Issues and pull requests

For typos, documentation fixes, and other clear changes that don't affect behavior, feel free to open a pull request directly without an issue.

For the following kinds of changes, please open an [issue](https://github.com/seekerkrt/moguet/issues) first — it helps avoid wasted effort on both sides:

- changes to user-visible behavior
- changes to architecture or dependencies
- changes to public contracts such as the CLI, configuration, or file layout
- larger implementations spanning multiple responsibilities

Discussing first doesn't guarantee a quick reply or that a change will be merged. Some changes may not fit the project's direction even if the code works, and the maintainer may end up implementing something different based on a report or idea instead of merging the submitted code. Final decisions on roadmap, scope, design, merges, and releases rest with the maintainer.

### When you submit a change

Please share what you were able to check — what you tried and any concerns you noticed. You don't need to verify everything; it helps a lot just to know what was checked and what wasn't. If review turns up an issue, it'd be great if you could help talk it through or fix it where possible.

The tools or methods used to create a contribution matter less than being clear about what was checked, what was tested, and what is still unverified.

### License and third-party material

Project-authored contributions accepted into Moguet are distributed under `GPL-3.0-or-later`, the license used for current Moguet development. We do not ask contributors to transfer any copyright they hold in their contributions.

Please only submit content you have the right to contribute. If it includes third-party code or text, make sure the source and license are clear.

### Running this as a personal project

This document describes how contributions are handled in the official Moguet repository. It doesn't restrict the freedom to fork, modify, or redistribute Moguet under the GPL.

Since external contributions are still new to this project, this guidance will likely evolve with experience. If anything is unclear, just ask in an issue.
