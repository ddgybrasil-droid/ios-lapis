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
        if t is not None: P('RET %s x0=%#x'%(t,r('x0')))
        return False
def RetBP(addr,tag):
    if addr not in _ret_bps:
        if len(_ret_bps)>=64: return
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
            NAMEI['seen'].add(k); P('NAMEI_FAIL err=%d path=%r proc=%s'%(rv,path,cur_pname()))
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
        P('PERSONA op=%d proc=%s'%(op,pname(r('x0'))[:2])); RetBP(r('x30'),'persona op=%d'%op)
    return False
BP('_persona',on_persona)
def cur_pname():
    try:
        th=r('TPIDR_EL1'); tro=m64(th+0x4e8); p=m64(tro+0x18); return pname(p)[:1]
    except Exception as e: return ['?']
_iok_seen=set()
def _iok(tag,ma,cnt):
    n=cur_pname()
    try:
        b=bytes(gdb.selected_inferior().read_memory(ma,min(cnt,4096)))
        s=[x.decode() for x in _re.findall(rb'[ -~]{3,}',b)]
    except Exception as e: s=['<%s>'%e]
    k=(n[0] if n else '?',tag,tuple(s))
    if k in _iok_seen: return False
    _iok_seen.add(k); P('IOKMATCH %s proc=%s %s'%(tag,n,s[:12])); return False
BP('_is_io_service_get_matching_services_bin',lambda: _iok('services',r('x1'),r('x2')))
BP('_is_io_service_get_matching_service_bin',lambda: _iok('service',r('x1'),r('x2')))
BP('_is_io_service_add_notification_bin_64',lambda: _iok('notify',r('x2'),r('x3')))
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
    if _cs_seen[k]<=2: RetBP(r('x30'),'CSOPS proc=%s pid=%d ops=%d usize=%d'%(n,r('x0')&0xffffffff,ops,r('x3')))
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
