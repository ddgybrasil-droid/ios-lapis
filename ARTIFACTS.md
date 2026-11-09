# Deliverable artifacts

Cloud agent artifacts directory: `/opt/cursor/artifacts/`

| File | sha256 | size |
|---|---|---|
| `lapis-xnu-hw-safe.img` | `6ea8b1c7353c7c55991b9eee6c72ce7999436f279b8ca52f2ad189172c78cf43` | 21 MiB |
| `lapis-xnu-hw-full.img` | `dcbeb60f7f26af5e7aae2176c3381cf52c16b38522a765298356437901cee6ce` | 21 MiB |
| `lapis-xnu-emu.img` | `d7b6528be8609a45159144ebb19674e409afd400aa4844e9b5cd42abe286e373` | 19 MiB |
| `ufs-launchd-v4-msgbuf.txt` | `fb79e902dc904aff4795dc348efa3fd825e44a14594bff217f2a6c794db50095` | evidence |
| `smp-secondary-hang-msgbuf.txt` | `087dd00f82e491be7f0324ccd58bd18ba1684b0a84e79e8574ab842e38614892` | evidence |
| `md0-launchd-msgbuf.txt` | `e0ac49fac1c1444916dfbaa572b647861e555ef88a5f2b95158cde2faab03f23` | evidence |

Local rebuild outputs (gitignored): `out/lapis-xnu-hw-{safe,full}.img`, `out-emu/lapis-xnu-emu.img`.

Images are too large for a normal git commit; fetch from the cloud agent artifacts / PR attachments.
