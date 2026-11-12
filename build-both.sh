#!/bin/bash
# hardware image (real SM8650 GIC) + emulator image (QEMU virt GIC); same kernel/shim.
# s11 root ramdisk (lapisfs, rd=md0):
#   hw : small image EMBEDDED in the payload (HW_RAMDISK; default real launchd+dyld, no dyld cache,
#        since fastboot-boot images can't carry the ~5 GB cache)
#   emu: large image loaded by QEMU '-device loader' (RAMDISK_EXT; emu only)
set -e; cd "$(dirname "$0")"
X="${CMDLINE_EXTRA:--arm64e_preview_abi bingrade=0x100000c:2}"
HW_DRIVERS='cpus=8 lapis_smp=1 lapis_psci=1 lapis_scm=1 lapis_sdhci=1 lapis_touch=1 lapis_batt=1 lapis_wifi=1 lapis_gpu=1 lapis_audio=1'
env -u RAMDISK_EXT OUT=out NAME=${TAG:-lapis-xnu-s8} RAMDISK=${HW_RAMDISK:-/workspace/ios-lapis/rd/hw-min-23F77.lfs} \
  CMDLINE_EXTRA="$X $HW_DRIVERS" ./build-s7.sh
# EMULATOR ONLY: QEMU's CNTFRQ is 1 GHz, so XNU's tick-based sched-hygiene
# timeouts (tuned for ~24 MHz) are ~50x too short under TCG -> disable them.
PROFILE=hw/qemu-virt-profile.json OUT=out-emu NAME=${TAG:-lapis-xnu-s8}-emu \
  RAMDISK_EXT=${RAMDISK_EXT:-/workspace/ios-lapis/rd/placeholder.lfs} CMDLINE_EXTRA="$X" \
  CMDLINE="debug=0xc cpus=8 keepsyms=1 -v lapis_smp=1 lapis_psci=0" ./build-s7.sh
