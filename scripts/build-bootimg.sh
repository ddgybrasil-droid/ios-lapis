#!/bin/bash
# Produce out/shim-boot.img for `fastboot boot`.
# With a kernel Mach-O arg, embed LAPISXNU in the Linux Image and emit
# out/lapis-fastboot-boot.img (GKI-safe: tiny boot.img ramdisk).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
KERNEL="${1:-}"
ADT_OUT="$ROOT/out/lapis.adt"
PAYLOAD_OUT="$ROOT/out/payload.bin"
mkdir -p out

python3 tools/mkadt.py "$ADT_OUT" --dump >out/adt-dump.txt
python3 - <<'PY'
import json, pathlib
p = json.load(open('hw/lapis-profile.json'))
phys = int(p['memory']['phys_base'], 16)
size = int(p['memory']['mem_size'], 16)
print(f"profile DRAM: phys_base={phys:#x} mem_size={size:#x} end={phys+size:#x}")
blob = pathlib.Path('out/lapis.adt').read_bytes()
assert phys.to_bytes(8, 'little') in blob, 'dram-base missing from ADT'
panic = int(p['memory'].get('panic_log_size', '0x100000'), 16)
adt_size = size - panic
assert adt_size.to_bytes(8, 'little') in blob, 'dram-size missing from ADT'
print(f"ADT dram-size (after panic carve): {adt_size:#x}")
open('out/DRAM.txt', 'w').write(
    f"phys_base={phys:#x}\nmem_size={size:#x}\nadt_dram_size={adt_size:#x}\n"
    f"free_until={p['memory'].get('free_until','')}\n"
    f"note={p['memory'].get('note','')}\n")
PY

if [ -n "$KERNEL" ]; then
	test -f "$KERNEL"
	python3 tools/mkpayload.py "$KERNEL" "$ADT_OUT" "$PAYLOAD_OUT" \
		--cmdline "debug=0xc serial=3 cpus=1 keepsyms=1 -v"
	(cd shim && ./build.sh "$PAYLOAD_OUT")
	cp -f shim/out/shim-boot.img out/shim-boot.img
	cp -f shim/out/shim-boot.img out/lapis-fastboot-boot.img
	KBN="$(basename "$KERNEL")"
	if [ "$(realpath "$KERNEL")" != "$(realpath "out/$KBN")" ]; then
		cp -f "$KERNEL" "out/$KBN"
	fi
	python3 tools/verify_bootimg.py out/lapis-fastboot-boot.img \
		--expect-payload out/payload.bin | tee out/VERIFY.txt
	ls -l out/lapis-fastboot-boot.img out/shim-boot.img out/payload.bin
	echo "Device: fastboot boot out/lapis-fastboot-boot.img"
else
	(cd shim && ./build.sh)
	cp -f shim/out/shim-boot.img out/shim-boot.img
	cp -f shim/out/Image out/Image
	ls -l out/shim-boot.img
	echo "Device (diagnostic, no XNU payload): fastboot boot out/shim-boot.img"
fi
