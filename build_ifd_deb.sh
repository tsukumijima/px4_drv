#!/bin/bash

set -euo pipefail
umask 022

root=$(cd -- "$(dirname -- "$0")" && pwd)
module_version=$(sed -n 's/^PACKAGE_VERSION="\([^"]*\)"$/\1/p' "$root/dkms.conf")
version=$module_version
output_dir="$root/.."

usage() {
    echo "Usage: $0 [--version DEBIAN_VERSION] [--output-dir DIRECTORY]"
    echo "Build a native-architecture IFD deb from the current working tree."
}

while (($#)); do
    case "$1" in
        --version|--output-dir)
            if (($# < 2)); then usage >&2; exit 2; fi
            if [[ $1 == --version ]]; then version=$2; else output_dir=$2; fi
            shift 2
            ;;
        -h|--help) usage; exit 0 ;;
        *) usage >&2; exit 2 ;;
    esac
done

for command in make pkg-config dpkg dpkg-deb dpkg-shlibdeps; do
    command -v "$command" >/dev/null || { echo "Missing command: $command" >&2; exit 1; }
done
if [[ ! $module_version =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] ||
   [[ $version != "$module_version" && $version != "$module_version"-* ]]; then
    echo "Debian version must be $module_version or $module_version-<revision>." >&2
    exit 2
fi
dpkg --validate-version "$version"
architecture=$(dpkg --print-architecture)
pkg-config --exists libpcsclite
usbdropdir=$(pkg-config --variable=usbdropdir libpcsclite)
if [[ $usbdropdir != /usr/lib/* || $usbdropdir == *..* ]]; then
    echo "Unsupported pcsc-lite driver directory: $usbdropdir" >&2
    exit 1
fi
mkdir -p -- "$output_dir"
output_dir=$(cd -- "$output_dir" && pwd)
output="$output_dir/libifd-px4_${version}_${architecture}.deb"
if [[ -e $output ]]; then
    echo "Already exists: $output (choose a new version or output directory)." >&2
    exit 1
fi

work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
package="$work/package"
doc_dir="$package/usr/share/doc/libifd-px4"
mkdir -p "$package/DEBIAN" "$doc_dir" "$work/debian"

# 既存の build/ に別環境のバイナリがあっても混入しないよう、一時領域で毎回ビルドする。
make -C "$root/userland/ifd-px4" BUILD="$work/build" \
    USBDROPDIR="$usbdropdir" DESTDIR="$package" install
install -m 644 "$root/LICENSE" "$doc_dir/copyright"
install -m 644 "$root/userland/ifd-px4/README.md" "$doc_dir/README.md"
install -m 644 "$root/userland/ifd-px4/DEVELOPMENT.md" "$doc_dir/DEVELOPMENT.md"

# libc・libstdc++ の必要な版はコンパイラーとビルド環境に依存するため、ELF から求める。
cat > "$work/debian/control" <<EOF
Source: px4-ifd

Package: libifd-px4
Architecture: any
EOF
dependencies=$(
    cd "$work"
    dpkg-shlibdeps -O -e"$package$usbdropdir/ifd-px4.bundle/Contents/Linux/libifd-px4.so"
)
dependencies=${dependencies#shlibs:Depends=}
test -n "$dependencies"
installed_size=$(du -sk --apparent-size "$package/usr" | cut -f1)
cat > "$package/DEBIAN/control" <<EOF
Package: libifd-px4
Version: $version
Architecture: $architecture
Section: libs
Priority: optional
Maintainer: px4_drv contributors <noreply@github.com>
Installed-Size: $installed_size
Depends: $dependencies
Recommends: pcscd
Provides: pcsc-ifd-handler
Homepage: https://github.com/tsukumijima/px4_drv
Description: PC/SC IFD handler for px4_drv internal card readers
 Connects DTV02A-5TS-P and PX-MLT5PE internal card readers to pcsc-lite.
 The host requires a card-capable px4_drv kernel module (card ABI version 1).
 Install this package in the environment running pcscd, including containers.
EOF
# pcscd の自動再起動は使用中のカード通信を切るため、IFD には再起動フックを設けない。
chmod -R u=rwX,go=rX "$package"
(
    cd "$package"
    find usr -type f -print0 | sort -z | xargs -0 md5sum > DEBIAN/md5sums
)
dpkg-deb --root-owner-group --build "$package" "$work/result.deb"
install -m 644 "$work/result.deb" "$output"
echo "Built: $output"
