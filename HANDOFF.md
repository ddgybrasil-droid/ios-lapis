# ios-lapis / lapis-drv handoff (2026-11-09, Europe/Moscow)

Cloud agent: you are continuing **binary-patched XNU driver work** for Lenovo Yoga Pad Pro 12.7 TB520FU (SM8650 / pineapple) under Gunyah EL1. Goal: finish drivers so a real IPSW 23F77 rootfs can boot far enough that iOS userspace (launchd + early daemons) runs. Boot is always `fastboot boot` — **never flash**.

**Honesty rules (owner-enforced):** label every claim VERIFIED / HYPOTHESIS / NOT DONE / compiled-only. Do not substitute docs for code. Emulator has no Adreno / Kiwi Wi-Fi / LPASS — those drivers must be real code, but only hardware can fully verify them.

Current git tip (this bundle):
- `drivers` branch: **`12198b0b963865a223e37c7ed0a52ad02dfbcfb6`** — `drivers v3 WIP (handoff)`
- `master` branch: **`12ef4f02fc48216ccb39aa40f09b0004f2d35349`** — s11 real launchd in emu

Build check at handoff time: **`./build-drv.sh` PASS** (warnings only in sdhci/batt). Image sha256:
- hw `out/lapis-xnu-drv3.img` = `cb336d8be251b2e8c2446e839383a868c257a4bf514eaa5553213c2c96b1db26`
- emu `out-emu/lapis-xnu-drv3-emu.img` = `8b61ee86988059fa1998a2fffe3cc7c6c4b39d700e1779e1f3a91bdf9bb32039`
v3 WIP (scm/crypter/smp/touch/batt rewrites) is **compiled** into those images but **NOT end-to-end verified** in emu after the WIP commit.

---

## 1. Repo layout

Single git repo (bundle `ios-lapis.gitbundle`). On the build box it lives as:

| Path on build box | Role |
|---|---|
| `/workspace/ios-lapis/src` | main worktree, branch `master` (s11) |
| `/workspace/lapis-drv` | linked worktree, branch `drivers` (**work here**) |
| `/workspace/ios-lapis/emu/` | QEMU harness (NOT in git — shipped in this handoff as `emu/`) |
| `/workspace/ios-lapis/rd/` | lapisfs images (hw-min + placeholder shipped; 4.84 GiB rootfs NOT shipped) |
| `/workspace/ios-lapis/emu/out/lapis-xnu-s6/Image` | prebuilt CI XNU we binary-patch (shipped as `lapis-xnu-s6.Image.xz`) |
| `/workspace/lapis_dump/lapis_dump/` | PixelOS TB520FU dump (texts + small FW headers shipped) |

Suggested restore on the cloud VM:
```bash
git clone ios-lapis.gitbundle ios-lapis-src
cd ios-lapis-src && git checkout drivers
# Expected paths used by scripts (create symlinks OR edit BASE/HERE):
sudo mkdir -p /workspace
sudo ln -s $PWD /workspace/lapis-drv
# Or just export BASE=... and edit drivers/emu/run-drv.sh HERE=
xz -dk lapis-xnu-s6.Image.xz
mkdir -p /workspace/ios-lapis/emu/out/lapis-xnu-s6
cp lapis-xnu-s6.Image /workspace/ios-lapis/emu/out/lapis-xnu-s6/Image
cp -a emu /workspace/ios-lapis/emu   # scripts; keep out/ksyms regenerable
mkdir -p /workspace/ios-lapis/rd
cp rd/*.lfs /workspace/ios-lapis/rd/
```

