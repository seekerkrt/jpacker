# jpacker 互換性と pass-through policy

`jpacker` は Arch Linux 向けの **pacman-first wrapper** として扱う。

目的は、日常的な `pacman` workflow をできるだけ自然に通しつつ、AUR build / install と source build preference を補助することにある。jpacker は Arch Linux / pacman / AUR の公式ツールではなく、現時点では `pacman` や既存 AUR helper と同じ挙動をすべて提供するものでもない。pacman options / flags の対応範囲は段階的に実装・検証しており、完全互換も宣言しない。

この文書は、pacman / makepkg 由来の operation / option をどこまで pass-through するか、また jpacker 固有の operation / option とどう分けるかを固定するための方針である。実装がまだ追いついていない部分は「暫定」または「未整理」として扱う。

---

## 基本方針

- jpacker が明示的に扱う operation / option は、jpacker 側で解釈する。
- jpacker 固有 option は、pacman execution や review 前の `.SRCINFO` 更新判定へ渡さない。
- pacman が自然に扱える operation / option は、可能な範囲で pacman へ pass-through する。
- AUR / source build 経路では、pacman transaction option を無条件に makepkg へ置き換えない。
- jpacker 固有 operation の未対応 option と、AUR / source build 経路へ安全に反映できない pacman option は、黙って無視せず実行前に停止する。
- `--noconfirm` は「全部 yes」ではなく、「対話で止まらない」指定として扱う。
- `--noconfirm` だけで rebuild / cleanbuild / provider selection / split package selection / conflicts / replaces / 未解決依存の突破を勝手に有効化しない。
- 値を取る option は、値を target と誤認しないように扱う。値が欠けている場合は停止する。

---

## jpacker 固有 operation

次の operation は jpacker 固有であり、pacman へそのまま委譲しない。

- `build <pkg> [VAR=VALUE...]`
- `upgrade`
- `clean`
- `deps [--recursive] <pkg>`
- `plan <pkg>`
- `fetch <pkg>`
- `-G <pkg>`
- `-Gp <pkg>`
- `add-src <pkg> [VAR=VALUE...]`
- `del-src <pkg>`
- `edit-src <pkg>`
- `list-src`
- `revert <pkg>`

`deps`、`plan`、`fetch`、`-G`、`-Gp` は調査・表示・取得段階の operation であり、build / install を混ぜない。`fetch` は internal cache に対する未取得 repository の clone と、既存 clone に対する `git fetch origin` までに留める。`-G` / `-Gp` は root PackageBase だけを一時 clone し、dependency plan や internal cache を使わない。

`deps` の `--recursive` を除き、jpacker 固有 operation に未対応 option を指定した場合は停止する。pacman option と同名でも、jpacker 固有 operation から pacman へ暗黙に転送しない。

`foo>=1.2` のような AUR dependency version constraint は、v1.x では検出・表示するが、pacman / libalpm 相当の完全な solver としては判定しない。package name 部分で解決できる場合も、version constraint を満たしているとは断定せず、未検証の constraint は warning または unresolved reason として扱う。

---

## AUR PKGBUILD export policy

`-G <pkg>` / `-Gp <pkg>` は exactly one target の jpacker 固有 AUR-only operation とする。official repository probe、source-build preference、repository fallback は使わず、AUR RPC の exact package info で requested `Name` と effective `PackageBase` を確定する。repository-qualified target、invalid identifier、missing / multiple target、jpacker global option、pacman option、`--` marker は AUR RPC または filesystem mutation より前に拒否する。option value や `--` 後の token を global option として再解釈しない parser role 契約は維持する。

`-G` は command 開始時に current directory を open / canonicalize し、その directory descriptor と device/inode identity を operation 終了まで固定する。destination はその descriptor が指す directory の direct child `<cwd>/<PackageBase>` だけとする。途中で pathname が rename / replacement されても、新しい pathname 側へ publish しない。split package では requested name ではなく PackageBase を directory 名に使う。destination に directory、git repository、regular file、symlink、dangling symlink、special file を含む何らかの entry が存在すれば fail-closed とし、fetch / pull / reset / merge / overwrite / remove を行わない。`--noconfirm` 自体が unsupported であり、existing path guard を突破しない。

clone は current directory 直下の invocation-owned secure temporary directory へ行う。clone 前に target / AUR schema / requested Name / PackageBase / destination / containment を preflight し、clone 後に regular directory、regular `.git` directory、local `remote.origin.url` の expected AUR URL との厳密一致、regular non-symlink `PKGBUILD` を確認する。temporary parent / root も directory descriptor と device/inode identity で固定し、clone、validation、recursive cleanup は descriptor 相対で行う。publish 直前にも checkout と destination を再検証し、directory descriptor 相対の no-replace rename で final path へ移す。identity mismatch を観測した場合は named path のcleanup / publishを拒否し、replacementを削除しないため、手動確認が必要な temporary artifact を残し得る。

