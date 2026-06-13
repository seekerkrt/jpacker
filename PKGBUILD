# Maintainer: seekerkrt <4817613+seekerkrt@users.noreply.github.com>
pkgname=jpacker
# pkgverはpkgver()関数によって自動更新されます
pkgver=1.3.1
pkgrel=1
pkgdesc="A simple C++ AUR helper and Pacman wrapper"
arch=('x86_64')
url="https://github.com/seekerkrt/jpacker"
license=('MIT')

# 実行時に必要な依存パッケージ
depends=('curl' 'pacman' 'git')

# ビルド時に必要なパッケージ ('git' が必須)
makedepends=('git' 'nlohmann-json' 'base-devel')

# GitLabのリポジトリをソースとして指定
source=("git+https://gitlab.com/seekerkrt/jpacker.git")

# Gitリポジトリの場合はチェックサムをSKIPにするのが通例
sha256sums=('SKIP')

# バージョン番号をGitのタグやコミットから自動生成する関数
pkgver() {
    cd "$pkgname"
    # リリースタグだけを使い、checkpoint系タグがpkgverに混ざらないようにする
    git describe --long --tags --match 'v[0-9]*' 2>/dev/null | sed 's/\([^-]*-g\)/r\1/;s/-/./g' ||
    printf "r%s.%s" "$(git rev-list --count HEAD)" "$(git rev-parse --short HEAD)"
}

build() {
    # git cloneされたディレクトリに入る
    cd "$pkgname"
    make
}

package() {
    cd "$pkgname"
    # インストール
    make PREFIX=/usr DESTDIR="$pkgdir" install

    # ライセンスファイルのインストール
    install -Dm644 LICENSE "$pkgdir/usr/share/licenses/$pkgname/LICENSE"
}
