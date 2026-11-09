# Rootfs files needed to push launchd past SIGSEGV

## Fault diagnosis (VERIFIED-emu)

KGDB break on `_handle_user_abort` after spawn:

| Field | Value |
|---|---|
| ESR_EL1 | `0x82000007` — **Instruction Abort** from EL0, IFSC=`0x07` (Translation fault, level 3) |
| ELR_EL1 / FAR_EL1 | `0x10……a40` (ASLR; e.g. `0x102477a40`) — unmapped page |
| SPSR | EL0 |
| Exit | `namespace 2 subcode 0xb` (OS_REASON_SIGNAL / SIGSEGV) |

Mini UFS root (`hw-min-23F77.lfs`) only has `/sbin/launchd` + `/usr/lib/dyld` and an **empty** `/System/Library/Caches/com.apple.dyld/`. Launchd is **arm64e**, `LC_LOAD_DYLIB` for:

- `/usr/lib/libSystem.B.dylib`
- `/usr/lib/libobjc.A.dylib`
- `/usr/lib/libsandbox.1.dylib`
- `/usr/lib/swift/libswiftCore.dylib`
- `/usr/lib/swift/libswiftDarwin.dylib`
- `/usr/lib/swift/libswiftDispatch.dylib`
- `/usr/lib/swift/libswiftObjectiveC.dylib`
- `/usr/lib/swift/libswift_Builtin_float.dylib`
- `/usr/lib/swift/libswift_DarwinFoundation1.dylib`

On iPadOS these live in the **dyld shared cache**, not as on-disk dylibs. dyld jumps into cache VAs → translation fault.

Not a TPRO/PAC decode issue (that would be a different ESR EC). Not a missing syscall yet — we never reach launchd main.

## What to send (25 MiB / file limit)

**Best:** host or attach `rootfs-23F77.lfs` (~4.84 GiB) somehow (not via 25 MiB chat chunks).

**Otherwise, phase 1 (small, so we can map which slices hold libSystem/swift):**

| Path | Size |
|---|---|
| `/System/Library/Caches/com.apple.dyld/dyld_shared_cache_arm64e` | 622592 |
| `/System/Library/Caches/com.apple.dyld/dyld_shared_cache_arm64e.atlas` | 2754649 |
| `/usr/lib/dyld` | 1333168 (already have) |
| `/sbin/launchd` | 620432 (already have) |

**Phase 2:** after atlas parse, we will name the exact `.NN` / `.dylddata` / `.dyldlinkedit` slices (many are 50–235 MiB → split into ≤25 MiB parts named `FILE.partNN`). Full unique cache list is in `rd/rootfs-23F77.UNIQUE-FILES.txt` (~4.84 GiB total).

Also useful later: `/System/Library/xpc/launchd.plist`, jetsam plists under `/System/Library/JetsamProperties/`.