destination の同時作成は `RENAME_NOREPLACE` でatomicに拒否する。一方、Linuxには「このinodeと一致する場合だけrename / unlinkする」syscallがないため、同一UID processがfinal identity checkとname-based syscallの極小区間でtemporary nameを敵対的に置換するケースまで原子的には保証しない。通常のfailure cleanupでは各entryのidentityをsyscall直前に再確認してこの窓を狭めるが、このoperationと同時にtemporary pathを変更する同一UID processが存在しないことを前提とする。

`-Gp` は AUR RPC で解決した PackageBase repository を descriptor で固定した secure temporary directory へ clone し、root の regular non-symlink `PKGBUILD` を `openat(O_NOFOLLOW | O_NONBLOCK)` / `fstat` して同じ file descriptor から読み取る。clone、validation、read、temporary cleanup がすべて成功した後にだけ PKGBUILD bytes を stdout へ書き、heading、color、command log、mapping、diagnostic は stdout へ混ぜない。stdout への書き込み開始前の failure では stdout を空に保ち、error は stderr へ出す。通常成功時と通常の failure 時には persistent checkout を残さず、current directory と jpacker cache を変更しない。temporary path の identity が変化した場合だけは replacement 保護を優先し、手動確認用 artifact を残し得る。

両 operation は root PackageBase repository だけを扱う。dependency repository / provider repository の取得、dependency build plan、PKGBUILD evaluation、`.install` / `.SRCINFO` の生成・出力、makepkg、pacman / sudo、editor、build / install は実行しない。既存の `fetch <pkg>` は recursive dependency plan を internal cache へ取得し、既存 clone を `git fetch origin` する別 operation であり、export 元・export 先として流用しない。

---

## AUR conflicts / replaces metadata policy

AUR RPC の `Conflicts` / `Replaces` は dependency resolution の入力とは分け、package metadata risk として raw value を保持する。jpacker v1.x は libalpm 相当の transaction solver を持たないため、この metadata が存在する plan を「安全に検証済み」と扱わない。

command ごとの扱いは次の通りとする。

- `-Si <aur-pkg>` は従来どおり `Conflicts With` / `Replaces` を package metadata として表示する。
- `deps <pkg>` は対象 AUR package の metadata warning を dependency 分類とは別に表示する。warning 自体は inspection failure とせず、return code 0 を維持する。
- `plan <pkg>` は recursive plan に含まれる root / dependency AUR package の metadata を package 名と PackageBase に結びつけて表示する。同一 package の重複は除き、risk が 1 件でもあれば incomplete plan とする。plan を表示できた場合の return code は 0 のままとする。
- `fetch <pkg>` は metadata risk を表示するが、unresolved dependency、ambiguous provider、cycle がなければ PackageBase の clone / `git fetch origin` を許可する。fetch は working tree update、build、install、transaction を行わない。
- AUR build/install plan は metadata risk が 1 件でもあれば clone / fetch / build / makepkg / pacman transaction より前に停止する。`--noconfirm` はこの停止を突破しない。

jpacker は installed/local DB や repo sync DB と metadata を照合して実際の衝突有無を判定しない。`Replaces` を dependency resolution や install target 選択に利用せず、package 削除・置換・provider 選択も自動実行しない。source-build preference が付いた official repository package は AUR metadata plan の対象外であり、従来どおり pacman / Arch packaging 側の情報を正とする。

---

## AUR dependency provider selection policy

jpacker は dependency provider の選択についても、独自 solver や独自 repository 優先順位を増やしすぎない。現時点では、まず pacman / makepkg の流儀を優先し、jpacker が扱う AUR helper 的な領域では曖昧なものを暗黙に成功扱いしない、という基本方針として扱う。

dependency `bar` を解決する場合、基本順序は次の通りとする。

1. official repository の exact package として解決できるか確認する。
2. repo exact package がある場合は repo dependency として扱う。repository order は `pacman.conf` / pacman の解決順に従い、jpacker 独自の repository 優先順位は作らない。
3. repo exact package がなければ、AUR exact package として解決できるか確認する。
4. exact package が repo / AUR のどちらにもなければ provider を探す。
5. provider が 0 件なら unresolved として扱う。
6. provider が 1 件なら auto-resolve してよい。`deps` / `plan` では provided dependency として表示する。
7. provider が複数なら ambiguous provider として扱い、暗黙に最初の 1 件を選ばない。

複数 provider の選択 prompt、direct target に対する provider selection、provider choice の保存は、jpacker v1.9.0 / #97 の必須範囲には含めない。必要に応じて後続 Issue で扱う。

### command ごとの扱い

`-Ss` は search / discovery として扱う。provider selection や unresolved stop とは分け、候補を表示するだけで provider を選択しない。

