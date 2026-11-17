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
