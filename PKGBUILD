# Maintainer: seekerkrt
pkgname=jpacker
_srcname=jpacker-src

_read_version_file() {
    local version_file="$1"
    if [[ -f "$version_file" ]]; then
        tr -d '[:space:]' < "$version_file"
    fi
}

pkgver=$(_read_version_file VERSION)
pkgrel=1
pkgdesc="A simple C++ AUR helper and Pacman wrapper"
arch=('x86_64')
url="https://github.com/seekerkrt/jpacker"
license=('MIT')

# 実行時に必要な依存パッケージ
depends=('curl' 'pacman' 'git')

# ビルド時に必要なパッケージ ('git' が必須)
makedepends=('git' 'nlohmann-json' 'base-devel')

# GitHub release tag をソースとして指定
source=("jpacker-src::git+https://github.com/seekerkrt/jpacker.git#tag=v${pkgver}")

# Gitリポジトリの場合はチェックサムをSKIPにするのが通例
sha256sums=('SKIP')

pkgver() {
    local version
    version=$(_read_version_file "$srcdir/$_srcname/VERSION")
    if [[ -z "$version" ]]; then
        version=$(_read_version_file "$startdir/VERSION")
    fi
    printf '%s\n' "$version"
}

build() {
    # git cloneされたディレクトリに入る
    cd "$_srcname"
    make
}

package() {
    cd "$_srcname"
    # インストール
    make PREFIX=/usr DESTDIR="$pkgdir" install

    # ライセンスファイルのインストール
    install -Dm644 LICENSE "$pkgdir/usr/share/licenses/$pkgname/LICENSE"
}
