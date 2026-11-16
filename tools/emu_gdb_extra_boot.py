LP={}
for l in open('/workspace/tpro-fix/pth/lp.syms'):
    a,t,n=l.split(); LP[n]=int(a,16)
cnt={}
# Return hooks: one persistent (never deleted) breakpoint per return address; pending tags keyed
# by (return pc, sp at call). Creating+deleting temporary breakpoints from stop() raced with
# gdb's own handling (boot50: stray SIGTRAP at a deleted temp bp -> harness stopped the run).
_ret_bps={}; _ret_pend={}
class _RetSite(gdb.Breakpoint):
    def __init__(s,addr):
        super().__init__('*%#x'%addr,internal=True); s.addr=addr
    def stop(s):
        t=_ret_pend.pop((s.addr,r('sp')),None)
        if t is not None:
            x0=r('x0'); extra=''
            if (x0 & 0xffffffff) == 0x9f: extra=' (xpcerr=159 Sandbox restriction)'
            if (x0 & 0xffffffff) == 0x35: extra=' (container err 0x35 null-path)'
            # Userland C-string return (CM_SYSPATH path, etc.)
            if 0x100000000 <= x0 < 0x300000000:
                try:
                    s=ustr(x0,256)
                    if s: extra+=' str=%r'%s
                except Exception: pass
            if 'outp=0x' in t:
                try:
                    op=int(t.split('outp=')[1].split()[0],16)
                    if op: extra+=' obj=%#x'%(m32(op)&0xffffffff)
                except Exception as e:
                    extra+=' obj_err=%r'%e
            if t.startswith('XPC_LOOKUP_RET'):
                # boot137/139: force BB ACCHW/RB → 0x9f + CLEAR ports (boot55 HID path).
                # boot145: ACCHW-only left BB with a real RB port; it hung on RB
                # check-in and never reached HIDServiceSupport/IOMFB. RB force is
                # required to skip that hang; post-open silence is a separate wall.
                # Out-port ptrs are embedded in the RetBP tag (out4=/out6=/out7=).
                force_bb = ("proc=backboardd" in t and (
                        "name='com.apple.runningboard'" in t or
                        "name='com.apple.ACCHWComponentAuthService'" in t))
                if force_bb:
                    try:
                        gdb.execute('set $x0 = 0x9f', to_string=True)
                        x0 = 0x9f
                        extra += ' FORCED_XPCERR_0x9f'
                    except Exception as e:
                        extra += ' force_err=%r' % e
                try:
                    def _tag_ptr(key):
                        if key not in t:
                            return 0
                        try:
                            return int(t.split(key)[1].split()[0], 16)
                        except Exception:
                            return 0
                    x4, x6, x7 = _tag_ptr('out4='), _tag_ptr('out6='), _tag_ptr('out7=')
                    if not (x4 or x6 or x7):
                        # Fallback for older tags.
                        sp0, x4, x6, x7 = _dsc.get('_xl_out', (0, 0, 0, 0))
                    for label, ptr in (('x4', x4), ('x6', x6), ('x7', x7)):
                        if not ptr:
                            continue
                        if 0x100000000 <= ptr < 0x300000000 or (ptr >> 40) == 0xfffffe:
                            try:
                                if force_bb:
                                    gdb.selected_inferior().write_memory(
                                        ptr, b'\x00\x00\x00\x00')
                                    extra += ' %s*=CLEARED' % label
                                else:
                                    v = m64(ptr)
                                    extra += ' %s*=%#x' % (label, v)
                                    if v and v < 0x10000:
                                        extra += '(port?)'
                            except Exception:
                                try:
                                    extra += ' %s*=w:%#x' % (label, m32(ptr))
                                except Exception:
                                    pass
                except Exception as e:
                    extra += ' xl_err=%r' % e
            P('RET %s x0=%#x%s' % (t, x0, extra))
        return False
def RetBP(addr,tag):
    if addr not in _ret_bps:
        if len(_ret_bps)>=128: return
        _ret_bps[addr]=_RetSite(addr)
    _ret_pend[(addr,r('sp'))]=tag
def mk(name,limit):
    def fn():
        cnt[name]=cnt.get(name,0)+1
        if cnt[name]<=limit:
            P('LP %s #%d x0=%#x x1=%#x x2=%#x x3=%#x x4=%#x x5=%#x x6=%#x x7=%#x'%((name,cnt[name])+tuple(r('x%d'%i) for i in range(8))))
            RetBP(r('x30'),name)
        return False
    BP(name,fn,LP[name])
LPHOOKS=int(os.environ.get('LPHOOKS','0'))
for n,l in [('lp_bsdthread_register',5),('lp_bsdthread_create',10),('lp_bsdthread_terminate',5),('lp_thread_selfid',3),
            ('lp_workq_create_threadstack',10),('lp_workq_setup_thread',10),('lp_enotsup',20),('lp_workq_handle_stack_events',5)]:
    if LPHOOKS: mk(n,l)
def on_wqk():
    P('WORKQ_KERNRETURN pid? x1=%#x'%r('x1')); return False
def cstr(a,n=200):
    try:
        b=bytes(gdb.selected_inferior().read_memory(a,n)); return b.split(b'\0')[0].decode('latin1')
    except Exception as e: return '<%s>'%e
def on_panic2():
    P('PANIC2 fmt=%r x1=%#x x2=%#x x3=%#x lr=%#x'%(cstr(r('x0')),r('x1'),r('x2'),r('x3'),r('x30')))
    for rg in ('x1','x2','x3','x4'):
        v=r(rg)
        if v>>40==0xfffffe: P('  %s str=%r'%(rg,cstr(v,300)))
    fp=r('x29')
    for i in range(16):
        try: nfp=m64(fp); lr=m64(fp+8)
        except Exception: break
        if not lr: break
        P('  bt %#x'%((lr & 0x1ffffffffff) | 0xfffffe0000000000)); fp=nfp
        if not fp: break
    return False
BP('_panic',on_panic2)
import re as _re
def pname(p):
    try:
        b=bytes(gdb.selected_inferior().read_memory(p,0x1000))
        m=_re.findall(rb'[a-zA-Z0-9_.\-]{4,}\x00',b); return [x[:-1].decode() for x in m[:4]]
    except Exception as e: return str(e)
