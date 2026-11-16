#!/bin/sh
# Build lapis-ramfs blob for the xnu-12377.121.6 Image (placed at 0xfffffe000743e100,
# free zero tail of dead _vm_parameter_validation_kern_test after the lapisfs blob; limit 0xfffffe00074443d8).
#
# Reproducibility: pin CFLAGS (no ident, no build-id, sorted objs). Record toolchain in
# TOOLCHAIN.txt. After link, write ramfs.bin.sha256. Expected bring-up size on
# aarch64-linux-gnu-gcc 13.3.0 / binutils 2.42: 11688 bytes
# (sha256 f84c4345f4da1bd913f409042e1d720e22ce4bfe8b213be67546479920eef3e5).
set -e; cd "$(dirname "$0")"
# Prefer the bring-up tree's ksyms; fall back to the common /tmp/ipsw copy.
if [ -z "${KSYMS:-}" ]; then
  if [ -f /workspace/tpro-fix/ksyms.txt ]; then KSYMS=/workspace/tpro-fix/ksyms.txt
  elif [ -f "$(dirname "$0")/../../tpro-fix/ksyms.txt" ]; then KSYMS="$(cd "$(dirname "$0")/../.." && pwd)/tpro-fix/ksyms.txt"
  elif [ -f /tmp/ipsw/ksyms.txt ]; then KSYMS=/tmp/ipsw/ksyms.txt
  else echo "KSYMS not set and no ksyms.txt found"; exit 1; fi
fi
CC="${CC:-aarch64-linux-gnu-gcc}"
LD="${LD:-aarch64-linux-gnu-ld}"
OBJCOPY="${OBJCOPY:-aarch64-linux-gnu-objcopy}"
NM="${NM:-aarch64-linux-gnu-nm}"
CFLAGS="-Os -ffreestanding -nostdlib -fno-pic -fno-pie -mgeneral-regs-only -mstrict-align \
    -fno-stack-protector -fno-asynchronous-unwind-tables -fno-common -fno-builtin \
    -fno-ident -fno-exceptions -Wall -Wno-unused-parameter"
for src in lapis_ramfs.c lapis_amfi.c lapis_iomedia.c lapis_iomfb.c lapis_sandbox.c; do
  # shellcheck disable=SC2086
  $CC $CFLAGS -c "$src" -o "${src%.c}.o"
done
DS=""
OBJS="lapis_ramfs.o lapis_amfi.o lapis_iomedia.o lapis_iomfb.o lapis_sandbox.o"
DEF=$($NM --defined-only $OBJS | awk 'NF==3{print $3}' | sort -u)
for s in $($NM -u $OBJS | awk 'NF==2{print $2}' | sort -u); do
  echo "$DEF" | grep -qx "$s" && continue
  case $s in lapis_mountroot_wrap_target) a=fffffe000743d9e8;;
    lapisfs_devread) a=fffffe000743d464;;
    *) a=$(awk -v n="_$s" '$2==n{print $1; exit}' "$KSYMS");; esac
  [ -n "$a" ] || { echo "missing symbol $s"; exit 1; }
  DS="$DS --defsym=$s=0x$a"
done
# Link every object in $OBJS (sandbox was compiled but omitted here in efe0a23).
# --build-id=none keeps the .text image stable across hosts.
$LD -T ramfs.ld --no-warn-rwx-segments --build-id=none -e lapis_ramfs_wrap $DS -o ramfs.elf $OBJS
$OBJCOPY -O binary -j .text ramfs.elf ramfs.bin
sz=$(stat -c %s ramfs.bin); echo "ramfs.bin $sz bytes"
[ $sz -le $((0x74443d8 - 0x743e100)) ] || { echo too big; exit 1; }
sha256sum ramfs.bin | tee ramfs.bin.sha256
{
  echo "CC=$CC"
  $CC --version | head -1
  echo "LD=$LD"
  $LD --version | head -1
  echo "KSYMS=$KSYMS"
  echo "CFLAGS=$CFLAGS"
  echo "OBJS=$OBJS"
  echo "ramfs.bin_bytes=$sz"
  cat ramfs.bin.sha256
  $NM ramfs.elf | grep -E ' lapis_(ramfs_wrap|amfi_boot|sandbox_register|iomedia_register|iomfb_register)$'
  $NM ramfs.elf | grep -E ' lapis_new_user_client'
} | tee TOOLCHAIN.txt
$NM ramfs.elf | grep -E ' lapis_(ramfs_wrap|sandbox_register|iomfb_register|new_user_client)'
