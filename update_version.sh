#!/bin/bash

set -e

# Check if version argument is provided
if [ $# -eq 0 ]; then
    echo "Usage: $0 <version>"
    echo "Example: $0 0.5.2"
    exit 1
fi

# Remove 'v' prefix if present
version=${1//v/}

cd $(dirname $0)

# Update version in dkms.conf
sed -i -e "s/^PACKAGE_VERSION=.*/PACKAGE_VERSION=\"$version\"/" dkms.conf

# Update version in driver/driver_module.h
sed -i -e "s/^#define\s\s*PX4_DRV_VERSION\s\s*.*/#define PX4_DRV_VERSION	\"$version\"/" driver/driver_module.h

# Update version in winusb/src/BonDriver_PX4/resource.h
sed -i -e "s/^#define\s\s*VER_FILE\s\s*.*/#define VER_FILE	${version//./,},0/" winusb/src/BonDriver_PX4/resource.h
sed -i -e "s/^#define\s\s*VER_FILE_STR\s\s*.*/#define VER_FILE_STR	\"$version\"/" winusb/src/BonDriver_PX4/resource.h
sed -i -e "s/^#define\s\s*VER_PRODUCT\s\s*.*/#define VER_PRODUCT	${version//./,},0/" winusb/src/BonDriver_PX4/resource.h
sed -i -e "s/^#define\s\s*VER_PRODUCT_STR\s\s*.*/#define VER_PRODUCT_STR	\"$version\"/" winusb/src/BonDriver_PX4/resource.h

# Update version in winusb/src/DriverHost_PX4/resource.h
sed -i -e "s/^#define\s\s*VER_FILE\s\s*.*/#define VER_FILE	${version//./,},0/" winusb/src/DriverHost_PX4/resource.h
sed -i -e "s/^#define\s\s*VER_FILE_STR\s\s*.*/#define VER_FILE_STR	\"$version\"/" winusb/src/DriverHost_PX4/resource.h
sed -i -e "s/^#define\s\s*VER_PRODUCT\s\s*.*/#define VER_PRODUCT	${version//./,},0/" winusb/src/DriverHost_PX4/resource.h
sed -i -e "s/^#define\s\s*VER_PRODUCT_STR\s\s*.*/#define VER_PRODUCT_STR	\"$version\"/" winusb/src/DriverHost_PX4/resource.h

# Update version in README.md
sed -i -e "s/px4-drv-dkms_[0-9]\+\.[0-9]\+\.[0-9]\+_all\.deb/px4-drv-dkms_${version}_all.deb/g" README.md
sed -i -e "s/v[0-9]\+\.[0-9]\+\.[0-9]\+/v${version}/g" README.md
sed -i -e "s/px4_drv-[0-9]\+\.[0-9]\+\.[0-9]\+/px4_drv-${version}/g" README.md
sed -i -e "s/px4_drv\/[0-9]\+\.[0-9]\+\.[0-9]\+/px4_drv\/${version}/g" README.md
