SPAWN={'n':0}
def on_spawn():
    a=r('x1'); path=m64(a+8)
    SPAWN['n']+=1
    if SPAWN['n']<=300: P('SPAWN #%d path=%r'%(SPAWN['n'],ustr(path,256)))
    return False
BP('_posix_spawn',on_spawn)
