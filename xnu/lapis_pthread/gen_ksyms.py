import re,sys
src=open('lapis_pthread.c').read()
need=sorted(set(re.findall(r'\bK_(\w+)',src))|set(re.findall(r'KF\((\w+)',src))-{'name'})
S={}
for l in open(sys.argv[1]):
    a,n=l.split(); S[n]=a
with open('ksyms.h','w') as f:
    for n in need:
        if n not in S: sys.exit('missing '+n)
        f.write('#define K_%s 0x%sUL\n'%(n,S[n]))
print(len(need),'syms')
