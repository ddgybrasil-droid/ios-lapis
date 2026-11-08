# Porting ios-sweet XNU patches onto xnu-12377

Source of truth for the approach: [dipa-studio/ios-sweet](https://github.com/dipa-studio/ios-sweet)
`xnu/patches/` (xnu-8796 / iOS 16 era). Target: `apple-oss-distributions/xnu`
tag **`xnu-12377.121.6`**.

## Verified ABI pitfall

| | ios-sweet (xnu-8796 era) | xnu-12377.121.6 |
|--|--------------------------|-----------------|
| `BOOT_LINE_LENGTH` | 608 | **1024** |
| `sizeof(boot_args)` | 736 | **1152** |
| payload cmdline slot | 608 @ `0x48`, segs @ `0x2a8` | 1024 @ `0x48`, segs @ **`0x448`** |

Verified from `pexpert/pexpert/arm64/boot.h` on tag `xnu-12377.121.6`.

## Machine differences (sweet → lapis)

| | sweet (SM7150) | lapis (SM8650) |
|--|----------------|----------------|
| GICD | `0x17a00000` | `0x17100000` |
| GICR | `0x17a60000` / stride `0x20000` | `0x17180000` / stride **`0x40000`** |
| virt timer INTID | 19 (Qualcomm remapped PPIs) | **27** (standard PPIs) |
| FB | `0x9c000000` 1080×2400 | `0xD5100000` 2944×1840 stride 11776 |
| Hypervisor | TZ / ABL EL1 | Gunyah, still EL1 entry |

## Feasible now (shipped as patches)

Bring-up / board identity / GIC / open-source asm: `0001`–`0005` in `xnu/patches/`.

## Needs rebase after a green(er) LAPIS build

Rough mapping from sweet patch numbers:

| sweet | Topic | Notes for 12377 |
|-------|-------|-----------------|
| 0001–0002 | clang flags / UIO_STACKBUF | 0001 already upstream (flag gone); 0002 may still be needed with new clang |
| 0003, 0010–0012, 0043+ | framebuffer console | Port after LAPIS links; FB base/stride from lapis profile |
| 0005 | non-PAC pmap field rename | Structure changed — re-audit `osfmk/arm/pmap/pmap.h` |
| 0008+ | closed-lib gaps / kpc stubs | Expect new missing symbols vs KDK 26.5 |
| 0013–0027 | crypto / BearSSL / AMFI / HFS staging | Stage scripts need new upstream paths |
| 0028–0034 | UFS root | SM8650 UFS base differs; rewrite MMIO |
| 0046+ | IOSurface / SpringBoard path | Far downstream of M1 |
| 0058–0063 | HID / Goodix | Different touch IP on lapis |
| 0065, 0078 | PSCI SMP | Reuse idea; MPIDR layout is 1+3+3+1 on SM8650 |
| 0072–0081 | USB gadget | Optional later |

## Strategy

1. Get `MACHINE_CONFIG=LAPIS` through compile+link with stubs (`0001`–`0005`).
2. Add early FB console next (sweet 0003/0010 family).
3. Only then stage filesystem/crypto patches, one dependency layer at a time.
