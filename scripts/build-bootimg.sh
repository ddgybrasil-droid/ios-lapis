#!/bin/bash
# Produce out/shim-boot.img for `fastboot boot`. Optionally wrap an XNU payload.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
KERNEL="${1:-}"
ADT_OUT="$ROOT/out/lapis.adt"
PAYLOAD_OUT="$ROOT/out/payload.bin"
mkdir -p out

python3 tools/mkadt.py "$ADT_OUT"
if [ -n "$KERNEL" ]; then
	python3 tools/mkpayload.py "$KERNEL" "$ADT_OUT" "$PAYLOAD_OUT" \
		--cmdline "debug=0xc serial=3 cpus=1 keepsyms=1 -v"
	(cd shim && ./build.sh "$PAYLOAD_OUT")
else
	(cd shim && ./build.sh)
fi
cp -f shim/out/shim-boot.img out/shim-boot.img
cp -f shim/out/Image out/Image
ls -l out/shim-boot.img
echo "Device: fastboot boot out/shim-boot.img"
