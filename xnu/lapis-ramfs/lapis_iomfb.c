/*
 * lapis_iomfb: built-in OSMetaClass stubs for display/surface/HID classes.
 * Same pattern as lapis_iomedia.c (string literals kept in this TU so the
 * .rodata.str merge into the ramfs .text blob stays self-contained).
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

#define VT_ENTRIES 32
#define VT_ALLOC   23

static void *null_alloc(const void *self) { (void)self; return 0; }

static void register_one(const char *name)
{
	uint64_t *vt;
	void *mc, *h;
	int r, i;
	vt = IOMallocZero(VT_ENTRIES * 8);
	mc = IOMallocZero(0x80);
	if (!vt || !mc) { IOLog("LAPIS iomfb: no memory\n"); return; }
	for (i = 0; i < VT_ENTRIES; i++) vt[i] = IOService_MetaClass_vtable[i];
	vt[VT_ALLOC] = (uint64_t)(uintptr_t)null_alloc;
	h = OSMetaClass_preModLoad("__kernel__");
	OSMetaClass_ctor(mc, name, IOService_metaClass, OSMetaClass_getClassSize(IOService_metaClass));
	*(uint64_t **)mc = vt + 2;
	r = OSMetaClass_postModLoad(h);
	if (r) IOLog("LAPIS iomfb: postModLoad failed\n");
	else IOLog("LAPIS iomfb: registered built-in %s metaclass\n", name);
}

void lapis_iomfb_register(void)
{
	register_one("IOMobileFramebuffer");
	register_one("IOSurfaceRoot");
	register_one("IOHIDEventService");
}
