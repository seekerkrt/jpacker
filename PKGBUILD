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

# POLICY: source tagとlicense metadataを同じversion boundaryへ揃える。
# v1.14.x以前はMITを維持し、libalpmを直接linkするv1.15.0以降だけGPLにする。
_license_version_comparison=$(vercmp "$pkgver" 1.15.0) || return 1
if (( _license_version_comparison < 0 )); then
    license=('MIT')
else
    license=('GPL-3.0-or-later')
fi
unset _license_version_comparison

# 実行時に必要な依存パッケージ
depends=('curl' 'pacman' 'libalpm.so' 'git')

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

    # POLICY: v1.14.x以前のtagはMakefile-owned license layout導入前なので、
    # legacy MIT本文だけPKGBUILD側で補完する。v1.15.0以降はmake installが単一owner。
    if [[ ${license[0]} == 'MIT' ]]; then
        install -Dm644 LICENSE "$pkgdir/usr/share/licenses/$pkgname/LICENSE"
    fi
}
