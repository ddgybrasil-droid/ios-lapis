#!/usr/bin/env python3
"""lapis_amfi (xnu/lapis-ramfs/lapis_amfi.c, linked into the ramfs blob): apply AFTER
patch_kernel_ramfs.py.  usage: patch_kernel_amfi.py IN OUT
1. kernel_bootstrap_thread: `bl load_static_trust_cache` -> `bl lapis_amfi_boot` (calls it, then
   amfi_interface_register(lapis_amfi_if) - still before machine_lockdown).
2. IOKit entitlement entry points: the prebuilt kernel has the amfi->OSEntitlements.* call in
   each one replaced by `nop` (was `ldr x8,[x8,#off]`) + `mov w0,#1` (was `blr x8`), i.e. every
   query answered "not entitled". Restore the original call sequence (same shape as the intact
   call in IOUserClient::copyClientEntitlement: ldr x8,[amfi]; ldr x8,[x8,#0x1e0]; ...; blr x8)."""
import struct, subprocess, sys, os
d = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'xnu', 'lapis-ramfs')
img = bytearray(open(sys.argv[1], 'rb').read())
off = lambda va: va + 0x175B0 - 0xFFFFFE0007004000
sym = lambda n: int([l.split()[0] for l in subprocess.check_output(['aarch64-linux-gnu-nm', os.path.join(d, 'ramfs.elf')], text=True).splitlines() if l.endswith(' ' + n)][0], 16)
blob = open(os.path.join(d, 'ramfs.bin'), 'rb').read()
BLOB = 0xfffffe000743e100
assert img[off(BLOB):off(BLOB) + len(blob)] == blob, 'apply patch_kernel_ramfs.py with the same build first'
def bl(src, dst): return 0x94000000 | (((dst - src) // 4) & 0x3ffffff)
def rd(va): return struct.unpack_from('<I', img, off(va))[0]
def wr(va, w): struct.pack_into('<I', img, off(va), w)
SITE, LSTC = 0xfffffe0007ba6e10, 0xfffffe000791a1c4
boot = sym('lapis_amfi_boot')
assert rd(SITE) in (bl(SITE, LSTC), bl(SITE, boot)), hex(rd(SITE))
wr(SITE, bl(SITE, boot))
NOP, MOV1, BLR8 = 0xd503201f, 0x52800020, 0xd63f0100
LDR = lambda o: 0xf9400108 | ((o // 8) << 10)
QB, QS, CP = 0x1c0, 0x1d0, 0x1e0   # queryEntitlementBooleanWithProc, ...StringWithProc, copyEntitlementAsOSObjectWithProc
SITES = [('IOCurrentTaskHasStringEntitlement', 0xfffffe0007b49878, 0xfffffe0007b49884, QS),
         ('IOTaskHasStringEntitlement', 0xfffffe0007b498f8, 0xfffffe0007b49904, QS),
         ('IOCurrentTaskHasEntitlement', 0xfffffe0007b49968, 0xfffffe0007b49970, QB),
         ('IOTaskHasEntitlement', 0xfffffe0007b499d0, 0xfffffe0007b499d8, QB),
         ('IOTaskGetIntegerEntitlement', 0xfffffe0007b49a70, 0xfffffe0007b49a7c, CP),
         ('IOTaskGetEntitlement', 0xfffffe0007b49b80, 0xfffffe0007b49b8c, CP),
         ('IOTaskHasEntitlementAsBooleanOrObject#1', 0xfffffe0007b49cc8, 0xfffffe0007b49cd4, QB),
         ('IOTaskHasEntitlementAsBooleanOrObject#2', 0xfffffe0007b49ce4, 0xfffffe0007b49cf4, CP)]
for name, a, b, o in SITES:
    assert (rd(a), rd(b)) in ((NOP, MOV1), (LDR(o), BLR8)), (name, hex(rd(a)), hex(rd(b)))
    wr(a, LDR(o)); wr(b, BLR8)
    print('restored amfi call in %s: %#x ldr x8,[x8,#%#x]; %#x blr x8' % (name, a, o, b))
open(sys.argv[2], 'wb').write(img)
print('kernel_bootstrap_thread %#x -> bl lapis_amfi_boot %#x; lapis_amfi_if %#x' % (SITE, boot, sym('lapis_amfi_if')))
