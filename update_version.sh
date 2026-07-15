#!/bin/bash

set -e

# Check if version argument is provided
if [ $# -eq 0 ]; then
    echo "Usage: $0 <version>"
    echo "Example: $0 0.5.2"
    exit 1
fi

# Remove a single 'v' prefix if present
version=${1#v}

# Reject values that cannot be represented by the four-part Windows version resource
if [[ ! "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "Invalid version: $1"
    echo "Expected format: X.Y.Z (an optional leading 'v' is accepted)"
    exit 1
fi

cd $(dirname $0)

# Update version in dkms.conf
sed -i -e "s/^PACKAGE_VERSION=.*/PACKAGE_VERSION=\"$version\"/" dkms.conf

# Update version in driver/driver_module.h
sed -i -e "s/^#define\s\s*PX4_DRV_VERSION\s\s*.*/#define PX4_DRV_VERSION	\"$version\"/" driver/driver_module.h

# Update version shared by BonDriver_PX4, DriverHost_PX4, and WinSCard_PX4
sed -i -e "s/^#define\s\s*VER_FILE\s\s*.*/#define VER_FILE	${version//./,},0/" winusb/src/common/version.h
sed -i -e "s/^#define\s\s*VER_FILE_STR\s\s*.*/#define VER_FILE_STR	\"$version\"/" winusb/src/common/version.h
sed -i -e "s/^#define\s\s*VER_PRODUCT\s\s*.*/#define VER_PRODUCT	${version//./,},0/" winusb/src/common/version.h
sed -i -e "s/^#define\s\s*VER_PRODUCT_STR\s\s*.*/#define VER_PRODUCT_STR	\"$version\"/" winusb/src/common/version.h

# Update version in README.md
sed -i -e "s/px4-drv-dkms_[0-9]\+\.[0-9]\+\.[0-9]\+_all\.deb/px4-drv-dkms_${version}_all.deb/g" README.md
sed -i -e "s/v[0-9]\+\.[0-9]\+\.[0-9]\+/v${version}/g" README.md
sed -i -e "s/px4_drv-[0-9]\+\.[0-9]\+\.[0-9]\+/px4_drv-${version}/g" README.md
sed -i -e "s/px4_drv\/[0-9]\+\.[0-9]\+\.[0-9]\+/px4_drv\/${version}/g" README.md
