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
     [0xd0000000, 0x91000000, 0xd518c000, 0xd5033fdf]),   # s8: word 0 rewritten below
    # s8: lapis_early_putc glyph bit order. iso_font rows are LSB = leftmost
    # pixel; the unrolled loop tested MSB first -> mirrored text on s7.
    ('early-font-lsb-left-0', 0xfffffe0007567c94, [0x38ed69c2, 0x7100005f, 0x5a9fa122],
                                                 [0x386d69c2, 0x7200005f, 0x5a9f0122]),
    ('early-font-lsb-left-1', 0xfffffe0007567cb0, [0x721a005f], [0x721f005f]),
    ('early-font-lsb-left-2', 0xfffffe0007567cc8, [0x721b005f], [0x721e005f]),
    ('early-font-lsb-left-3', 0xfffffe0007567ce0, [0x721c005f], [0x721d005f]),
    ('early-font-lsb-left-4', 0xfffffe0007567cf8, [0x721d005f], [0x721c005f]),
    ('early-font-lsb-left-5', 0xfffffe0007567d10, [0x721e005f], [0x721b005f]),
    ('early-font-lsb-left-6', 0xfffffe0007567d28, [0x721f005f], [0x721a005f]),
    ('early-font-lsb-left-7', 0xfffffe0007567d40, [0x7200005f], [0x7219005f]),
    # s8: keep early console enabled (do not set early_disabled)
    ('early-keep-enabled', 0xfffffe0007568198, [0x52800028, 0xd0003b09, 0x393c1128],
                                              [0xd503201f, 0xd503201f, 0xd503201f]),
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
# Do not skip trust_cache_runtime_init. The original initializer must fail
# closed when a real AppleImage4/AMFI provider is absent.

# ---- s8: early console past arm_vm_init (source: patch 0016) ----
def _b(src, dst, link=False):
    off = dst - src; assert -(1 << 27) <= off < (1 << 27) and off % 4 == 0
    return (0x94000000 if link else 0x14000000) | ((off >> 2) & 0x3ffffff)
_P = {p[0]: p for p in PATCHES}
_e = _P['early-disable-no-paint+kernel-vbar']
PATCHES[PATCHES.index(_e)] = (_e[0], _e[1], _e[2],
    [_b(0xfffffe0007568188, _sym('ttbr_restore'), True), 0xd503201f, 0xd503201f, 0xd503201f])
PATCHES.append(('lapis_early_panic-guard', 0xfffffe0007567f8c, [0xd10103ff], [_b(0xfffffe0007567f8c, _sym('panic_guard'))]))
PATCHES.append(('printf-tee-early', 0xfffffe00073b6c64, [0xd10083ff], [_b(0xfffffe00073b6c64, _sym('printf_hook'))]))
PATCHES.append(('kprintf-tee-early', 0xfffffe0007b714fc, [0xd10103ff], [_b(0xfffffe0007b714fc, _sym('kprintf_hook'))]))
for _site, _tgt, _h in ((0xfffffe0007bcead8, 0xfffffe0007b6f810, 'h_after_vm'),
                        (0xfffffe0007bceadc, 0xfffffe0007b7162c, 'h_serial'),
                        (0xfffffe0007bcec38, 0xfffffe0007b70748, 'h_pe_plat'),
                        (0xfffffe0007bcec64, 0xfffffe0007b71a0c, 'h_gic'),
                        (0xfffffe0007bcec94, 0xfffffe0007571474, 'h_mstart')):
    PATCHES.append(('arm_init-crumb-' + _h, _site, [_b(_site, _tgt, True)], [_b(_site, _sym(_h), True)]))
PATCHES.append(('pe_init_iokit-vcattach-mute-early', 0xfffffe0007b70318,
                [_b(0xfffffe0007b70318, 0xfffffe0007b6f0f8, True)], [_b(0xfffffe0007b70318, _sym('h_vc_on'), True)]))
# s9: bzero DC ZVA loop -> stp loop (device-type-safe); see exc_text.S
PATCHES.append(('bzero-no-dczva', 0xfffffe0007c48744, [0xd50b7423], [_b(0xfffffe0007c48744, _sym('bzero_zva_loop'))]))
# s10: LapisPlatformExpert::start -> also ml_set_max_cpus(1) (platform-kext duty)
PATCHES.append(('lapispe-set-max-cpus', 0xfffffe0007b6d01c, [_b(0xfffffe0007b6d01c, 0xfffffe0007aa6fdc, True)],
                [_b(0xfffffe0007b6d01c, _sym('lapis_pe_cpus'), True)]))