`deps` は dependency inspection / classification として扱う。repo exact、AUR exact、provided dependency、ambiguous provider、unknown dependency、version constraint 未検証を分類して表示する。provider が複数ある場合は ambiguous として候補一覧を表示し、ここでは選択しない。

`plan` は build / install そのものではなく、build plan の可視化・診断として扱う。可能な範囲で build order を表示し、unresolved dependency、ambiguous provider、cyclic dependency、未検証 version constraint、split package install target が残る場合は complete plan ではなく incomplete plan として表示する。`jpacker plan <pkg>` 自体は、情報を表示できたなら基本的に成功扱いでよい。ただし target package が見つからない、引数が不正、AUR RPC が失敗するなど、plan 作成自体ができない場合は失敗扱いにしてよい。

build / install / fetch 実行系では、ambiguous provider、unresolved dependency、cyclic dependency が残る plan は実行不可として停止する。#96 の方針により、未検証 version constraint を理由に unresolved とした dependency も実行不可のまま扱う。

---

## plan official repository package size metadata policy

`jpacker plan <pkg>` は、AUR build graph の既存表示に続く jpacker-owned presentation として、official repository dependency の package size metadata を表示する。この metadata は `BuildPlan` の graph safety や実行可否を構成せず、`BuildPlan::order` に並ぶ AUR build unit の size も推定しない。

size candidate は `BuildPlan::dependency_edges` の first-seen order から次のように抽出する。

- `DependencyKind::Repo` で `resolved_package_name` がある edge は、configured repository precedence でpackageを検索する。
- `DependencyKind::Provided` で一意な `resolved_provider` があり、そのrepositoryがconfigured repositoryに含まれ、かつ`aur`ではないedgeは、providerのrepositoryをexact指定してpackageを検索する。
- AUR dependency / provider、ambiguous provider、unknown dependency、resolved valueの欠落、unconfiguredまたはstaleなrepositoryを指すproviderはcandidateにしない。

repository名とprecedenceは`pacman-conf --repo-list`のconfigured orderだけを正本とする。hard-coded repository list、sync directory走査によるfallback、unconfigured databaseの採用、alphabetical sortは行わない。configured repositoryの既存sync DBは順番にすべてregister、validate、package cache preloadし、1件でも失敗した場合はsession全体をunavailableとする。これは壊れた先行repositoryをskipして同名packageのprecedenceを変えないためである。sync DBはread-only metadata sourceとして参照するだけで、refresh / update、server設定、transaction、database mutationを行わない。

configured precedence lookupはrepositoryをconfigured orderで照会し、同名packageが複数にあれば最初のrepositoryのpackageを採用する。explicit not-foundの場合だけ次のrepositoryへ進み、query failureでは後続へfallbackしない。exact provider lookupは指定されたconfigured repositoryだけを照会し、他repositoryへfallbackしない。

candidateとquery cacheのsemantic identityは`(exact_repository_name, package_name)`とする。したがって、同じpackage名でもconfigured precedence lookupとexact provider lookupは別candidateである。query成功後はreturned `(repository_name, package_name)`をdisplay identityとして重複表示を除き、同じ実packageへ解決したdirect lookupとexact lookupを1表示にまとめる。異なるrepositoryの同名package、ならびにrepository identityを持たないnot-found / failureは別の結果として扱い、edgeのfirst-seen orderを維持する。

`Package size`はcompressed repository package archive、`Installed size`はinstalled contentsのsizeである。0 bytesはknown zeroとして表示し、not foundやunavailableへ変換しない。表示はlocaleとfloating-pointに依存しないbase 1024のIEC unit (`B` / `KiB` / `MiB` / `GiB` / `TiB` / `PiB` / `EiB`)を使う。1024 bytes未満はinteger `B`、それ以上は小数2桁のround-half-upとし、丸め結果が`1024.00`になれば次のunitへpromoteする。

package absence、query failure、malformed metadata、configuration/session failureは区別する。failureをnot foundや0 bytesへflattenせず、user-visible outputには安定した分類だけを表示する。metadata failureがあっても既存のplan本文と他package / targetの表示を続け、metadata availabilityだけを理由にexit statusをnon-zeroへ変えず、graph safetyの正本である`Plan status`へ混ぜない。

このpresentationは`plan`だけに閉じ、pacman passthrough、`-Ss` / `-Si` routing、dependency resolution、provider selection、fetch、build / install lifecycle、transaction behaviorを変更しない。

---

## AUR split package install target policy

AUR metadata の `PackageBase` は clone / fetch / build repository の単位であり、package name は install 対象である。単体 package では結果として一致してよいが、split package では一致するとは限らない。

jpacker v1.9.0 / #98 では、`PackageBase` と install target package name を別概念として扱う。jpacker が install target を安全に一意決定できない場合は、暗黙に先頭候補や PackageBase 名を選ばない。

現時点の具体挙動は次の通り。

