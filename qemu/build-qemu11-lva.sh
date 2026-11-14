#!/bin/sh
# Build a minimal qemu-system-aarch64 (QEMU 11.0.0 + lva property) for the lapis emulator.
# Why: -cpu max advertises FEAT_LVA (ID_AA64MMFR2_EL1.VARange=1). The target SoC (SM8650:
# Cortex-X4/A720/A520) reports VARange=0 (48-bit VA) per the Arm TRMs, and XNU's
# vm_sanitize_addr_size asserts (brk #0xbffc) when VARange!=0. Run with -cpu max,pauth-impdef=on,lva=off
# Deps without system -dev packages: glib/zlib headers from Debian debs extracted to a prefix,
# ninja + pkgconf from pip (venv).  ~175 MB build dir.
set -e
SRC=${SRC:-$PWD/qemu-11.0.0}; DEPS=${DEPS:-$PWD/qemu-deps/root/usr}
cd "$SRC" && patch -p1 -N < "$(dirname "$0")/qemu-11.0.0-arm-lva-property.patch" || true
mkdir -p build && cd build
PKG_CONFIG_PATH=$DEPS/lib/x86_64-linux-gnu/pkgconfig:$DEPS/share/pkgconfig ../configure \
  --target-list=aarch64-softmmu --without-default-features --enable-tcg --enable-fdt=internal \
  --disable-docs --disable-rust --disable-werror --disable-debug-info --disable-install-blobs
ninja qemu-system-aarch64