Key trees inside the repo:
```
build-drv.sh / build-both.sh / build-s7.sh
drivers/          # built-in driver blob (this is the work)
  build.sh drv.ld syms.txt
  common/{klog.c,drv_common.c,drv_entry.S}
  gic/ ufs/ blk/ fb/ smp/ sdhci/ touch/ batt/ wifi/ gpu/ audio/
  scm/{qcom_scm.c,qcom_scm.h,lapis_pil.c}   # v3 WIP
  crypter/lapis_crypter.c                   # v3 WIP
  emu/{run-drv.sh,mkgpt.py,mkdisk.py,evidence-*}
tools/{repack.py,xnu_binpatches.py,drv_binpatches.py,drv_adt.py,mkadt.py,...}
xnu/{lapis-exc,lapis-crypto,lapis-mockfs}   # built-in blobs patched into kernel
hw/{TB520FU-hw-map.md,qemu-virt-profile.json}
shim/   # EL1 entry, FDT→ADT, copies LAPISXNU to phys_base
```

---

## 2. How to build

Toolchain (STOCK — no custom QEMU build):
- `aarch64-linux-gnu-gcc` / `ld` / `objcopy` / `nm`
- `qemu-system-aarch64` **10.0.x** (Debian package is fine)
- `gdb-multiarch`, Python 3
- See `TOOLCHAIN.txt`

```bash
# Drivers-only fast check:
./drivers/build.sh
# Expect: drivers.bin N/40320 + drivers2.bin M/60412, bss <= 4096, .data == 0

# Full hw+emu boot images (uses BASE Image + shim + binpatches + ADT):
./build-drv.sh
# outputs: out/lapis-xnu-drv.img  and  out-emu/lapis-xnu-drv-emu.img

# s11-style build with embedded/ext ramdisk + driver cmdline flags:
HW_RAMDISK=/workspace/ios-lapis/rd/hw-min-23F77.lfs \
RAMDISK_EXT=/workspace/ios-lapis/rd/placeholder.lfs \
  ./build-both.sh
```

`BASE` default: `/workspace/ios-lapis/emu/out/lapis-xnu-s6/Image`  
sha256 `fc334c3320059de3203b5e84d26a191ff07260f2a3fc9354f77ac74b5c4d3dac` (19 744 976 bytes).

After first build, regenerate ksyms for KGDB:
```bash
python3 /workspace/ios-lapis/emu/ksyms.py "$BASE" dump /workspace/ios-lapis/emu/out/ksyms.txt
```

---

## 3. Emulator tests (exact commands)

Harness: `drivers/emu/run-drv.sh` (copy of emu `run.sh` with `HERE=/workspace/ios-lapis/emu`).  
UFS disk helpers: `drivers/emu/mkgpt.py`, `drivers/emu/mkdisk.py`.

### 3a. GIC + UFS + Blk + lapisfs from GPT (drv2 VERIFIED path)
```bash
cd /workspace/lapis-drv
python3 drivers/emu/mkgpt.py /tmp/ufs-disk.img 64
# optional: put a tiny lapisfs into lapis-root via mkdisk instead:
#   python3 drivers/emu/mkdisk.py /tmp/ufs-disk.img /workspace/ios-lapis/rd/placeholder.lfs
TAG=lapis-xnu-drv ./build-drv.sh
QEMU_EXTRA="-device ufs,serial=x -device ufs-lu,drive=u -drive if=none,id=u,file=/tmp/ufs-disk.img,format=raw" \
  KGDB=1 TIMEOUT=55 PASS2=0 \
  drivers/emu/run-drv.sh out-emu/lapis-xnu-drv-emu.img /tmp/drv-out
# Read /tmp/drv-out/msgbuf.txt
```

### 3b. s11 real launchd via md0 ramdisk (master 12ef4f0 VERIFIED)
```bash
# build-both with RAMDISK_EXT = hw-min (launchd+dyld) or a larger image
RAMDISK_EXT=/workspace/ios-lapis/rd/hw-min-23F77.lfs ./build-both.sh
# The emu image writes a sibling file NAME.img.ramdisk-pa that run.sh picks up.
KGDB=1 TIMEOUT=55 PASS2=0 \
  /workspace/ios-lapis/emu/run.sh out-emu/lapis-xnu-s8-emu.img /tmp/s11-out
```

