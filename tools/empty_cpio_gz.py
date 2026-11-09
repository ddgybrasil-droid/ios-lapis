#!/usr/bin/env python3
"""Write a gzip-compressed empty newc cpio to stdout (ABL-friendly empty ramdisk)."""
import gzip
import sys

name = b"TRAILER!!!\0"
hdr = (
    b"070701"
    + b"%08X" % 0
    + b"%08X" % 0
    + b"%08X" % 0
    + b"%08X" % 0
    + b"%08X" % 0
    + b"%08X" % 0
    + b"%08X" % 0
    + b"%08X" % 0
    + b"%08X" % 0
    + b"%08X" % len(name)
    + b"%08X" % 0
)
pad = (4 - ((len(hdr) + len(name)) % 4)) % 4
sys.stdout.buffer.write(gzip.compress(hdr + name + b"\0" * pad))