def ustr(va,n=512):
    out=b''
    while len(out)<n:
        chunk=0x1000-(va&0xfff); chunk=min(chunk,n-len(out))
        try: d=uread(va,chunk)
        except Exception as e: return out.decode('latin1')+'<%s>'%e
        out+=d; va+=chunk
        if b'\0' in d: break
    return out.split(b'\0')[0].decode('latin1')
def on_awp(tag):
    def f():
        a=r('x1'); ns=m64(a)&0xffffffff; code=m64(a+8); s=m64(a+32); fl=m64(a+40)
        P('%s proc=%#x name=%s ns=%d code=%#x flags=%#x'%(tag,r('x0'),pname(r('x0')),ns,code,fl))
        P('%s reason_string=%r'%(tag,ustr(s) if s else None))
        return False
    return f
BP('_abort_with_payload',on_awp('ABORT_WITH_PAYLOAD'))
def on_twp():
    a=r('x1')
    P('TERMINATE_WITH_PAYLOAD args=%s'%[hex(m64(a+8*i)) for i in range(7)]); return False
BP('_terminate_with_payload',on_twp)
NAMEI={'n':0,'fail':0,'pend':{}}
NAMEI_RET=0xfffffe00075ea5a8   # the single `ret` of _namei (xnu-12377.121.6 image)
def on_namei():
    if '_dsc_arm' in globals() and not _dsc.get('armed'): _dsc_arm()
    nd=r('x0'); dirp=m64(nd); seg=m32(nd+8)
    try: path=cstr(dirp) if seg==2 else ustr(dirp,256)
    except Exception as e: path='<%s>'%e
    NAMEI['n']+=1; NAMEI['pend'][r('sp')]=path
    return False
def on_namei_ret():
    path=NAMEI['pend'].pop(r('sp'),None); rv=r('x0')&0xffffffff
    if rv and path is not None:
        NAMEI['fail']+=1
        k=(rv,path)
        if k not in NAMEI.setdefault('seen',set()) and len(NAMEI['seen'])<6000:
            NAMEI['seen'].add(k)
            pn = cur_pname()
            # boot143: SpringBoard often shows as proc=? — cache TPIDR from path.
            try:
                th = r('TPIDR_EL1')
                if path and th and (pn == ['?'] or (pn and pn[0] == '?')):
                    pl = path.lower()
                    if 'springboard' in pl:
                        _pname_by_tpidr[th] = ['SpringBoard']
                        pn = ['SpringBoard']
                    elif 'backboardd' in pl:
                        _pname_by_tpidr[th] = ['backboardd']
                        pn = ['backboardd']
            except Exception:
                pass
            P('NAMEI_FAIL err=%d path=%r proc=%s' % (rv, path, pn))
    return False
BP('_namei',on_namei); BP('namei_ret',on_namei_ret,NAMEI_RET)
SPAWN={'n':0}
def on_spawn():
    a=r('x1'); path=m64(a+8)
    SPAWN['n']+=1
    if SPAWN['n']<=300: P('SPAWN #%d path=%r'%(SPAWN['n'],ustr(path,256)))
    return False
BP('_posix_spawn',on_spawn)
def on_rk():
    msg=r('x1')
    P('REBOOT_KERNEL howto=%#x msg=%r'%(r('x0'), cstr(msg,400) if msg>>40==0xfffffe else hex(msg)))
    return False
BP('_reboot_kernel',on_rk)
def on_pwo():
    sp=r('sp'); args=[m64(sp+8*i) for i in range(4)]
    P('PANIC_WITH_OPTIONS fmt=%r x4=%#x stack=%s'%(cstr(r('x4'),200), r('x4'), [cstr(a,300) if a and a>>40==0xfffffe else hex(a or 0) for a in args]))
    return False
BP('_panic_with_options_and_initiator',on_pwo)
# --- platform / cs blob diagnostics ---
CS={'mac':0,'cave':0,'plat':0}
def on_mac_sig():
    CS['mac']+=1
    if CS['mac']<=20:
        # x0=vp x1=? x2=flags_out
        P('MAC_SIG #%d vp=%#x flags_out=%#x'%(CS['mac'],r('x0'),r('x2')))
    return False
def on_mac_sig_ret():
    # after return: w0=err. Catch via breakpoint at return site in ubc
    return False
def on_cave():
    CS['cave']+=1
    if CS['cave']<=30:
        # before ldp, then we need after orr - just log lr/sp
        P('CAVE #%d lr=%#x'%(CS['cave'],r('lr')))
    return False
def on_after_flags():
    # at 8fe4 tbnz - w8 should have flags
    CS['plat']+=1
    if CS['plat']<=30:
        P('FLAGS #%d w8=%#x tbnz_will=%d'%(CS['plat'],r('w8')&0xffffffff, 1 if (r('w8')&0x4000000) else 0))
    return False
BP('_mac_vnode_check_signature', on_mac_sig)
BP('cave', on_cave, addr=0xfffffe0007996100)
BP('flags', on_after_flags, addr=0xfffffe0007938fe4)
# also csops
def on_csops():
    # x0=pid x1=ops x2=useraddr x3=usersize
    ops=r('x1')&0xffffffff
    if ops in (0,1,5,6,7):  # STATUS etc
        if CS.get('ops',0)<40:
            CS['ops']=CS.get('ops',0)+1
            P('CSOPS #%d pid=%d ops=%d'%(CS['ops'],r('x0')&0xffffffff,ops))
    return False
# try symbol
try:
    BP('_csops_internal', on_csops)
except Exception as e:
    P('no csops_internal: %s'%e)
# --- execve + exit status ---
EX={'n':0}
def on_execve():
    a=r('x1'); path=m64(a)
    EX['n']+=1
    if EX['n']<=200:
        argv=m64(a+8); args=[]
        for k in range(8):
            p=m64(argv+8*k) if argv else None  # user ptrs: use uread
            break
        P('EXECVE #%d path=%r'%(EX['n'],ustr(path,256)))
    return False
for nm in ('_execve','___mac_execve'):
    if nm in S:
        BP(nm,on_execve); break
