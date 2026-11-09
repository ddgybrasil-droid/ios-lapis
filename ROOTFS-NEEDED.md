# Launchd SIGSEGV — status (corrected)

## What was wrong with the earlier diagnosis

KGDB: `ESR 0x82000007` Instr Abort / translation L3 at `FAR ≈ slide+0x30f0` is **dyld's entry**, not a missing dyld shared cache access (`0x180000000`).

The user's full rootfs (4.9 GB, 283 entries, complete cache) still faults the same way. Cache slices are **not** the first blocker.

## VERIFIED-emu

| Case | Result |
|------|--------|
| Static arm64 `/sbin/launchd` (`placeholder.lfs`) from md0 lapisfs | Runs in EL0 (`PC=0x100001000`, `PSTATE=EL0t`, `b .` loop) |
| Real arm64e `launchd` + `/usr/lib/dyld` (`hw-min` or full root) md0 or UFS | Spawn OK → SIGSEGV at dyld entry (`slide+0x30f0`) |

So lapisfs **can** map executable pages for a plain arm64 main binary. The failure is specific to the **arm64e MH_DYLINKER (dyld) load/fault path**.

## What we tried (compiled + emu-tested)

- Prefer `buf_strategy(md0)` for strategy when the ramdisk holds the whole image (s10 style)
- `ubc_msync(..., UBC_INVALIDATE)` on `VNOP_MMAP` so dyld page 0 is demand-paged (we then see `pagein idx=5 foff=0`)
- Direct UPL fill + commit — hung when using kernel-only `UPL_COMMIT_CS_VALIDATED`
- Stage small blk roots (≤48 MiB) fully into RAM for md0-parity I/O
- CS-disable boot-args — no change

Page 0 of dyld is filled during load after invalidate, but EL0 still takes L3 translation at the entry VA (no further `pagein foff=0` on the user fault). Soft-fault of that resident/entry page into the user pmap is still broken for arm64e dyld.

## Cache slices (only if/when past dyld entry)

If a later failure is actually cache-related, the full image listing is in the attached dyld-cache directory. Slices are 50–235 MB each; split into ≤24 MB parts as needed. **Do not prioritize shipping cache until arm64e dyld executes its first instruction.**

## SMP note

`random_cpu_init` panics when `prng_ready==0` (corecrypto PRNG never installs without the kext). Binpatch: `random_cpu_init` → `ret`. `read_random` already has a software fallback.
