# Maintainer: Your Name <youremail@example.com>
pkgname=jpacker
pkgver=4.0.1
pkgrel=1
pkgdesc="A simple C++ AUR helper and Pacman wrapper"
arch=('x86_64')
url="https://github.com/example/jpacker" # プロジェクトのURL（あれば）
license=('MIT')

# 実行時に必要な依存パッケージ
depends=('curl' 'pacman' 'git')

# ビルド時のみ必要な依存パッケージ (ヘッダーファイルなど)
makedepends=('nlohmann-json' 'base-devel')

# ソースコードの場所 (ローカルファイルを指定)
# 実際のリリース時はここをGitHubのURLなどに変更します
# ... (前略) ...

# 変更点1: sourceにLICENSEを追加
source=("file://Makefile"
        "file://src/jpacker.cpp"
        "file://LICENSE")

sha256sums=('SKIP' 'SKIP' 'SKIP')

prepare() {
    mkdir -p "$srcdir/src"
    mv "$srcdir/jpacker.cpp" "$srcdir/src/"
}

build() {
    cd "$srcdir"
    make
}

package() {
    cd "$srcdir"
    make DESTDIR="$pkgdir" install

    # 変更点2: ライセンスファイルを正規のパスにインストール
    install -Dm644 LICENSE "$pkgdir/usr/share/licenses/$pkgname/LICENSE"
}
