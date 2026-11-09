#!/bin/bash
# hardware image (real SM8650 GIC) + emulator image (QEMU virt GIC); same kernel/shim.
set -e; cd "$(dirname "$0")"
OUT=out NAME=${TAG:-lapis-xnu-s8} ./build-s7.sh
# EMULATOR ONLY: QEMU's CNTFRQ is 1 GHz, so XNU's tick-based sched-hygiene
# timeouts (tuned for ~24 MHz) are ~50x too short under TCG -> disable them.
PROFILE=hw/qemu-virt-profile.json OUT=out-emu NAME=${TAG:-lapis-xnu-s8}-emu \
  RAMDISK=tools/ramdisk/launchd.macho \
  CMDLINE="debug=0xc cpus=1 keepsyms=1 -v interrupt-masked-debug-mode=0 sched_preemption_disable_debug_mode=0" ./build-s7.sh