- `deps <pkg>` は入力 target を package name として AUR RPC info を確認し、`Package` と `Package Base` を表示する。
- `plan <pkg>` は、AUR RPC info 上で `Name` と `PackageBase` が異なる target を `Split package install targets` として表示し、install target selection 未実装の incomplete plan として扱う。
- `fetch <pkg>` は PackageBase 単位の取得操作であるため、package name から PackageBase へ解決でき、ambiguous provider / unresolved dependency / cyclic dependency が残らない場合は実行してよい。
- `-G <pkg>` は root PackageBase repository を current directory の `./<PackageBase>` へ export し、`-Gp <pkg>` はその root `PKGBUILD` だけを stdout へ表示する。read-only inspection なので `Name` と `PackageBase` の相違自体では停止せず、dependency repository や split package install target selection は扱わない。
- `-S <pkg>` などの install 経路と `build <pkg>` は、fresh `PKGDEST` へ single artifact を build して typed `sudo pacman -U` で install する separated 経路である。`Name` と `PackageBase` が異なる AUR target では、install target を一意に決められないため clone / build / install 前に停止する。
- `--noconfirm` は split package install target selection 未実装を自動承認しない。

この guard は、AUR RPC info で requested package の `Name` と `PackageBase` が異なる場合を対象にする。`PackageBase == Name` だが同じ PackageBase から sibling package も生成されるケースの完全な列挙・選択は、`.SRCINFO` / generated package list / package file selection と合わせて後続 Issue で扱う。

---

## jpacker 固有 option

次の option は jpacker 固有として扱う。

- `--noedit`
- `--nodiff`
- `--rebuild`
- `--cleanbuild`
- `--rmdeps`
- `--aur`
- `--repo`

`--noedit` は build/install 前の PKGBUILD / `.install` review / edit prompt を省略する。
`.install` review は PKGBUILD を評価して `install=` を解決するものではなく、作業ツリー直下の `*.install` を見落としにくくするための案内である。

`--nodiff` は既存 cache repository 更新後の diff prompt と、diff 対象ファイルの案内を省略する。
初回 clone では比較元になる既存 checkout がないため、update diff prompt は出ない。build/install 前の review prompt で PKGBUILD / `*.install` の存在を確認する。
既存 cache repository では `git fetch origin` 後、reset 前に `HEAD..origin/<branch>` の diff を確認できる。この diff は「現在 cache にある checkout から、取得した remote branch へ進めた場合の変更」を示す。

`--rebuild` は AUR / source build の build-only makepkg 実行時に `-f` として扱う。`--cleanbuild` は同じ実行時に `-C` として扱う。これらは pacman 由来 option ではないため、pacman execution へは渡さない。

`--rebuild` / `--cleanbuild` が未指定の場合、jpacker は既存の package artifact や `src/` directory がある場面で、必要に応じて default no の prompt で rebuild / cleanbuild を確認する。cleanbuild を有効にし、同じ package directory に既存 package artifact がある場合は `-f` も有効にする。`--noconfirm` 指定時は prompt を出さず、未指定の rebuild / cleanbuild は no として扱う。

`--rmdeps` は jpacker 固有 option として認識するが、separated AUR / source-build 経路では未対応とする。source target を含む invocation では all-target preflight で拒否し、artifact workspace 作成、makepkg、installed metadata query、sudo のいずれも開始しない。`makepkg -r` へは変換せず、`--noconfirm` を併記しても拒否を突破しない。

jpacker は `pacman -Rns`、`pacman -Qdt`、独自 orphan cleanup を追加しない。pacman-only 経路や official repository package の通常 install では `--rmdeps` を pacman へ渡さず、作用させない。

`--aur` / `--repo` は package source を invocation 単位で限定する selector である。pacman / makepkg へは渡さず、下記の source selection policy に従って jpacker が routing に使う。

---

## Package source selection policy

source selection は排他的な 3 状態として扱う。

- Auto: selector 未指定時の default。既存の自動分類を変更しない。
- AurOnly (`--aur`): root target を AUR に限定し、official repository へ fallback しない。
- RepoOnly (`--repo`): target を official binary repository に限定し、AUR / source build へ fallback しない。

同じ selector の重複指定は idempotent として許可する。`--aur` と `--repo` が同じ invocation に存在する場合は、順序にかかわらず conflict とし、pacman / sudo / AUR RPC / git / makepkg や cache mutation より前に停止する。`--noconfirm` は selection、conflict、not-found、build plan の安全 guard を変更しない。

selector の初期対応 scope は、plain sync install (`-S`)、sync search (`-Ss`)、sync info (`-Si`) に限る。plain sync install には refresh / upgrade / clean modifier を含めない。`upgrade`、`-Syu`、`-Su`、`-Sy`、`-Qua`、`-Q`、`-F`、`-U`、`build`、`fetch`、`deps`、`plan`、`-G`、`-Gp` など、scope 外の operation で selector を認識した場合は、黙って無視せず外部コマンド前に停止する。

