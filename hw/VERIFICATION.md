# Verification

## M0 (reported complete by task)

A bare-metal test image was `fastboot boot`ed on TB520FU and drew correctly on the
framebuffer at `0xD5100000` (2944×1840, stride 11776, XRGB8888, no rotation).

This repository does not re-run M0; the shim console reuses those FB parameters.

## M1 (this bootstrap)

- [x] `BOOT_LINE_LENGTH=1024` and `sizeof(boot_args)==1152` verified against
      `apple-oss-distributions/xnu` tag `xnu-12377.121.6`
      (`pexpert/pexpert/arm64/boot.h`).
- [x] Linux-hosted aarch64 shim build + boot.img v4 packer.
- [x] Generated lapis ADT + LAPISXNU payload format (1024-byte cmdline).
- [x] XNU patch series scaffold for LAPIS on xnu-12377 + macOS CI workflow.
- [ ] On-device `fastboot boot` of shim+payload with a real XNU Mach-O (blocked on
      successful macOS kernel artifact + verified RAM map).