### 3c. UFS root = real launchd (combine)
```bash
python3 drivers/emu/mkdisk.py /tmp/ufs-root.img /workspace/ios-lapis/rd/hw-min-23F77.lfs
# build emu image WITHOUT relying on md0, or with both
QEMU_EXTRA="-device ufs,serial=x -device ufs-lu,drive=u -drive if=none,id=u,file=/tmp/ufs-root.img,format=raw" \
  KGDB=1 TIMEOUT=60 PASS2=0 \
  drivers/emu/run-drv.sh out-emu/lapis-xnu-drv-emu.img /tmp/ufs-root-out
```

---

## 4. Known-verified results (with log excerpts)

### GIC — VERIFIED emu + VERIFIED hw
Emu (`drivers/emu/evidence-gic-run2-msgbuf.txt` / drv2-emu5):
```
LapisGICv3: registered as IOInterruptController0000000D (288 vectors), cpu_data interrupt_handler installed
LapisGICv3: selftest: PASS pl031 alarm: INTID 34 dispatched 1 time(s) ...
LapisGICv3: selftest: PASS causeInterrupt: INTID 40 (edge) delivered 3/3 ...
LapisGICv3: selftest: done: spurious 0 unhandled 0
```
Hw (tablet photo 2026-11-09 ~15:34 MSK): GIC found at `0x17100000`, LapisGICv3 registered, vtimer INTID 27, selftest PASS, spurious 0 / unhandled 0.

### UFS — VERIFIED emu (full); VERIFIED hw probe-only
Emu CAP (QEMU PCI ufs):
```
LapisUFS: initial: CAP 0x107021f VER 0x400 ...
LapisUFS: NOP OUT -> NOP IN ok
LapisUFS: GPT: ... disk0s2 "lapis-root" ... block 2054: "LAPISFS1"
```
Hw probe (tablet photo): controller `@ 0x1d84000`, **CAP `0xf5c7031f` VER `0x400`** (UFS 4.0), ABL left it enabled; probe-only completed without crash.  
Hw default mode going forward: **reuse** (`lapis_ufs_mode=2`, no HCE reset). Full DMA/READ10 on hw = NOT DONE.

### Blk + lapisfs — VERIFIED emu (drv2-emu5)
```
LapisBlk: ROOT = disk0s2 (LBA 2054, 4096 blocks)
LAPIS lapisfs: registered
LAPIS lapisfs: mounted blk disk0s2 size=8000 entries=5
load_init_program: attempting to load /sbin/launchd
```
(That particular disk held a tiny test lapisfs, not full IPSW rootfs.)

### s11 real launchd — VERIFIED emu (commit 12ef4f0)
With `hw-min-23F77.lfs` / full rootfs as md0:
```
LAPIS lapisfs: mounted md0 ... entries=283   # full rootfs
load_init_program: attempting to load /sbin/launchd
coredump (launchd, pid 1): writing core to /cores/core.1
```
→ launchd **exec'd and ran in EL0 as pid 1**, then crashed (no `/cores`, missing userspace deps / dyld cache issues depending on image). Evidence: `evidence/s11-y5-msgbuf.txt`.

### NOT DONE / compiled-only (be honest)
| Piece | Status |
|---|---|
| LapisSMP secondary CPUs / `ml_set_max_cpus(8)` | NOT DONE (v3 WIP probes PSCI; still effectively 1 CPU) |
| LapisSDHCI data path | NOT DONE (v3 WIP more code; unverified) |
| LapisTouch / LapisBatt | compiled-only → v3 WIP started real SPMI/touch paths; **unverified** |
| LapisWiFi / GPU / Audio beyond MMIO probe | compiled-only; v3 WIP added `scm/` + `crypter/` — **unverified**, must continue |
| IOMedia / IOStorageFamily | impossible in this kernel — BSD `LapisBlk` is the path |
| SpringBoard / Metal on Adreno | long-term; out of scope for “launchd up” |

---

## 5. Hardware map

Canonical: `hw/TB520FU-hw-map.md` (commit ad8f4b8 + dump). Highlights:

