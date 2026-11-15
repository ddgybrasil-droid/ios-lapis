#!/bin/bash
# Cloud-VM variant of emu_run_gdb.sh: qemu11 binary + RAM capped for ~15–32 GiB hosts.
# usage: emu_run_gdb_cloud.sh KERNEL ROOTIMG OUTDIR [timeout_s] [mem_gib]
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
K=$1; IMG=$2; OUT=$3; T=${4:-720}; MEM=${5:-43}
mkdir -p "$OUT"
PORT=$((30000+RANDOM%20000))
DRIVE=${DRIVE:-$ROOT/tools/emu_gdb_driver.py}
CPU=${CPU:-max,pauth-impdef=on,lva=off}
QEMU=${QEMU:-$ROOT/qemu/qemu-11.0.0/build/qemu-system-aarch64}
EXTRA=${EXTRA:-$ROOT/tools/emu_gdb_extra_boot.py}
export OUT EXTRA CONT_REASON=${CONT_REASON:-1} MAXEXC=${MAXEXC:-40}
echo "cloud-emu: QEMU=$QEMU CPU=$CPU MEM=${MEM}G IMG=$IMG K=$K OUT=$OUT T=$T"
$QEMU -nodefaults -display none -serial none \
 -M virt,virtualization=off,secure=off,gic-version=3,memory-backend=ram0 \
 -m ${MEM}G -object memory-backend-ram,id=ram0,size=${MEM}G,reserve=off,prealloc=off \
 -cpu "$CPU" -smp 4 \
 -drive if=none,id=ufsdisk,file="$IMG",format=raw,readonly=on \
 -device ufs,id=ufs0 -device ufs-lu,drive=ufsdisk,lun=0 \
 -kernel "$K" -no-reboot -gdb tcp::$PORT -S -monitor none >"$OUT/qemu.out" 2>&1 &
QP=$!; sleep 2
if ! kill -0 $QP 2>/dev/null; then
  echo "qemu failed to start:"; tail -50 "$OUT/qemu.out"; exit 1
fi
timeout -s INT -k 120 "$T" gdb-multiarch -q -batch \
  -ex "set pagination off" -ex "target remote :$PORT" -x "$DRIVE" >"$OUT/gdb.out" 2>&1 || true
kill $QP 2>/dev/null; wait $QP 2>/dev/null || true
echo done
