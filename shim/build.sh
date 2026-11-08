#!/bin/bash
# Build the lapis shim and wrap it in an Android boot.img v4 for `fastboot boot`.
set -euo pipefail
cd "$(dirname "$0")"
ROOT="$(cd .. && pwd)"
CC="${CROSS_COMPILE:-aarch64-linux-gnu-}gcc"
OBJCOPY="${CROSS_COMPILE:-aarch64-linux-gnu-}objcopy"
READELF="${CROSS_COMPILE:-aarch64-linux-gnu-}readelf"
NM="${CROSS_COMPILE:-aarch64-linux-gnu-}nm"
CFLAGS="-O2 -ffreestanding -nostdlib -fno-pic -mgeneral-regs-only -mstrict-align -fno-stack-protector -Wall -Wextra"
mkdir -p out
for f in start.S shim.c console.c fdt.c loader.c; do
	$CC $CFLAGS -c "$f" -o "out/${f%.*}.o"
done
# Image runs wherever ABL places it; only the header's image_size may be absolute.
abs=$($READELF -r out/shim.o out/console.o out/fdt.o out/loader.o | grep -c ABS64 || true)
[ "$abs" = 0 ] || { echo "error: $abs absolute relocations in C objects" >&2; exit 1; }
$CC $CFLAGS -static -T shim.ld -Wl,--build-id=none -Wl,--no-warn-rwx-segments -Wl,-N \
	out/start.o out/shim.o out/console.o out/fdt.o out/loader.o -o out/shim.elf
$OBJCOPY -O binary out/shim.elf out/Image
size=$($NM out/shim.elf | awk '$3=="_end"{print $1}')
size=$((16#$size))
truncate -s "$size" out/Image

# Ramdisk: LAPISXNU payload if given as $1, else empty newc cpio (diagnostic boot).
if [ -n "${1:-}" ]; then
	cp "$1" out/ramdisk.bin
else
	python3 "$ROOT/tools/empty_cpio_gz.py" > out/ramdisk.bin
fi

python3 "$ROOT/tools/mkbootimg_v4.py" \
	--kernel out/Image \
	--ramdisk out/ramdisk.bin \
	--cmdline "console=null" \
	--os-version 15.0.0 \
	--os-patch-level 2025-01 \
	-o out/shim-boot.img

# Host render of the console layout (uses host FB dims from console.h).
gcc -O1 -Wall -o out/host_test console.c host_test.c && ./out/host_test || true
ls -l out/Image out/shim-boot.img
echo "Boot (temporary, never flash):  fastboot boot out/shim-boot.img"
