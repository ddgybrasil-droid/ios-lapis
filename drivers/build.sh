#!/bin/bash
# Build the lapis built-in driver blob (drivers.bin) linked at its kernel VA.
# Kernel symbols come from the base kernel's own symtab (emu/ksyms.py) via syms.txt.
set -euo pipefail
cd "$(dirname "$0")"
BASE=${BASE:-/workspace/ios-lapis/emu/out/lapis-xnu-s6/Image}
KSYMS=out/ksyms.txt; mkdir -p out
[ -s $KSYMS ] || python3 /workspace/ios-lapis/emu/ksyms.py "$BASE" dump $KSYMS >/dev/null
DS=$(python3 - "$KSYMS" <<'PY'
import sys
k={}
for l in open(sys.argv[1]):
    a,n=l.split(None,1); k[n.strip()]=a
out=[]
for l in open('syms.txt'):
    l=l.split('#')[0].split()
    if not l: continue
    alias,sym=l; assert sym in k, sym
    out.append(f'--defsym={alias}=0x{k[sym]}')
out.append('--defsym=k_lapis_vtimer_intid=0x'+k['_lapis_vtimer_intid'])
print(' '.join(out))
PY
)
CC=aarch64-linux-gnu-gcc
CFLAGS="-Os -std=gnu11 -ffreestanding -nostdlib -fno-pic -fno-pie -mgeneral-regs-only -mstrict-align
 -fno-stack-protector -fno-asynchronous-unwind-tables -fno-common -fno-builtin -fno-jump-tables
 -Wall -Wextra -Wno-unused-parameter -Werror=implicit-function-declaration -Werror=incompatible-pointer-types"
SRCS="common/klog.c common/drv_common.c gic/lapis_gic.c gic/gic_selftest.c ufs/lapis_ufs.c blk/lapis_blk.c fb/lapis_fb.c smp/lapis_smp.c sdhci/lapis_sdhci.c touch/lapis_touch.c batt/lapis_batt.c wifi/lapis_wifi.c gpu/lapis_gpu.c audio/lapis_audio.c crypter/lapis_crypter.c scm/qcom_scm.c scm/lapis_pil.c"
OBJS=""
for s in $SRCS; do o=out/$(basename ${s%.c}).o; $CC $CFLAGS -c $s -o $o; OBJS="$OBJS $o"; done
$CC -c common/drv_entry.S -o out/drv_entry.o
aarch64-linux-gnu-ld -T drv.ld --no-warn-rwx-segments -e drv_pe_hook $DS -o out/drivers.elf out/drv_entry.o $OBJS
aarch64-linux-gnu-objcopy -O binary -j .text out/drivers.elf out/drivers.bin
aarch64-linux-gnu-objcopy -O binary -j .text2 out/drivers.elf out/drivers2.bin
dsz=$(aarch64-linux-gnu-size -A out/drivers.elf | awk '$1==".data"{print $2}')
[ "${dsz:-0}" = 0 ] || { echo "drivers: .data not allowed ($dsz bytes)"; exit 1; }
bsz=$(aarch64-linux-gnu-size -A out/drivers.elf | awk '$1==".bss"{print $2}')
echo "drivers.bin $(stat -c %s out/drivers.bin)/40320 + drivers2.bin $(stat -c %s out/drivers2.bin)/60412 bytes text, bss $((${bsz:-0}))/4096"
