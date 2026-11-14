#!/usr/bin/env python3
"""usage: patch_kernel_rootdev_bootarg.py IN OUT
Rewrite the boot command line token `rd=md0` as `rootdev=md0`.
IOFindBSDRoot accepts `rootdev` exactly like `rd` (xnu IOKitBSDInit.cpp:730,
1032), so the kernel's root-device handling is unchanged. But launchd treats
strstr(kern.bootargs, "rd=md0") as a restore ramdisk boot (launchd
0x10004ec48) and then runs PerformInRestore boot tasks (HorizonSetup,
RequireSuccess). bsd_rooted_ramdisk() (only parses `rd`) now correctly says
the root is not a ramdisk: the root is lapisfs on UFS disk0s2."""
import sys, hashlib
CMDLINE_OFF, CMDLINE_LEN = 0x17048, 1024   # tools/make_boot_image.py
src, dst = sys.argv[1:3]
img = bytearray(open(src, "rb").read())
field = bytes(img[CMDLINE_OFF:CMDLINE_OFF + CMDLINE_LEN])
cur = field[:field.index(b"\0")].decode()
toks = cur.split()
assert "rd=md0" in toks, cur
new = " ".join("rootdev=md0" if t == "rd=md0" else t for t in toks).encode()
assert len(new) + 1 <= CMDLINE_LEN
img[CMDLINE_OFF:CMDLINE_OFF + CMDLINE_LEN] = new + b"\0" * (CMDLINE_LEN - len(new))
open(dst, "wb").write(img)
print("cmdline:", new.decode()); print("sha256", hashlib.sha256(img).hexdigest())
