# Ambiguous provider selection contract

## 文書の位置づけ

この文書は、dependencyに複数provider候補が残る場合のinvocation-localな明示選択と、選択前のmutation禁止を定めるnormative production contractである。文書の規範上の正本は日本語本文である。

- Origin Issue: [#272](https://github.com/seekerkrt/moguet/issues/272)
- Related Issues: [#97](https://github.com/seekerkrt/moguet/issues/97)、[#217](https://github.com/seekerkrt/moguet/issues/217)、[#268](https://github.com/seekerkrt/moguet/issues/268)、[#271](https://github.com/seekerkrt/moguet/issues/271)、[#266](https://github.com/seekerkrt/moguet/issues/266)、[#267](https://github.com/seekerkrt/moguet/issues/267)
- Related PRs: #341（#272 provider selection）、#277（typed provider origin）
- Update history: Issue #373で旧decision 13の本文から安定contractへ分離。
- Related upper decisions: [decision 1](../DECISIONS.md#decision-1)、[decision 2](../DECISIONS.md#decision-2)、[decision 4](../DECISIONS.md#decision-4)、[decision 5](../DECISIONS.md#decision-5)、[decision 6](../DECISIONS.md#decision-6)、[decision 7](../DECISIONS.md#decision-7)

## Contract本文（日本語normative source of truth）

### Resolution orderとcandidate identity

dependency `bar`の解決では、pacman-firstの既存順序を維持する。

1. official repositoryのexact packageを確認する。
2. repo exact packageがあればrepo dependencyとし、repository orderはpacman / pacman.confの順序に従う。
3. repo exact packageがなければAUR exact packageを確認する。
4. exact packageがなければproviderを探す。
5. providerが0件ならunresolvedとする。
6. providerが1件ならauto-resolveできる。
7. providerが複数ならambiguousとし、候補順で先頭を選ばない。

candidateはsource kind、package name、repository packageならrepository name、AUR packageならPackageBase、provided dependency name、取得可能なversion / constraint metadataを保持する。candidate順はconfigured repository orderと既存AUR aggregation orderを維持し、Moguet独自のscoreやdefault candidateを導入しない。

### Interactive selection

複数providerの選択はinteractive TTYの番号入力だけで受け付ける。候補を番号付きで表示し、defaultを設けず、validな番号1件を明示入力として受理する。empty input、`q`、`quit`、`cancel`、EOFは取消とする。invalidまたはout-of-range inputは再入力を求める。

non-TTYではpromptを開始せず、stdin pipeをprovider selection inputとして暗黙使用しない。`--noconfirm`でも先頭候補やdefault候補を選ばず、ambiguous errorとしてfail closedする。cancel、EOF、non-TTY、`--noconfirm`はmutation可能なrouteをnon-zeroで停止させる。

### Ownership、plan、route

provider choiceはcanonical dependency identityごとにinvocation内だけで共有する。同じdependencyが複数edgeから要求された場合は同じchoiceを使い、incompatible choiceまたはconstraint conflictはmutation前に停止する。choiceをconfigやcacheへ永続化しない。

selected providerはdependency edgeへsource-aware identityとselection provenanceを保持する。selected AUR providerのPackageBaseはBuildPlanのdependency edge、build order、`fetch`対象へ渡す。selected repository providerはAUR repositoryとして取得・buildせず、exactな`repository/package`をofficial dependencyとしてpacman / makepkg側の経路へ渡す。

selected repository providerの`pacman -S --asdeps --needed`成功だけでは、実際にpackage stateが変更されたことを断言しない。transaction outcomeはsource work itemと分離して保持し、authoritativeな変更証拠を取得できないpackage stateは`Unknown`としてaggregateへ伝播する。

対象phaseの全provider choiceとstatic preflightが完了するまで、dependency transaction、Git checkout、makepkg、source-artifact install、pacman、sudoを開始しない。transaction failureはsource mutationへ進む根拠にならず、すでに完了した別phaseやpackage transactionをrollbackしたと報告しない。

`--noconfirm`はprovider selectionを自動化しない。root package discovery、split artifact selection、conflicts / replaces、complete version solverとは別責務である。

## Non-scope / implementationを固定しない範囲

- provider choiceのconfig / cache永続化。
- complete version constraint solver、conflicts / replaces solver、cross-source atomic transaction。
- root package selection、split package artifact selection、fuzzy finder、GUI / TUI。
- candidate model、session、callback、prompt表示の具体実装を恒久固定すること。

## Compatibility

provider順序、TTY / non-TTY、`--noconfirm`、cancel / EOF、selection-before-mutation、selected repository / AUR routeの要約は、[`COMPATIBILITY.md`のdependency provider section](../COMPATIBILITY.md#compat-ambiguous-provider)を参照する。
