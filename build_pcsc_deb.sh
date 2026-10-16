#!/bin/bash

# 内蔵カードリーダー用 IFD ハンドラーの deb を、実行したマシンの CPU 向けに作る
# libstdc++ と libgcc を静的にリンクして依存を glibc だけにし、必要な glibc の版を満たす Debian 系の環境で使えるようにする
# 出力先は DKMS の deb (build_deb.sh) と同じく、リポジトリの1つ上のディレクトリ

set -CEuo pipefail

cd "$(dirname "$0")"

version=$(sed -n 's/^PACKAGE_VERSION="\(.*\)"$/\1/p' dkms.conf)
arch=$(dpkg --print-architecture)
dpkg --validate-version "$version"
case "$arch" in
    amd64|arm64) ;;
    *) echo "Unsupported architecture: $arch" >&2; exit 1 ;;
esac
stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT

make -C pcsc BUILD="$stage/build" LDFLAGS="-static-libstdc++ -static-libgcc"
make -C pcsc BUILD="$stage/build" LDFLAGS="-static-libstdc++ -static-libgcc" install DESTDIR="$stage/root" USBDROPDIR=/usr/lib/pcsc/drivers
install -D -m 0644 pcsc/copyright "$stage/root/usr/share/doc/px4-drv-pcsc/copyright"

library="$stage/root/usr/lib/pcsc/drivers/ifd-px4.bundle/Contents/Linux/libifd-px4.so"

# ディストリビューションごとの C++ ランタイムに依存しないよう、動的リンク先が glibc のライブラリだけであることを確かめる
needed=$(objdump -p "$library" | awk '$1 == "NEEDED" { print $2 }')
if [[ -z "$needed" ]] || grep -v -x -E 'lib(c|m|pthread|dl|rt)\.so\.[0-9]+|ld-linux[^/]*\.so\.[0-9]+' <<< "$needed"; then
    echo "libifd-px4.so depends on libraries other than glibc" >&2
    exit 1
fi

# 参照している glibc のシンボルの版のうち、最も新しいものを必要な版にする
glibc_version=$(objdump -T "$library" | awk '/\*UND\*/' | grep -o -E 'GLIBC_[0-9]+(\.[0-9]+)+' | sed 's/GLIBC_//' | sort -V | tail -n 1)

# 配布用のライブラリにはデバッグ情報が不要なので、依存する版を調べた後で取り除く
strip --strip-unneeded "$library"

mkdir -p "$stage/root/DEBIAN"
cat > "$stage/root/DEBIAN/control" <<EOF
Package: px4-drv-pcsc
Version: $version
Architecture: $arch
Maintainer: tsukumi <tsukumijima@users.noreply.github.com>
Section: libs
Priority: optional
Depends: libc6 (>= $glibc_version)
Recommends: pcscd
Homepage: https://github.com/tsukumijima/px4_drv
Description: PC/SC driver for the built-in card readers of px4_drv tuners
 IFD handler that lets pcsc-lite use the built-in B-CAS card readers
 exposed by px4_drv under /dev/px4card/.
EOF

dpkg-deb --build --root-owner-group "$stage/root" "../px4-drv-pcsc_${version}_${arch}.deb"
