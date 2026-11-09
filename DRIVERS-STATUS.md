# Lapis drivers v4 — honest status

Built images (also under `/opt/cursor/artifacts/`):

| Image | Path | sha256 |
|---|---|---|
| hw-safe | `out/lapis-xnu-hw-safe.img` | `050768555f0be27424c6e93e09930fa6b16a0788fabeaf2aca86e10a8c88c3a3` |
| hw-full | `out/lapis-xnu-hw-full.img` | `1c5f29132b737245b8eeae239308fc7dfc2e5fafc80a636c1da36b85e7eeef08` |
| emu | `out-emu/lapis-xnu-emu.img` | `578665d876cf934a160162665e332ac586b1775e169e89fda86a153fbf54760c` |

Rebuild: `./build-images.sh` (needs `BASE=/workspace/ios-lapis/emu/out/lapis-xnu-s6/Image` or equivalent).

Evidence logs (copied to artifacts):
- `ufs-launchd-v4-msgbuf.txt` — UFS GPT → LapisBlk ROOT → lapisfs mount → launchd spawn/crash
- `smp-secondary-hang-msgbuf.txt` — PSCI OK, hang inside `ml_processor_register` for cpu 1
- `md0-launchd-msgbuf.txt` — md0 launchd spawn/crash (same exit)

## Per-driver status (exactly one label)

| Driver | Label | Notes / evidence |
|---|---|---|
| LapisGICv3 | **VERIFIED-emu** (+ prior VERIFIED-hw) | UFS UTRCS via GIC; prior hw selftest 0 spurious |
| LapisUFS (QEMU full) | **VERIFIED-emu** | link, UPIU, SCSI, GPT, IRQs — `ufs-launchd-v4-msgbuf.txt` L124–144 |
| LapisUFS (hw reuse mode=2) | **compiled-only** | Real code path; prior hw was probe-only (CAP/VER). SMMU stream 0x60 assumed ABL-bypass; MCQ kept off unless `lapis_ufs_mcq=1` |
| LapisBlk ROOT + GPT | **VERIFIED-emu** | `ROOT = disk0s2` named `lapis-root` — same log L145–152 |
| LapisBlk `/dev/disk*` | **NOT DONE** | `devfs never became ready` (log L157); ROOT locator still works without nodes |
| lapisfs ← UFS `lapis-root` | **VERIFIED-emu** | `mounted blk root size=1e4000 entries=15` — log L244 |
| launchd from UFS root | **VERIFIED-emu** (spawn only) | Loads `/sbin/launchd`, then `pid 1 exited -- exit reason namespace 2 subcode 0xb`. No first daemons. Last useful lines in excerpt below |
| LapisFB splash + props | **VERIFIED-emu** | Banner at 0xD5100000; platform props `lapis-fb-*`. No DSI |
| LapisSMP PSCI probe | **VERIFIED-emu** | HVC PSCI 1.1, Aff0 layout auto-detect — `smp-secondary-hang-msgbuf.txt` L126–135 |
| LapisSMP 8 CPUs online | **NOT DONE** | Hang in `ml_processor_register` for cpu≥1 before `PE_cpu_start` log; DebuggerXCallEnter follows. Code for PSCI CPU_ON + Aff1<<8 hw layout is present |
| qcom_scm SMCCC | **compiled-only** | Real `smc #0` client (Linux convention A). Off on emu (SMC abort). hw-safe probes `is_call_available`; hw-full enables `lapis_scm_pas=1` |
| LapisPIL PAS/mdt loader | **compiled-only** | ELF32 parse, carveout load, init_image/mem_setup/auth_and_reset. ADSP needs `.bNN` blobs (missing) |
| LapisWiFi Kiwi | **compiled-only** (probe) / **NOT DONE** (MHI→WMI) | PARF/BAR/BHI probe + gated IMGTXDB. Needs `kiwi/amss.bin` (7.2 MiB). No WMI/scan |
| LapisGPU Adreno 750 | **compiled-only** (probe) / **NOT DONE** (submit/blit) | KGSL/GPUCC probe; ZAP via PAS gated; CP_NOP path gated by `lapis_gpu_submit=1`. No 2D fill |
| LapisAudio ADSP | **compiled-only** (probe) / **NOT DONE** (PCM) | Probes ADSP rproc `0x3000000` + rx-macro `0x6AC0000` (removed bogus LPASS_CC `0x06F00000`). PIL needs `adsp.bNN`. No GLINK/GPR/PCM |
| LapisTouch Novatek | **compiled-only** | GENI SPI @ **`0xa90000`** (dtb `spi@a90000`/novatek@0; not `0xa80000`). IRQ gpio 162. No IOHID delivery |
| LapisBatt SPMI FG | **compiled-only** | SPMI EXT_READ best-effort only — **no qpnp-fg in dtb**; real path is ADSP `pmic_glink` |
| LapisSDHCI | **compiled-only** | Card detect + CMD0/8/ACMD41/2/3/9/7 + CMD17 PIO + LapisBlk publish. Not exercised in QEMU this pass |
| mockfs IOLog varargs | **VERIFIED-emu** | Fixed via `lapis_log` / `klog.c`; lapisfs lines print correctly |

