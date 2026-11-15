#!/bin/bash
# Re-apply the mandatory fullroot DSC bring-up patches after any .31 TEXT replace.
# Order matters: container_init → pac_strip → syspath stub → CD rehash.
# Missing container_init after a .31 replace causes boot69-style launchd abort:
#   _container_init(): sandbox_get_container_expected() failed: -1
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
IMG=${1:?fullroot image}
python3 "$ROOT/tools/patch_fullroot_container_init.py" "$IMG"
python3 "$ROOT/tools/patch_fullroot_pac_strip.py" --nop-brk1 "$IMG"
python3 "$ROOT/tools/reseal_corecrypto_fips_hmac.py" "$IMG" --write
python3 "$ROOT/tools/patch_dsc_container_syspath.py" --image "$IMG" --write
python3 "$ROOT/tools/dsc_cd_rehash.py" "$IMG" --write
echo "reapply_dsc_bringup_patches: done on $IMG"
