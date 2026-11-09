#!/usr/bin/env python3
"""Add the nodes/properties the lapis built-in drivers need to an ADT made by mkadt.py.
Kept separate from mkadt.py (owned by the kernel/ramdisk work).

usage: drv_adt.py IN.adt OUT.adt --target emu|hw

All targets: /arm-io/gic '#interrupt-cells' = 2  (specifier <INTID flags>, flags = Linux
GIC binding trigger bits: 1 edge-rising, 4 level-high; LapisGICv3 convention).
emu (QEMU virt, values from QEMU 10.0 `-M virt,dumpdtb` of the same command line):
  /lapis-pl031        pl031@9010000, interrupts <0 2 4> (SPI 2)  -> INTID 34 level
  /lapis-gic-selftest unused SPI 8 (virt secure-UART slot, absent with secure=off) -> INTID 40 edge
  /ufs                QEMU `-device ufs` on the virt PCIe host: ECAM 0x40_1000_0000 (256 MiB),
                      MMIO32 window 0x1000_0000+0x2eff_0000, INTx: interrupt-map (slot+pin)%4 -> SPI 3..6
hw (TB520FU live FDT /soc/ufshc@1d84000, see hw/TB520FU-hw-map.md):
  /ufs                reg 0x01D84000+0x3000, interrupts SPI 265 level -> INTID 297
"""
import struct, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import adt

u32 = lambda *v: struct.pack('<%dI' % len(v), *v)
u64 = lambda *v: struct.pack('<%dQ' % len(v), *v)
cstr = lambda s: s.encode() + b'\0'

def find(node, name):
    return next((c for c in node.children if c.name == name), None)

def phandles(node, acc):
    p = node.prop('AAPL,phandle')
    if p: acc.append(struct.unpack('<I', p[:4])[0])
    for c in node.children: phandles(c, acc)
    return acc

def setprop(node, name, val):
    node.props = [p for p in node.props if p[0] != name] + [(name, 0, val)]

def main():
    src, dst = sys.argv[1], sys.argv[2]
    target = sys.argv[sys.argv.index('--target') + 1]
    root, _ = adt.parse(open(src, 'rb').read())
    gic = find(find(root, 'arm-io'), 'gic')
    gic_ph = struct.unpack('<I', gic.prop('AAPL,phandle')[:4])[0]
    setprop(gic, '#interrupt-cells', u32(2))
    nxt = iter(range(max(phandles(root, [])) + 1, 1 << 20))
    def mk(name, props):
        n = adt.Node(); n.props = [('name', 0, cstr(name)), ('AAPL,phandle', 0, u32(next(nxt)))] + \
            [(k, 0, v) for k, v in props]
        root.children = [c for c in root.children if c.name != name] + [n]
    irq = lambda intid, flags: [('interrupt-parent', u32(gic_ph)), ('interrupts', u32(intid, flags))]
    if target == 'emu':
        mk('lapis-pl031', [('compatible', cstr('arm,pl031')), ('reg', u64(0x09010000, 0x1000))] + irq(32 + 2, 4))
        mk('lapis-gic-selftest', [('compatible', cstr('lapis,gic-selftest'))] + irq(32 + 8, 1))
        mk('ufs', [('compatible', cstr('lapis,ufshci-qemu-pci')),
                   ('pci-ecam', u64(0x4010000000, 0x10000000)),
                   ('pci-mmio32', u64(0x10000000, 0x2eff0000)),
                   ('pci-intx-spi-base', u32(3)),
                   ('lapis,mode', cstr('full'))])
    elif target == 'hw':
        mk('ufs', [('compatible', cstr('qcom,ufshc') + cstr('jedec,ufs-2.0')),
                   ('reg', u64(0x01D84000, 0x3000))] + irq(32 + 265, 4) +
                  [('lapis,mode', cstr('probe'))])
    else:
        raise SystemExit('bad --target')
    open(dst, 'wb').write(adt.serialize(root))
    print(f'{dst}: drivers ADT ({target}) gic phandle {gic_ph}')

main()
