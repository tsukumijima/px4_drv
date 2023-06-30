#!/bin/sh

# Unload previous driver
KVER=`uname -r`
if [ `grep -e '^px4_drv' /proc/modules | wc -l` -ne 0 ]; then
    modprobe -r px4_drv
fi

if [ `find /lib/modules/ -name px4_drv.ko | wc -l` -eq 0 ]; then
    rm -fv /etc/udev/rules.d/90-px4.rules /etc/udev/rules.d/99-px4video.rules
    rm -fv /lib/firmware/it930x-firmware.bin
fi
