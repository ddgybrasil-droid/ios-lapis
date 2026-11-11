#!/usr/bin/env python3
"""Static name-only audit of kext imports against a kernel symbol list and dependency metadata.
Not a linker, loader, ABI test, signature verifier, or permission to execute code.

LC_SYMTAB symoff/stroff values are file offsets in the kernelcache container.
Target lines are "address name" (ksyms.txt) or nm "address type name".
"""
import argparse, hashlib, json, struct
from pathlib import Path
import kc_inventory

ROOTS=('com.apple.security.AppleImage4','com.apple.driver.AppleMobileFileIntegrity')
N_STAB=0xe0; N_PEXT=0x10; N_TYPE=0x0e; N_EXT=0x01; N_UNDF=0; N_ABS=2; N_SECT=0x0e

def symbols(data, offset):
    n=len(data); kc_inventory.region(offset,32,n,'Mach-O header')
    hdr=struct.unpack_from('<8I',data,offset); kc_inventory.need(hdr[0]==0xfeedfacf,'invalid Mach-O header')
    cmd_off=offset+32; end=cmd_off+hdr[5]; kc_inventory.region(cmd_off,hdr[5],n,'Mach-O commands')
    sym=None
    for _ in range(hdr[4]):
        kc_inventory.region(cmd_off,8,end,'load command'); cmd,size=struct.unpack_from('<II',data,cmd_off)
        kc_inventory.need(size>=8 and size%8==0,'invalid command size'); kc_inventory.region(cmd_off,size,end,'load command')
        if cmd==2:
            kc_inventory.need(size>=24 and sym is None,'invalid/duplicate LC_SYMTAB'); sym=struct.unpack_from('<4I',data,cmd_off+8)
        cmd_off+=size
    kc_inventory.need(cmd_off==end and sym is not None,'missing symbol table or incorrect commands')
    so,num,st,sz=sym; kc_inventory.need(num<=n//16,'excess symbol count'); kc_inventory.region(so,num*16,n,'symbols');kc_inventory.region(st,sz,n,'string table')
    imports=set(); exports=set()
    for i in range(num):
        si,t,sect,desc,val=struct.unpack_from('<IBBHQ',data,so+16*i)
        if not si or t&N_STAB or not t&N_EXT: continue
        kc_inventory.need(si<sz,'symbol string out of bounds'); name=kc_inventory.cstr(data,st+si,st+sz,'symbol')
        typ=t&N_TYPE
        if typ==N_UNDF: imports.add(name)
        elif typ in (N_ABS,N_SECT) and not t&N_PEXT: exports.add(name)
    return imports,exports

def audit(data, target_names):
    inventory=kc_inventory.inspect(data)
    off=32; entries={}
    for _ in range(struct.unpack_from('<I',data,16)[0]):
        cmd,size=struct.unpack_from('<II',data,off)
        if cmd==kc_inventory.LC_FILESET_ENTRY:
            vm,fo,nameoff,_=struct.unpack_from('<QQII',data,off+8)
            name=kc_inventory.cstr(data,off+nameoff,off+size,'entry name'); entries[name]=fo
        off+=size
    required=set(ROOTS)
    for root in ROOTS:required.update(inventory['components'][root]['transitive_executables'])
    tables={name:symbols(data,entries[name]) for name in sorted(required)}
    result={}
    for root in ROOTS:
        imports,_=tables[root]; deps=set(inventory['components'][root]['transitive_executables'])-{root}
        providers={}
        for dep in sorted(deps):
            for name in tables[dep][1]: providers.setdefault(name,[]).append(dep)
        missing=sorted(imports-target_names-providers.keys()); target=imports&target_names; via=imports&providers.keys()
        result[root]={'undefined_unique':len(imports),'in_target_symbol_names':len(target),'in_dependency_external_definitions':len(via),'in_either':len(target|via),'not_found_in_either':len(missing),'not_found_names':missing,'dependency_providers_for_imports':{n:providers[n] for n in sorted(via)},'dependencies_scanned':sorted(deps),'missing_metadata':inventory['components'][root]['missing_metadata']}
    return {'kernel_version_string':inventory['kernel_version_string'],'components':result,'limitations':'Names only. LC_SYMTAB file offsets are kernelcache file offsets. Target lines are "address name" or nm "address type name". The target list may include non-exports or omit symbols. N_PEXT and N_INDR are not providers. Dependency definitions are not verified usable exports. missing_metadata entries were not scanned. No resolution, ABI, PAC, linkage, registration, authentication or execution was performed.'}

def target_names(text):
    names=set()
    for line in text.splitlines():
        line=line.strip()
        if not line or line.startswith('#'): continue
        parts=line.split()
        if len(parts)>=3 and len(parts[1])==1 and parts[1].isalpha(): names.add(parts[2])
        elif len(parts)>=2: names.add(parts[1])
    return names

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('kernelcache');p.add_argument('target_symbol_table');a=p.parse_args()
    data=Path(a.kernelcache).read_bytes(); target=Path(a.target_symbol_table).read_bytes(); names=target_names(target.decode())
    out=audit(data,names);out['kernelcache_sha256']=hashlib.sha256(data).hexdigest();out['target_symbol_table_sha256']=hashlib.sha256(target).hexdigest();print(json.dumps(out,indent=2,sort_keys=True))
if __name__=='__main__':main()
