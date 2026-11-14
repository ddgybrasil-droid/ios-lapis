#!/bin/bash
# Build the lapis_pthread provider blob for the cave at _copyio_test.
# usage: build.sh [ksyms.txt]   (without arg uses the committed ksyms.h)
set -e
cd "$(dirname "$0")"
CAVE=0xfffffe0007993e80
[ -n "$1" ] && python3 gen_ksyms.py "$1"
aarch64-linux-gnu-gcc -O2 -ffreestanding -fno-builtin -nostdlib -mgeneral-regs-only \
  -fno-stack-protector -fno-pic -fno-asynchronous-unwind-tables -mbranch-protection=none \
  -fno-tree-loop-distribute-patterns -Wall -Werror -c lapis_pthread.c -o lp.o
aarch64-linux-gnu-ld -T link.ld --defsym CAVE=$CAVE -o lp.elf lp.o
test -z "$(aarch64-linux-gnu-objdump -h lp.elf | awk '$2==".data"&&$3!="00000000"')"
aarch64-linux-gnu-objcopy -O binary -j .text lp.elf lp.bin
aarch64-linux-gnu-nm lp.elf > lp.syms
awk '$3=="lapis_pthread_functions"{print $1}' lp.syms > lp.table
echo "blob $(stat -c %s lp.bin) bytes, table 0x$(cat lp.table)"
