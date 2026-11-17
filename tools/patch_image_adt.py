#!/usr/bin/env python3
"""Add/replace a property in the ADT embedded in a lapis Image (LAPISXNU payload; the ADT is
the last blob of the payload, so it may grow). usage: IN OUT NODEPATH PROP u32:VALUE|str:TEXT|hex:BYTES
e.g. chosen ephemeral-storage u32:1   (xnu kern_mib.c: /chosen/ephemeral-storage -> hw.ephemeral_storage,
read by libdarwin os_variant_uses_ephemeral_storage)."""
import struct, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import adt
inp, out, node, prop, val = sys.argv[1:6]
b = bytearray(open(inp, 'rb').read()); off = b.find(b'LAPISXNU')
virt, entry, adt_off, adt_sz = struct.unpack_from('<4Q', b, off + 16)
assert off + adt_off + adt_sz <= len(b) and len(b) - (off + adt_off + adt_sz) < 0x10, 'ADT is not the trailing blob'
root, _ = adt.parse(bytes(b[off + adt_off:off + adt_off + adt_sz]))
n = root
for part in node.strip('/').split('/'):
    n = next(c for c in n.children if c.name == part)
t, v = val.split(':', 1)
data = struct.pack('<I', int(v, 0)) if t == 'u32' else (v.encode() + b'\0') if t == 'str' else bytes.fromhex(v)
n.props = [p for p in n.props if p[0] != prop] + [(prop, 0, data)]
new = adt.serialize(root)
tail = bytes(b[off + adt_off + adt_sz:])
b[off + adt_off:] = new + tail
struct.pack_into('<Q', b, off + 16 + 24, len(new))
open(out, 'wb').write(b)
print('ADT %#x -> %#x bytes; /%s/%s = %s' % (adt_sz, len(new), node, prop, data.hex()))
