# Deliverable artifacts

Cloud agent artifacts directory: `/opt/cursor/artifacts/`

| File | sha256 | size |
|---|---|---|
| `lapis-xnu-hw-safe.img` | `050768555f0be27424c6e93e09930fa6b16a0788fabeaf2aca86e10a8c88c3a3` | 21 MiB |
| `lapis-xnu-hw-full.img` | `1c5f29132b737245b8eeae239308fc7dfc2e5fafc80a636c1da36b85e7eeef08` | 21 MiB |
| `lapis-xnu-emu.img` | `578665d876cf934a160162665e332ac586b1775e169e89fda86a153fbf54760c` | 19 MiB |
| `ufs-launchd-v4-msgbuf.txt` | `fb79e902dc904aff4795dc348efa3fd825e44a14594bff217f2a6c794db50095` | evidence |
| `smp-secondary-hang-msgbuf.txt` | `087dd00f82e491be7f0324ccd58bd18ba1684b0a84e79e8574ab842e38614892` | evidence |
| `md0-launchd-msgbuf.txt` | `e0ac49fac1c1444916dfbaa572b647861e555ef88a5f2b95158cde2faab03f23` | evidence |

Local rebuild outputs (gitignored): `out/lapis-xnu-hw-{safe,full}.img`, `out-emu/lapis-xnu-emu.img`.

Images are too large for a normal git commit; fetch from the cloud agent artifacts / PR attachments.