| Block | Addr / note |
|---|---|
| GICD / GICR / ITS | `0x17100000` / `0x17180000` stride `0x40000` (8 redist) / `0x17140000` |
| Arch virt timer | PPI 11 → INTID 27; CNTFRQ **19.2 MHz** on hw (emu QEMU = 1 GHz → disable sched-hygiene) |
| PSCI | method **smc**; 8 CPUs MPIDR `0x0..0x700` |
| UFS HC / PHY / ICE | `0x1d84000` SPI 265 / `0x1d80000` qmp-v4-pineapple / `0x1d88000` |
| PCIe0 PARF / WLAN BAR | `0x1c00000` / `0x60400000` — cnss2 + **qca_cld3_kiwi_v2** (WCN785x), NOT ath12k |
| GPU | KGSL `0x3d00000`, GPUCC `0x3d90000`, Adreno750v2 gen7_9_1 chipid `0x43051401` |
| Splash FB | `0xD5100000` (shim / emu map as plain RAM) |

Dump texts in this handoff: `dump/{fdt.dtb,iomem,interrupts,cmdline,lsmod,props,dt_tree}.txt` plus `*.listing.txt` for firmware trees.

---

## 6. Patch region budget (drivers blob)

From `drivers/drv.ld` (freed `vm_range_lock_test*` bodies; callers patched to ENOTSUP):

| Region | VA | Length |
|---|---|---|
| TEXT | `0xfffffe00074fdb18` | **40320** B |
| TEXT2 | `0xfffffe0007509d90` | **60412** B |
| BSS | `0xfffffe0007daf000` | **4096** B (first 64 B = `lapis_blkops` fixed addr for lapisfs) |

**No `.data` allowed** (nothing loads it). At handoff build: `drivers.bin 8340/40320 + drivers2.bin 24448/60412`, bss `672/4096`.

Other built-in blobs (mockfs/crypto/exc) live in their own freed regions — see `tools/xnu_binpatches.py`.

---

## 7. ABI notes you MUST not break

### klog.c (drivers)
Kernel is Darwin arm64 ABI: **variadic args on the stack**. Blob is compiled by aarch64-linux gcc (AAPCS64: varargs in x1..x7).  
Therefore drivers **never** call `IOLog(fmt, ...)` with real arguments. `klog()` formats locally and calls `drv_iolog_str(s)` (`drv_entry.S`) which does `IOLog("%s", s)` with Darwin stack layout.

### mockfs / lapisfs IOLog varargs
Same bug class. Fixed in s11 with `lapis_log(fmt, a, b, c)` — an asm thunk that places up to 3 integer args in Darwin stack slots then `bl IOLog`. String-only `IOLog("...")` is safe. **Do not** add `IOLog("x=%d", v)` from C without going through `lapis_log` / a new thunk.

---

## 8. What v3 WIP already touched (unfinished)

Commit `12198b0` (this handoff):
- **new** `drivers/scm/{qcom_scm.c,qcom_scm.h,lapis_pil.c}` — EL1 SMC wrappers + PIL skeleton
- **new** `drivers/crypter/lapis_crypter.c`
- rewrites: `smp`, `touch`, `batt`, `sdhci`, `blk`, `drv_common`, `drv.ld`, `syms.txt`
- `drivers/emu/mkdisk.py` — GPT disk with real lapisfs in `lapis-root`
- `xnu/lapis-mockfs/lapis_mockfs.c` + rebuilt `mockfs.bin`
- `tools/drv_binpatches.py`, `tools/xnu_binpatches.py`, `build-both.sh`, `drivers/build.sh`

Owner rejected the earlier conclusion that SCM/PIL is impossible under Gunyah: PixelOS on the same tablet loads modem/audio/GPU firmware via TZ from EL1. Continue that path; do not re-declare a hard blocker without a failed SMC log.

---

## 9. Your task (priority order)

