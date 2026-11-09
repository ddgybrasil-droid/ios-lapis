# IPSW / rootfs install & boot (TB520FU) — never flash

Boot is always:

```
fastboot boot lapis-xnu-hw-safe.img   # first bring-up
# or
fastboot boot lapis-xnu-hw-full.img   # after safe looks healthy
```

Do **not** `fastboot flash` boot, vendor_boot, super, or any A/B slot.

## What the kernel mounts as root

1. Embedded ramdisk (`hw-min-23F77.lfs`) as `md0` (BSD root).
2. If LapisBlk finds a GPT partition named **`lapis-root`** (or any volume whose first LBA is `LAPISFS1`), lapisfs mounts that as the real userspace root and `load_init_program` pulls `/sbin/launchd` from it.
3. SDHCI (`0x8804000`) can publish `sd0` the same way when `lapis_sdhci_mode=2` and a card is present.

## Recommended install: microSD (preferred)

From the PixelOS dump this tablet uses **dynamic partitions** (`ro.boot.dynamic_partitions=true`) and A/B OTA for boot/system/vendor. There is **no** existing `lapis-root` GPT name. Touching `super` or shrinking Android userdata from a live system is the riskiest option.

**Prefer a microSD card** (external, reversible, zero flash of boot/system):

1. On a host, build or obtain `rootfs-23F77.lfs` (≈4.84 GiB; not in this handoff).
2. Partition the SD with GPT; create one partition named `lapis-root` (≥5.5 GiB), type Apple APFS GUID `7C3457EF-0000-11AA-AA11-00306543ECAC` is fine (kernel matches **name**, not type).
3. Write the image:
   ```
   dd if=rootfs-23F77.lfs of=/dev/diskNsM bs=4M status=progress
   ```
   Or write the raw `.lfs` starting at the partition’s first LBA so offset 0 contains `LAPISFS1`.
4. Insert the card, `fastboot boot` the kernel with `lapis_sdhci=1 lapis_sdhci_mode=2` (already on hw-full).

## Alternative: new UFS GPT partition (no boot/system flash)

Only if `sgdisk -p /dev/block/sda` (or equivalent) shows **unallocated** space after Android’s partitions:

1. Under TWRP / rooted Android, create a new partition named `lapis-root` in free space only. Do not delete or resize `super`, `boot`, `vendor_boot`, `userdata` unless you accept bricking risk.
2. Stage then write:
   ```
   adb push rootfs-23F77.lfs /data/local/tmp/lapis-root.lfs
   dd if=/data/local/tmp/lapis-root.lfs of=/dev/block/by-name/lapis-root bs=4M
   ```
3. A file on userdata alone is **staging only** — this kernel does not open Android `userdata` filesystems; it needs a GPT name or `LAPISFS1` at LBA0 of a LapisBlk disk.

## Boot-args (safety)

| Arg | Safe default | Full |
|---|---|---|
| `lapis_ufs_mode` | `2` (reuse ABL link) | `2` |
| `lapis_scm_pas` | `0` | `1` (auth_and_reset allowed) |
| `lapis_pil` | `0` | `2` (needs staged mdt PA) |
| `lapis_wifi_fw_doorbell` | unset | only after BHI looks sane |
| `lapis_gpu_gdsc` / `lapis_gpu_submit` | unset | only after ZAP OK |
| `lapis_smp_n` | `1` (safe) | `8` + `lapis_mpidr_shift=8` |

Override UFS to probe-only with `lapis_ufs_mode=0` if reuse DMA faults (SMMU stream `0x60`).

## Emulator reproduction

```
# Build GPT disk with a lapis-root LAPISFS1 partition (see drivers/emu / tools/mkgpt.py)
QEMU_EXTRA="-smp 1 -device ufs,serial=x -device ufs-lu,drive=u \
  -drive if=none,id=u,file=ufs-disk.img,format=raw" \
  KGDB=1 TIMEOUT=60 drivers/emu/run-drv.sh out-emu/lapis-xnu-emu.img /tmp/out
```

Verified outcome: UFS GPT + `ROOT=disk0s2` + lapisfs mount + launchd spawn, then immediate exit (`namespace 2 subcode 0xb`). See `DRIVERS-STATUS.md`.
