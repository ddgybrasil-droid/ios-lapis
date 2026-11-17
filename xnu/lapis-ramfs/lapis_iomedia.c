/*
 * lapis_iomedia: minimal built-in "IOMedia" OSMetaClass for the lapis kernel.
 * IOMedia normally comes from IOStorageFamily.kext, which this OSS-built kernel does not
 * have (and Apple's kernel-collection kexts cannot be linked against it). IOService still
 * uses the class: publishHiddenMedia() does assert(OSMetaClass::getMetaClassWithName("IOMedia"))
 * (reached via kernelmanagerd -> OSKext::loadFileSetKexts -> iokitDaemonLaunched -> adjustBusy),
 * and IOKitBSDInit / IOPolledInterface match on "IOMedia".
 * The class is registered exactly like built-in kernel classes: OSMetaClass::preModLoad
 * ("__kernel__") -> OSMetaClass::OSMetaClass("IOMedia", IOService::metaClass, size) ->
 * OSMetaClass::postModLoad(). Its vtable is IOService::MetaClass's with alloc() returning
 * NULL: there is no storage driver to back an IOMedia instance, so none can be created;
 * applyToInstances()/serviceMatching() see zero instances.
 */
#include <stdint.h>
#include <stddef.h>
extern void *IOMallocZero(size_t);
extern void IOLog(const char *, ...);
extern void *OSMetaClass_preModLoad(const char *) __asm__("_ZN11OSMetaClass10preModLoadEPKc");
extern int OSMetaClass_postModLoad(void *) __asm__("_ZN11OSMetaClass11postModLoadEPv");
extern void OSMetaClass_ctor(void *, const char *, const void *, unsigned int) __asm__("_ZN11OSMetaClassC2EPKcPKS_j");
extern unsigned int OSMetaClass_getClassSize(const void *) __asm__("_ZNK11OSMetaClass12getClassSizeEv");
extern const void *IOService_metaClass __asm__("_ZN9IOService9metaClassE");
extern const uint64_t IOService_MetaClass_vtable[] __asm__("_ZTVN9IOService9MetaClassE");

#define VT_ENTRIES 32          /* _ZTVN9IOService9MetaClassE: offset-to-top, RTTI, 30 virtuals */
#define VT_ALLOC   23          /* entry 23 = IOService::MetaClass::alloc() (verified in image) */

static void *iomedia_alloc(const void *self) { (void)self; return 0; }

void lapis_iomedia_register(void)
{
	uint64_t *vt;
	void *mc, *h;
	int r, i;
	vt = IOMallocZero(VT_ENTRIES * 8);
	mc = IOMallocZero(0x80);   /* >= sizeof(OSMetaClass) */
	if (!vt || !mc) { IOLog("LAPIS iomedia: no memory\n"); return; }
	for (i = 0; i < VT_ENTRIES; i++) vt[i] = IOService_MetaClass_vtable[i];
	vt[VT_ALLOC] = (uint64_t)(uintptr_t)iomedia_alloc;
	h = OSMetaClass_preModLoad("__kernel__");
	OSMetaClass_ctor(mc, "IOMedia", IOService_metaClass, OSMetaClass_getClassSize(IOService_metaClass));
	*(uint64_t **)mc = vt + 2;  /* derived-class vtable, as the compiler-generated ctor does */
	r = OSMetaClass_postModLoad(h);
	if (r) IOLog("LAPIS iomedia: postModLoad failed\n");
	else IOLog("LAPIS iomedia: registered built-in IOMedia metaclass\n");
}
