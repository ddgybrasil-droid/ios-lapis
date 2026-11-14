# gdb-multiarch -x emu_gdb_driver.py (run via tools/emu_run_gdb.sh). Read-only observer:
# breakpoints only log; no guest memory or register is written.
# gdb -x drive.py ; env: OUT, MAXEXC
import gdb, os, struct, time
OUT=os.environ.get('OUT','/tmp'); MAXEXC=int(os.environ.get('MAXEXC','40'))
log=open(OUT+'/gdb-events.txt','w')
def P(s):
    log.write(s+'\n'); log.flush()
S={}
for l in open(os.environ['KSYMS']):
    a,n=l.split(); S[n]=int(a,16)
def r(reg): return int(gdb.parse_and_eval('$'+reg)) & 0xffffffffffffffff
def m64(a):
    try: return struct.unpack('<Q',bytes(gdb.selected_inferior().read_memory(a,8)))[0]
    except Exception: return None
def m32(a):
    try: return struct.unpack('<I',bytes(gdb.selected_inferior().read_memory(a,4)))[0]
    except Exception: return None
def sysreg(n):
    try: return r(n)
    except Exception: return None
state={'exc':0,'done':False}
def h(v): return 'None' if v is None else hex(v)
class BP(gdb.Breakpoint):
    def __init__(s,name,fn,addr=None):
        super().__init__('*%#x'%(addr if addr else S[name]),internal=True); s.fn=fn; s.nm=name
    def stop(s):
        try: return s.fn()
        except Exception as e: P('ERR %s %s'%(s.nm,e)); return False
def on_triage():
    exc=r('x0'); codes=r('x1'); c0=m64(codes); c1=m64(codes+8)
    state['exc']+=1
    if state['exc']<=MAXEXC:
        for e,f,ss,w in RING: P('  UA esr=%#x far=%#x ss=%#x words=%s'%(e,f,ss,' '.join(h(x) for x in w)))
        if RING:
            w=RING[-1][3]
            for nm,a in (('pc',w[33]),('lr',w[31])):
                try: P('  MEM %s %#x %s'%(nm,a,uread(a-32,64).hex()))
                except Exception as ex: P('  MEM %s %#x err %s'%(nm,a,ex))
        if RING:
            w=RING[-1][3]; fp=w[30]; chain=[w[33],w[31]]
            for _ in range(24):
                if not fp or fp&7: break
                try: nfp,lr=struct.unpack('<QQ',uread(fp,16))
                except Exception: break
                chain.append(lr&0xfffffffff); fp=nfp
            P('  FPCHAIN '+' '.join(hex(x) for x in chain))
        P('EXC_TRIAGE exc=%d code0=%s code1=%s elr=%s far=%s esr=%s'%(exc,h(c0),h(c1),h(sysreg('ELR_EL1')),h(sysreg('FAR_EL1')),h(sysreg('ESR_EL1'))))
    return False
def on_reason():
    P('OS_REASON ns=%d code=%#x'%(r('x0'),r('x1'))); state['reason']=1
    return False
def on_prepexit():
    state.setdefault('pe',0); state['pe']+=1
    P('PROC_PREPAREEXIT #%d reason_seen=%s'%(state['pe'],state.get('reason')))
    if state.get('reason'): state['done']=True; return True
    return False
def on_panic():
    P('PANIC'); state['done']=True; return True
def on_mwl():
    P('MWL enter'); return False
def on_srmap():
    P('SR_MAP2 enter'); return False
RING=[]
def physread(pa,n):
    gdb.execute('maint packet Qqemu.PhyMemMode:1',to_string=True)
    try: return bytes(gdb.selected_inferior().read_memory(pa,n))
    finally: gdb.execute('maint packet Qqemu.PhyMemMode:0',to_string=True)
def uwalk(va):
    ttbr=sysreg('TTBR0_EL1'); tcr=sysreg('TCR_EL1')
    t0sz=tcr&0x3f; tg0=(tcr>>14)&3
    gs={0:12,1:16,2:14}[tg0]; bits=64-t0sz
    stride=gs-3; lvl_bits=[]
    table=ttbr&0x0000fffffffffffe & ~((1<<gs)-1) if False else ttbr&0x0000ffffffffffc0
    # levels from top
    shift=gs+stride*((bits-gs-1)//stride)
    while True:
        idx=(va>>shift)&((1<<stride)-1) if shift+stride<=bits else (va>>shift)&((1<<(bits-shift))-1)
        d=struct.unpack('<Q',physread(table+8*idx,8))[0]
        if not d&1: return None,'invalid at shift %d desc %#x'%(shift,d)
        if shift==gs or not d&2:
            pa=(d&0x0000fffffffff000)&~((1<<shift)-1)
            return pa+(va&((1<<shift)-1)),'desc %#x'%d
        table=d&0x0000fffffffff000 & ~((1<<gs)-1)
        shift-=stride
def uread(va,n):
    pa,why=uwalk(va)
    if pa is None: raise Exception(why)
    return physread(pa,n)
def on_ua():
    ss=r('x0'); words=[m64(ss+8*i) for i in range(48)]
    RING.append((r('x1'),r('x2'),ss,words)); del RING[:-6]
    state.setdefault('ua',0); state['ua']+=1
    if state['ua']<=int(os.environ.get('MAXUA','0')):
        P('UA esr=%#x far=%#x'%(r('x1'),r('x2')))
    return False
BP('_exception_triage',on_triage); BP('_os_reason_create',on_reason)
BP('_proc_prepareexit',on_prepexit); BP('_map_with_linking_np',on_mwl); BP('_shared_region_map_and_slide_2_np',on_srmap)
BP('_handle_user_abort',on_ua); BP('_panic',on_panic)
extra=os.environ.get('EXTRA')
if extra: exec(open(extra).read())
gdb.execute('continue')
P('STOPPED done=%s'%state['done'])
# dump msgbuf
mb=m64(S['_msgbufp']); size=m32(mb+4); bufx=m32(mb+8); buf=m64(mb+16)
raw=bytes(gdb.selected_inferior().read_memory(buf,size))
open(OUT+'/msgbuf.txt','w').write((raw[bufx:]+raw[:bufx]).replace(b'\0',b'').decode(errors='replace'))
P('MSGBUF dumped')
gdb.execute('kill')