def on_exit():
    a=r('x1'); P('SYS_EXIT rval=%d proc=%s'%(m32(a) if m32(a) is not None else -1, pname(r('x0')))); return False
if '_exit' in S: BP('_exit',on_exit)
# --- console capture + reboot(2) caller (boot19+) ---
CON={'buf':b'','n':0}
def on_conw():
    a=r('x0'); n=r('x1')&0xffffffff
    if n<=0 or n>4096 or CON['n']>60000: return False
    try: d=bytes(gdb.selected_inferior().read_memory(a,n))
    except Exception: return False
    CON['buf']+=d
    while b'\n' in CON['buf']:
        l,CON['buf']=CON['buf'].split(b'\n',1); CON['n']+=1
        P('CON %s'%l.decode('latin1').rstrip())
    return False
BP('_console_write',on_conw)
def on_sysreboot():
    p=r('x0'); u=r('x1'); opt=m32(u); msg=m64(u+8)
    P('SYS_REBOOT proc=%s opt=%#x msg=%r'%(pname(p),opt,ustr(msg,400) if msg else None))
    return False
BP('_reboot',on_sysreboot)
# --- unmount tracing (boot27) ---
UM={'on':0,'ms':0,'n':{}}
def _cnt(k,lim):
    UM['n'][k]=UM['n'].get(k,0)+1; return UM['n'][k]<=lim
def mkh(tag,lim,fmt):
    def f():
        if _cnt(tag,lim): P(tag+' '+fmt())
        if tag=='DOUNMOUNT': UM['on']=1
        return False
    return f
BP('_dounmount',mkh('DOUNMOUNT',20,lambda:'mp=%#x flags=%#x lr=%#x'%(r('x0'),r('x1'),r('x30'))))
BP('_vflush',mkh('VFLUSH',20,lambda:'mp=%#x skipvp=%#x flags=%#x'%(r('x0'),r('x1'),r('x2'))))
BP('_vnode_reclaim_internal',mkh('VRECLAIM',40,lambda:'vp=%#x'%r('x0')))
BP('r_reclaim',mkh('R_RECLAIM',40,lambda:'ap=%#x'%r('x0')),addr=0xfffffe000743f5e8)
BP('r_unmount',mkh('R_UNMOUNT',10,lambda:'mp=%#x'%r('x0')),addr=0xfffffe000743e6d8)
def on_msleep():
    if UM['on'] and _cnt('MSLEEP',300):
        w=r('x3'); P('MSLEEP chan=%#x wmesg=%r lr=%#x'%(r('x0'), cstr(w,40) if w>>40==0xfffffe else hex(w), r('x30')))
    return False
BP('_msleep',on_msleep)
def on_pwtks():
    ss=r('x1'); P('PWTKS msg=%r ss=%#x sp_now=%#x tpidr_el1=%#x'%(cstr(r('x0')),ss,r('sp'),r('TPIDR_EL1') if True else 0))
    try:
        b=8; x=[m64(ss+b+8*i) for i in range(29)]
        fp,lr,sp,pc=[m64(ss+b+0xe8+8*i) for i in range(4)]
        cpsr=m64(ss+b+0x108)&0xffffffff; far=m64(ss+b+0x110); esr=m64(ss+b+0x118)
        P('  pc=%#x lr=%#x sp=%#x fp=%#x cpsr=%#x far=%#x esr=%#x'%(pc,lr,sp,fp,cpsr,far,esr))
        P('  x=%s'%' '.join('%#x'%v for v in x))
        f=fp
        for i in range(40):
            try: nf=m64(f); l=m64(f+8)
            except Exception: break
            if not l: break
            P('  sbt %#x fp=%#x'%((l & 0x1ffffffffff)|0xfffffe0000000000,f)); f=nf
            if not f: break
    except Exception as e: P('  PWTKS read err %r'%e)
    return False
BP('_panic_with_thread_kernel_state',on_pwtks)
_pcnt=[0]
def on_persona():
    _pcnt[0]+=1
    if _pcnt[0]<=60:
        op=m64(r('x1'))&0xffffffff
        P('PERSONA op=%d proc=%s'%(op,pname(r('x0'))[:2]))  # no return hook: x30 of a syscall handler is unix_syscall's return path (persistent bp there = every syscall)
    return False
BP('_persona',on_persona)
# boot134: TPIDR→proc walk intermittently fails (esp. runningboardd) with
# "Cannot access memory at address 0xfffffe…"; cache last good name per TPIDR.
_pname_by_tpidr={}
def cur_pname():
    """Return up to 2 name candidates from the proc struct (was [:1] → 'C' on errors)."""
    th=0
    try:
        th=r('TPIDR_EL1'); tro=m64(th+0x4e8); p=m64(tro+0x18)
        n=pname(p)
        if isinstance(n,str):
            if n.startswith('Cannot access') or n.startswith('<'):
                return list(_pname_by_tpidr.get(th, ['?']))[:2]
            _pname_by_tpidr[th]=[n[:48]]
            return [n[:48]]
        if n:
            _pname_by_tpidr[th]=list(n[:2])
            return n[:2]
        return list(_pname_by_tpidr.get(th, ['?']))[:2]
    except Exception:
        return list(_pname_by_tpidr.get(th, ['?']))[:2] if th else ['?']

_iok_seen=set()
def _iok(tag,ma,cnt):
    n=cur_pname()
    try:
        b=bytes(gdb.selected_inferior().read_memory(ma,min(cnt,4096)))
        s=[x.decode() for x in _re.findall(rb'[ -~]{3,}',b)]
    except Exception as e: s=['<%s>'%e]
    k=(n[0] if n else '?',tag,tuple(s))
    pn0=(n[0] if n else '') or ''
    ui = ('backboard' in pn0.lower() or 'SpringBoard' in pn0 or 'springboard' in pn0.lower()
          or 'runningboard' in pn0.lower())
    interesting=any(x in ('IOGPU','IOMobileFramebuffer','IOSurfaceRoot','IOCoreSurfaceRoot','AGXAccelerator','IOAccelerator','AppleCLCD','AppleKeyStore','IOHIDEventService','AppleParavirtGPU','DisplayPortCount','HIDServiceSupport') for x in s)
    if k not in _iok_seen or ui:
        _iok_seen.add(k); P('IOKMATCH %s proc=%s %s'%(tag,n,s[:12]))
    # Return value: for getMatchingService, x0=service (0 = no match). Critical for IOGPU wall.
    # boot133: always ret-log UI stack (SB/BB/RB) — they may match via property dicts we don't key on.
    if interesting or tag=='service' or ui:
        try: RetBP(r('x30'), 'IOKMATCH_RET %s proc=%s keys=%s'%(tag, pn0 or '?', ','.join(s[:6])))
        except Exception as e: P('ERR iok_ret %s'%e)
    return False
