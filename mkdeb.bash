#!/bin/bash

set -CEuo pipefail
SCRIPT_DIR=$(cd $(dirname $0) && pwd)

VER_MAJ=0
VER_MIN=2
VER_BUILD=1
ARCH="amd64"

## Prep with fwtool
# apt install dkms dpkg make unzip --no-install-recommends -y
# cd $SCRIPT_DIR/fwtool
# make
# wget http://plex-net.co.jp/plex/pxw3u4/pxw3u4_BDA_ver1x64.zip -O pxw3u4_BDA_ver1x64.zip
# unzip -oj pxw3u4_BDA_ver1x64.zip pxw3u4_BDA_ver1x64/PXW3U4.sys && rm pxw3u4_BDA_ver1x64.zip
# ./fwtool PXW3U4.sys it930x-firmware.bin && rm PXW3U4.sys
#cd ..
#mkdir -p ./installer/lib/firmware
#cp ./fwtool/it930x-firmware.bin ./installer/lib/firmware


mkdir -p ./installer/lib/firmware
cp ./winusb/pkg/DriverHost_PX4/it930x-firmware.bin ./installer/lib/firmware


dkms mkdeb
dpkg-deb --build installer
mv ./installer.deb ../px4-drv_${VER_MAJ}.${VER_MIN}.${VER_BUILD}_${ARCH}.deb -v
