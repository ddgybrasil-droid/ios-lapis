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

`package-bootimg` workflow (and the `package-bootimg` job on green `xnu`
runs) packs shim + `LAPISXNU` payload and uploads artifact
**`lapis-fastboot-boot`** (`lapis-fastboot-boot.img`).

HEAD packaging (shim + xnu + package all green):
https://github.com/ddgybrasil-droid/ios-lapis/actions/runs/42519261116
Artifact: https://github.com/ddgybrasil-droid/ios-lapis/actions/runs/42519261116/artifacts/13633301014

```sh
fastboot boot lapis-fastboot-boot.img
```

Shim paints staged FB progress (STEP 0–7) before the XNU jump.

## Final XNU CI — **green**

Run: https://github.com/ddgybrasil-droid/ios-lapis/actions/runs/42517908527

- `MACOS_VERSION=26.5`, `MACHINE_CONFIG=LAPIS`, KDK 26.5 / Xcode 26.3
- Artifact: `kernel.development.lapis` (+ unstripped)
- Patches `0001`–`0011`

## Earlier XNU CI history (failures → green)

See git history / prior commits for the AMCC, Tightbeam, KPI-export path.
On-device XNU boot is still **not claimed** — next is watching shim STEP
paint + early XNU on real hardware.