BP('_is_io_service_get_matching_services_bin',lambda: _iok('services',r('x1'),r('x2')))
BP('_is_io_service_get_matching_service_bin',lambda: _iok('service',r('x1'),r('x2')))
BP('_is_io_service_add_notification_bin_64',lambda: _iok('notify',r('x2'),r('x3')))
# boot108: 8 deferred pings, no poison panic, still no FDR IOSOPEN.
# Log whether FDR ever drains the notify iterator (and what Next returns).
_ionext_n = {'n': 0}
def on_io_iterator_next():
    try:
        pn = cur_pname()
        pn0 = (pn[0] if pn else '') or ''
        pl = pn0.lower()
        if ('IOMFB' not in pn0 and 'FDR' not in pn0 and 'backboard' not in pl and
            'SpringBoard' not in pn0 and 'springboard' not in pl):
            return False
        _ionext_n['n'] += 1
        if _ionext_n['n'] > 80:
            return False
        # is_io_iterator_next(iterator, io_object_t *out): x0=kr on return; *x1=object.
        try:
            outp = r('x1'); obj0 = m32(outp) if outp else 0
        except Exception:
            outp, obj0 = 0, 0
        P('IOITER_NEXT proc=%s outp=%#x obj_before=%#x' % (pn, outp, obj0))
        RetBP(r('x30'), 'IOITER_NEXT_RET proc=%s outp=%#x' % (pn0 or '?', outp))
    except Exception as e:
        P('ERR ioiter %s' % e)
    return False
BP('_is_io_iterator_next', on_io_iterator_next, addr=0xfffffe0007b13c88)
def _uframes(tag, extra=''):
    th=r('TPIDR_EL1'); ss=m64(th+0x110)
    fp,lr,sp,pc=[m64(ss+8+0xe8+8*i) for i in range(4)]
    ch=[pc,lr]; f=fp
    for i in range(24):
        try: d=uread(f,16); nf,l=struct.unpack('<QQ',d)
        except Exception: break
        if not l: break
        ch.append(l & 0xfffffffff); f=nf
        if not f: break
    P('%s proc=%s pc=%#x lr=%#x %s FRAMES %s'%(tag,cur_pname(),pc,lr,extra,' '.join('%#x'%x for x in ch)))
def on_pkill():
    try:
        if '_dsc_arm' in globals(): _dsc_arm()
        sig=m64(r('x1')+8)&0xffffffff
        if sig==6: _uframes('SIGABRT_PTHREAD_KILL')
    except Exception as e: P('PKILL err %r'%e)
    return False
BP('___pthread_kill',on_pkill)

# csops entitlement queries (CS_OPS_STATUS 0, CS_OPS_ENTITLEMENTS_BLOB 7, CS_OPS_DER_ENTITLEMENTS_BLOB 16) with result
_cs_seen={}
def on_csops():
    ops=r('x1')&0xffffffff
    if ops not in (0,7,16): return False
    n=cur_pname(); k=(n[0] if n else '?',ops)
    _cs_seen[k]=_cs_seen.get(k,0)+1
    if _cs_seen[k]<=2: P('CSOPS proc=%s pid=%d ops=%d usize=%d'%(n,r('x0')&0xffffffff,ops,r('x3')))  # entry only: csops tail-calls csops_internal, so x30 is unix_syscall's return (a bp there hits every syscall; boot51 crawled)
    return False
BP('_csops_internal',on_csops)

# ramfs owners: r_setattr (lapis-ramfs) entry - who sets which uid/gid on which /private/var node
RAMFS_SETATTR=0xfffffe000743eb60
_own_cnt={}
def _rpath(n):
    parts=[]
    for _ in range(16):
        if not n: break
        nm=m64(n+0x50); nl=m32(n+0x28)&0xffff
        if nm and nl: parts.append(bytes(gdb.selected_inferior().read_memory(nm,min(nl,255))).decode('latin1'))
        n=m64(n)
    return '/private/var/'+'/'.join(reversed(parts))
def on_rsetattr():
    try:
        ap=r('x0'); vp=m64(ap+8); vap=m64(ap+16); act=m64(vap+8)
        if not (act & (1<<7)): return False
        uid=m32(vap+68); gid=m32(vap+72); n=m64(vp+0xe0); p=_rpath(n)
        k=(uid,gid); _own_cnt[k]=_own_cnt.get(k,0)+1
        if uid!=0 or gid!=0 or p.startswith('/private/var/mobile') and p.count('/')<=5:
            if _own_cnt[k]<=40 or p.count('/')<=4: P('RAMFS_CHOWN uid=%d gid=%d path=%r proc=%s'%(uid,gid,p,cur_pname()))
    except Exception as e: P('ERR rsetattr %s'%e)
    return False
BP('ramfs_setattr',on_rsetattr,RAMFS_SETATTR)

# --- user-space XPC lookup trace (chunk 16). QEMU gdbstub breakpoints are TCG breakpoints by
# virtual address (no memory patching), so a bp on a dyld-shared-cache VA fires in every process.
# The cache slide is per boot: taken from vm_shared_region_start_address() (unslid base 0x180000000).
DSC_BASE=0x180000000
DSC_SYMS={'xpc_look_up_endpoint':0x197f20fbc,'container_xpc_create_connection':0x1e2761b08,
          'container_system_path_for_identifier':0x1e276a4e8,
          'container_legacy_lookup':0x1e275de24,
          'err':0x18f098f44,
          'abort':0x18f099c04,
          'abort_with_reason':0x23226a5f0,
          # SpringBoard _SBStaticInitializer right after SecTaskGetCodeSignStatus (w0=status)
          'SBStatic_status':0x224040bf8,
          'SBStatic_cold':0x2241f6e00}
