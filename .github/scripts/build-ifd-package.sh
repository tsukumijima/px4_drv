#!/bin/bash
set -euo pipefail

# USB と既存の PC/SC ソケットを渡さない、使い捨ての CI コンテナ向け。
test "${PX4_ISOLATED_TEST:-}" = 1
test -f /.dockerenv
export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y --no-install-recommends \
    build-essential dpkg-dev pkg-config libpcsclite-dev python3

bash ./build_ifd_deb.sh --output-dir /packages
make -C driver check
# 模擬カードの統合試験は、ここで生成したバイナリを各 OS の pcscd と組み合わせる。
make -C userland/ifd-px4 BUILD=/ifd-tests check integration-build