# Do not install the historical built-in pthread_functions table. Its
# pthread_init/proc-hash/workq callbacks were no-op functions, which allowed
# XNU to continue as if pthread.kext had initialized when it had not. Keep the
# original NULL table so XNU's existing missing-provider panic remains active.
# A matching pthread.kext in a verified kernel collection is required.
# s10: lapis_crypto (built-in corecrypto stand-in) over the sysctl-only
# _vm_parameter_validation_kern_test (39600 bytes); entry now returns ENOTSUP.
PATCHES.append(('vm_param_test-entry-enotsup', 0xfffffe000743aa04, [0xa9ba6ffc, 0xa90167fa, 0xa9025ff8],
                [0x528005a0, 0xd65f03c0, 0xd503201f]))
_cdir = _os.path.join(_os.path.dirname(_bin), '..', 'lapis-crypto')
_cb = open(_os.path.join(_cdir, 'crypto.bin'), 'rb').read()
# Hash and patch a fixed, non-overlapping cave so compiler-dependent blob size
# changes do not invalidate the original-kernel guard.
_CRYPTO_MAX = 0x20b0  # ends exactly at the lapisfs cave, 0x743cac0
assert len(_cb) <= _CRYPTO_MAX, ('lapis-crypto overflow', len(_cb))
_cb += b'\0' * (_CRYPTO_MAX - len(_cb))
PATCHES.append(('lapis-crypto-provider', 0xfffffe000743aa10, 'fd6dab868a7c2808c4134825cf3231cf8b4f16798dcbf9948ceee40eb4faed91',
                list(_st.unpack('<%dI' % (len(_cb) // 4), _cb))))
_ct = int([l.split()[0] for l in _sp.check_output(['aarch64-linux-gnu-nm', _os.path.join(_cdir, 'crypto.elf')], text=True).splitlines()
           if l.endswith(' lapis_crypto_functions')][0], 16)
PATCHES.append(('g_crypto_funcs-builtin', 0xfffffe0007277400, [0, 0], [_ct & 0xffffffff, _ct >> 32]))
PATCHES.append(('crypto_init-true', 0xfffffe00072773f8, [0], [1]))

# s10: mockfs (built-in, since HFS/APFS are kexts) over leftover of the same
# reclaim region; bsd_init's vfs_mountroot call redirected to register+mount.
_mdir = _os.path.join(_os.path.dirname(_bin), '..', 'lapis-mockfs')
_mb = open(_os.path.join(_mdir, 'mockfs.bin'), 'rb').read()
_MOCKFS_MAX = 31000  # same budget enforced by lapis-mockfs/build.sh
assert len(_mb) <= _MOCKFS_MAX, ('lapisfs overflow', len(_mb))
_mb += b'\0' * (_MOCKFS_MAX - len(_mb))
PATCHES.append(('lapis-mockfs', 0xfffffe000743cac0, '610e2fdb27538a1c65d015c076fc0bbe9951dfd1508c2bfa49a8b94ddf88141b',
                list(_st.unpack('<%dI' % (len(_mb) // 4), _mb))))
_mw = int([l.split()[0] for l in _sp.check_output(['aarch64-linux-gnu-nm', _os.path.join(_mdir, 'mockfs.elf')], text=True).splitlines()
           if l.endswith(' lapis_mountroot_wrap')][0], 16)
def _bl(src, dst):
    off = (dst - src) // 4
    return 0x94000000 | (off & 0x3ffffff)
PATCHES.append(('bsd_init-mockfs-mountroot', 0xfffffe00078a5a94, [0x97f52420],
                [_bl(0xfffffe00078a5a94, _mw)]))
# The original AMFI interface remains unmodified. A real matching provider is
# required; an all-success placeholder must not be installed.

# IOService::setRootMedia is left unchanged. Replacing its prologue with RET
# hid the missing IOStorageFamily/IOMedia kext. The original assert stays.

# Entitlement entry points remain original; real AMFI must implement policy.