_dsc={'slide':None,'pend':{}}
def _dsc_ret():
    st=_dsc['pend'].pop(r('sp'),None)
    if st is None or _dsc['slide'] is not None: return False
    start=m64(st); slide=start-DSC_BASE
    if not (0<=slide<0x40000000): return False
    _dsc['slide']=slide; P('DSC_SLIDE start=%#x slide=%#x'%(start,slide))
    return False
# The user bps are inserted from a later kernel stop taken in a process that has the cache mapped
# (TTBR0 = that process): QEMU invalidates already-translated code for a new breakpoint through the
# current address space, so inserting them in a context where the VA is unmapped (boot54) left the
# cached translations of the shared-cache code without the breakpoint and they never fired.
def _dsc_arm():
    if _dsc['slide'] is None: return
    p=cur_pname()
    if not p or p[0] in ('launchd','?'): return
    # Re-arm in SpringBoard so TCG BPs bind in the crashing process AS.
    key='armed_'+p[0]
    if _dsc.get(key): return
    try: uread(DSC_SYMS['err']+_dsc['slide'],4)
    except Exception: return
    for n,a in DSC_SYMS.items():
        BP('u_%s_%s'%(p[0],n),_ufn(n),a+_dsc['slide'])
    _dsc[key]=1
    _dsc['armed']=1
    P('DSC_BPS armed in proc=%s slide=%#x err=%#x'%(p,_dsc['slide'],DSC_SYMS['err']+_dsc['slide']))
def on_vsrsa():
    if _dsc['slide'] is None: _dsc['pend'][r('sp')]=r('x1'); RetBP2(r('x30'),_dsc_ret)
    return False
_rb2={}
class _RetSite2(gdb.Breakpoint):
    def __init__(s,addr,fn):
        super().__init__('*%#x'%addr,internal=True); s.fn=fn
    def stop(s): return s.fn()
def RetBP2(addr,fn):
    if addr not in _rb2: _rb2[addr]=_RetSite2(addr,fn)
_xl_cnt={}
def _ufn(n):
    def fn():
        p=cur_pname(); pn=p[0] if p else '?'
        if n in ('err','abort','abort_with_reason'):
            P('DSC_HIT %s gdbpc=%#x lr=%#x proc=%s'%(n,r('pc'),r('x30'),pn))
        if n=='xpc_look_up_endpoint':
            name=ustr(r('x0'),128)
            k=(pn,name); _xl_cnt[k]=_xl_cnt.get(k,0)+1
            interesting_svc = any(s in (name or '') for s in (
                'containermanager', 'cfprefs', 'backboard', 'CARender', 'render',
                'frontboard', 'runningboard', 'display', 'hid'))
            if ((interesting_svc or
                 pn in ('backboardd','SpringBoard','containermanager','runningboardd','cfprefsd','?'))
                and _xl_cnt[k]<=8):
                # Signature (Frida/libxpc): kr = xpc_look_up_endpoint(name, type, handle, ..., port*, ...)
                # x0 on return is kern_return_t (0 = success). Port is an out-param later on stack;
                # also log x7/x6 which often hold port* / aux on arm64e calling convention variants.
                # Save likely out-port pointers (ABI variants use x4/x6/x7).
                # Embed out-port ptrs in the ret tag — global _xl_out races (boot141).
                _dsc['_xl_out'] = (r('sp'), r('x4'), r('x6'), r('x7'))
                P('XPC_LOOKUP proc=%s name=%r type=%d handle=%#x flags=%#x x4=%#x x5=%#x x6=%#x x7=%#x'%(
                    pn,name,r('x1')&0xffffffff,r('x2'),r('x3'),r('x4'),r('x5'),r('x6'),r('x7')))
                RetBP(r('x30'),
                      "XPC_LOOKUP_RET proc=%s name=%r out4=%#x out6=%#x out7=%#x" % (
                          pn, name, r('x4'), r('x6'), r('x7')))
        elif n=='container_xpc_create_connection':
            P('CM_CONNECT proc=%s disposition=%d x1=%#x x2=%#x'%(pn,r('x0'),r('x1'),r('x2')))
        elif n=='container_legacy_lookup':
            # w0=class (12=systemData), x1=identifier
            P('CM_LOOKUP proc=%s class=%d id=%r flags=%#x'%(pn,r('x0')&0xffffffff,ustr(r('x1'),128) if r('x1') else None,r('x4')))
            RetBP(r('x30'),'CM_LOOKUP_RET proc=%s'%pn)
        elif n=='err':
            try: fmt=ustr(r('x1'),256) if r('x1') else None
            except Exception: fmt=None
            P('ERR eval=%d fmt=%r x2=%#x pc=%#x lr=%#x x0=%#x proc=%s'%(
                r('x0')&0xffffffff,fmt,r('x2'),r('pc'),r('x30'),r('x0'),pn))
            _uframes('ERR_FRAMES')
        elif n=='abort':
            P('ABORT gdbpc=%#x lr=%#x x0=%#x x1=%#x proc=%s'%(r('pc'),r('x30'),r('x0'),r('x1'),pn))
            # If LR points into CoverSheet / known TEXT, also dump nearby C-strings from x0/x1
            for reg in ('x0','x1','x2','x3'):
                try:
                    s=ustr(r(reg),128)
                    if s and len(s)>=4: P('  ABORT_%s str=%r'%(reg,s))
                except Exception: pass
            if pn=='SpringBoard': _uframes('ABORT_FRAMES')
        elif n=='abort_with_reason':
            try: reason=ustr(r('x3'),256) if r('x3') else None
            except Exception: reason=None
            P('ABORT_REASON ns=%d code=%#x flags=%#x reason=%r proc=%s'%(
                r('x0')&0xffffffff,r('x1'),r('x2'),reason,pn))
            _uframes('ABORT_REASON_FRAMES')
        elif n=='container_system_path_for_identifier':
            # boot139: BB past HID then SIGABRT — CM_SYSPATH id=NULL → stub
            # returns NULL → NSURL fileURLWithPath:nil (same boot47/55 wall).
            # DSC syspath stub is patched; feed a real bundle-id CString from DSC.
            idv = r('x0')
            ids = ustr(idv, 128) if idv else None
            P('CM_SYSPATH proc=%s id=%r' % (pn, ids))
            if not idv and _dsc.get('slide') is not None:
                # Unslid CStrings in pristine 23F77 DSC (exact "com.apple.*" only).
                _id_va = {
                    'backboardd': 0x21efbfc5e,       # PowerlogLiteOperators
                    'SpringBoard': 0x1c1445950,      # SpringBoardHome
                }.get(pn)
                if _id_va:
                    a = _id_va + _dsc['slide']
                    try:
                        gdb.execute('set $x0 = %#x' % a, to_string=True)
                        P('CM_SYSPATH_FORCE_ID proc=%s va=%#x' % (pn, a))
                    except Exception as e:
                        P('CM_SYSPATH_FORCE_ERR %r' % e)
            RetBP(r('x30'), 'CM_SYSPATH_RET proc=%s' % pn)
        elif n=='SBStatic_status':
            # boot143: pname often '?' here — pin SpringBoard for later XPC/IOKit logs.
            try:
                th = r('TPIDR_EL1')
                if th:
                    _pname_by_tpidr[th] = ['SpringBoard']
                    pn = 'SpringBoard'
            except Exception:
                pass
            st=r('x0')&0xffffffff
            masked=st&0x0fffffff&0xfc000001
            P('SBSTATIC_STATUS proc=%s status=%#x masked=%#x want=0x4000001 pass=%d'%(
                pn,st,masked,1 if masked==0x04000001 else 0))
        elif n=='SBStatic_cold':
            try:
                th = r('TPIDR_EL1')
                if th:
                    _pname_by_tpidr[th] = ['SpringBoard']
                    pn = 'SpringBoard'
            except Exception:
                pass
            P('SBSTATIC_COLD proc=%s (platformBinary assert path)'%pn)
        else:
            P('DSC_CALL %s proc=%s'%(n,pn))
        return False
    return fn
