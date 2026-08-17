# Contributing to Moguet

[日本語](#日本語) | [English](#english)

---

## 日本語

Moguetは、maintainerが個人で育てている開発中のprojectです。bug report、要望、設計相談、改善提案は歓迎します。共同開発の進め方もまだ手探りなので、projectの状況に合わせて少しずつ整えていくつもりです。

### Discussions、Issue、pull request

質問、bug reportや不具合かもしれない相談、機能要望、設計・workflowの提案、挙動の確認は、原則として[GitHub Discussions](https://github.com/seekerkrt/moguet/discussions)を最初の入口にしてください。

具体的なbugについて再現・観測情報が十分に揃っている場合は、専用の[Bug Issue Form](https://github.com/seekerkrt/moguet/issues/new?template=bug-report.yml)から直接Issueを作成して構いません。bugかどうか、期待する挙動、再現条件がまだ不明確な場合は、DiscussionsのBug / 不具合相談を利用してください。

Discussionで内容が具体的な開発作業として固まった場合は、maintainerが[GitHub Issue](https://github.com/seekerkrt/moguet/issues)へ整理します。報告者にscope / non-scope、Acceptance Criteria、implementation slice、milestone、validation設計、internal ownershipの完成を求めるものではありません。

typoやdocumentationの修正など、動作を変えない明白な小修正は、Issueなしでそのままpull requestを送ってもらって構いません。

一方、次のような変更へ着手する前には、Discussionまたはすでに追跡中のIssueでscopeを相談してもらえると、お互いの手戻りを減らせます。

- user-visibleなbehaviorの変更
- architectureやdependencyの変更
- CLI、設定、file layoutなどpublic contractに関わる変更
- 複数の責務にまたがる変更や大きめの実装

事前相談のない大規模なpull requestは、reviewやmergeを保証しません。相談した場合も、必ず取り込まれる・すぐ返信が来るとは限りません。projectの方向性と合わない場合は、動くcodeでも見送ることがありますし、いただいた報告や提案を参考にmaintainerが別の形で実装することもあります。roadmapやscope、design、merge、releaseの最終判断はmaintainerが行います。

### Security report

Security-sensitiveな内容は、publicなDiscussion、Issue、pull requestへ投稿しないでください。[SECURITY.md](SECURITY.md)を確認し、[GitHub Private vulnerability reporting](https://github.com/seekerkrt/moguet/security/advisories/new)から非公開で報告してください。

### 変更を送るときにお願いしたいこと

確認できた範囲でよいので、試した内容や気づいた懸念点を教えてください。全部を検証していなくても大丈夫です。「ここは試した」「ここはまだ未確認」が分かれば十分助かります。reviewで気になる点が見つかったら、可能な範囲で相談に付き合ってもらえると嬉しいです。

使ったtoolや作り方よりも、確認したこと・試したこと・まだ未確認のことが分かるほうを大切にしています。

### Licenseとthird-party material

Moguetに取り込まれたproject-authored contributionは、現在のMoguetと同じ`GPL-3.0-or-later`で配布されます。contributorが保有するcopyrightをMoguetへ譲渡することは求めません。現時点ではDCOやCLAも要求しません。

提出するのは、自分が提出してよい内容に限ってください。third-partyのcodeや文章を含む場合は、出典とlicenseが分かるようにしておいてください。

### 個人projectとしての運用

この文書は、official Moguet repositoryでのcontributionの受け入れ方についてのものです。GPLが認めるfork・改変・再配布の自由を制限するものではありません。

外部からのcontribution実績がまだ少ないprojectなので、この案内はprojectの成長や経験に合わせて今後見直していきます。迷ったことがあれば、[GitHub Discussions](https://github.com/seekerkrt/moguet/discussions)で気軽に聞いてください。

---

## English

Moguet is a personal project that the maintainer develops on their own, and it's still under active development. Bug reports, feature requests, design discussions, and improvement ideas are all welcome. The collaboration process itself is still taking shape and will be adjusted as the project and its contributions grow.

### Discussions, issues, and pull requests

Questions, bug reports or suspected bugs, feature requests, design or workflow proposals, and behavior clarifications should generally start in [GitHub Discussions](https://github.com/seekerkrt/moguet/discussions).

If you have sufficient reproduction and observation details for a concrete bug, you may open an issue directly with the dedicated [Bug Issue Form](https://github.com/seekerkrt/moguet/issues/new?template=bug-report.yml). If it is not yet clear whether the behavior is a bug, what the expected behavior is, or how to reproduce it, use the Bug category in Discussions.

When a discussion becomes concrete development work, the maintainer will organize it as a [GitHub issue](https://github.com/seekerkrt/moguet/issues). Reporters are not expected to complete the scope, non-scope, acceptance criteria, implementation slices, milestone, validation design, or internal ownership.

For typos, documentation fixes, and other clear changes that don't affect behavior, feel free to open a pull request directly without an issue.

Before implementing the following kinds of changes, please discuss the scope in a Discussion or an existing tracked issue — it helps avoid wasted effort on both sides:

- changes to user-visible behavior
- changes to architecture or dependencies
- changes to public contracts such as the CLI, configuration, or file layout
- larger implementations spanning multiple responsibilities

Large unsolicited pull requests are not guaranteed review or merge. Discussing first also doesn't guarantee a quick reply or that a change will be merged. Some changes may not fit the project's direction even if the code works, and the maintainer may end up implementing something different based on a report or idea instead of merging the submitted code. Final decisions on roadmap, scope, design, merges, and releases rest with the maintainer.

### Security reports

Do not post security-sensitive details in a public Discussion, issue, or pull request. Follow [SECURITY.md](SECURITY.md) and report them privately through [GitHub Private vulnerability reporting](https://github.com/seekerkrt/moguet/security/advisories/new).

### When you submit a change

Please share what you were able to check — what you tried and any concerns you noticed. You don't need to verify everything; it helps a lot just to know what was checked and what wasn't. If review turns up an issue, it'd be great if you could help talk it through or fix it where possible.

The tools or methods used to create a contribution matter less than being clear about what was checked, what was tested, and what is still unverified.

### License and third-party material

Project-authored contributions accepted into Moguet are distributed under `GPL-3.0-or-later`, the license used for current Moguet development. We do not ask contributors to transfer any copyright they hold in their contributions, and we currently require neither a DCO nor a CLA.

Please only submit content you have the right to contribute. If it includes third-party code or text, make sure the source and license are clear.

### Running this as a personal project

This document describes how contributions are handled in the official Moguet repository. It doesn't restrict the freedom to fork, modify, or redistribute Moguet under the GPL.

Since external contributions are still new to this project, this guidance will likely evolve with experience. If anything is unclear, just ask in [GitHub Discussions](https://github.com/seekerkrt/moguet/discussions).
