
#!/bin/bash
# Build the lapis shim and wrap it in an Android boot.img v4 for `fastboot boot`.
#
# On TB520FU (Android 13+ GKI) stock boot.img is kernel-only; ramdisk lives in
# init_boot. ABL ignores a ramdisk packed into a temporary boot.img and leaves
# linux,initrd-* on the flashed init_boot. Therefore the LAPISXNU payload is
# appended to the Linux Image and image_size is patched so ABL loads it.
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
SHIM_SIZE=$size
echo "shim Image size (to _end): $SHIM_SIZE"

PAYLOAD="${1:-}"
if [ -n "$PAYLOAD" ]; then
	test -f "$PAYLOAD"
	# Append LAPISXNU at _end; patch Linux Image header image_size (u64 @ 16).
	cat out/Image "$PAYLOAD" > out/Image.with_payload
	mv out/Image.with_payload out/Image
	python3 - "$SHIM_SIZE" <<'PY'
import struct, sys, pathlib
shim_size = int(sys.argv[1])
path = pathlib.Path('out/Image')
img = bytearray(path.read_bytes())
assert img[56:60] == b'ARM\x64', img[56:60]
assert img[shim_size:shim_size+8] == b'LAPISXNU', img[shim_size:shim_size+8]
struct.pack_into('<Q', img, 16, len(img))
path.write_bytes(img)
print(f'Image+payload: {len(img)} bytes (shim={shim_size}, payload={len(img)-shim_size})')
print(f'image_size field={len(img):#x}')
PY
fi

# Always use a tiny empty gzip cpio ramdisk — matches GKI boot.img convention
# (real initrd is init_boot; we must not depend on boot.img ramdisk).
python3 "$ROOT/tools/empty_cpio_gz.py" > out/ramdisk.bin

python3 "$ROOT/tools/mkbootimg_v4.py" \
	--kernel out/Image \
	--ramdisk out/ramdisk.bin \
	--cmdline "console=null" \
	--os-version 15.0.0 \
	--os-patch-level 2025-01 \
	-o out/shim-boot.img

# Host render of the console layout (uses host FB dims from console.h).
gcc -O1 -Wall -o out/host_test console.c host_test.c && ./out/host_test || true
ls -l out/Image out/shim-boot.img out/ramdisk.bin
echo "Boot (temporary, never flash):  fastboot boot out/shim-boot.img"

