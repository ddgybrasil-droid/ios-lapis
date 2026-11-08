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
