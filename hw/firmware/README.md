# TB520FU firmware blobs (from PixelOS dump)

Copied from `/vendor/firmware_mnt/image/kiwi` and `/vendor/firmware` on device.
**Do not commit binary blobs** — they are gitignored. See `MANIFEST.sha256` and `../firmware-MANIFEST.sha256`.

- `wifi-kiwi/` — WCN785x / Kiwi v2 (cnss2 + qca_cld3_kiwi_v2), not ath12k
- `gpu/` — Adreno Gen7_9_1 / Adreno750v2 (gen71100 preferred; gen70900 also present on vendor)
- `ufs/` — empty: UFS PHY is programmed from registers; no separate FW blob in dump