## Exact last emulator log lines (UFS → launchd)

From `/opt/cursor/artifacts/ufs-launchd-v4-msgbuf.txt`:

```
LAPIS lapisfs: mounted blk root size=1e4000 entries=15 state=1
load_init_program: attempting to load /usr/appleinternal/sbin/launchd.development
load_init_program: attempting to load /sbin/launchd
Allowing spawn of iOS binary launchd since it has com.apple.private.security.no-sandbox entitlement or unentitled_ios_sim_launch boot-arg set to true
pid 1 exited -- exit reason namespace 2 subcode 0xb, description none
coredump (launchd, pid 1): writing core to /cores/core.1
coredump (launchd, pid 1): failed to open core dump file /cores/core.1: error 2
```

## Expected hw log lines

### hw-safe (`lapis-xnu-hw-safe.img`)

```
LAPIS drv: built-in drivers v4 (GIC,UFS,Blk,FB,SMP,SCM,PIL,Crypt,SDHCI,Touch,Batt,WiFi,GPU,Audio)
LapisSMP: PSCI ... version ...
LapisSMP: registering cpu 0 ...
LapisGICv3: registered ...
LapisUFS: probing qcom,ufshc @ 0x1d84000 (mode 2) ...
LapisUFS: hw: SMMU stream 0x60 ...
LapisUFS: reuse mode: keeping ABL link ...
LapisFB: splash fb 0xd5100000 mapped ...
LapisSCM: SCM probe: INFO.IS_CALL_AVAIL=... PIL.INIT_IMAGE=... PIL.AUTH_AND_RESET=...
LapisSCM: PAS auth_and_reset gated ...
LapisPIL: PIL ready (mode 0, ...)
LapisSDHCI: HC @ 0x8804000 ... card=...
LapisTouch: QUPv3 SE @ 0xa90000 ...
LapisBatt: SPMI core @ 0xc400000 ...
LapisWiFi: PCIe0 PARF @ 0x1c00000 ...
LapisGPU: KGSL @ 0x3d00000 ...
LapisAudio: LPASS_CC @ 0x6f00000 ...
```

### hw-full (`lapis-xnu-hw-full.img`)

Same probes, plus bring-up modes (`lapis_*_mode` ≥1/2, `lapis_scm_pas=1`, `lapis_smp_n=8`). Destructive ops still need extra gates (`lapis_wifi_fw_doorbell`, `lapis_gpu_submit`, `lapis_gpu_gdsc`). Expect additional lines from SCM PAS supported dump, touch identify, batt FG windows, wifi BHI, GPU ZAP attempt (needs staged mbn), audio PIL (needs `.bNN`).

## Blobs still needed from owner

| Blob | Why |
|---|---|
| `firmware_mnt/image/kiwi/amss.bin` (≈7.2 MiB) + `amss20.bin` | Wi-Fi MHI firmware download |
| `firmware_mnt/image/adsp.b00` … `adsp.b55` (all non-empty segments) | ADSP PAS load (have `adsp.mdt` only) |
| Full `rootfs-23F77.lfs` (≈4.84 GiB) or at least dyld shared cache slices + `/usr/lib/dyld` + framework dylibs launchd needs | Push launchd past immediate exit; current UFS test root is a 15-entry mini tree |
| Optional: confirm spare UFS GPT free space / userdata free GiB from `adb shell df` | Finalize install target |

## Known bugs / gaps

1. SMP secondary: `ml_processor_register(cpu≥1)` never returns in QEMU; PE_cpu_start not reached.
2. `/dev/disk*` never published (`devfs` wait times out).
3. launchd dies immediately after spawn (missing userspace deps / Mach bootstrap — needs fuller rootfs).
4. Wi-Fi / GPU / Audio beyond MMIO probe blocked on firmware blobs + live SCM.
5. Never claim hw-full paths are VERIFIED — they are compiled and boot-arg gated only.
