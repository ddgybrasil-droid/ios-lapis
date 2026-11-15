#!/bin/bash
# Apply IPSW 23F77 ICU / locale / zoneinfo.default onto a COPY of fullroot-mnt2.
# Fixes containermanagerd SIGSEGV in ___CFCheckCFInfoPACSignature (FAR=0x8) during
# MCMMigrationStatus ISO8601 formatter init: CFDateFormatterCreateISO8601Formatter
# returned NULL because __cficu_udat_open failed without /usr/share/icu/icudt78l.dat,
# then CFDateFormatterSetFormat was called on the NULL ref (open-source CF, no NULL check).
#
# usage: apply_icu_data.sh FULLROOT_MNT2_COPY [METADATA.tsv]
# Does not touch the pristine ufs-fullroot.img.
# Does not invent runtime_state.plist or /private/var/containers.
# Does not patch CF/MCM or insert instruction NOPs.
set -euo pipefail
IMG=${1:?need path to fullroot-mnt2 COPY}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
SRC="$ROOT/rootfs/ipsw-icu"
DAT="$SRC/usr/share/icu/icudt78l.dat"
test -f "$DAT"
# Expect ~34 MiB IPSW icudt78l.dat (sha256 e95268c396c0ea5cfa122f754afbebebb631028a9451ed81c122e45028afed4b).
sz=$(stat -c %s "$DAT")
test "$sz" -gt 30000000 || { echo "icudt78l.dat too small ($sz)"; exit 1; }

python3 "$ROOT/tools/lapisfs_add.py" "$IMG" \
  "usr/share/icu=$SRC/usr/share/icu" \
  "usr/share/locale=$SRC/usr/share/locale" \
  "usr/share/zoneinfo.default=$SRC/usr/share/zoneinfo.default"

if [ -n "${2:-}" ]; then
  python3 "$ROOT/tools/lapisfs_owners.py" "$IMG" "$2" --modes /private/var
else
  echo "NOTE: skipped lapisfs_owners (pass METADATA.tsv as \$2 to keep /private/var/mobile 501:501)"
fi
echo "done: $IMG (+ /usr/share/icu/icudt78l.dat ${sz} bytes)"
