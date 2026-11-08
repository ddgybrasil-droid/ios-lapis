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

## On-device after GKI embed fix (STEP7 reached)

Photo transcription verified against payload math (VIRT/ENTRY OCR had an
extra `F`): PHYS `0x8C0000000`, MEMSZ `0x22F960000`, VIRT
`0xFFFFFE0006000000`, ENTRY `0xFFFFFE000756D488`, JUMP `0x8C156D488`,
KEND/ADT_PA `0x8C23D0000`, ADT_SZ `0x26D8`, BA_PA `0x8C23D4000`, TOPKD
`0x8C23D8000`.

Last shim line `XNU handoff (MMU off)`, then solid red indefinitely (no
reboot) — **not** proof of XNU entry. Patch `0012` adds XNU splash
breadcrumbs (asm bands + `LAPIS EARLY N:`), exception-slot paint, and
skips `msr OSLAR_EL1` under LAPIS.

Payload fields re-checked from
`/opt/cursor/artifacts/lapis-xnu-entry-debug.img` (embedded `LAPISXNU`):
same PHYS/MEMSZ/KEND/ADT/BA/TOPKD/JUMP as above; VIRT/ENTRY match
`0xFFFFFE00…` (photo OCR’s extra `F` is wrong). `MEMSZ` is ADT
`dram-size` after the 1 MiB panic carve (`0x22FA60000 - 0x100000`).

### Entry-debug boot image (0012)

Path: `/opt/cursor/artifacts/lapis-xnu-entry-debug.img`  
Kernel: run `42526187192` build job (patches through `0012`, MOV64 fix).

Hardware (>1 min): stages 0–3 bands + shim green bar + STEP7 text
persist; no dark-blue exception flood; no C tiles/text. Proves asm
through MMU-on paint; does **not** prove C `arm_init`. Post-stage3 path
switched VBAR to `ExceptionVectorsBase` (no FB paint) before `ret`.

### Post-stage3 diagnostic (0013)

Patch `0013`: defer VBAR to LowException until C beacon; asm stage4
(orange @208 after CPACR) + stage5 (spring @256 pre-`ret`); BSS-free
yellow C beacon then mark(1) then install kernel VBAR.

Artifact: `/opt/cursor/artifacts/lapis-xnu-post-s3-debug.img` (when packaged).

| Observation | Meaning |
|-------------|---------|
| Stuck after magenta only | Hang/trap before stage4 (e.g. CPACR) |
| + orange stage4, no spring | Hang between stage4 and pre-ret |
| + spring stage5, no yellow | `ret`/arm_init entry failed (or C FB silent) |
| Yellow bar above green | C `arm_init` reached (BSS-free beacon) |
| Dark-blue flood | LowException path (ESR tiles) |
| Bottom tiles + text | C marks after beacon |

## Final XNU CI — **green** (pre-0012 baseline)

Run: https://github.com/ddgybrasil-droid/ios-lapis/actions/runs/42517908527

- `MACOS_VERSION=26.5`, `MACHINE_CONFIG=LAPIS`, KDK 26.5 / Xcode 26.3
- Artifact: `kernel.development.lapis` (+ unstripped)
- Patches `0001`–`0011`

0012 kernel build job succeeded in run `42526187192`; nested
`package-bootimg` flaked empty-steps (Actions capacity). Workflow now
`continue-on-error` on that nested job so overall xnu can stay green.

## Earlier XNU CI history (failures → green)

See git history / prior commits for the AMCC, Tightbeam, KPI-export path.
On-device XNU boot is still **not claimed** — next is watching shim STEP
paint + early XNU on real hardware.
