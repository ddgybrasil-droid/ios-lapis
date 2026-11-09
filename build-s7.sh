#!/bin/bash
# Local s7 build: shim (source) + ADT (source) + CI kernel from s6 with binpatches.
set -euo pipefail
cd "$(dirname "$0")"
BASE=${BASE:-/workspace/ios-lapis/emu/out/lapis-xnu-s6/Image}
OUT=${OUT:-out}; mkdir -p $OUT
make -s -C shim -f /dev/null 2>/dev/null || true
( cd shim; CC=aarch64-linux-gnu-gcc; CFLAGS="-O2 -ffreestanding -nostdlib -fno-pic -mgeneral-regs-only -mstrict-align -fno-stack-protector -Wall -Wextra"
  mkdir -p out; for f in start.S shim.c console.c fdt.c loader.c; do $CC $CFLAGS -c $f -o out/${f%.*}.o; done
  $CC $CFLAGS -static -T shim.ld -Wl,--build-id=none -Wl,--no-warn-rwx-segments -Wl,-N out/start.o out/shim.o out/console.o out/fdt.o out/loader.o -o out/shim.elf
  aarch64-linux-gnu-objcopy -O binary out/shim.elf out/Image
  size=$(aarch64-linux-gnu-nm out/shim.elf | awk '$3=="_end"{print $1}'); truncate -s $((16#$size)) out/Image )
python3 tools/mkadt.py $OUT/lapis.adt ${PROFILE:+--profile $PROFILE}
xnu/lapis-exc/build.sh && xnu/lapis-crypto/build.sh >/dev/null && xnu/lapis-mockfs/build.sh >/dev/null
RD_ARGS=()
if [ -n "${RAMDISK:-}" ]; then
  RD_ARGS=(--ramdisk "$RAMDISK")
  CMDLINE="${CMDLINE:-debug=0xc cpus=1 keepsyms=1 -v} rd=md0"
fi
python3 tools/repack.py "$BASE" shim/out/Image $OUT/lapis.adt $OUT/Image ${EXTRA:+--extra $EXTRA} --cmdline "${CMDLINE:-debug=0xc cpus=1 keepsyms=1 -v}" "${RD_ARGS[@]}"
python3 tools/empty_cpio_gz.py > $OUT/ramdisk.bin
python3 tools/mkbootimg_v4.py --kernel $OUT/Image --ramdisk $OUT/ramdisk.bin --cmdline "console=null" \
	--os-version 15.0.0 --os-patch-level 2025-01 -o $OUT/${NAME:-lapis-xnu-s7}.img
python3 tools/verify_bootimg.py $OUT/${NAME:-lapis-xnu-s7}.img | tail -1
sha256sum $OUT/${NAME:-lapis-xnu-s7}.img
