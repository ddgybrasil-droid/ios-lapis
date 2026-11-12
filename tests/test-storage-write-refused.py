#!/usr/bin/env python3
"""Host source guard: media writes and the retired trust bypasses stay absent.

This does not boot a kernel, talk to UFS, or prove a device is read-only.
"""
import re
from pathlib import Path

root = Path(__file__).resolve().parents[1]

def body(text, signature):
    start = text.find(signature)
    assert start >= 0, signature
    brace = text.find("{", start)
    depth = 0
    for i in range(brace, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return text[brace:i + 1]
    raise AssertionError("unclosed " + signature)

ufs = (root / "drivers/ufs/lapis_ufs.c").read_text()
blk = (root / "drivers/blk/lapis_blk.c").read_text()
patches = (root / "tools/xnu_binpatches.py").read_text()
scripts = "\n".join((root / name).read_text() for name in ("build-images.sh", "build-both.sh", "build-drv.sh"))

write_fn = body(ufs, "static int ufs_write_blocks(uint64_t lba, uint32_t n, const void *in)")
assert "return -1" in write_fn
for banned in ("0x2a", "UTRD_DD_WRITE", "utr_issue", "upiu_hdr"):
    assert banned not in write_fn, banned
xfer = body(ufs, "static int ufs_xfer")
assert "if (write) return ufs_write_blocks(lba, n, va);" in xfer
assert "ufs_read_blocks" in xfer
assert 'disk.writable = 0;' in ufs
assert 'drv_bootarg("lapis_ufs_write"' not in ufs

disk_w = body(blk, "static int disk_write")
assert "return -1" in disk_w
assert "d->xfer" not in disk_w
c_w = body(blk, "static int c_write")
assert "return 30" in c_w
assert "physio" not in c_w
strategy = body(blk, "static void blk_strategy")
assert "disk_write" in strategy and "buf_seterror(bp, 30)" in strategy
# The write refusal happens before the buffer is mapped.
assert strategy.find("disk_write") < strategy.find("buf_map")

assert "setRootMedia-noop" not in patches
assert "0xfffffe0007abebd8" not in patches
for forbidden in (
    "unentitled_ios_sim_launch",
    "interrupt-masked-debug-mode=0",
    "sched_preemption_disable_debug_mode=0",
):
    assert forbidden not in scripts, forbidden

print("PASS: UFS/block media writes refused in source; setRootMedia-noop and forbidden boot-args absent (host source guard only)")
