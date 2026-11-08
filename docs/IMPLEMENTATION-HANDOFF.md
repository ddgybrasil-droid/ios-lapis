# Implementation handoff — M1

> The original `uploads/IMPLEMENTATION-HANDOFF.md` was not present in the agent
> filesystem. This file is the working spec reconstructed from the task brief
> and verified sources.

## Goal

Boot open-source **xnu-12377** on **Lenovo Yoga Pad Pro 12.7 (TB520FU / lapis /
SM8650)** using the ios-sweet pattern: temporary `fastboot boot` of a shim
`boot.img` that loads an XNU Mach-O + generated Apple Device Tree.

## Non-negotiables

- Code only in git — **no** IPSW, DeviceTree IM4P, shared cache, or other Apple
  proprietary binaries.
- `fastboot boot` only — **never** flash boot / init_boot / vendor_boot.
- Loader must use xnu-12377 `boot_args` (`BOOT_LINE_LENGTH=1024`, size 1152).

## Deliverables

1. Linux-buildable aarch64 shim + boot.img v4 packer
2. Patch series / porting plan onto xnu-12377
3. macOS GH Actions workflow for the kernel
4. Honest README / status

## Status

See `docs/STATUS.md` and root `README.md`.
