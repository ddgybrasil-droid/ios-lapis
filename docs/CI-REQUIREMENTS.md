# CI requirements

## Linux — shim (`shim.yml`)

- `gcc-aarch64-linux-gnu`, `binutils-aarch64-linux-gnu`
- Python 3 (ADT / payload / boot.img v4 packer)
- No proprietary blobs

## macOS — XNU (`xnu.yml`)

Uses [blacktop/darwin-xnu-build](https://github.com/blacktop/darwin-xnu-build) with:

| Variable | Value |
|----------|-------|
| `MACOS_VERSION` | `26.5` (maps to xnu-12377.x via `distribution-macOS` `macos-265`) |
| `MACHINE_CONFIG` | `LAPIS` |
| `KERNEL_CONFIG` | `DEVELOPMENT` (default) |

### Fetched automatically (when the runner network + Dortania manifest work)

- XNU sources for the macOS 26.5 release tag
- Kernel Debug Kit **26.5 build 25F71** (via Dortania `KdkSupportPkg` manifest)
- Homebrew deps used by `build.sh` (`jq`, `gum`, `cmake`, `ninja`, …)

### Often blocked / manual

- **Apple Developer authentication** if the KDK DMG URL requires a session —
  runners usually pull the Dortania mirror; if that 404s, install the KDK on the
  runner image yourself under `/Library/Developer/KDKs/`.
- **Xcode version**: xnu-12377 needs a new enough Xcode on the selected
  `macos-15` (or newer) image. Pin `DEVELOPER_DIR` if multiple Xcodes exist.
- **Closed `libLAPIS.os.*.a`**: does not exist; patches skip `force_load` and
  enable `nos_arm_asm` / `nos_arm_pmap`. Further missing Apple-only objects will
  show up as link errors — see workflow logs / `docs/STATUS.md`.
- **SPTM / exclaves / firehose**: blacktop's own `patches/26.0` still apply;
  LAPIS patches are layered on top.

### Local one-liner (Mac with Xcode + KDK)

```sh
export MACOS_VERSION=26.5 MACHINE_CONFIG=LAPIS KERNEL_CONFIG=DEVELOPMENT
# clone darwin-xnu-build, copy xnu/patches/*.patch into patches/26.5/, run build.sh
```