Auto の契約は次のとおりであり、selector 追加後も維持する。

- plain `-S`: official repository package は binary repository、source build preference がある official package は official source-build route、official repository にない package は AUR route を使う。
- `-Ss`: official repository search と AUR search を組み合わせる。
- `-Si`: official repository を優先し、見つからない場合だけ AUR metadata へ fallback する。

AurOnly の契約は次のとおりとする。

- plain `-S`: official repository probe と source build preference を root target の分類に使わず、全 root target を AUR RPC / PackageBase / AUR build plan で preflight する。official repository に同名 package があっても AUR を選ぶ。AUR build plan 内の official dependency は既存の dependency 処理へ委ねてよい。全 root target の validation、AUR info、PackageBase、build plan、executable-install guard、metadata risk guard が成功した後にだけ execution へ進み、後続 target の明白な失敗より前に先行 target を clone / build / install しない。AUR に存在しなければ明示的に失敗し、repository へ fallback しない。
- `-Ss`: AUR search だけを実行する。repository search へ fallback せず、match がなければ non-zero とする。
- `-Si`: AUR RPC info だけを使い、repository info へ fallback しない。AUR に存在しない target は not-found とする。
- `core/filesystem` のような repository-qualified target は AUR package name へ暗黙変換せず、AUR RPC や外部コマンドより前に invalid target として停止する。

RepoOnly の契約は次のとおりとする。

- plain `-S`: per-target の repository/AUR classification probe を行わず、selector だけを除いた ordered pacman argv を一度の binary repository transaction へ渡す。source build preference、AUR RPC、AUR build/install は使わない。package が repository に存在しない場合は pacman の failure を返し、AUR へ fallback しない。repository-qualified target を許可する。
- `-Ss`: official repository search だけを pacman へ委譲し、AUR search / RPC を実行しない。
- `-Si`: official repository info だけを pacman へ委譲し、missing target でも AUR へ fallback しない。qualified / unqualified target の両方を許可する。

source build preference は Auto でのみ従来どおり有効である。`--repo` は preference がある package でも、この invocation だけ official binary package を選び、preference file を変更・削除しない。`--aur` は official source-build preference と official source-build route を使わず、AUR package を選ぶ。

refresh の契約は selection ごとに分ける。AurOnly の `-Ss` / `-Si` と refresh は、official database refresh を AUR-only operation へ混ぜないため外部コマンド前に拒否する。RepoOnly の `-Ss` / `-Si` と refresh は AUR fallback risk がないため許可し、既存 routing どおり `sudo pacman` へ委譲する。特に `-Si --repo --refresh <unqualified-target>` は許可する。Auto の refresh guard と routing は変更しない。

selector は operation の前後にある通常の global option 位置で認識する。ただし parser の優先順位は、(1) pacman option value 待ち、(2) `--` 後の opaque operand、(3) `--` marker、(4) jpacker global option、(5) pacman option / target の順を維持する。したがって `-Q --root --aur filesystem` の `--aur` は `--root` の value、`-U -- --repo` の `--repo` は opaque operand であり、source selection へ反映しない。通常位置で selector として認識した token だけを pacman argv から除去し、他の option / value / target の相対順を維持する。

永続的な source priority、selector の config file 保存、upgrade / `-Syu` / `-Qua` の source policy、provider selection、dependency solver の変更は、この source selection policy の scope 外とする。

---

## pacman 由来として pass-through する operation

次の pacman operation は、jpacker が AUR / source build のために介入しない場合、基本的に pacman へ委譲する。

- `-S` 系
- `-R` 系
- `-Q` 系
- `-U` 系
- `-D` 系
- `-F` 系
- `-T` 系

ただし、次のように jpacker が補完する operation がある。

- `-S <target>`: official repository package は pacman へ渡し、official repository にない target または source build preference がある target は AUR / source build 経路へ進める。
- `-Ss <query>`: pacman search のあと AUR search を補完する。
- `-Si <target>`: official repository にあれば pacman info を使い、なければ AUR metadata を表示する。
- `-Sc`: `sudo pacman -Sc` へ委譲し、pacman cache のみを対象にする。jpacker の build/cache も削除したい場合は `clean` を使う。
- `-Qua`: foreign packages を見て AUR update を確認する。
- `-Syu` / `-Sy` / `-Su`: pacman-compatible system upgrade として扱い、登録済み source build preferences の全体走査は行わない。source build preferences も確認したい場合は `upgrade` を使う。

`-R` / `-Q` / `-U` / `-D` / `-F` / `-T` 系と、AUR / source build へ分岐しない `-S` 系は、jpacker 固有 option を取り除いた引数列を pacman へ委譲する。`-S <target>` だけは target を official repository と AUR / source build に分類するため、下記の追加制約を持つ。

