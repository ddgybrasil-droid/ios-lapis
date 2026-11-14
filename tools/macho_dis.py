import lief, sys, struct, capstone
p=sys.argv[1]; b=lief.parse(p); raw=open(p,'rb').read()
sec={s.name:s for s in b.sections}
got={}
try:
    for bi in b.dyld_chained_fixups.bindings: got[bi.address]=bi.symbol.name
except Exception as e: print('nofix',e)
def cstr(va):
    for s in b.sections:
        if s.virtual_address<=va<s.virtual_address+s.size:
            o=s.offset+va-s.virtual_address; e=raw.find(b'\0',o); return raw[o:e].decode('latin1')
    return None
# stub -> name
stubs={}
st=sec['__auth_stubs']; md=capstone.Cs(capstone.CS_ARCH_ARM64,capstone.CS_MODE_ARM)
ins=list(md.disasm(raw[st.offset:st.offset+st.size],st.virtual_address))
for i in range(0,len(ins)-1):
    if ins[i].mnemonic=='adrp' and ins[i+1].mnemonic=='ldr':
        page=ins[i].operands[1].imm; off=ins[i+1].operands[1].mem.disp
        stubs[ins[i].address]=got.get(page+off,'?%x'%(page+off))
t=sec["__text"]; md.detail=True; md.skipdata=True
regs={}
for i in md.disasm(raw[t.offset:t.offset+t.size],t.virtual_address):
    c=''
    if i.mnemonic=='adrp': regs[i.op_str.split(',')[0]]=i.operands[1].imm
    elif i.mnemonic=='add' and len(i.operands)==3 and i.operands[2].type==capstone.arm64.ARM64_OP_IMM:
        r=i.op_str.split(',')[1].strip()
        if r in regs:
            s=cstr(regs[r]+i.operands[2].imm)
            if s: c='"%s"'%s[:70]
    elif i.mnemonic in('bl','b') and i.operands[0].imm in stubs: c=stubs[i.operands[0].imm]
    print('%x\t%s\t%s\t%s'%(i.address,i.mnemonic,i.op_str,c))
