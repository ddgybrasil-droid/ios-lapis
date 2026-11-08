# Status log

## boot_args verification

Against `apple-oss-distributions/xnu` tag `xnu-12377.121.6`
(`pexpert/pexpert/arm64/boot.h`):

- `#define BOOT_LINE_LENGTH 1024`
- LP64 `sizeof(boot_args) == 1152`, `offsetof(CommandLine) == 108`

ios-sweet's loader (`608` / `736`) is **wrong** for this kernel; lapis uses
`LAPISXNU` payloads with a 1024-byte cmdline (segment table at `0x448`).

## DRAM map (from live iomem)

Chosen kernel-managed region (wired into `hw/lapis-profile.json`, ADT
`/chosen` `dram-base`/`dram-size`, and `boot_args.physBase`/`memSize`):

| Field | Value |
|-------|-------|
| `phys_base` | `0x8C0000000` |
| `mem_size` | `0x22FA60000` (~8.74 GiB) |
| Exclusive end | `0xAEFA60000` |
| Panic carve | top `0x100000` → ADT `dram-size` `0x22F960000` |

**Rationale:** live `/proc/iomem` lists `8c0000000-affffefff : System RAM` with
reserved carveouts starting at `aefa60000`. The free prefix
`[0x8C0000000, 0xAEFA60000)` avoids Gunyah/hyp (`0x80000000`), low-bank
carveouts, splash FB (`0xD5100000`–`0xD7BFFFFF`), and TZ/VM regions. Same
“upper bank” placement idea as ios-sweet.

Sources: `hw/HW.md`, `hw/iomem.txt`, `hw/VERIFICATION.md` (M0 FB
`0xD5100000` XRGB8888 stride 11776, 2944×1840, entry EL1 under Gunyah).

## Local / Linux CI (shim)

`./scripts/build-bootimg.sh` succeeds. GitHub Actions `shim` workflow builds
the diagnostic (no-payload) boot.img.

## Flashable `fastboot boot` image

### On-device failure (19746816-byte image from run 42519261116)

Screen: `STOP loader returned` / `WHY no LAPISXNU payload`.

**Verified cause (packaging vs ABL handoff, not missing bytes):** the boot.img
bytes contained `LAPISXNU` in the **ramdisk slot** with correct v4 sizes, but
TB520FU is Android 13+ GKI — stock `boot.img` is kernel-only and the generic
ramdisk lives in **`init_boot`**. Qualcomm ABL `fastboot boot` loads our shim
(kernel) and leaves `linux,initrd-*` on the flashed init_boot (gzip/cpio), so
the shim saw an initrd that was not `LAPISXNU`.

**Fix:** append `LAPISXNU` to the Linux Image after `_end`, patch `image_size`,
ship a tiny empty boot.img ramdisk, prefer embedded discovery (FDT initrd only
if it already starts with `LAPISXNU`). `tools/verify_bootimg.py` enforces this.

```sh
fastboot boot lapis-fastboot-boot.img
```

Shim paints staged FB progress + peek diagnostics before the XNU jump.
Green CI does **not** prove on-device success.

## Final XNU CI — **green**

Run: https://github.com/ddgybrasil-droid/ios-lapis/actions/runs/42517908527

- `MACOS_VERSION=26.5`, `MACHINE_CONFIG=LAPIS`, KDK 26.5 / Xcode 26.3
- Artifact: `kernel.development.lapis` (+ unstripped)
- Patches `0001`–`0011`

## Earlier XNU CI history (failures → green)

See git history / prior commits for the AMCC, Tightbeam, KPI-export path.
On-device XNU boot is still **not claimed** — next is watching shim STEP
paint + early XNU on real hardware.
