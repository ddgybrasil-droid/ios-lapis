# XNU for lapis (xnu-12377)

Target tag: [`xnu-12377.121.6`](https://github.com/apple-oss-distributions/xnu/tree/xnu-12377.121.6)
(iPadOS / macOS 26.5-era open source drop).

Patches in `patches/` are adapted from the ios-sweet approach
([dipa-studio/ios-sweet](https://github.com/dipa-studio/ios-sweet), MIT) onto this newer tree.

## What is ported (M1)

| Patch | Purpose |
|-------|---------|
| `0001-lapis-machine-config` | `LAPIS` board config, GICv3 stride `0x40000`, plain `arm64`, skip KDK `libLAPIS` |
| `0002-lapis-gic-group1-timer-irq` | Group-1 IRQ timer/IPI path (Gunyah/TZ owns Group 0) |
| `0003-lapis-master-open-source-asm-pmap` | `MASTER.arm64.lapis` with `nos_arm_asm` / `nos_arm_pmap` |
| `0004-lapis-stub-apple-internal-headers` | No-op SART/UAT/tunables stubs |
| `0005-lapis-device-tree-platform-expert` | Minimal `LapisPlatformExpert` |
| `0006-lapis-apple-arm64-cpu-mpidr-fallbacks` | Non-Apple `MPIDR_CORETYPE_*` fallbacks |
| `0007-lapis-pmap-without-closed-ppl` | Avoid closed `ptep_get_iommu` / `pmap_ppl_*` |

## Not yet ported (see `docs/PATCH-PORTING.md`)

Framebuffer console, crypto/HFS/AMFI/UFS/HID/USB and most of sweet's later
patches need a successful LAPIS link first, then careful rebases onto 12377 APIs.

## Build

GitHub Actions: `.github/workflows/xnu.yml` (macOS runner, `MACOS_VERSION=26.5`,
`MACHINE_CONFIG=LAPIS`). See `docs/CI-REQUIREMENTS.md` for KDK/Xcode caveats.