1. **Stabilize v3 WIP in emu**: `./build-drv.sh` + UFS GPT run; confirm GIC/UFS/Blk/lapisfs still PASS; fix anything the WIP broke.
2. **Launchd from UFS**: `mkdisk.py` + `hw-min-23F77.lfs`; get msgbuf to `coredump (launchd, pid 1)` or better (keep running).
3. **SMP**: make `ml_set_max_cpus` reflect brought-up CPUs; PSCI CPU_ON for MPIDRs 0x100..0x700; prove with per-CPU log lines in emu (QEMU `-smp 8`) AND keep hw path (`method=smc`) ready.
4. **SCM/PIL**: finish `qcom_scm` + `lapis_pil` enough to load **ADSP** (audio) and **Kiwi/WPSS** (wifi) and **GPU ZAP** headers; log every SMC success/fail. Small headers are in `firmware/`. Big blobs (`amss.bin` 7.2M, full ADSP segments) are listed in `dump/*.listing.txt` but not shipped — request them if you need bytes.
5. **Touch + Batt**: turn v3 WIP into drivers that publish state via IOLog periodically (even without full HID stack).
6. **Do NOT** ask the owner to reboot the tablet until one combined image is ready. Deliver: image sha256 + emu msgbuf excerpts labeled VERIFIED.

Out of scope for this handoff: SpringBoard/Metal, flashing, rebuilding XNU from source, the 4.9 GiB dyld cache (exists only on the build box as `rootfs-23F77.lfs`).

---

## 10. Ramdisk / IPSW binaries in this package

| File | Size | Notes |
|---|---|---|
| `rd/hw-min-23F77.lfs` | 1.9 MiB | real `/sbin/launchd` + `/usr/lib/dyld`, empty dyld-cache dir |
| `rd/placeholder.lfs` | 32 KiB | static `tools/ramdisk/launchd.macho` stub |
| `ipsw-bins/launchd` | 620 432 | from IPSW 23F77 `os/sbin/launchd` |
| `ipsw-bins/dyld` | 1 333 168 | from IPSW 23F77 `os/usr/lib/dyld` |
| `rd/rootfs-23F77.UNIQUE-FILES.txt` | listing | full 4.84 GiB image = launchd+dyld+dyld_shared_cache (path aliases×3). **Not included.** |

Rebuild recipe: `tools/ramdisk/build-images.sh` (needs IPSW extract tree).

---

## 11. Firmware shipped (small)

```
firmware/firmware_mnt/image/adsp.mdt
firmware/firmware_mnt/image/adsp_dtb.mdt
firmware/firmware_mnt/image/kiwi/{bdwlan_lapis,bdwlan_row_lapis,bdwlan_prc_lapis}.elf
firmware/firmware_mnt/image/kiwi/{regdb.bin,phy_ucode.elf,phy_ucode20.elf}
firmware/vendor_firmware/{gen70900_zap.mbn,gen71100_zap.mbn,regulatory.db,regulatory.db.p7s}
```
Listings with sizes for the full trees: `dump/vendor_firmware.listing.txt`, `dump/firmware_mnt.listing.txt`.  
**Not shipped (too big):** `kiwi/amss.bin` (7.2M), `amss20.bin` (6.2M), ADSP `.bXX` segments, VPU/CAMERA ELFs, `vendor_dlkm_modules/`, `vendor_etc/`.

No separate `wpss.mdt` found in the dump — Wi-Fi firmware is the kiwi `amss*.bin` + bdwlan/regdb/phy_ucode set under cnss2.

---

## 12. Missing from this handoff (cloud agent cannot obtain alone)

1. **`rootfs-23F77.lfs` (4.84 GiB)** — full dyld shared cache. Ask parent to attach or host if you must boot past launchd with real dylibs.
2. **Large FW blobs** (amss.bin, ADSP segments) — listed; request on demand.
3. **IPSW extract tree** (`/workspace/ios-lapis/ipsw/extract/`) — only launchd+dyld copied.
4. **Custom QEMU** — not required; stock 10.0.x works. If your VM has no `qemu-system-aarch64`, install distro package.
5. **Owner tablet** — you cannot run hw tests; parent will after one combined image.
