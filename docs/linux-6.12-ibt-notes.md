# Linux 6.12 IBT compatibility note

This fork carries a local compatibility change for Linux 6.12 kernels with x86 CET/IBT enabled.

## Problem

On Debian 13 with kernel `6.12.101+deb13-amd64`, loading `px4_drv` built from the upstream 0.5.6 develop source could trigger a control-protection fault:

```text
Missing ENDBR: init_module+0x0/0x1a [px4_drv]
kernel BUG at arch/x86/kernel/cet.c:132!
Oops: invalid opcode
```

The module build also emitted objtool warnings:

```text
objtool: cleanup_module(): not an indirect call target
objtool: init_module(): not an indirect call target
```

## Local change

Upstream currently switches from literal `init_module()` / `cleanup_module()` entry points to the canonical `module_init()` / `module_exit()` path only at Linux 6.15.4 and newer. This fork lowers that threshold to Linux 6.12.0 in `driver/driver_module.c`.

```diff
-#if LINUX_VERSION_CODE >= KERNEL_VERSION(6,15,4)
+#if LINUX_VERSION_CODE >= KERNEL_VERSION(6,12,0)
```

The threshold change is applied to the init function, cleanup function, and `module_init()` / `module_exit()` registration block.

## Validation

Validated on Debian 13 (trixie), kernel `6.12.101+deb13-amd64`, GCC 14.2.0, and a PLEX PX-W3U4 (`0511:083f`). After rebuilding with DKMS, the driver loaded successfully, firmware `1.4.0.0` was detected, and `/dev/px4video0` through `/dev/px4video3` were created. The setup survived a VM reboot and was validated with live TV through mirakc and KonomiTV.

This branch is maintained as a local compatibility record and is not intended as an upstream pull request at this time.