refresh modifierを含むread/query経路のsudo境界は次のように扱う。

- `-F <file>` / `-Fl <pkg>` などのread-only file database queryはplain `pacman`へ委譲する。
- `-Fy` / `-Fyy` / `-F -y` / `-F --refresh` はfile databaseを更新するため`sudo pacman`へ委譲する。
- selector 未指定の`-Ss <query>`は従来どおりplain pacman searchとAUR searchを組み合わせる。`-Ssy` / `-Ss --refresh`ではofficial repository search側を`sudo pacman`で実行したあとAUR searchを行う。
- selector 未指定の`-Si`とrefreshを組み合わせる場合、targetは`repo/package`形式に限定し、AUR fallbackを行わない。unqualified targetが1件でもあれば、official refreshだけを先行させないためpacman / sudo / AUR queryより前に停止する。必要ならrefreshと`-Si`を別invocationに分ける。
- refreshなしの通常の`-Si <target>`は、official repositoryを優先し、見つからない場合にAUR metadataを表示する従来契約を維持する。

`upgrade` の source-build 更新判定では、working tree にある既存 `.SRCINFO` を使う。`.SRCINFO` がない、または version 情報が不完全な場合、review 前に `makepkg --printsrcinfo` は実行しない。対話実行では続行確認を行い、`--noconfirm` または非対話実行では対象 package を skip する。

---

## AUR metadata 表示ポリシー

v1.8.0 では、AUR package status display cleanup として、AUR package の状態を分かりやすく表示することを優先する。

`jpacker -Ss` は検索・発見用途として軽く保つ。AUR search result に表示する状態タグは次の順序に固定する。

- `[installed]`
- `[out-of-date]`
- `[orphaned]`

状態タグの色は次の通りとする。

- `[installed]`: cyan
- `[out-of-date]`: red
- `[orphaned]`: yellow

`-Si` は AUR package の状態情報を確認できる表示として扱う。v1.8.0 では、少なくとも次の情報を確認できるようにする。

- maintainer
- out-of-date
- orphaned / maintainer missing
- installed state

v1.8.0 では次の表示は扱わない。これらは検索表示や package info 表示の契約に混ぜず、別 Issue で必要性と表示位置を整理してから扱う。

- `-Ss` size 表示
- `-Si` size 表示
- `plan` size 表示
- AUR build 前 size 推定
- AUR build 後 package artifact size 表示
- transaction 全体の容量見積もり
- votes / popularity
- first submitted / last modified

---

## pacman / makepkg 由来 option の pass-through

pacman へ直接委譲する経路では、jpacker が明示的に消費しない pacman-compatible option を pacman へ渡す。

AUR / source build 経路では、pacman option をそのまま makepkg option とみなさない。production の separated 経路では、makepkg が source package artifact の build、jpacker が artifact workspace・validation・install policy、`pacman -U` が検証済み artifact の install transaction を所有する。各 PackageBase に fresh `PKGDEST` を作り、同じ structured source environment で `makepkg --packagelist`、build-only `makepkg -sc`、artifact validation、installed metadata query、typed `sudo pacman -U` の順に実行する。jpacker が明示的に変換するのは次の範囲に留める。

- `--noconfirm`: build-only makepkg と typed `sudo pacman -U` の両方へ渡す。
- `--needed`: target を伴う対応済み `-S` の AUR / source build 経路では、最終 `sudo pacman -U` だけへ install-only policy として 1 回渡す。makepkg へは渡さない。
- `--rebuild`: jpacker 固有 option として build-only makepkg の `-f` へ変換する。
- `--cleanbuild`: jpacker 固有 option として build-only makepkg の `-C` へ変換する。
- `--rmdeps`: separated source-build 経路では mutation 前に拒否し、`makepkg -r` へ変換しない。

inherited process environment または各 target の structured source environment が `PKGDEST` を定義している場合は、empty value でも all-target preflight で拒否する。later target の conflict でも先行 unit を開始しない。jpacker は `makepkg.conf` 内の `PKGDEST` をこの preflight では解析しない。

現行経路が受け入れるのは PackageBase ごとに exactly one artifact だけである。split package の install target selection、sibling / debug package、multiple output は未対応とし、曖昧な artifact を選ばず fail closed で停止する。

build 後の artifact validation、metadata query、または install が失敗した場合は、artifact workspace を診断用に保持する。保持した workspace は次回 invocation の入力として自動再利用しない。`pacman -U` の成功後に workspace cleanup が失敗した場合は package install 成功済みの partial success であり、同じ package の install を無条件に再試行してはならない。

それ以外の pacman transaction option は official repository target へ pass-through する。AUR / source build target が同じ `-S` invocation に含まれる場合は、option を黙って無視した部分実行を避けるため、pacman / makepkg の実行前に transaction 全体を停止する。必要なら official repository target と AUR / source build target を別 invocation に分ける。

---

