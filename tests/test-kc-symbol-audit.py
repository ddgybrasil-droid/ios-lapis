#!/usr/bin/env python3
"""Synthetic parser tests only; no Apple kext or kernel execution."""
import importlib.util,struct,sys
from pathlib import Path
root=Path(__file__).resolve().parents[1]; sys.path.insert(0,str(root/'tools'))
s=importlib.util.spec_from_file_location('kc_symbol_audit',root/'tools/kc_symbol_audit.py');a=importlib.util.module_from_spec(s);s.loader.exec_module(a)

def macho():
    names=b'\0_undefined\0_defined\0_private\0_local\0_debug\0_weak\0'
    def ix(x):return names.index(x.encode())
    header=struct.pack('<8I',0xfeedfacf,0x100000c,2,0xb,1,24,0,0)
    symoff=56; entries=[
      (ix('_undefined'),0x01,0,0,0),
      (ix('_defined'),0x0f,1,0,0x1000),
      (ix('_private'),0x1f,1,0,0x2000),
      (ix('_local'),0x0e,1,0,0x3000),
      (ix('_debug'),0xe1,0,0,0),
      (ix('_weak'),0x01,0,0x40,0),
    ]
    strings=symoff+len(entries)*16
    return bytearray(header+struct.pack('<6I',2,24,symoff,len(entries),strings,len(names))+b''.join(struct.pack('<IBBHQ',*e) for e in entries)+names)
d=macho(); imports,exports=a.symbols(d,0)
assert imports=={'_undefined','_weak'},imports
assert exports=={'_defined'},exports
for mutate in ('bad_string','truncated','bad_command'):
    bad=macho()
    if mutate=='bad_string': struct.pack_into('<I',bad,56,0xffffffff)
    elif mutate=='truncated': del bad[-1:]
    else: struct.pack_into('<I',bad,36,4)
    try:a.symbols(bad,0)
    except ValueError:pass
    else:raise AssertionError('accepted '+mutate)
print('PASS: imports, external definitions, weak refs, private/local/debug exclusions, malformed bounds (synthetic only)')

import plistlib
INV = importlib.util.spec_from_file_location('kc_inventory', root/'tools/kc_inventory.py')
inv = importlib.util.module_from_spec(INV); INV.loader.exec_module(inv)

def pad(s, n=16):
    b = s.encode(); return b + bytes(n - len(b))

def kext(file_off, specs):
    blobs = [b'\0']
    entries = []
    for name, typ, desc in specs:
        entries.append((len(b''.join(blobs)), typ, 1 if typ & 0x0e else 0, desc, 0x1000))
        blobs.append(name.encode() + b'\0')
    strings = b''.join(blobs)
    symoff = file_off + 56
    stroff = symoff + len(entries) * 16
    body = struct.pack('<8I', 0xfeedfacf, 0x100000c, 2, 0xb, 1, 24, 0, 0)
    body += struct.pack('<6I', 2, 24, symoff, len(entries), stroff, len(strings))
    body += b''.join(struct.pack('<IBBHQ', *e) for e in entries) + strings
    assert len(body) <= 0x800
    return body

