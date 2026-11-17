#!/usr/bin/env python3
"""usage: patch_kernel_bootarg_add.py IN OUT key=value [...]
Append (or replace) boot-args in the kernel image's built-in command line
(field at CMDLINE_OFF, see tools/make_boot_image.py). Userland reads these via
kern.bootargs (e.g. cc_fips_test reads `fips_mode`)."""
import sys, hashlib
CMDLINE_OFF, CMDLINE_LEN = 0x17048, 1024
src, dst, *args = sys.argv[1:]
img = bytearray(open(src, "rb").read())
field = bytes(img[CMDLINE_OFF:CMDLINE_OFF + CMDLINE_LEN])
toks = field[:field.index(b"\0")].decode().split()
for a in args:
    k = a.split("=", 1)[0]
    toks = [t for t in toks if t.split("=", 1)[0] != k] + [a]
new = " ".join(toks).encode()
assert len(new) + 1 <= CMDLINE_LEN
img[CMDLINE_OFF:CMDLINE_OFF + CMDLINE_LEN] = new + b"\0" * (CMDLINE_LEN - len(new))
open(dst, "wb").write(img)
print("cmdline:", new.decode()); print("sha256", hashlib.sha256(img).hexdigest())