## pass-through するが jpacker 側でも意味を見る option

### `--noconfirm`

`--noconfirm` は pacman 由来の互換 option として扱う。pacman-only execution では pacman へ、separated source-build 経路では build-only makepkg と typed `sudo pacman -U` の両方へ pass-through する。`makepkg --packagelist` には追加しない。

同時に、jpacker 自身の prompt 制御にも関係する。ただし意味は「全部 yes」ではなく「対話で止まらない」指定である。

そのため、`--noconfirm` 指定時でも次は自動承認しない。

- 未解決依存を含む AUR build plan の実行
- 循環依存を含む AUR build plan の実行
- 未実装の provider selection
- 未実装の split package selection
- conflicts / replaces の自動判断
- 明示されていない rebuild
- 明示されていない cleanbuild
- separated source-build 経路での `--rmdeps` 拒否
- 危険な削除や reset

この方針は #83 の prompt helper 実装前提でもある。prompt helper では、単純に `--noconfirm` を「yes」として扱うのではなく、非対話時に安全に停止するもの、default を選べるもの、明示 option が必要なものを分ける。

現時点の jpacker 独自 prompt は、prompt ごとに default selection を持つ。`Updates detected in existing cache repository. View git diff?`、`Edit PKGBUILD?`、`Edit install script <file>?`、`Rebuild package?`、`Clean build existing build directory?`、`Clean jpacker build cache?` は default no とし、`Proceed with build?` は default yes とする。`--noconfirm` 指定時は prompt を表示せず、この default selection を採用する。ただし EOF や入力読み取り失敗は Enter と同一扱いにしない。stdin が TTY でない場合も、危険側へ進まないように扱う。

### `--needed`

`--needed` は jpacker global option ではなく、pacman 由来の互換 option として扱う。pacman-only 経路では ordered pacman argv にそのまま保持する。official repository target と AUR / source build target が混在する `-S` install でも、official transaction の argv には元の `--needed` を保持する。

target を伴う対応済み `-S` の AUR / source build 経路では、`--needed` を build skip option として扱わない。`--needed` 自体を理由に、package/source validation、AUR RPC / PackageBase 解決、dependency/build plan、conflicts/replaces・provider・split package などの guard、git clone/fetch/update、PKGBUILD / `.install` review、`makepkg --packagelist`、build-only makepkg までの経路を省略しない。jpacker は installed version、AUR RPC version、`.SRCINFO`、既存 local artifact を根拠に独自の build skip 判定を追加せず、各 PackageBase を fresh `PKGDEST` で build する。

semantic な `--needed` は makepkg command へ渡さず、jpacker が検証済み artifact に対して構築する typed `sudo pacman -U` command へ 1 回だけ渡す。AUR root target と source build preference がある official package は同じ契約を使い、preference を binary repository route へ切り替えない。重複指定は pacman-only argv では元の順序と個数を保持するが、source build 側では boolean policy として `--needed` を 1 回だけ生成する。

`--needed` は前述の exactly-one artifact guard を緩和せず、split / sibling / debug / multiple output を選択可能にしない。受け入れた single artifact についてだけ、pacman / libalpm が `pacman -U --needed` の policy に従って install 要否を判断する。

`--rebuild` / `--cleanbuild` は build / build directory の再実行方針、`--noconfirm` は prompt suppression であり、`--needed` の install-only policy とは独立して併用できる。`--rmdeps` だけは separated source-build 経路で併用できず、mutation 前に拒否する。どの組み合わせでも plan / review / safety guard は省略しない。

semantic option の判定は parser の token role に従う。通常の pacman option 位置にある exact `--needed` だけを source build policy として認識し、`--root --needed` の option value、`--` 後の opaque operand、`--needed=true` は認識・正規化しない。source route で意味を保てない形や option は、従来どおり外部 mutation 前に停止する。

この変換対象は `-S` install で source target が存在する場合に限る。`jpacker -Ss --aur --needed ...` / `jpacker -Si --aur --needed ...` は AUR RPC 前に unsupported として停止し、RepoOnly の search/info は pacman へそのまま委譲する。target なしの pacman-compatible `jpacker -Syu --needed` は pacman-only pass-through を維持する。target 付きの既存対応形で source route が生じる場合は、その source target にも同じ install-only 契約を適用する。一方、jpacker 固有の `upgrade --needed` は未対応 option として外部 command や cache/source mutation 前に停止し、既存 `.SRCINFO` による `NeedsBuild` / `UpToDate` / `Unknown` の更新判定へ `--needed` を流用しない。

### `--asdeps` / `--asexplicit`

`--asdeps` と `--asexplicit` は pacman 由来 option として、pacman execution へ pass-through する。

AUR / source build 経路では、最終的な install reason と関係するため、単純な pass-through だけでは足りない。現時点では `--asdeps` / `--asexplicit` を含む invocation を unsupported として停止し、将来対応する場合は build plan / install reason の契約として扱う。