def fileset():
    base = 0xfffffe0007000000
    roots = inv.ROOTS
    kernel = 'com.apple.kernel'
    md = []
    for n in roots:
        deps = {}
        if n != roots[-1]:
            deps[roots[-1]] = '1.0'
        if n == roots[0]:
            deps[kernel] = '1.0'
        md.append({'CFBundleIdentifier': n, 'OSBundleLibraries': deps})
    plist = plistlib.dumps({'_PrelinkInfoDictionary': md})
    text_vm, text_off, text_size = base + 0x4000, 0x4000, 0x2000
    info_vm, info_off = base + 0x8000, 0x8000
    seg1 = struct.pack('<II16s4Q4I', 0x19, 72, pad('__PRELINK_TEXT'), text_vm, text_size, text_off, text_size, 7, 5, 0, 0)
    sec = struct.pack('<16s16sQQIIIIIIII', pad('__info'), pad('__PRELINK_INFO'), info_vm, len(plist), info_off, 0, 0, 0, 0, 0, 0, 0)
    seg2 = struct.pack('<II16s4Q4I', 0x19, 152, pad('__PRELINK_INFO'), info_vm, len(plist), info_off, len(plist), 3, 1, 1, 0) + sec
    cmds = seg1 + seg2
    offs = {}
    for i, n in enumerate(roots):
        label = n.encode() + b'\0'
        size = (32 + len(label) + 7) & ~7
        fo = text_off + i * 0x800
        offs[n] = fo
        cmds += struct.pack('<IIQQII', 0x80000035, size, text_vm + i * 0x800, fo, 32, 0) + label + bytes(size - 32 - len(label))
    blob = bytearray(info_off + len(plist))
    struct.pack_into('<8I', blob, 0, 0xfeedfacf, 0x100000c, 2, 0xc, 2 + len(roots), len(cmds), 0, 0)
    blob[32:32 + len(cmds)] = cmds
    image4 = kext(offs[roots[0]], [
        ('_need_kernel', 0x01, 0),
        ('_from_dep', 0x01, 0),
        ('_weak_missing', 0x01, 0x40),
        ('_alias', 0x01, 0),
        ('_typed_name', 0x01, 0),
        ('_image4_export', 0x0f, 0),
        ('_image4_private', 0x1f, 0),
        ('_image4_local', 0x0e, 0),
        ('_image4_stab', 0xe1, 0),
    ])
    amfi = kext(offs[roots[1]], [
        ('_from_dep', 0x01, 0),
        ('_only_amfi', 0x01, 0),
        ('_hidden_dep', 0x01, 0),
        ('_amfi_export', 0x0f, 0),
    ])
    core = kext(offs[roots[-1]], [
        ('_from_dep', 0x0f, 0),
        ('_hidden_dep', 0x1f, 0),
        ('_alias', 0x0b, 0),
    ])
    pthread = struct.pack('<8I', 0xfeedfacf, 0x100000c, 2, 0xb, 0, 0, 0, 0)
    blob[offs[roots[0]]:offs[roots[0]] + len(image4)] = image4
    blob[offs[roots[1]]:offs[roots[1]] + len(amfi)] = amfi
    blob[offs[roots[2]]:offs[roots[2]] + len(pthread)] = pthread
    blob[offs[roots[-1]]:offs[roots[-1]] + len(core)] = core
    blob[info_off:] = plist
    note = b'Darwin Kernel Version 23.0.0: lapis synthetic\n'
    blob[0x6000:0x6000 + len(note)] = note
    return blob

fs = fileset()
report = a.audit(fs, {'_need_kernel'})
assert report['kernel_version_string'] == 'Darwin Kernel Version 23.0.0: lapis synthetic', report['kernel_version_string']
img = report['components']['com.apple.security.AppleImage4']
amfi_r = report['components']['com.apple.driver.AppleMobileFileIntegrity']
assert img['undefined_unique'] == 5, img
assert img['in_target_symbol_names'] == 1
assert img['in_dependency_external_definitions'] == 1
assert img['in_either'] == 2
assert img['not_found_names'] == ['_alias', '_typed_name', '_weak_missing'], img['not_found_names']
assert img['dependency_providers_for_imports'] == {'_from_dep': ['com.apple.kec.corecrypto']}
assert img['dependencies_scanned'] == ['com.apple.kec.corecrypto']
assert img['missing_metadata'] == ['com.apple.kernel']
assert amfi_r['not_found_names'] == ['_hidden_dep', '_only_amfi'], amfi_r
assert amfi_r['in_dependency_external_definitions'] == 1
assert amfi_r['missing_metadata'] == []
# A fileset member not at offset 0 still uses container file offsets.
moved = bytearray(b'\0' * 16) + fs
try:
    a.audit(moved, set())
except ValueError:
    pass
else:
    raise AssertionError('shifted container accepted')
assert a.target_names('fffffe0007001000 _need_kernel\n\n# comment\nfffffe0007002000 T _typed_name\nbare\n') == {'_need_kernel', '_typed_name'}
print('PASS: fileset name audit, absolute symtab offsets, nm/ksyms target lines, missing metadata (synthetic only)')
