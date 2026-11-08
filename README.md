# ios-lapis

Personal hobby port: boot Apple's open-source **XNU** (`xnu-12377`, iPadOS / macOS 26 era)
on a **Lenovo Yoga Pad Pro 12.7** (`TB520FU`, codename **lapis**, Snapdragon **SM8650**).

Method follows [ios-sweet](https://github.com/dipa-studio/ios-sweet): a shim packed into an
Android boot image, started with **`fastboot boot` only** (nothing is flashed), which loads
an XNU Mach-O plus a generated Apple Device Tree (ADT).

**No Apple proprietary binaries or IPSW contents are committed here.**

## Status (M1)

| Piece | State |
|-------|-------|
| Hardware profile / FB / DRAM | `hw/lapis-profile.json` — FB M0-verified; DRAM `0x8C0000000` + `0x22FA60000` from live iomem |
| aarch64 shim + boot.img v4 | Builds on Linux; paints STEP progress on FB before XNU jump |
| `boot_args` for xnu-12377 | **Verified**: `BOOT_LINE_LENGTH=1024`, `sizeof=1152` |
| XNU patches for LAPIS | `0001`–`0011` on `xnu-12377.121.6` |
| macOS XNU CI | **Green** — `kernel.development.lapis` ([run 42517908527](https://github.com/ddgybrasil-droid/ios-lapis/actions/runs/42517908527)) |
| Flashable boot artifact | CI artifact **`lapis-fastboot-boot`** → `lapis-fastboot-boot.img` |
| On-device XNU boot | **Not claimed** — image is ready to try; early bring-up TBD |

## Build the shim (Linux)

```sh
sudo apt-get install gcc-aarch64-linux-gnu binutils-aarch64-linux-gnu
./scripts/build-bootimg.sh
# => out/shim-boot.img  (diagnostic, no XNU payload)
```

With a kernel Mach-O from CI:

```sh
./scripts/build-bootimg.sh path/to/kernel.development.lapis
# => out/lapis-fastboot-boot.img
```

## Boot on the tablet

Unlocked bootloader. **Temporary boot only:**

```sh
fastboot boot out/lapis-fastboot-boot.img
```

Do **not** `fastboot flash` boot / init_boot / vendor_boot.

TB520FU is Android 13+ GKI: stock `boot.img` is kernel-only (ramdisk in
`init_boot`). The packer therefore **embeds** `LAPISXNU` in the Linux Image
(after `_end`) and keeps a tiny empty boot.img ramdisk. Without a payload the
shim paints a red diagnostic screen; with a payload it paints STEP progress on
the splash FB, sets up ADT + `boot_args` (`physBase=0x8C0000000`), and jumps to
XNU at EL1 (MMU off).

## Build XNU (macOS / CI)

```sh
gh workflow run xnu.yml
gh run download -n xnu-kernel-lapis -D xnu/out
```

Requirements: KDK 26.5, recent Xcode — details in `docs/CI-REQUIREMENTS.md`.

## Layout

```
shim/           EL1 Linux-Image shim + XNU loader (1024/1152 boot_args)
tools/          mkadt.py, mkpayload.py, mkbootimg_v4.py, adt.py
scripts/        build-bootimg.sh
hw/             lapis-profile.json + hardware notes
xnu/patches/    LAPIS patches for xnu-12377.121.6
docs/           handoff, porting plan, CI, status
```

## License

MIT. Shim/ADT tooling adapted from ios-sweet (MIT). XNU patches are modifications
of Apple Public Source License code — see Apple's headers in the xnu tree.
