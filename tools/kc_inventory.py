#!/usr/bin/env python3
"""Read-only MH_FILESET inventory; this is not a kext linker or loader."""
import argparse, hashlib, json, mmap, plistlib, re, struct

LC_SEGMENT_64=0x19
LC_FILESET_ENTRY=0x80000035
MH_MAGIC_64=0xfeedfacf
MH_FILESET=0xc
ROOTS=('com.apple.security.AppleImage4','com.apple.driver.AppleMobileFileIntegrity',
       'com.apple.kec.pthread','com.apple.kec.corecrypto')
MAX_INFO=16*1024*1024

def need(ok,msg):
    if not ok: raise ValueError(msg)
def region(off,size,limit,what):
    need(off>=0 and size>=0 and off<=limit and size<=limit-off,what+' out of bounds')
def cstr(data,start,end,what):
    need(start<end,what+' missing')
    i=data.find(b'\0',start,end);need(i>=0,what+' not terminated')
    try:return bytes(data[start:i]).decode('ascii')
    except UnicodeDecodeError:raise ValueError(what+' not ASCII') from None

def inspect(data,roots=ROOTS):
    n=len(data);region(0,32,n,'Mach-O header')
    magic,cpu,sub,typ,ncmds,cmdbytes,flags,_=struct.unpack_from('<8I',data,0)
    need(magic==MH_MAGIC_64 and typ==MH_FILESET,'expected 64-bit MH_FILESET')
    need(cpu==0x100000c,'expected ARM64')
    region(32,cmdbytes,n,'load commands');need(ncmds <= cmdbytes//8,'invalid load command count')
    entries={};segments=[];info=None;off=32;end=32+cmdbytes
    for _ in range(ncmds):
        region(off,8,end,'load command')
        cmd,size=struct.unpack_from('<II',data,off)
        need(size>=8 and size%8==0,'invalid load command size');region(off,size,end,'load command')
        if cmd==LC_SEGMENT_64:
            need(size>=72,'truncated segment')
            name=cstr(data,off+8,off+24,'segment name')
            vm,vmsize,fo,fs,_,_,ns,_=struct.unpack_from('<4Q4I',data,off+24)
            need(ns <= (size-72)//80,'truncated section table');region(fo,fs,n,'segment')
            need(fs <= vmsize and vm+vmsize <= 1<<64,'invalid segment VM bounds')
            segments.append((vm,vmsize,fo,fs,name))
            for j in range(ns):
                so=off+72+80*j;sn=cstr(data,so,so+16,'section name')
                sv,ss,sfile=struct.unpack_from('<QQI',data,so+32)
                if name=='__PRELINK_INFO' and sn=='__info':
                    need(info is None and ss<=MAX_INFO,'duplicate/oversized prelink info')
                    region(sfile,ss,n,'prelink info')
                    need(sfile>=fo and sfile-fo<=fs and ss<=fs-(sfile-fo),'prelink info outside segment')
                    need(sv>=vm and sv-vm<=vmsize and ss<=vmsize-(sv-vm),'prelink info outside VM segment')
                    info=bytes(data[sfile:sfile+ss]).rstrip(b'\0')
        elif cmd==LC_FILESET_ENTRY:
            need(size>=32,'truncated fileset entry')
            vm,fo,nameoff,_=struct.unpack_from('<QQII',data,off+8)
            need(nameoff>=32 and nameoff<size,'invalid fileset entry name offset')
            name=cstr(data,off+nameoff,off+size,'fileset entry name')
            need(name and name not in entries,'empty/duplicate fileset entry')
            region(fo,32,n,'fileset Mach-O');need(struct.unpack_from('<I',data,fo)[0]==MH_MAGIC_64,'invalid fileset Mach-O')
            entries[name]={'vmaddr':vm,'file_offset':fo}
        off+=size
    need(off==end,'load commands size mismatch');need(info is not None,'missing prelink info')
    # The entry header must occur at the mapped file/VM position, not merely at a plausible offset.
    for name,e in entries.items():
        valid=any(e['vmaddr']>=vm and e['vmaddr']-vm<fs and fo+(e['vmaddr']-vm)==e['file_offset']
                  for vm,vms,fo,fs,_ in segments)
        need(valid,'fileset entry outside mapped segments: '+name)
    try:plist=plistlib.loads(info)
    except Exception as e:raise ValueError('invalid prelink plist') from e
    need(isinstance(plist,dict) and isinstance(plist.get('_PrelinkInfoDictionary'),list),'invalid prelink dictionary')
    metadata={}
    for item in plist['_PrelinkInfoDictionary']:
        need(isinstance(item,dict),'invalid metadata entry')
        name=item.get('CFBundleIdentifier')
        if name is None: continue
        need(isinstance(name,str) and name not in metadata,'duplicate/invalid bundle identifier')
        deps=item.get('OSBundleLibraries',{})
        need(isinstance(deps,dict) and all(isinstance(k,str) and isinstance(v,str) for k,v in deps.items()),'invalid dependency dictionary')
        metadata[name]=item
    report={}
    for root in roots:
        need(root in entries and root in metadata,'missing required executable: '+root)
        seen=set();todo=[root];missing=set()
        while todo:
            name=todo.pop()
            if name in seen:continue
            seen.add(name)
            if name not in metadata:missing.add(name);continue
            todo.extend(metadata[name].get('OSBundleLibraries',{}))
        report[root]={'direct_dependencies':metadata[root].get('OSBundleLibraries',{}),
                      'transitive_executables':sorted(seen & entries.keys()),
                      'missing_metadata':sorted(missing)}
    match=re.search(rb'Darwin Kernel Version [^\x00\n]{1,400}',data)
    version=match.group().decode('ascii','replace') if match else None
    return {'cpu_subtype':sub,'mach_flags':flags,'entry_count':len(entries),
            'metadata_count':len(metadata),'kernel_version_string':version,
            'components':report,'warning':'Inventory only: no ABI compatibility, fixup, initialization or security verification.'}

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('kernelcache',help='decompressed MH_FILESET');p.add_argument('--expect-xnu',help='require version string to contain this exact xnu version')
    a=p.parse_args()
    with open(a.kernelcache,'rb') as f:
        with mmap.mmap(f.fileno(),0,access=mmap.ACCESS_READ) as data:
            report=inspect(data)
            report['sha256']=hashlib.sha256(data).hexdigest()
    if a.expect_xnu:
        version=report['kernel_version_string'] or ''
        need(re.search(r'(?<![\w.])'+re.escape(a.expect_xnu)+r'(?![\w.])',version) is not None,
             'kernel version mismatch; no compatibility assumed')
    print(json.dumps(report,indent=2,sort_keys=True))
if __name__=='__main__':main()
