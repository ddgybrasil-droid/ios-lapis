"""Binary patch entries for the lapis built-in drivers (drivers/, LapisGICv3 + LapisUFS).
Kept separate from xnu_binpatches.py; applied via `repack.py ... --extra tools/drv_binpatches.py`.
Each entry: (name, VA, old words | sha256-of-old-bytes, new words) - same format/guards."""
import os, struct, subprocess, hashlib
_D = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'drivers', 'out')
_ELF, _BIN, _BIN2 = os.path.join(_D, 'drivers.elf'), os.path.join(_D, 'drivers.bin'), os.path.join(_D, 'drivers2.bin')
BLOB_VA, BLOB_MAX = 0xfffffe00074fdb18, 40320
# sha256 of the original 40320 bytes at BLOB_VA (vm_range_lock_test_* bodies) in the s6 CI kernel
BLOB_OLD_SHA = '4f4c8b878142b8e687bcefe133ffed776159dab290ea18a03a6cfe7f3c7fd54b'
BLOB2_VA, BLOB2_MAX = 0xfffffe0007509d90, 60412
BLOB2_OLD_SHA = 'd2b1d53c2f935308358d56e488813e0c22f2d2f0bec2029beac260e155ff83a9'

def _sym(n):
    for l in subprocess.check_output(['aarch64-linux-gnu-nm', _ELF], text=True).splitlines():
        p = l.split()
        if p[-1] == n: return int(p[0], 16)
    raise KeyError(n)

def _b(src, dst, link=False):
    off = dst - src; assert -(1 << 27) <= off < (1 << 27) and off % 4 == 0
    return (0x94000000 if link else 0x14000000) | ((off >> 2) & 0x3ffffff)

_blob = open(_BIN, 'rb').read()
assert len(_blob) <= BLOB_MAX, len(_blob)
_blob += b'\0' * (BLOB_MAX - len(_blob))     # whole region, so the sha guard is size-independent
_blob2 = open(_BIN2, 'rb').read() if os.path.exists(_BIN2) else b''
assert len(_blob2) <= BLOB2_MAX, len(_blob2)
_blob2 += b'\0' * (BLOB2_MAX - len(_blob2))

PATCHES = [
    # sysctl debug.test vm_range_lock_test(): only caller of the region below -> ENOTSUP
    ('drv-vm_range_lock_test-enotsup', 0xfffffe00074f9088, [0xd100c3ff, 0xa9027bfd], [0x528005a0, 0xd65f03c0]),
    ('drv-blob', BLOB_VA, BLOB_OLD_SHA, list(struct.unpack('<%dI' % (len(_blob) // 4), _blob))),
    # vm_range_lock_preflight_test(): only caller of region 2 -> ENOTSUP
    ('drv-vm_range_lock_preflight_test-enotsup', 0xfffffe00074f93a8, [0xd100c3ff, 0xa9027bfd], [0x528005a0, 0xd65f03c0]),
    ('drv-blob2', BLOB2_VA, BLOB2_OLD_SHA, list(struct.unpack('<%dI' % (len(_blob2) // 4), _blob2))),
    # LapisPlatformExpert::start+0x60 `mov x0,x20` -> bl drv_pe_hook (which does mov x0,x20)
    ('drv-lapispe-hook', 0xfffffe0007b6d020, [0xaa1403e0], [_b(0xfffffe0007b6d020, _sym('drv_pe_hook'), True)]),
    # PE_cpu_start_internal → lapis_pe_cpu_start (PSCI CPU_ON)
    ('drv-pe-cpu-start', 0xfffffe0007afed2c, [0xf9400008], [_b(0xfffffe0007afed2c, _sym('lapis_pe_cpu_start'), False)]),
    ('drv-pe-cpu-machine-init', 0xfffffe0007afed5c, [0xd10143ff], [_b(0xfffffe0007afed5c, _sym('lapis_pe_cpu_machine_init'), False)]),
    ('drv-pe-cpu-signal', 0xfffffe0007afed38, [0xf9400008], [_b(0xfffffe0007afed38, _sym('lapis_pe_cpu_signal'), False)]),
]
