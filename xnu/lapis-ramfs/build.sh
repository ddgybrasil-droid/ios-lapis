#!/bin/sh
# Build lapis-ramfs blob for the xnu-12377.121.6 Image (placed at 0xfffffe000743e100,
# free zero tail of dead _vm_parameter_validation_kern_test after the lapisfs blob; limit 0xfffffe00074443d8).
set -e; cd "$(dirname "$0")"
KSYMS=${KSYMS:-/workspace/tpro-fix/ksyms.txt}
CC=aarch64-linux-gnu-gcc
$CC -Os -ffreestanding -nostdlib -fno-pic -fno-pie -mgeneral-regs-only -mstrict-align \
    -fno-stack-protector -fno-asynchronous-unwind-tables -fno-common -fno-builtin -Wall -Wno-unused-parameter \
    -c lapis_ramfs.c -o lapis_ramfs.o
$CC -Os -ffreestanding -nostdlib -fno-pic -fno-pie -mgeneral-regs-only -mstrict-align \
    -fno-stack-protector -fno-asynchronous-unwind-tables -fno-common -fno-builtin -Wall -Wno-unused-parameter \
    -c lapis_amfi.c -o lapis_amfi.o
$CC -Os -ffreestanding -nostdlib -fno-pic -fno-pie -mgeneral-regs-only -mstrict-align \
    -fno-stack-protector -fno-asynchronous-unwind-tables -fno-common -fno-builtin -Wall -Wno-unused-parameter \
    -c lapis_iomedia.c -o lapis_iomedia.o
DS=""
OBJS="lapis_ramfs.o lapis_amfi.o lapis_iomedia.o"
DEF=$(aarch64-linux-gnu-nm --defined-only $OBJS | awk 'NF==3{print $3}' | sort -u)
for s in $(aarch64-linux-gnu-nm -u $OBJS | awk 'NF==2{print $2}' | sort -u); do
  echo "$DEF" | grep -qx "$s" && continue
  case $s in lapis_mountroot_wrap_target) a=fffffe000743d9e8;;
    lapisfs_devread) a=fffffe000743d464;;
    *) a=$(awk -v n="_$s" '$2==n{print $1; exit}' "$KSYMS");; esac
  [ -n "$a" ] || { echo "missing symbol $s"; exit 1; }
  DS="$DS --defsym=$s=0x$a"
done
aarch64-linux-gnu-ld -T ramfs.ld --no-warn-rwx-segments -e lapis_ramfs_wrap $DS -o ramfs.elf lapis_ramfs.o lapis_amfi.o lapis_iomedia.o
aarch64-linux-gnu-objcopy -O binary -j .text ramfs.elf ramfs.bin
sz=$(stat -c %s ramfs.bin); echo "ramfs.bin $sz bytes"
[ $sz -le $((0x74443d8 - 0x743e100)) ] || { echo too big; exit 1; }
aarch64-linux-gnu-nm ramfs.elf | grep -E " lapis_ramfs_wrap$"
