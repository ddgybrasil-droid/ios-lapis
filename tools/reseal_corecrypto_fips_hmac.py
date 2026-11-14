#!/usr/bin/env python3
"""usage: reseal_corecrypto_fips_hmac.py IMG [--write]
Recompute the corecrypto FIPS integrity HMAC of /usr/lib/system/libcorecrypto.dylib
inside the dyld shared cache of a LAPISFS fullroot image, after
tools/patch_fullroot_pac_strip.py rewrote its __TEXT,__text.

Algorithm = corecrypto cc_fips/src/fipspost_get_hmac.c (userland: cc_dylib_in_cache=0):
HMAC-SHA256, key = 32 zero bytes, over `sect->size` bytes at
`(uint8_t*)mach_header + sect->offset` of the first __text section in
__TEXT/__TEXT_EXEC. fipspost_post_integrity compares it with the 32 bytes in
__TEXT,__fips_hmacs (FIPSPOST_HMAC_VALUE). Mismatch -> CCPOST_INTEGRITY_ERROR,
and cc_fips_test (launchd boot task `fips`, RequireSuccess) fails.
Without --write: report stored vs computed. With --write: store computed."""
import hashlib, hmac, os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lapisfs_names import gpt_partitions
img, write = sys.argv[1], "--write" in sys.argv
f = open(img, "r+b" if write else "rb")
root = [p for p in gpt_partitions(f) if p[0] == "lapis-root"][0][1]
f.seek(root); magic, nent, ver, eo, so, total = struct.unpack("<8sIIQQQ", f.read(0x28))
f.seek(root); meta = f.read(so + 0x200000)
E = [struct.unpack_from("<IHHQQII", meta, eo + 32 * i) for i in range(nent)]
nm = lambda i: meta[so + E[i][5]: so + E[i][5] + E[i][6]].decode()
def path(i):
    p = []
    while i:
        p.append(nm(i)); i = E[i][0]
    return "/" + "/".join(reversed(p))
caches = {}
for i, e in enumerate(E):
    if e[1] == 2 and nm(i).startswith("dyld_shared_cache_arm64e") and "/System/Library/Caches/com.apple.dyld/" in path(i) \
            and not nm(i).endswith((".map", ".atlas")):
        caches.setdefault(nm(i), (root + e[3], e[4]))   # first (canonical) entry
def rd(c, off, n): f.seek(caches[c][0] + off); return f.read(n)
def mappings(c):
    mo, mc = struct.unpack("<II", rd(c, 0x10, 8))
    return [struct.unpack_from("<QQQII", rd(c, mo + 32 * k, 32)) for k in range(mc)]
NAME = b"/usr/lib/system/libcorecrypto.dylib\0"
hit = None
for c, (base, size) in sorted(caches.items()):
    for (addr, msz, foff, maxp, initp) in mappings(c):
        if not (initp & 4):   # only executable (TEXT) mappings hold mach headers
            continue
        CH = 64 << 20
        for o in range(0, msz, CH):
            buf = rd(c, foff + o, min(CH + 0x4000, msz - o))
            j = buf.find(NAME)
            while j != -1:
                # LC_ID_DYLIB (0xd): cmd, cmdsize, name_off(0x18) ... name at +0x18
                lc = j - 0x18
                if lc >= 0 and struct.unpack_from("<I", buf, lc)[0] == 0xD:
                    h = lc & ~0xFFF
                    while h >= 0 and struct.unpack_from("<I", buf, h)[0] != 0xFEEDFACF: h -= 0x1000
                    if h >= 0: hit = (c, foff + o + h, addr + o + h); break
                j = buf.find(NAME, j + 1)
            if hit: break
        if hit: break
    if hit: break
assert hit, "libcorecrypto mach header not found"
c, hfoff, hva = hit
hdr = rd(c, hfoff, 0x8000)
ncmds = struct.unpack_from("<I", hdr, 16)[0]; p = 32
text = fips = None
for _ in range(ncmds):
    cmd, csz = struct.unpack_from("<II", hdr, p)
    if cmd == 0x19:
        seg = hdr[p + 8:p + 24].split(b"\0")[0]; nsect = struct.unpack_from("<I", hdr, p + 64)[0]
        for k in range(nsect):
            s = p + 72 + 80 * k
            sect, sseg = hdr[s:s + 16].split(b"\0")[0], hdr[s + 16:s + 32].split(b"\0")[0]
            addr, size, off = struct.unpack_from("<QQI", hdr, s + 32)
            if sect == b"__text" and sseg in (b"__TEXT", b"__TEXT_EXEC") and text is None: text = (addr, size, off)
            if sect == b"__fips_hmacs": fips = (addr, size, off)
    p += csz
assert text and fips, (text, fips)
def va2file(va, n):
    for cc in caches:
        for (addr, msz, foff, maxp, initp) in mappings(cc):
            if addr <= va and va + n <= addr + msz: return cc, foff + va - addr
    raise SystemExit("VA %x not mapped" % va)
# userland path: data = header + sect->offset
tva = hva + text[2]
tc, tfo = va2file(tva, text[1])
data = rd(tc, tfo, text[1])
mac = hmac.new(b"\0" * 32, data, hashlib.sha256).digest()
fc, ffo = va2file(fips[0], 32)
stored = rd(fc, ffo, 32)
print("cache", c, "header va %#x" % hva, "__text addr %#x size %#x offset %#x (hashed va %#x in %s)" % (text[0], text[1], text[2], tva, tc))
print("__fips_hmacs va %#x in %s @%#x" % (fips[0], fc, ffo))
print("stored  ", stored.hex()); print("computed", mac.hex()); print("match", stored == mac)
if write and stored != mac:
    f.seek(caches[fc][0] + ffo); f.write(mac); print("written")