BP('_vm_shared_region_start_address',on_vsrsa)

# chunk 17: ___mac_syscall("Sandbox", call, arg) — without lapis_sandbox this returns ENOPOLICY (103)
# and launchd maps sandbox_check failure to xpc 159. With the policy, call 2 must return 0.
_mac_cnt={'n':0}
def on_mac_syscall():
    try:
        name=cstr(r('x0'),32); call=r('x1')&0xffffffff
    except Exception as e:
        P('ERR mac_syscall %s'%e); return False
    if name!='Sandbox': return False
    _mac_cnt['n']+=1
    if _mac_cnt['n']<=40 or call==2:
        P('MAC_SYSCALL Sandbox call=%d arg=%#x proc=%s'% (call, r('x2'), cur_pname()))
        RetBP(r('x30'), 'MAC_SYSCALL_RET Sandbox call=%d'%call)
    return False
BP('___mac_syscall', on_mac_syscall)

# IOServiceOpen → is_io_service_open_extended (x0=service, x3=type, …). Log open attempts
# on our stub providers so boot84 can prove newUserClient is reached after IOGPU/IOMFB match.
_iosopen_cnt = {'n': 0}
def on_iosopen():
    try:
        pn = cur_pname(); pn0 = (pn[0] if pn else '') or ''
        interesting = ('IOMFB' in pn0 or 'FDR' in pn0 or 'backboard' in pn0.lower()
                       or 'SpringBoard' in pn0 or 'springboard' in pn0.lower())
        _iosopen_cnt['n'] += 1
        # Always log FDR/display clients; cap noise from others.
        if not interesting and _iosopen_cnt['n'] > 80:
            return False
        typ = r('x3') & 0xffffffff
        # is_io_service_open_extended: connection out lives at *(x7) on this
        # kernel (MIG wrapper); log it on return when present.
        outp = r('x7')
        P('IOSOPEN proc=%s type=%d svc=%#x outp=%#x' % (pn, typ, r('x0'), outp))
        RetBP(r('x30'), 'IOSOPEN_RET proc=%s type=%d outp=%#x' % (
            pn0 or '?', typ, outp))
    except Exception as e:
        P('ERR iosopen %s' % e)
    return False

# boot114/115: does FDR call IOObjectConformsTo on the IOMFB stub?
def on_conforms():
    try:
        pn=cur_pname(); pn0=(pn[0] if pn else '') or ''
        if 'IOMFB' not in pn0 and 'FDR' not in pn0 and 'backboard' not in pn0.lower():
            return False
        # x1 = class name (kernel pointer to C string)
        try:
            name=bytes(gdb.selected_inferior().read_memory(r('x1'), 64)).split(b'\0',1)[0].decode()
        except Exception:
            name='?'
        P('CONFORMS proc=%s class=%r'%(pn,name))
        RetBP(r('x30'), 'CONFORMS_RET proc=%s class=%s'%(pn0,name))
    except Exception as e:
        P('ERR conforms %s'%e)
    return False
BP('_is_io_object_conforms_to', on_conforms)

BP('_is_io_service_open_extended', on_iosopen)

# boot146: after IOMFB open BB went silent — catch close/teardown if it bails.
def on_iosclose():
    try:
        pn = cur_pname(); pn0 = (pn[0] if pn else '') or ''
        if 'backboard' not in pn0.lower() and 'SpringBoard' not in pn0 and 'IOMFB' not in pn0:
            return False
        P('IOSCLOSE proc=%s conn=%#x' % (pn, r('x0')))
        RetBP(r('x30'), 'IOSCLOSE_RET proc=%s' % (pn0 or '?'))
    except Exception as e:
        P('ERR iosclose %s' % e)
    return False
BP('_is_io_service_close', on_iosclose)

