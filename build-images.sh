#!/bin/bash
# Build the three deliverable images:
#   out/lapis-xnu-hw-safe.img   — probes only; no PAS auth, no PCIe/GDSC resets, UFS reuse
#   out/lapis-xnu-hw-full.img   — all bring-up boot-args enabled (still gated per-op)
#   out-emu/lapis-xnu-emu.img   — QEMU virt profile + UFS-root capable (rd=md0 placeholder)
set -euo pipefail
cd "$(dirname "$0")"
BASE=${BASE:-/workspace/ios-lapis/emu/out/lapis-xnu-s6/Image}
TAG_SAFE=${TAG_SAFE:-lapis-xnu-hw-safe}
TAG_FULL=${TAG_FULL:-lapis-xnu-hw-full}
TAG_EMU=${TAG_EMU:-lapis-xnu-emu}
X="-arm64e_preview_abi bingrade=0x100000c:2 unentitled_ios_sim_launch=1"

# SAFE: enable driver threads but keep destructive modes at 0 / probe.
# UFS defaults to reuse (mode 2) via ADT; SCM/PIL/WiFi-reset/GPU-gdsc off.
SAFE_DRV="cpus=1 lapis_smp=1 lapis_smp_n=1 lapis_psci=1 lapis_scm=1 lapis_scm_pas=0 lapis_pil=0"
SAFE_DRV="$SAFE_DRV lapis_sdhci=1 lapis_sdhci_mode=0"
SAFE_DRV="$SAFE_DRV lapis_touch=1 lapis_touch_mode=0"
SAFE_DRV="$SAFE_DRV lapis_batt=1 lapis_batt_mode=0"
SAFE_DRV="$SAFE_DRV lapis_wifi=1 lapis_wifi_mode=0"
SAFE_DRV="$SAFE_DRV lapis_gpu=1 lapis_gpu_mode=0"
SAFE_DRV="$SAFE_DRV lapis_audio=1 lapis_audio_mode=0"
SAFE_DRV="$SAFE_DRV lapis_ufs_mode=2"

# FULL: enable bring-up modes; still requires explicit *=1 for doorbells/auth.
FULL_DRV="cpus=1 lapis_smp=1 lapis_smp_n=8 lapis_mpidr_shift=8 lapis_psci=1 lapis_scm=1 lapis_scm_pas=1 lapis_pil=2"
FULL_DRV="$FULL_DRV lapis_sdhci=1 lapis_sdhci_mode=2"
FULL_DRV="$FULL_DRV lapis_touch=1 lapis_touch_mode=2"
FULL_DRV="$FULL_DRV lapis_batt=1 lapis_batt_mode=1"
FULL_DRV="$FULL_DRV lapis_wifi=1 lapis_wifi_mode=2"
FULL_DRV="$FULL_DRV lapis_gpu=1 lapis_gpu_mode=2"
FULL_DRV="$FULL_DRV lapis_audio=1 lapis_audio_mode=2"
FULL_DRV="$FULL_DRV lapis_ufs_mode=2"

echo "=== hw-safe ==="
OUT=out NAME=$TAG_SAFE \
  RAMDISK=${HW_RAMDISK:-/workspace/ios-lapis/rd/hw-min-23F77.lfs} \
  CMDLINE="debug=0xc keepsyms=1 -v $SAFE_DRV $X" \
  EXTRA=tools/drv_binpatches.py DRV_TARGET=hw \
  ./build-s7.sh

echo "=== hw-full ==="
OUT=out NAME=$TAG_FULL \
  RAMDISK=${HW_RAMDISK:-/workspace/ios-lapis/rd/hw-min-23F77.lfs} \
  CMDLINE="debug=0xc keepsyms=1 -v $FULL_DRV $X" \
  EXTRA=tools/drv_binpatches.py DRV_TARGET=hw \
  ./build-s7.sh

echo "=== emu ==="
PROFILE=hw/qemu-virt-profile.json OUT=out-emu NAME=$TAG_EMU \
  RAMDISK_EXT=${RAMDISK_EXT:-/workspace/ios-lapis/rd/placeholder.lfs} \
  CMDLINE="debug=0xc cpus=1 keepsyms=1 -v interrupt-masked-debug-mode=0 sched_preemption_disable_debug_mode=0" \
  CMDLINE_EXTRA="$X" \
  EXTRA=tools/drv_binpatches.py DRV_TARGET=emu \
  ./build-s7.sh

mkdir -p /opt/cursor/artifacts
cp -f out/$TAG_SAFE.img out/$TAG_FULL.img out-emu/$TAG_EMU.img /opt/cursor/artifacts/ 2>/dev/null || true
sha256sum out/$TAG_SAFE.img out/$TAG_FULL.img out-emu/$TAG_EMU.img
ls -lh out/$TAG_SAFE.img out/$TAG_FULL.img out-emu/$TAG_EMU.img