---

## 値を取る option の扱い

operation の後ろに置かれた、値を取る pacman option は、次のどちらの形式も option と値の組として扱う。

- `--option value`
- `--option=value`

現在 jpacker が値を取るものとして追跡する pacman long option は次の通り。

- `--arch`
- `--assume-installed`
- `--cachedir`
- `--color`
- `--config`
- `--dbpath`
- `--gpgdir`
- `--hookdir`
- `--ignore`
- `--ignoregroup`
- `--logfile`
- `--overwrite`
- `--print-format`
- `--root`
- `--sysroot`

現在 jpacker が値を取るものとして追跡する pacman short option は次の通り。

- `-b`
- `-r`

この追跡は、`-S` の target 分類時に option value を package target と誤認しないための最小限の parsing であり、pacman option 全体を再実装するものではない。追跡対象の option で値が欠けている場合、jpacker は pacman 実行前に停止する。pacman へ直接委譲する経路での option の意味・値の妥当性は pacman に委ねる。

operation 確定後、値を取る option の次の token は、`--rmdeps` や `--noconfirm` など jpacker global option と同じ綴りでも option value として優先する。option value として消費されていない最初の `--` は end-of-options marker として pacman argv に保持し、それ以降は先頭が `-` の token も opaque operand として扱う。jpacker global optionを認識・消費するのは、option value待ちでも `--` 後でもない通常位置だけとする。

---

## AUR / source build 経路では単純 pass-through できない option

次の option は pacman へ渡すことはできても、AUR / source build 経路で同じ意味を保つには追加設計が必要である。

- `--asdeps`
- `--asexplicit`
- `--ignore`
- `--ignoregroup`
- `--overwrite`
- `--config`
- `--dbpath`
- `--root`
- `--sysroot`
- `--cachedir`
- `--gpgdir`
- `--hookdir`
- `--logfile`
- `--print-format`
- `--nodeps` / `--assume-installed`
- `--dbonly` / `--noscriptlet`
- `--downloadonly`
- `--print`

特に database path、root、sysroot、config を変える option は、pacman の見ている world と jpacker の AUR metadata / installed package state / build cache の見ている world がずれる可能性がある。install reason、dependency check、download-only、print-only なども separated source-build lifecycle で同じ意味にはならない。現時点では、これらを含む `-S` に AUR / source build target があれば unsupported として停止する。`--needed` だけは前節の install-only policy に限って明示的に変換する。

---

## 未整理・慎重扱いの option / 状態

次は未整理または慎重扱いとする。

- provider selection
- split package selection
- conflicts / replaces
- dependency solver 的な自動解決
- `--overwrite`
- `--ignore` / `--ignoregroup`
- `--config` / `--dbpath` / `--root` / `--sysroot`
- makepkg と pacman の両方に似た名前がある option
- pacman / makepkg の hook や database state に影響する option

これらは、jpacker 側で意味を再実装するのではなく、pacman / makepkg に任せる範囲と jpacker が安全に停止する範囲を分けてから扱う。

---

## Exit codes, AUR fallback, and output format

- pacman へ直接委譲した command は、pacman の終了コードを返す。
- `-Ss` のような統合表示では、official repository または AUR のどちらかで match すれば成功として扱う。
- `-Si` や `-Qua` では、処理継続できる per-package failure は警告に留め、critical failure は non-zero exit code にする。
- pacman の標準出力・標準エラーはできるだけそのまま保つ。
- AUR / source build の表示は jpacker 側の format を使う。

---

## 外部コマンド表示

jpacker が利用者に影響する主要な外部コマンドを実行する場合は、実行前に対象のコマンドを表示する。

ただし、metadata 確認や内部判定のための小さな確認コマンドまで、通常ログにすべて列挙する方針ではない。

---

## Out of scope

次はこの方針文書では扱わない。

- pacman / 既存 AUR helper 完全互換の宣言
- provider selection の本格実装
- split package selection の本格実装
- conflicts / replaces の自動解決
- dependency solver の強化
- pacman database 書き込みや package verification の再実装

---

## Future roadmap

#56 は v1.11.0 付近で現在の compatibility policy に一度区切りを付ける。完全な pacman option table や AUR helper 互換を完成条件にはせず、新しい option を AUR / source build 経路へ反映する場合は、安全境界を個別に設計する。

## Related future topics

次の話題は、この方針の周辺で今後整理する。

- no-argument behavior: `jpacker` 単体実行時に help 表示、safe status check、interactive update のどれを採るか。
- integrated upgrade flow: official repository update と AUR / source-build update のつなぎ方。
- mixed dependency handling: official repository package と AUR package が混ざる transaction / build plan の扱い。
- search / info output alignment: `-Ss` / `-Si` の official repository 結果と AUR 結果の見せ方。
- argument parsing tests: operation / option routing の regression test。
