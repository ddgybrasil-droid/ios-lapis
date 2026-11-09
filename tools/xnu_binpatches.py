"""Binary patches applied to the CI-built kernel.development.lapis (as embedded
in the s6 payload) until the matching source edits in xnu/patches are rebuilt
on the macOS CI runner. Each entry: (name, VA, [old words], [new words])."""

PATCHES = [
    # 0012 start.s: map a 2nd 32 MiB L2 block (0xD6000000) so the whole FB
    # 0xD5100000..0xD65AA000 is reachable (was rows 0..1335 only).
    ('fb-l2-second-block', 0xfffffe000756f2ec,
     [0xf2e00000, 0xf2c00000, 0xf2baa360, 0xf2900000, 0xf2e00001, 0xf2c00001, 0xf2bff001, 0xf29fe001],
     [0xd2baa360, 0xf2900000, 0xb267016b, 0x910021ad, 0xf82c79ab, 0xd2bff001, 0xf29fe001, 0xd503201f]),
    # s6 arm_init-entry yellow beacon trampoline clobbered x0 (boot_args):
    # use x6 as the row pointer instead.
    ('arm_init-beacon-keep-x0-a', 0xfffffe000756d114,
     [0xf2e00000, 0xf2c00000, 0xf2baa8c0, 0xf2940000], [0xf2e00006, 0xf2c00006, 0xf2baa8c6, 0xf2940006]),
    ('arm_init-beacon-keep-x0-b', 0xfffffe000756d13c, [0xaa0003e5], [0xaa0603e5]),
    ('arm_init-beacon-keep-x0-c', 0xfffffe000756d15c, [0x8b040000], [0x8b0400c6]),
    # 0012 arm_vm_init: lapis_early_disable() ran after set_mmu_ttb_alternate()
    # switched to kernel tables that do not map the FB, so its mark(8) paint
    # faulted (L1 translation, FAR 0xd65xxxxx). Just set early_disabled.
    # ...and install XNU's real ExceptionVectorsBase (VA 0xfffffe000756a000)
    # here: the s6 LowException table sits at a *physical* VBAR that is
    # unmapped once the bootstrap TTBR0 is gone, so later faults just looped
    # silently. With the real vectors a fault becomes an XNU panic that the
    # video console prints.  (start.s msr vbar stays NOP'd for early debug.)
    ('early-disable-no-paint+kernel-vbar', 0xfffffe0007568188,
     [0xb0ffd761, 0x912f0c21, 0x52800100, 0x97fffefb],
     [0xd0000000, 0x91000000, 0xd518c000, 0xd5033fdf]),
]

# s7: replace the s6 bit-bar regdump (bars did not survive phone photos) with a
# hex-TEXT reporter, assembled from xnu/lapis-exc/exc_text.S by build.sh.
# Region 0x756e788..0x756f000 (all vector-slot stubs branch to 0x756e788).
import os as _os, struct as _st
_EXC_VA, _EXC_LEN = 0xfffffe000756e788, 0x878
_EXC_OLD_SHA = '65f4c6ecfa4a0353f40193b24e5657a392635701ae1fd30e8965abaa64383985'
_bin = _os.path.join(_os.path.dirname(_os.path.abspath(__file__)), '..', 'xnu', 'lapis-exc', 'exc_text.bin')
_blob = open(_bin, 'rb').read()
assert len(_blob) <= _EXC_LEN and len(_blob) % 4 == 0, len(_blob)
_blob += b'\x1f\x20\x03\xd5' * ((_EXC_LEN - len(_blob)) // 4)
PATCHES.append(('exc-hex-text-reporter', _EXC_VA, _EXC_OLD_SHA,
                list(_st.unpack('<%dI' % (_EXC_LEN // 4), _blob))))
# s7: read_random() -> prng_fallback trampoline (inside the blob above, VA
# 0xfffffe000756efc0) while prng_ready == 0.  Source: patch 0014.
import subprocess as _sp
_pf = int([l.split()[0] for l in _sp.check_output(['aarch64-linux-gnu-nm', _bin[:-4] + '.elf'], text=True).splitlines()
           if l.endswith(' prng_fallback')][0], 16)
assert _EXC_VA <= _pf < _EXC_VA + _EXC_LEN
PATCHES.append(('read_random-erandom-fallback', 0xfffffe0007548bb4, [0xa9bb67fa],
                [0x14000000 | (((_pf - 0xfffffe0007548bb4) >> 2) & 0x3ffffff)]))
# s7: crypto_random_{generate,uniform,kmem_ctx_size,kmem_init} wrappers ->
# corecrypto-less fallbacks in the blob (source: patch 0014).
def _sym(n):
    return int([l.split()[0] for l in _sp.check_output(['aarch64-linux-gnu-nm', _bin[:-4] + '.elf'], text=True).splitlines()
                if l.endswith(' ' + n)][0], 16)
for _name, _va in (('cr_random_generate', 0xfffffe0007a918c4), ('cr_random_uniform', 0xfffffe0007a918d8),
                   ('cr_random_kmem_ctx_size', 0xfffffe0007a918ec), ('cr_random_kmem_init', 0xfffffe0007a91900)):
    _t = _sym(_name); assert _EXC_VA <= _t < _EXC_VA + _EXC_LEN
    PATCHES.append((_name + '-fallback', _va, [0xd0ffbf28], [0x14000000 | (((_t - _va) >> 2) & 0x3ffffff)]))
# s7: trust_cache_init -> tc_init_guard (skip when img4if/amfi NULL; patch 0015)
_t = _sym('tc_init_guard'); assert _EXC_VA <= _t < _EXC_VA + _EXC_LEN
PATCHES.append(('trust_cache_init-guard', 0xfffffe0007919954, [0xa9bf7bfd],
                [0x14000000 | (((_t - 0xfffffe0007919954) >> 2) & 0x3ffffff)]))
