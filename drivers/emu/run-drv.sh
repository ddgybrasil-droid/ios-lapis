#!/bin/bash
# ios-lapis QEMU harness: boot a lapis image (boot.img or raw arm64 Image with
# embedded LAPISXNU) the way Lenovo ABL does: EL1, MMU off, x0 = DTB, shim then
# copies XNU to phys_base 0x8C0000000 and jumps. FB 0xD5100000 is plain RAM.
#
# usage: run.sh <image> [outdir]
# env:   TIMEOUT=secs (default 8)   TRACE=1 (adds -d in_asm,nochain: huge log)
#        GDB=1 (start halted, gdbstub :1234, no automation — attach yourself)
#        PASS2=0 (skip gdb re-run that captures GPRs at first exception)
set -euo pipefail
HERE=/workspace/ios-lapis/emu
IMG="$(readlink -f "$1")"
NAME="$(basename "${IMG%.*}")"
OUT="${2:-$HERE/out/$NAME}"
TIMEOUT="${TIMEOUT:-8}"
mkdir -p "$OUT"; rm -f "$OUT"/*
FB_BASE=0xd5100000; FB_BYTES=$((11776*1840))

if head -c 8 "$IMG" | grep -q 'ANDROID!'; then
	python3 "$HERE/extract_kernel.py" "$IMG" "$OUT/Image" >/dev/null
else
	cp "$IMG" "$OUT/Image"
fi
KERNEL="$OUT/Image"

# RAM 0x40000000..0xB00000000 (43 GiB, lazily allocated / MAP_NORESERVE) covers
# phys_base 0x8C0000000 + mem_size and the splash FB at 0xD5100000.
QEMU=(qemu-system-aarch64 -nodefaults -display none -serial none
	-M virt,virtualization=off,secure=off,gic-version=3,memory-backend=ram0
	-m 43G -object memory-backend-ram,id=ram0,size=43G,reserve=off
	-cpu max -smp ${SMP:-1} -kernel "$KERNEL" ${QEMU_EXTRA:-})
# emu-only external ramdisk (repack --ramdisk-ext): "<pa> <file>" next to the boot image
if [ -f "$IMG.ramdisk-pa" ]; then read RD_PA RD_FILE < "$IMG.ramdisk-pa"
	QEMU+=(-device loader,file="$RD_FILE",addr=$RD_PA,force-raw=on); echo "ramdisk-ext: $RD_FILE @ $RD_PA"; fi
DFLAGS=${DFLAGS:-guest_errors,unimp}
[ "${TRACE:-0}" = 1 ] && DFLAGS=$DFLAGS,in_asm,nochain

if [ "${GDB:-0}" = 1 ]; then
	echo "gdbstub on :1234, CPU halted. gdb-multiarch -ex 'target remote :1234'"
	exec "${QEMU[@]}" -monitor stdio -d $DFLAGS -D "$OUT/qemu.log" -gdb tcp::1234 -S
fi

SOCK="$OUT/mon.sock"
# KGDB=1: gdbstub on the main run; at TIMEOUT dump msgbuf (full kernel log) + backtrace
KPORT=$((40000 + RANDOM % 20000)); KG=(); [ "${KGDB:-0}" = 1 ] && KG=(-gdb tcp::$KPORT)
"${QEMU[@]}" "${KG[@]}" -monitor unix:"$SOCK",server,nowait -d $DFLAGS -D "$OUT/qemu.log" &
QPID=$!
sleep "$TIMEOUT"
if [ "${KGDB:-0}" = 1 ]; then
	MSGBUFP=$(awk '$2=="_msgbufp"{print "0x"$1}' "$HERE/out/ksyms.txt")
	timeout 60 gdb-multiarch -q -batch -ex "target remote :$KPORT" -x "$HERE/el1.gdb" \
	  -ex "set \$mb = *(unsigned long *)$MSGBUFP" \
	  -ex "printf \"msgbuf %#lx size %d bufx %d bufc %#lx\\n\", \$mb, *(int*)(\$mb+4), *(int*)(\$mb+8), *(unsigned long*)(\$mb+16)" \
	  -ex "eval \"dump binary memory $OUT/msgbuf.raw %lu %lu\", *(unsigned long*)(\$mb+16), *(unsigned long*)(\$mb+16) + *(int*)(\$mb+4)" \
	  -x "$HERE/bt.gdb" -ex detach > "$OUT/kgdb.txt" 2>&1 || true
	python3 - "$OUT" <<'PY'
import sys,re
o=sys.argv[1]; t=open(o+'/kgdb.txt').read(); m=re.search(r'bufx (\d+)',t)
b=open(o+'/msgbuf.raw','rb').read(); x=int(m.group(1)) if m else 0
open(o+'/msgbuf.txt','w').write((b[x:]+b[:x]).replace(b'\0',b'').decode(errors='replace'))
PY
	python3 "$HERE/ksyms.py" "$KERNEL" addr $(grep -oP '(pc|lr|ret) \K0x[0-9a-f]+' "$OUT/kgdb.txt") > "$OUT/bt_sym.txt" 2>&1 || true
fi
python3 "$HERE/hmp.py" "$SOCK" stop "info registers" \
	"pmemsave $FB_BASE $FB_BYTES \"$OUT/fb.raw\"" ${MEMDUMP:+"pmemsave $MEMDUMP \"$OUT/mem.raw\""} quit > "$OUT/regs_final.txt" 2>&1 || true
wait $QPID 2>/dev/null || true
rm -f "$SOCK"

python3 "$HERE/fb2png.py" "$OUT/fb.raw" "$OUT/fb.png" > "$OUT/fb_bars.txt"
rm -f "$OUT/fb.raw"
python3 "$HERE/analyze.py" "$OUT" > "$OUT/summary.txt" || true

# Pass 2: deterministic re-run halted at the first exception vector to grab GPRs.
VEC=$(grep -m1 -oP '\.\.\.to EL1 PC \K0x[0-9a-f]+' "$OUT/qemu.log" || true)
if [ -n "$VEC" ] && [ "${PASS2:-1}" = 1 ]; then
	PORT=$((20000 + RANDOM % 20000))
	"${QEMU[@]}" -monitor none -gdb tcp::$PORT -S &
	QPID=$!; sleep 1
	timeout 60 gdb-multiarch -q -batch -ex "target remote :$PORT" -ex "hbreak *$VEC" -ex c \
		-x "$HERE/exc.gdb" > "$OUT/regs_at_exception.txt" 2>&1 || true
	kill $QPID 2>/dev/null || true; wait $QPID 2>/dev/null || true
fi
cat "$OUT/summary.txt"
echo "outputs: $OUT  (fb.png, fb_small.png, qemu.log, regs_final.txt, regs_at_exception.txt, summary.txt)"
