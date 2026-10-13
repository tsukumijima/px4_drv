#!/bin/bash
set -euo pipefail

# パッケージの導入・削除は、実機を渡していない専用コンテナに限定する。
test "${PX4_ISOLATED_TEST:-}" = 1
test -f /.dockerenv
test ! -e /dev/bus/usb
test ! -e /run/pcscd/pcscd.comm
export DEBIAN_FRONTEND=noninteractive
architecture=$(dpkg --print-architecture)
set -- /packages/libifd-px4_*_"$architecture".deb
test "$#" = 1
test -f "$1"
test "$(dpkg-deb -f "$1" Architecture)" = "$architecture"

# 最小イメージは文書を除外するため、このパッケージだけは文書も展開して全収録物を検証する。
cat > /etc/dpkg/dpkg.cfg.d/zz-px4-package-test <<'EOF'
path-include=/usr/share/doc/libifd-px4
path-include=/usr/share/doc/libifd-px4/*
EOF

# 開発パッケージのない環境で、deb の依存関係だけで共有ライブラリを読み込めるか確認する。
apt-get update
apt-get install -y --no-install-recommends python3 pcscd "$1"
test "$(dpkg-query -W -f='${Status}' libifd-px4)" = 'install ok installed'
test -z "$(dpkg --verify libifd-px4)"
dkms_status=$(dpkg-query -W -f='${Status}\n' px4-drv-dkms dkms 2>/dev/null || true)
if [[ $dkms_status == *'install ok installed'* ]]; then
    echo 'The IFD package must be installable without DKMS in a container.' >&2
    exit 1
fi
python3 - <<'PY'
import ctypes
import os
from pathlib import Path
import plistlib
import subprocess

files = subprocess.check_output(["dpkg-query", "-L", "libifd-px4"], text=True).splitlines()
plists = [Path(path) for path in files if path.endswith("/Contents/Info.plist")]
assert len(plists) == 1, plists
with plists[0].open("rb") as source:
    info = plistlib.load(source)
assert len(info["ifdVendorID"]) == len(info["ifdProductID"]) == len(info["ifdFriendlyName"]) > 0
library = plists[0].parent / "Linux" / info["CFBundleExecutable"]
assert str(library) in files
driver = ctypes.CDLL(str(library), mode=os.RTLD_NOW)
for name in ("IFDHCreateChannelByName", "IFDHCreateChannel", "IFDHCloseChannel",
             "IFDHGetCapabilities", "IFDHSetCapabilities", "IFDHSetProtocolParameters",
             "IFDHPowerICC", "IFDHTransmitToICC", "IFDHICCPresence", "IFDHControl"):
    getattr(driver, name)
# 未登録のリーダー照会は I/O を伴わず、失敗として返る必要がある。
driver.IFDHICCPresence.argtypes = [ctypes.c_ulong]
driver.IFDHICCPresence.restype = ctypes.c_long
assert driver.IFDHICCPresence(0) != 0
print(f"Installed IFD loaded successfully: {library}")
PY

# 配布用 IFD のロード確認に加え、各 OS の pcscd との通信を模擬カードで検証する。
(
    cd userland/ifd-px4
    BUILD=/ifd-tests sh tests/run-pcsc-integration.sh
)

# 再導入で配置内容が変わらず、削除で bundle を残さないことも確認する。
apt-get install -y --reinstall --no-install-recommends "$1"
test -z "$(dpkg --verify libifd-px4)"
library=$(dpkg-query -L libifd-px4 | grep '/Contents/Linux/libifd-px4.so$')
plist=$(dpkg-query -L libifd-px4 | grep '/Contents/Info.plist$')
apt-get purge -y libifd-px4
test ! -e "$library"
test ! -e "$plist"
echo "IFD package install, load, reinstall and removal passed ($architecture)."
