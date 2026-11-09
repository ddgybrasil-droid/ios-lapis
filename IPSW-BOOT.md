# IPSW / rootfs install & boot (TB520FU)

Boot is always `fastboot boot` — **never flash**.

## What this kernel can mount as root

1. **Ramdisk (md0)** — small `hw-min-23F77.lfs` embedded in the boot image (launchd+dyld, no dyld cache).
2. **UFS GPT partition named `lapis-root`** (or any partition / whole disk whose LBA0 is `LAPISFS1`).
3. **SD card** (SDHCI @ 0x8804000) — same LAPISFS1 image; enable with `lapis_sdhci=1 lapis_sdhci_mode=1` (data path still incomplete; use UFS for now).

## Recommended: put the IPSW rootfs on UFS without flashing the bootloader

### One-time prep (from Android / PixelOS, unlocked fastboot)

1. Build the full lapisfs (≈5 GiB, includes dyld shared cache):
   ```
   # already at /workspace/ios-lapis/rd/rootfs-23F77.lfs on the build machine
   ```
2. On the tablet under Android, free space on `/data` (≥5.5 GiB) and push:
   ```
   adb push rootfs-23F77.lfs /data/local/tmp/lapis-root.lfs
   ```
3. Write it into a spare GPT partition **you create once** (does not touch Android `super`/`boot`):
   - Option A (safest): use an SD card — `dd if=rootfs-23F77.lfs of=/dev/block/mmcblk1 bs=4M`
   - Option B: a new GPT partition on UFS named `lapis-root` (via `sgdisk` / `parted` from TWRP/Android). Then:
     ```
     dd if=/data/local/tmp/lapis-root.lfs of=/dev/block/by-name/lapis-root bs=4M
     ```
   The kernel looks for the name `lapis-root` **or** the magic `LAPISFS1` at the first LBA.

### Every boot

```
fastboot boot lapis-xnu-drv2.img
```

Expected log lines (serial / photo of early console / msgbuf):

```
LAPIS drv: built-in drivers v2 (...)
LapisGICv3: registered ...
LapisUFS: probing qcom,ufshc @ 0x1d84000 (mode 2) ...
LapisUFS: NOP OUT -> NOP IN ok
LapisBlk: ROOT = disk0sN (...)
LAPIS lapisfs: mounted blk root ...
load_init_program: attempting to load /sbin/launchd
```

Hardware image already carries: `lapis_sdhci=1 lapis_smp=1 lapis_touch=1 lapis_batt=1 lapis_wifi=1 lapis_gpu=1 lapis_audio=1`.
UFS defaults to **reuse** (ABL link, no HCE reset). Override with `lapis_ufs_mode=0` (probe-only) or `=1` (full reset — risky).

## Emulator check (already VERIFIED)

```
QEMU_EXTRA="-device ufs,serial=x -device ufs-lu,drive=u -drive if=none,id=u,file=ufs-disk.img,format=raw" \
  KGDB=1 TIMEOUT=55 drivers/emu/run-drv.sh out-emu/lapis-xnu-drv2-emu.img /tmp/out
```

## Blockers (honest)

| Piece | Status | Evidence |
|---|---|---|
| Wi-Fi firmware (PIL/SCM) | BLOCKER at EL1 under Gunyah | needs TrustZone; probe-only code present |
| GPU ZAP (SCM) | BLOCKER at EL1 under Gunyah | needs TrustZone; probe-only code present |
| ADSP audio (PIL/SCM) | BLOCKER at EL1 under Gunyah | needs TrustZone; probe-only code present |
| Full SMP (XNU processor_start) | NOT DONE | PSCI CPU_ON probe only; `ml_set_max_cpus` stays 1 |
| SDHCI data path | NOT DONE | probe-only; clocks/SMMU hypothesis |
| Touch / battery | NOT DONE | stubs only |
