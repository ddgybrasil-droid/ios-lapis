#!/usr/bin/env python3
"""Minimal (sp=0 -> kernel allocates the user stack) static arm64 MH_EXECUTE used as /sbin/launchd on the mockfs ramdisk.
__TEXT maps file [0,0x4000) at 0x100000000 (r-x); entry at +0x1000 is `b .`."""
import struct, sys
PAGE=0x4000; BASE=0x100000000; ENTRY_OFF=0x1000
def seg(name, vm, vms, fo, fs, maxp, initp, nsect=0, flags=0):
    return struct.pack('<II16sQQQQiiII', 0x19, 72, name.encode().ljust(16,b'\0'), vm, vms, fo, fs, maxp, initp, nsect, flags)
def sect(name, segn, addr, size, off):
    return struct.pack('<16s16sQQIIIIIIII', name.encode().ljust(16,b'\0'), segn.encode().ljust(16,b'\0'), addr, size, off, 2, 0, 0, 0x80000400, 0,0,0)
code = struct.pack('<I', 0x14000000)  # b .
text = bytearray(seg('__TEXT', BASE, PAGE, 0, PAGE, 5, 5, 1))
struct.pack_into('<I', text, 4, 72+80)
text += sect('__text', '__TEXT', BASE+ENTRY_OFF, len(code), ENTRY_OFF)
# LC_UNIXTHREAD, ARM_THREAD_STATE64 (flavor 6, count 68): x0-x28, fp, lr, sp, pc, cpsr, pad
st = [0]*33; regs = struct.pack('<33QII', *st, 0, 0)
regs = bytearray(regs); struct.pack_into('<Q', regs, 32*8, BASE+ENTRY_OFF)
th = struct.pack('<IIII', 5, 16+len(regs), 6, 68) + regs
cmds = seg('__PAGEZERO', 0, BASE, 0, 0, 0, 0) + bytes(text) + th
hdr = struct.pack('<IiiIIIII', 0xfeedfacf, 0x0100000c, 0, 2, 3, len(cmds), 0x1, 0)
img = bytearray(PAGE); img[:len(hdr)+len(cmds)] = hdr+cmds
img[ENTRY_OFF:ENTRY_OFF+4] = code; img[0x2000:0x2000+13] = b'LAPIS_LAUNCHD'
open(sys.argv[1] if len(sys.argv)>1 else 'launchd.macho','wb').write(img)
