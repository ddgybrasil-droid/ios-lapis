#!/usr/bin/env python3
"""Build the minimal Apple Device Tree that lapis xnu needs to boot early.

Addresses come from hw/lapis-profile.json (see hw/HW.md). Usage:
  mkadt.py OUT.adt [--dump] [--profile PATH]
"""
import hashlib, json, os, struct, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import adt  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_PROFILE = os.path.join(HERE, '../hw/lapis-profile.json')

u32 = lambda *v: struct.pack('<%dI' % len(v), *v)
u64 = lambda *v: struct.pack('<%dQ' % len(v), *v)
cstr = lambda s: s.encode() + b'\0'


def node(name, props=(), children=()):
    n = adt.Node()
    n.props = [('name', 0, cstr(name))] + [(k, 0, v) for k, v in props]
    n.children = list(children)
    return n


def load_profile(path):
    with open(path) as f:
        return json.load(f)


def hx(s):
    return int(s, 16) if isinstance(s, str) else int(s)


def build(profile, seed=None):
    gic = profile['gic']
    fb = profile['framebuffer']
    mem = profile['memory']
    timer = profile['timer']

    phys_base = hx(mem['phys_base'])
    mem_size = hx(mem['mem_size'])
    panic_log = hx(mem.get('panic_log_size', '0x100000'))
    panic_base = phys_base + mem_size - panic_log
    mem_size -= panic_log

    gicd, gicd_sz = hx(gic['gicd_base']), hx(gic['gicd_size'])
    gicr, gicr_sz = hx(gic['gicr_base']), hx(gic['gicr_size'])
    vtimer = int(timer['vtimer_intid'])
    fb_base = hx(fb['base'])
    fb_w, fb_h, fb_stride = int(fb['width']), int(fb['height']), int(fb['stride'])
    fb_size = fb_stride * fb_h

    SOC_BASE = 0x01000000
    TIMEBASE = 19200000  # CXO; confirm from CNTFRQ on device
    LAPIS_ECID = 0x4C41504953000001  # 'LAPIS' + 1
    BOOT_MANIFEST_HASH = hashlib.sha384(b'lapis boot manifest').digest()

    seed = seed if seed is not None else os.urandom(64)
    phandle = iter(range(1, 1000))
    ph = lambda: ('AAPL,phandle', u32(next(phandle)))

    chosen = node('chosen', [
        ph(),
        ('dram-base', u64(phys_base)),
        ('dram-size', u64(mem_size)),
        ('random-seed', seed),
        ('debug-enabled', u32(1)),
        ('embedded-panic-log-size', u32(panic_log)),
        ('firmware-version', cstr('lapis-shim')),
        ('system-firmware-version', cstr('lapis-shim')),
        ('boot-manifest-hash', BOOT_MANIFEST_HASH),
        ('crypto-hash-method', cstr('sha2-384')),
        ('display-scale', u32(2)),
        ('unique-chip-id', struct.pack('<Q', LAPIS_ECID)),
        ('chip-id', u32(0x8650)),
        ('certificate-production-status', u32(0)),
        ('effective-production-status-ap', u32(0)),
        ('certificate-security-mode', u32(1)),
        ('effective-security-mode-sep', u32(1)),
    ], [node('memory-map', [ph()]),
        node('asmb', [ph(), ('lp-sip0', u64(0x6f)), ('lp-sip1', u32(1))])])

    # Eight CPUs: SM8650 is 2+5+1 (A520/A720/X4). Homogeneous cluster for early bring-up.
    cpu_nodes = []
    for core in range(8):
        cpu_nodes.append(node(f'cpu{core}', [
            ph(),
            ('device_type', cstr('cpu')),
            ('compatible', cstr('qcom,oryon') + cstr('ARM,v8')),
            ('reg', u32(core << 8)),
            ('cpu-id', u32(core)),
            ('cluster-id', u32(0)),
            ('cluster-type', u32(0)),
            ('die-cluster-id', u32(0)),
            ('cluster-core-id', u32(core)),
            ('l2-cache-id', u32(core)),
            ('l3-cache-id', u32(0)),
            ('state', cstr('running' if core == 0 else 'stopped')),
            ('timebase-frequency', u32(TIMEBASE)),
            ('clock-frequency', u32(3000000000)),
            ('bus-frequency', u32(100000000)),
            ('memory-frequency', u32(3200000000)),
            ('peripheral-frequency', u32(TIMEBASE)),
            ('fixed-frequency', u32(TIMEBASE)),
        ]))
    cpus = node('cpus', [ph(), ('#address-cells', u32(1)),
                         ('#size-cells', u32(0))], cpu_nodes)

    gic_node = node('gic', [
        ph(),
        ('compatible', cstr('arm,gic-v3')),
        ('interrupt-controller', b''),
        ('virtual-timer-intid', u32(vtimer)),
        ('reg', u64(gicd - SOC_BASE, gicd_sz, gicr - SOC_BASE, gicr_sz)),
        ('redistributor-stride', u64(hx(gic.get('gicr_stride', '0x40000')))),
    ])
    arm_io = node('arm-io', [
        ph(),
        ('device_type', cstr('lapis-io')),
        ('compatible', cstr('lapis-io') + cstr('arm-io')),
        ('#address-cells', u32(2)),
        ('#size-cells', u32(2)),
        ('ranges', u64(0, SOC_BASE, 0x100000000 - SOC_BASE)),
    ], [gic_node,
        node('wdt', [ph(), ('no-pmu', u32(1))]),
        node('disp0', [
            ph(),
            ('device_type', cstr('display-subsystem')),
            ('compatible', cstr('lapis,display')),
            ('canvas-width', u32(fb_w)),
            ('canvas-height', u32(fb_h)),
        ])])

    fbcon = node('lapis-fbcon', [
        ph(),
        ('reg', u64(fb_base, fb_size)),
        ('width', u32(fb_w)),
        ('height', u32(fb_h)),
        ('stride', u32(fb_stride)),
    ])

    defaults = node('defaults', [ph(), ('no-effaceable-storage', u32(1))])
    buttons = node('buttons', [
        ph(),
        ('button-names', cstr('volup') + cstr('voldown') + cstr('hold')),
        ('home-button-type', u32(2)),
        ('opposed-power-vol-buttons', u32(1)),
    ])
    pram = node('pram', [ph(), ('reg', u64(panic_base) + u64(panic_log))])

    return node('device-tree', [
        ph(),
        ('compatible', cstr('lapis') + cstr('lenovo,tb520fu') + cstr('qcom,sm8650') + cstr('AppleARM')),
        ('model', cstr('Lenovo Yoga Pad Pro 12.7')),
        ('target-type', cstr('lapis')),
        ('target-sub-type', cstr('LAPIS')),
        ('manufacturer', cstr('Lenovo')),
        ('#address-cells', u32(2)),
        ('#size-cells', u32(2)),
    ], [chosen, defaults, cpus, arm_io, fbcon, buttons, pram])


if __name__ == '__main__':
    profile_path = DEFAULT_PROFILE
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    if '--profile' in sys.argv:
        profile_path = sys.argv[sys.argv.index('--profile') + 1]
        args = [a for a in args if a != profile_path]
    if not args:
        raise SystemExit('usage: mkadt.py OUT.adt [--dump] [--profile PATH]')
    root = build(load_profile(profile_path))
    blob = adt.serialize(root)
    open(args[0], 'wb').write(blob)
    if '--dump' in sys.argv:
        adt.dump(adt.parse(blob)[0])
    print(f'{args[0]}: {len(blob)} bytes', file=sys.stderr)
