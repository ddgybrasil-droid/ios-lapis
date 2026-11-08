# Status log

## boot_args verification

Against `apple-oss-distributions/xnu` tag `xnu-12377.121.6`
(`pexpert/pexpert/arm64/boot.h`):

- `#define BOOT_LINE_LENGTH 1024`
- LP64 `sizeof(boot_args) == 1152`, `offsetof(CommandLine) == 108`

ios-sweet's loader (`608` / `736`) is **wrong** for this kernel; lapis uses
`LAPISXNU` payloads with a 1024-byte cmdline (segment table at `0x448`).

## Agent environment notes

- `uploads/` (PLAN.md, HW.md, iomem.txt, handoff, …) were **not mounted** into
  the cloud agent VM. Hardware numbers combine the task text with public
  `sm8650.dtsi`. Replace placeholder RAM with the real free ranges before
  trusting on-device jumps.

## Local shim build

`./scripts/build-bootimg.sh` succeeds on Ubuntu with `aarch64-linux-gnu-gcc`:

- `out/Image` (Linux Image header) ≈ 77824 bytes
- `out/shim-boot.img` Android boot **v4**
- `out/lapis.adt` generated
- Patches `0001`–`0005` `git apply --check` clean on tag `xnu-12377.121.6`

## macOS XNU workflow

See the Actions run linked from the PR. Expected early failures without a full
KDK/Xcode matrix are documented in `docs/CI-REQUIREMENTS.md`. Update this
section with the exact compiler/linker error once the first run finishes.