# Log IOUserClient::externalMethod selectors (selector in w1) after our opens.
_em_cnt = {'n': 0}
def on_external_method():
    _em_cnt['n'] += 1
    try:
        sel = r('x1') & 0xffffffff
        args = r('x2')
        extra = ''
        pn = cur_pname()
        pn0 = (pn[0] if pn else '') or ''
        # Always keep BB/SB/IOMFB method traffic; cap unrelated noise.
        if _em_cnt['n'] > 80 and 'backboard' not in pn0.lower() and 'SpringBoard' not in pn0 and 'IOMFB' not in pn0:
            return False
        # Dump IOExternalMethodArguments output fields for RB/surface sels.
        if args and (sel in (0, 2, 4, 13) or (pn and 'runningboard' in str(pn))
                     or 'backboard' in pn0.lower()):
            try:
                sin_c = m32(args + 40); sout_c = m32(args + 80)
                st_in = m32(args + 56); st_out = m32(args + 96)
                sout_p = m64(args + 72); stout_p = m64(args + 88)
                extra = ' sin=%d sout=%d stin=%d stout=%d soutp=%s stoutp=%s' % (
                    sin_c or 0, sout_c or 0, st_in or 0, st_out or 0,
                    h(sout_p), h(stout_p))
            except Exception as e:
                extra = ' args_err=%s' % e
        P('IOEXTMETHOD proc=%s sel=%d self=%#x%s' % (pn, sel, r('x0'), extra))
        RetBP(r('x30'), 'IOEXTMETHOD_RET proc=%s sel=%d' % (
            pn[0] if pn else '?', sel))
    except Exception as e:
        P('ERR extmethod %s' % e)
    return False
BP('__ZN12IOUserClient14externalMethodEjP25IOExternalMethodArgumentsP24IOExternalMethodDispatchP8OSObjectPv',
   on_external_method)

_cmem_cnt = {'n': 0}
def on_client_memory():
    _cmem_cnt['n'] += 1
    if _cmem_cnt['n'] > 40:
        return False
    try:
        typ = r('x1') & 0xffffffff
        P('IOCMEM proc=%s type=%d self=%#x' % (cur_pname(), typ, r('x0')))
        RetBP(r('x30'), 'IOCMEM_RET proc=%s type=%d' % (
            cur_pname()[0] if cur_pname() else '?', typ))
    except Exception as e:
        P('ERR cmem %s' % e)
    return False
BP('__ZN12IOUserClient19clientMemoryForTypeEjPjPP18IOMemoryDescriptor', on_client_memory)

# boot140/142: BB opens IOMFB (x0=0) then silence — no IOEXT. Hook ALL MIG
# connect-method variants (bluetoothd hit externalMethod without the plain
# is_io_connect_method) + property bin_buf/properties.
def _ui_iokit_proc(pn0):
    pl = (pn0 or '').lower()
    return ('backboard' in pl or 'SpringBoard' in (pn0 or '') or 'springboard' in pl
            or 'IOMFB' in (pn0 or '') or 'FDR' in (pn0 or ''))

_iocm_cnt = {'n': 0}
def _mk_ioconn(tag):
    def on():
        try:
            pn = cur_pname(); pn0 = (pn[0] if pn else '') or ''
            if not _ui_iokit_proc(pn0):
                return False
            _iocm_cnt['n'] += 1
            if _iocm_cnt['n'] > 200:
                return False
            sel = r('x1') & 0xffffffff
            P('IOCONN_%s proc=%s sel=%d conn=%#x' % (tag, pn, sel, r('x0')))
            RetBP(r('x30'), 'IOCONN_%s_RET proc=%s sel=%d' % (tag, pn0 or '?', sel))
        except Exception as e:
            P('ERR ioconn_%s %s' % (tag, e))
        return False
    return on
BP('_is_io_connect_method', _mk_ioconn('M'), addr=0xfffffe0007b18584)
BP('_is_io_connect_method_scalarI_scalarO', _mk_ioconn('SS'), addr=0xfffffe0007b18ac4)
BP('_is_io_connect_method_scalarI_structureO', _mk_ioconn('SSo'), addr=0xfffffe0007b19d24)
BP('_is_io_connect_method_scalarI_structureI', _mk_ioconn('SSi'), addr=0xfffffe0007b1a4a8)
BP('_is_io_connect_method_structureI_structureO', _mk_ioconn('SoSo'), addr=0xfffffe0007b1ac04)
BP('_is_io_connect_method_var_output', _mk_ioconn('VO'), addr=0xfffffe0007b18170)
# boot146: BB opens IOMFB then only sends mach 'w00t' — never hits method MIG.
# Catch notification/async/get_service which often follow Open before methods.
BP('_is_io_connect_async_method', _mk_ioconn('ASYNC'), addr=0xfffffe0007b18818)
BP('_is_io_connect_set_notification_port', _mk_ioconn('NOTE'), addr=0xfffffe0007b177cc)
BP('_is_io_connect_set_notification_port_64', _mk_ioconn('NOTE64'), addr=0xfffffe0007b1785c)
BP('_is_io_connect_get_service', _mk_ioconn('GETSVC'), addr=0xfffffe0007b1773c)
BP('_is_io_connect_add_client', _mk_ioconn('ADDCL'), addr=0xfffffe0007b180c8)
BP('_is_io_connect_set_properties', _mk_ioconn('SETPROP'), addr=0xfffffe0007b1816c)

_ioreg_cnt = {'n': 0}
def _mk_ioreg(tag):
    def on():
        try:
            pn = cur_pname(); pn0 = (pn[0] if pn else '') or ''
            if not _ui_iokit_proc(pn0):
                return False
            _ioreg_cnt['n'] += 1
            if _ioreg_cnt['n'] > 120:
                return False
            # bin_buf ABI: x0=entry, x1=plane, x2=property_name (boot143 logged
            # empty key from x1=plane). Try x2 first, then x1.
            def _cstr_reg(reg):
                try:
                    a = r(reg)
                    if not a:
                        return ''
                    return bytes(gdb.selected_inferior().read_memory(a, 64)).split(b'\0', 1)[0].decode()
                except Exception:
                    return ''
            plane = _cstr_reg('x1'); key = _cstr_reg('x2') or _cstr_reg('x1')
            P('IOREGPROP_%s proc=%s key=%r plane=%r entry=%#x' % (tag, pn, key, plane, r('x0')))
            RetBP(r('x30'), 'IOREGPROP_%s_RET proc=%s key=%s' % (tag, pn0 or '?', key))
        except Exception as e:
            P('ERR ioreg_%s %s' % (tag, e))
        return False
    return on
