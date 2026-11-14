#!/bin/bash
# usage: emu_run_gdb.sh KERNEL ROOTIMG OUTDIR [timeout_s]
set -e
K=$1; IMG=$2; OUT=$3; T=${4:-900}
mkdir -p "$OUT"; PORT=$((30000+RANDOM%20000))
DRIVE=${DRIVE:-/workspace/tpro-fix/drive.py}
CPU=${CPU:-max,pauth-impdef=on,lva=off}
QEMU=${QEMU:-qemu-system-aarch64}
EXTRA=${EXTRA:-/workspace/tools/emu_gdb_extra_boot.py}
export OUT EXTRA CONT_REASON=${CONT_REASON:-1} MAXEXC=${MAXEXC:-40}
$QEMU -nodefaults -display none -serial none \
 -M virt,virtualization=off,secure=off,gic-version=3,memory-backend=ram0 \
 -m 43G -object memory-backend-ram,id=ram0,size=43G,reserve=off,prealloc=off \
 -cpu "$CPU" -smp 8 \
 -drive if=none,id=ufsdisk,file="$IMG",format=raw,readonly=on \
 -device ufs,id=ufs0 -device ufs-lu,drive=ufsdisk,lun=0 \
 -kernel "$K" -no-reboot -gdb tcp::$PORT -S -monitor none >"$OUT/qemu.out" 2>&1 &
QP=$!; sleep 1
timeout -s INT -k 120 "$T" gdb-multiarch -q -batch \
  -ex "set pagination off" -ex "target remote :$PORT" -x "$DRIVE" >"$OUT/gdb.out" 2>&1 || true
kill $QP 2>/dev/null; wait $QP 2>/dev/null || true
echo done
