#!/bin/bash

set -CEuo pipefail
SCRIPT_DIR=$(cd $(dirname $0) && pwd)

VER_MAJ=0
VER_MIN=2
VER_BUILD=1

cd $SCRIPT_DIR

# Prep
sudo apt install dkms dpkg

# Source pkg
dkms mkdeb

# Binary pkg
mkdir -p ./installer/lib/firmware
cp ./winusb/pkg/DriverHost_PX4/it930x-firmware.bin ./installer/lib/firmware
cp -r ./debian ./installer/DEBIAN
dpkg-deb --build installer

# Clean up, and then place them all
rm -rf installer
mv ./installer.deb ../px4-drv_${VER_MAJ}.${VER_MIN}.${VER_BUILD}_all.deb -v
