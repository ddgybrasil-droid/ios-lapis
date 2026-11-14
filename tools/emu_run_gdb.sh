#!/bin/bash
# QEMU (virt, 8 CPU, cpu max,pauth-impdef=on, fullroot on UFS read-only) halted under gdb;
# tools/emu_gdb_driver.py logs exception_triage / os_reason_create / panic,
# walks the user frame-pointer chain, and dumps msgbuf. Needs KSYMS=<addr name file>.
# env: QEMU=<qemu-system-aarch64>, EXTRA=<extra gdb-python hooks>, MAXEXC, MAXUA
# usage: run.sh KERNEL ROOTIMG OUTDIR [timeout_s]
K=$1; IMG=$2; OUT=$3; T=${4:-900}
mkdir -p $OUT; PORT=$((30000+RANDOM%20000))
${QEMU:-qemu-system-aarch64} -nodefaults -display none -serial none \
 -M virt,virtualization=off,secure=off,gic-version=3,memory-backend=ram0 \
 -m 43G -object memory-backend-ram,id=ram0,size=43G,reserve=off,prealloc=off \
 -cpu max,pauth-impdef=on -smp 8 \
 -drive if=none,id=ufsdisk,file=$IMG,format=raw,readonly=on \
 -device ufs,id=ufs0 -device ufs-lu,drive=ufsdisk,lun=0 \
 -kernel $K -no-reboot -gdb tcp::$PORT -S -monitor none > $OUT/qemu.out 2>&1 &
QP=$!; sleep 1
OUT=$OUT timeout $T gdb-multiarch -q -batch -ex "set pagination off" -ex "target remote :$PORT" -x "$(dirname "$(readlink -f "$0")")/emu_gdb_driver.py" > $OUT/gdb.out 2>&1
kill $QP 2>/dev/null; wait $QP 2>/dev/null
echo done
