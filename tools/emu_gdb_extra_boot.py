LP={}
for l in open('/workspace/tpro-fix/pth/lp.syms'):
    a,t,n=l.split(); LP[n]=int(a,16)
cnt={}
class RetBP(gdb.Breakpoint):
    def __init__(s,addr,tag):
        super().__init__('*%#x'%addr,internal=True,temporary=True); s.tag=tag
    def stop(s):
        P('RET %s x0=%#x'%(s.tag,r('x0'))); return False
def mk(name,limit):
    def fn():
        cnt[name]=cnt.get(name,0)+1
        if cnt[name]<=limit:
            P('LP %s #%d x0=%#x x1=%#x x2=%#x x3=%#x x4=%#x x5=%#x x6=%#x x7=%#x'%((name,cnt[name])+tuple(r('x%d'%i) for i in range(8))))
            RetBP(r('x30'),name)
        return False
    BP(name,fn,LP[name])
for n,l in [('lp_bsdthread_register',5),('lp_bsdthread_create',10),('lp_bsdthread_terminate',5),('lp_thread_selfid',3),
            ('lp_workq_create_threadstack',10),('lp_workq_setup_thread',10),('lp_enotsup',20),('lp_workq_handle_stack_events',5)]:
    mk(n,l)
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
        if NAMEI['fail']<=800: P('NAMEI_FAIL err=%d path=%r'%(rv,path))
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
