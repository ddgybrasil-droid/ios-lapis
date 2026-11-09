#!/bin/sh
# lapis_crypto -> crypto.bin, linked over _vm_parameter_validation_kern_test (39588 bytes)
set -e; cd "$(dirname "$0")"
CC=aarch64-linux-gnu-gcc
$CC -Os -ffreestanding -nostdlib -fno-pic -fno-pie -mgeneral-regs-only -mstrict-align -fno-stack-protector \
    -fno-asynchronous-unwind-tables -Wall -Wextra -Wno-unused-parameter -c lapis_crypto.c -o lapis_crypto.o

aarch64-linux-gnu-ld -T crypto.ld --no-warn-rwx-segments -e lapis_crypto_functions \
    --defsym=memcpy=0xfffffe0007c48550 --defsym=memset=0xfffffe0007c487a0 --defsym=read_random=0xfffffe0007548bb4 \
    -o crypto.elf lapis_crypto.o
aarch64-linux-gnu-objcopy -O binary -j .text crypto.elf crypto.bin
sz=$(stat -c %s crypto.bin); [ $sz -le 39588 ] || { echo "crypto.bin too big: $sz"; exit 1; }
if aarch64-linux-gnu-objdump -h crypto.elf | grep -qE '\.(data|bss)\b'; then echo "writable sections!"; exit 1; fi
aarch64-linux-gnu-readelf -r lapis_crypto.o | grep -c R_AARCH64 >/dev/null || true
echo "crypto.bin $sz bytes"
