# Status log

## boot_args verification

Against `apple-oss-distributions/xnu` tag `xnu-12377.121.6`
(`pexpert/pexpert/arm64/boot.h`):

- `#define BOOT_LINE_LENGTH 1024`
- LP64 `sizeof(boot_args) == 1152`, `offsetof(CommandLine) == 108`

ios-sweet's loader (`608` / `736`) is **wrong** for this kernel; lapis uses
`LAPISXNU` payloads with a 1024-byte cmdline (segment table at `0x448`).

## Agent environment notes

- `uploads/` were **not mounted** into the cloud agent VM. Hardware numbers
  combine the task text with public `sm8650.dtsi`. Placeholder RAM:
  `phys_base=0x100000000` / `mem_size=0x80000000` until real free ranges are wired in.

## Local / Linux CI (shim)

`./scripts/build-bootimg.sh` succeeds. GitHub Actions `shim` workflow: **green**.

## macOS XNU CI (first run — failed)

Run: https://github.com/ddgybrasil-droid/ios-lapis/actions/runs/42512699572

What worked:

- Fetched KDK **26.5 build 25F71**
- Applied LAPIS patches `0001`–`0005`
- Started `make install` with `TARGET_CONFIGS=DEVELOPMENT ARM64 LAPIS`

Exact compile failures (before follow-up patches `0006`/`0007`):

1. `pexpert/arm64/apple_arm64_cpu.h`: undeclared `MPIDR_CORETYPE_*` (Apple ACC encoding)
2. `osfmk/kern/{ast,processor}.c`: `kperf_ast` / `RUNNING_TIMER_KPERF` — MASTER had dropped `kperf`
3. `osfmk/arm/pmap/pmap.c`: `ptep_get_iommu` / `pmap_ppl_*` / `pmap_phys_write_disable` (closed PPL)

Follow-up patches add MPIDR fallbacks, restore `kperf` (without Apple `kpc`/CPMU),
and avoid closed-PPL helpers. Re-run recorded below after push.

## Second XNU CI (after 0006/0007 — failed)

Run: https://github.com/ddgybrasil-droid/ios-lapis/actions/runs/42513744253

`BUILD_EXIT=2`. Previous MPIDR/kperf/pmap errors cleared. New hard stop:

```
No rule to make target .../osfmk/arm64/amcc_rorgn_ppl.c
No rule to make target .../osfmk/arm64/amcc_rorgn_ppl_amcc.c
No rule to make target .../osfmk/arm64/amcc_rorgn_common.c
No rule to make target .../osfmk/arm64/amcc_rorgn_pv_ctrr.c
```

These are closed-lib-only AMCC/CTRR sources listed under `nos_arm_asm` in
`osfmk/conf/files.arm64`. Patch `0008` adds empty translation units (same
approach as ios-sweet). Re-run after that push.

## Third XNU CI (after 0008 — failed at link)

Run: https://github.com/ddgybrasil-droid/ios-lapis/actions/runs/42514760316

Reached the link stage. Undefined symbols:

- `CleanPoC_DcacheRegion_Force_nopreempt_nohid`
- `ml_enable_monitor`
- `ml_device_is_prod_fused`
- `pmap_pending_preemption`
- `tb_transport_startup`

Patch `0009` provides stubs / asm aliases. Re-run after push.

## Fourth XNU CI (after 0009 v1 — failed at link)

Run: https://github.com/ddgybrasil-droid/ios-lapis/actions/runs/42515561940

Only remaining undefined symbol: `_tb_transport_startup`. The stub TU was
gated on `#if defined(LAPIS)` without including `board_config.h`, so the
symbol was never emitted. `0009` rewritten to define it unconditionally.

## Fifth XNU CI (after tb_transport fix — linked, failed symbolset)

Run: https://github.com/ddgybrasil-droid/ios-lapis/actions/runs/42516313076

`kernel.development.lapis` **linked successfully**, then `kextsymboltool` failed
building `Private.symbolset` (~276 "exported name not in import list", mostly
`_tb_*` from force-loaded `libTightbeam.a`, plus Apple IOMMU/SPTM exports).

Patch `0010` skips Tightbeam force_load for LAPIS and drops Apple-only IOMMU
exports from `Private.arm64.exports` (ios-sweet 0009 approach).