BP('_is_io_registry_entry_get_property_bin', _mk_ioreg('bin'), addr=0xfffffe0007b165a8)
BP('_is_io_registry_entry_get_property_bin_buf', _mk_ioreg('buf'), addr=0xfffffe0007b16098)
BP('_is_io_registry_entry_get_properties_bin', _mk_ioreg('all'), addr=0xfffffe0007b16084)

_iomap_cnt = {'n': 0}
def on_io_map_mem():
    try:
        pn = cur_pname(); pn0 = (pn[0] if pn else '') or ''
        if not _ui_iokit_proc(pn0):
            return False
        _iomap_cnt['n'] += 1
        if _iomap_cnt['n'] > 40:
            return False
        typ = r('x1') & 0xffffffff
        P('IOMAPMEM proc=%s type=%d conn=%#x' % (pn, typ, r('x0')))
        RetBP(r('x30'), 'IOMAPMEM_RET proc=%s type=%d' % (pn0 or '?', typ))
    except Exception as e:
        P('ERR iomap %s' % e)
    return False
BP('_is_io_connect_map_memory', on_io_map_mem, addr=0xfffffe0007b17dd0)
BP('_is_io_connect_map_memory_into_task', on_io_map_mem, addr=0xfffffe0007b17bec)

# boot99/100: cfprefsd burns CPU — shm_open path. Kernel ABI is
# shm_open(proc_t, struct shm_open_args *uap, int32_t *retval). On arm64 each
# arg is padded to 8: uap->name @0, uap->oflag @8, uap->mode @0x10.
# boot100 logged garbage oflags from reading mode at @0x10 as oflag.
_shm_cnt = {'n': 0}
def on_shm_open():
    _shm_cnt['n'] += 1
    try:
        uap = r('x1')
        name_va = m64(uap) if uap else 0
        oflag = m32(uap + 0x8) if uap else 0
        mode = m32(uap + 0x10) if uap else 0
        name = ustr(name_va, 96) if name_va else None
        pn = cur_pname()
        pn0 = (pn[0] if pn else '') or ''
        # Always log cfprefsd (boot106 hit the 40-cap before it spawned).
        is_cfprefs = ('cfprefs' in pn0.lower()) or (name and 'cfprefs' in name)
        interesting = is_cfprefs or (name and name.startswith('apple.')) or _shm_cnt['n'] <= 8
        if not interesting:
            if _shm_cnt['n'] > 200:
                return False
            return False
        P('SHM_OPEN proc=%s name=%r oflag=%#x mode=%#x' % (
            pn, name, oflag or 0, mode or 0))
        RetBP(r('x30'), 'SHM_OPEN_RET proc=%s name=%r' % (pn0 or '?', name))
    except Exception as e:
        P('ERR shm_open %s' % e)
    return False
BP('_shm_open', on_shm_open, addr=0xfffffe0007971bbc)

# boot137: mach_msg2_trap(struct mach_msg2_trap_args *args) — x0 is ARGS PTR
# (boot136 treated regs as fields → garbage; 400-cap burned; 0 RETs).
# Struct: data, options, bits|send<<32, remote|local<<32, voucher|id<<32,
#         desc|rcv_name<<32, rcv_size|prio<<32, timeout.
_mm2 = {'n': 0, 'rb': 0}
def _ui_proc(pn0):
    pl = (pn0 or '').lower()
    return ('backboard' in pl or 'springboard' in pl or 'runningboard' in pl
            or pn0 in ('SpringBoard', 'backboardd', 'runningboardd'))
def on_mach_msg2():
    try:
        pn = cur_pname(); pn0 = (pn[0] if pn else '') or ''
        if not _ui_proc(pn0):
            return False
        if 'runningboard' in pn0.lower():
            _mm2['rb'] = 1
        # boot146: BB post-IOMFB-open only shows up as mach 'w00t' — always
        # log BB/SB; keep the RB-first gate for other UI procs only.
        if (not _mm2['rb'] and 'runningboard' not in pn0.lower()
                and 'backboard' not in pn0.lower()
                and 'springboard' not in pn0.lower()
                and pn0 != 'SpringBoard'):
            return False
        ap = r('x0')
        # Trap entry: x0 → mach_msg2_trap_args on kernel stack.
        if (ap >> 40) != 0xfffffe:
            return False
        opt64 = m64(ap + 8)
        mb_ss = m64(ap + 16); mr_lp = m64(ap + 24); mv_id = m64(ap + 32)
        dc_rn = m64(ap + 40); rs_pr = m64(ap + 48)
        timeout = m64(ap + 56) & 0xffffffff
        bits = mb_ss & 0xffffffff
        send_sz = (mb_ss >> 32) & 0xffffffff
        remote = mr_lp & 0xffffffff
        local = (mr_lp >> 32) & 0xffffffff
        msgid = (mv_id >> 32) & 0xffffffff
        rcv_name = (dc_rn >> 32) & 0xffffffff
        rcv_sz = rs_pr & 0xffffffff
        sending = bool(opt64 & 1); recving = bool(opt64 & 2)
        # Real mach ports are small names; skip nonsense / recv-only polls.
        if remote > 0x10000 and not recving:
            return False
        if not sending and recving and _mm2['n'] > 40:
            return False
        _mm2['n'] += 1
        if _mm2['n'] > 300:
            return False
        P('MACHMSG2 proc=%s opt=%#x bits=%#x send=%d rcv=%d remote=%#x local=%#x rcv_name=%#x msgid=%d to=%d' % (
            pn0, opt64 & 0xffffffff, bits, send_sz, rcv_sz, remote, local,
            rcv_name, msgid, timeout))
        if sending and recving:
            RetBP(r('x30'), 'MACHMSG2_RET proc=%s msgid=%d remote=%#x' % (
                pn0, msgid, remote))
    except Exception as e:
        P('ERR mach_msg2 %s' % e)
    return False
BP('_mach_msg2_trap', on_mach_msg2, addr=0xfffffe000737cc9c)
