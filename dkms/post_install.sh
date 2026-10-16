#!/bin/sh

rm -fv /etc/udev/rules.d/90-px4.rules
install -D -v -m 644 ./etc/99-px4video.rules /etc/udev/rules.d/99-px4video.rules
install -D -v -m 644 ./etc/it930x-firmware.bin /lib/firmware/it930x-firmware.bin
