# Maintainer: seekerkrt
pkgname=moguet
_srcname=moguet-src

_read_version_file() {
    local version_file="$1"
    if [[ -f "$version_file" ]]; then
        tr -d '[:space:]' < "$version_file"
    fi
}

pkgver=$(_read_version_file VERSION)
pkgrel=1
pkgdesc="A pacman-first AUR helper for Arch Linux with verified source builds and per-package build preferences"
arch=('x86_64')
url="https://github.com/seekerkrt/jpacker"
license=('GPL-3.0-or-later')

# Directly linked libraries and production external commands.
depends=('curl' 'git' 'libalpm.so' 'libarchive' 'nano' 'pacman' 'sudo')

# makepkg assumes base-devel is already installed. Its current members provide
# gettext and pkgconf, while git is already a runtime dependency. Only
# additional build-time packages belong in this metadata.
makedepends=('nlohmann-json' 'tomlplusplus')

# Moguet and jpacker v1.16.0 have disjoint package payloads. Do not add
# provides/conflicts/replaces: there is no jpacker command alias, coexistence
# is supported, and replacement would remove the rollback package implicitly.

# GitHub release tag をソースとして指定
source=("moguet-src::git+https://github.com/seekerkrt/jpacker.git#tag=v${pkgver}")

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
    make PREFIX=/usr DESTDIR="$pkgdir" install
}
