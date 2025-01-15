#!/bin/bash

set -e

version=$1

cd $(dirname $0)

# Update version in dkms.conf
sed -i -e "s/^PACKAGE_VERSION=.*/PACKAGE_VERSION=\"$version\"/" dkms.conf

# Update version in driver/driver_module.h
sed -i -e "s/^#define\s\s*PX4_DRV_VERSION\s\s*.*/#define	PX4_DRV_VERSION	\"$version\"/" driver/driver_module.h

# Update version in winusb/src/BonDriver_PX4/resource.h
sed -i -e "s/^#define\s\s*VER_FILE\s\s*.*/#define VER_FILE	${version//./,},0/" winusb/src/BonDriver_PX4/resource.h
sed -i -e "s/^#define\s\s*VER_FILE_STR\s\s*.*/#define	VER_FILE_STR	\"$version\"/" winusb/src/BonDriver_PX4/resource.h
sed -i -e "s/^#define\s\s*VER_PRODUCT\s\s*.*/#define VER_PRODUCT	${version//./,},0/" winusb/src/BonDriver_PX4/resource.h
sed -i -e "s/^#define\s\s*VER_PRODUCT_STR\s\s*.*/#define	VER_PRODUCT_STR	\"$version\"/" winusb/src/BonDriver_PX4/resource.h

# Update version in winusb/src/DriverHost_PX4/resource.h
sed -i -e "s/^#define\s\s*VER_FILE\s\s*.*/#define VER_FILE	${version//./,},0/" winusb/src/DriverHost_PX4/resource.h
sed -i -e "s/^#define\s\s*VER_FILE_STR\s\s*.*/#define	VER_FILE_STR	\"$version\"/" winusb/src/DriverHost_PX4/resource.h
sed -i -e "s/^#define\s\s*VER_PRODUCT\s\s*.*/#define VER_PRODUCT	${version//./,},0/" winusb/src/DriverHost_PX4/resource.h
sed -i -e "s/^#define\s\s*VER_PRODUCT_STR\s\s*.*/#define	VER_PRODUCT_STR	\"$version\"/" winusb/src/DriverHost_PX4/resource.h

