# Lapis drivers v5 — honest status

Built images (also under `/opt/cursor/artifacts/`):

| Image | Path | sha256 |
|---|---|---|
| hw-safe | `out/lapis-xnu-hw-safe.img` | *(see ARTIFACTS.md)* |
| hw-full | `out/lapis-xnu-hw-full.img` | *(see ARTIFACTS.md)* |
| emu | `out-emu/lapis-xnu-emu.img` | *(see ARTIFACTS.md)* |

Rebuild: `./build-images.sh`. **hw-full clamps `lapis_smp_n=1` until SMP is VERIFIED-emu.**

## Per-driver status (exactly one label)

| Driver | Label | Notes / evidence |
|---|---|---|
| LapisGICv3 | **VERIFIED-emu** (+ prior VERIFIED-hw) | UFS UTRCS via GIC |
| LapisUFS (QEMU full) | **VERIFIED-emu** | link→GPT→IRQs |
| LapisUFS (hw reuse) | **compiled-only** | mode=2; SMMU stream 0x60 assumed ABL-bypass |
| LapisBlk ROOT + GPT | **VERIFIED-emu** | `ROOT = disk0s2` |
| LapisBlk `/dev/disk*` | **VERIFIED-emu** | `devfs ready — publishing /dev/disk*` then `published /dev/disk0 (+ 3 slices)` — `/tmp/evidence/ufs-devfs-v5/msgbuf.txt` |
| lapisfs ← UFS | **VERIFIED-emu** | mounted 15-entry mini root |
| launchd from UFS | **NOT DONE** (spawn+SIGSEGV) | Instr Abort EL0, ESR=`0x82000007` IFSC=translation L3, FAR≈`0x10……a40` — **missing dyld shared cache**. See `ROOTFS-NEEDED.md` |
| LapisFB | **VERIFIED-emu** | splash + platform props; no DSI |
| LapisSMP PSCI probe | **VERIFIED-emu** | HVC 1.1, Aff0 auto-detect |
| LapisSMP 8 CPUs online | **NOT DONE** | `ml_processor_register(cpu≥1)` → Assert in `random_cpu_init` (bt: `ml_processor_register+0x180`). Fixed PE thiscall (cpu in x1) + SGI; deferral does not clear Assert. Keep `smp_n=1` on hw images |
| qcom_scm + PIL | **compiled-only** | Real SMCCC/PAS; off on emu |
| LapisWiFi | **compiled-only** | MHI BHI/BHIe state machine + gated IMGTXDB; needs `amss.bin` |
| LapisGPU | **compiled-only** | GMU/RSC dump + CP ring/NOP/MEM_WRITE; 2D fill NOT DONE |
| LapisAudio | **compiled-only** | ADSP rproc + rx-macro `0x6AC0000` (no LPASS_CC `0x6F00000`); GPR OPEN hdr; PCM NOT DONE |
| LapisTouch | **compiled-only** | GENI SPI @ `0xa90000`; IOHID delivery NOT DONE |
| LapisBatt | **compiled-only** | SPMI best-effort; real path = ADSP pmic_glink |
| LapisSDHCI | **compiled-only** | CMD+PIO+blk publish |
| mockfs IOLog | **VERIFIED-emu** | `lapis_log` thunk |

## Exact last emulator log lines (UFS + /dev + launchd)

From `/tmp/evidence/ufs-devfs-v5/msgbuf.txt` (copied to artifacts):

```
LapisBlk: ROOT = disk0s2 (LBA 2054, 484 blocks)
LapisBlk: /dev nodes deferred (devfs not ready yet); ROOT locator still active
…
LapisBlk: devfs ready — publishing /dev/disk*
LapisBlk: published /dev/disk0 (+ 3 slices)
LAPIS lapisfs: mounted blk root size=1e4000 entries=15 state=1
load_init_program: attempting to load /sbin/launchd
Allowing spawn of iOS binary launchd ...
pid 1 exited -- exit reason namespace 2 subcode 0xb, description none
```

## Launchd fault (KGDB)

```
ESR_EL1 0x82000007   # Instr Abort EL0, translation fault level 3
ELR/FAR 0x10……a40    # ASLR; unmapped (shared-cache) page
```

## Expected hw log lines

### hw-safe
```
LAPIS drv: built-in drivers v4 (...)
LapisSMP: … registering cpu 0 … (smp_n=1)
LapisGICv3: registered ...
LapisUFS: probing qcom,ufshc @ 0x1d84000 (mode 2) ...
LapisFB: splash fb 0xd5100000 mapped ...
LapisSCM: SCM probe: ...
LapisSDHCI: HC @ 0x8804000 ...
LapisTouch: QUPv3 SE @ 0xa90000 ...
LapisBatt: SPMI core @ 0xc400000 ...
LapisWiFi: PCIe0 PARF @ 0x1c00000 ...
LapisGPU: KGSL @ 0x3d00000 ...
LapisAudio: ADSP rproc @ 0x3000000 ...
LapisAudio: LPASS rx-macro @ 0x6ac0000 ...
```

### hw-full
Same probes with higher `lapis_*_mode`; `lapis_scm_pas=1`; **still `lapis_smp_n=1`**.

## Blobs needed

See `ROOTFS-NEEDED.md` (dyld shared cache — phase1 atlas+index first) plus `kiwi/amss.bin`, `adsp.b00..b55`.
