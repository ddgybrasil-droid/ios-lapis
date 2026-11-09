#!/bin/bash
# s10 kernel + lapis built-in drivers (drivers/): hw image + emulator image.
# Same pipeline as build-s7.sh/build-both.sh, plus:
#   drivers/build.sh            -> drivers/out/drivers.{elf,bin}
#   tools/drv_adt.py            -> ADT nodes for LapisGICv3/LapisUFS (emu | hw)
#   repack --extra tools/drv_binpatches.py
set -euo pipefail
cd "$(dirname "$0")"
BASE=${BASE:-/workspace/ios-lapis/emu/out/lapis-xnu-s6/Image}
TAG=${TAG:-lapis-xnu-drv}
( cd shim; CC=aarch64-linux-gnu-gcc; CFLAGS="-O2 -ffreestanding -nostdlib -fno-pic -mgeneral-regs-only -mstrict-align -fno-stack-protector -Wall -Wextra"
  mkdir -p out; for f in start.S shim.c console.c fdt.c loader.c; do $CC $CFLAGS -c $f -o out/${f%.*}.o; done
  $CC $CFLAGS -static -T shim.ld -Wl,--build-id=none -Wl,--no-warn-rwx-segments -Wl,-N out/start.o out/shim.o out/console.o out/fdt.o out/loader.o -o out/shim.elf
  aarch64-linux-gnu-objcopy -O binary out/shim.elf out/Image
  size=$(aarch64-linux-gnu-nm out/shim.elf | awk '$3=="_end"{print $1}'); truncate -s $((16#$size)) out/Image ) 2>/dev/null
xnu/lapis-exc/build.sh >/dev/null && xnu/lapis-crypto/build.sh >/dev/null && xnu/lapis-mockfs/build.sh >/dev/null
drivers/build.sh
one() {  # one OUTDIR NAME PROFILE TARGET CMDLINE [RAMDISK]
	local out=$1 name=$2 prof=$3 tgt=$4 cmd=$5 rd=${6:-}; mkdir -p $out
	python3 tools/mkadt.py $out/lapis.base.adt ${prof:+--profile $prof} >/dev/null
	python3 tools/drv_adt.py $out/lapis.base.adt $out/lapis.adt --target $tgt
	local RD=(); [ -n "$rd" ] && { RD=(--ramdisk "$rd"); cmd="$cmd rd=md0"; }
	python3 tools/repack.py "$BASE" shim/out/Image $out/lapis.adt $out/Image --extra tools/drv_binpatches.py \
		--cmdline "$cmd" "${RD[@]}" | grep -E "drv-|cmdline|Image:" || true
	python3 tools/empty_cpio_gz.py > $out/ramdisk.bin
	python3 tools/mkbootimg_v4.py --kernel $out/Image --ramdisk $out/ramdisk.bin --cmdline "console=null" \
		--os-version 15.0.0 --os-patch-level 2025-01 -o $out/$name.img
	python3 tools/verify_bootimg.py $out/$name.img | tail -1
	sha256sum $out/$name.img
}
[ "${SKIP_HW:-0}" = 1 ] || one out $TAG "" hw "debug=0xc cpus=1 keepsyms=1 -v"
# EMULATOR ONLY: 1 GHz CNTFRQ under TCG -> sched-hygiene timeouts off (as build-both.sh)
one out-emu $TAG-emu hw/qemu-virt-profile.json emu \
	"debug=0xc cpus=1 keepsyms=1 -v interrupt-masked-debug-mode=0 sched_preemption_disable_debug_mode=0" \
	${RAMDISK-tools/ramdisk/launchd.macho}
