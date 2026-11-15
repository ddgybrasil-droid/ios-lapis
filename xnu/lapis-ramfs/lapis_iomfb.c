/*
 * lapis_iomfb: OSMetaClass + published IOService stubs for display/surface/HID.
 *
 * Metaclass-only registration (pth21) lets matching dictionaries name the class,
 * but IOMFB_FDR_Loader / backboardd need a live registry instance. We construct
 * plain IOService objects tagged with our metaclasses and registerService() them.
 * String literals stay in this TU so .rodata.str merges into the ramfs .text blob.
 *
 * IOLog varargs use the Apple stack ABI; avoid printf-style %s here (garbled on
 * gcc). Fixed messages only.
 */
#include <stdint.h>
#include <stddef.h>

extern void *IOMallocZero(size_t);
extern void IOLog(const char *, ...);

extern void *OSMetaClass_preModLoad(const char *) __asm__("_ZN11OSMetaClass10preModLoadEPKc");
extern int OSMetaClass_postModLoad(void *) __asm__("_ZN11OSMetaClass11postModLoadEPv");
extern void OSMetaClass_ctor(void *, const char *, const void *, unsigned int)
	__asm__("_ZN11OSMetaClassC2EPKcPKS_j");
extern unsigned int OSMetaClass_getClassSize(const void *)
	__asm__("_ZNK11OSMetaClass12getClassSizeEv");
extern const void *IOService_metaClass __asm__("_ZN9IOService9metaClassE");
extern const uint64_t IOService_MetaClass_vtable[] __asm__("_ZTVN9IOService9MetaClassE");

extern void *IOService_nw(unsigned long) __asm__("_ZN9IOServicenwEm");
extern void IOService_ctor_meta(void *, const void *) __asm__("_ZN9IOServiceC1EPK11OSMetaClass");
extern int IOService_init(void *, void *) __asm__("_ZN9IOService4initEP12OSDictionary");
extern int IOService_attach(void *, void *) __asm__("_ZN9IOService6attachEPS_");
extern void IOService_registerService(void *, unsigned int)
	__asm__("_ZN9IOService15registerServiceEj");
extern void *IOService_getServiceRoot(void) __asm__("_ZN9IOService14getServiceRootEv");
extern void IORegistryEntry_setName(void *, const char *, const void *)
	__asm__("_ZN15IORegistryEntry7setNameEPKcPK15IORegistryPlane");
extern void IORegistryEntry_setProperty_bool(void *, const char *, unsigned char)
	__asm__("_ZN15IORegistryEntry11setPropertyEPKcb");
extern void IORegistryEntry_setProperty_cstr(void *, const char *, const char *)
	__asm__("_ZN15IORegistryEntry11setPropertyEPKcS1_");
extern void IORegistryEntry_setProperty_obj(void *, const char *, void *)
	__asm__("_ZN15IORegistryEntry11setPropertyEPKcP8OSObject");
extern void *OSNumber_withNumber(unsigned long long, unsigned int)
	__asm__("_ZN8OSNumber10withNumberEyj");

static void set_u32(void *obj, const char *key, unsigned int val)
{
	void *n = OSNumber_withNumber(val, 32);
	if (n)
		IORegistryEntry_setProperty_obj(obj, key, n);
}

#define VT_ENTRIES 32
#define VT_ALLOC   23

struct lapis_mc {
	void *mc;
	uint64_t *vt;
	const char *name;
};

static void *null_alloc(const void *self) { (void)self; return 0; }

static int register_mc(struct lapis_mc *out, const char *name)
{
	uint64_t *vt;
	void *mc, *h;
	int r, i;

	vt = IOMallocZero(VT_ENTRIES * 8);
	mc = IOMallocZero(0x80);
	if (!vt || !mc) {
		IOLog("LAPIS iomfb: no memory for metaclass\n");
		return -1;
	}
	for (i = 0; i < VT_ENTRIES; i++)
		vt[i] = IOService_MetaClass_vtable[i];
	vt[VT_ALLOC] = (uint64_t)(uintptr_t)null_alloc;
	h = OSMetaClass_preModLoad("__kernel__");
	OSMetaClass_ctor(mc, name, IOService_metaClass,
			 OSMetaClass_getClassSize(IOService_metaClass));
	*(uint64_t **)mc = vt + 2;
	r = OSMetaClass_postModLoad(h);
	if (r) {
		IOLog("LAPIS iomfb: postModLoad failed\n");
		return -1;
	}
	out->mc = mc;
	out->vt = vt;
	out->name = name;
	return 0;
}

static void publish_one(struct lapis_mc *m, int hid_support)
{
	unsigned int sz;
	void *obj, *root;
	int ok;

	if (!m || !m->mc)
		return;
	sz = OSMetaClass_getClassSize(IOService_metaClass);
	obj = IOService_nw(sz);
	if (!obj) {
		IOLog("LAPIS iomfb: nw failed\n");
		return;
	}
	IOService_ctor_meta(obj, m->mc);
	ok = IOService_init(obj, 0);
	if (!ok) {
		IOLog("LAPIS iomfb: init failed\n");
		return;
	}
	IORegistryEntry_setName(obj, m->name, 0);
	if (hid_support)
		IORegistryEntry_setProperty_bool(obj, "HIDServiceSupport", 1);
	/* IOMFB clients often look for a named framebuffer resource. */
	if (m->name[0] == 'I' && m->name[1] == 'O' && m->name[2] == 'M') {
		IORegistryEntry_setProperty_cstr(obj, "IOFBShared", "lapis");
		/* iPad Pro M4 11" panel (iPad16,3 / TB520FU bring-up). */
		set_u32(obj, "IODisplayWidth", 2420);
		set_u32(obj, "IODisplayHeight", 1668);
		set_u32(obj, "IODisplayPixelsWide", 2420);
		set_u32(obj, "IODisplayPixelsHigh", 1668);
		set_u32(obj, "IODisplayFrequency", 120);
		set_u32(obj, "AppleMaxHorizontalPixels", 2420);
		set_u32(obj, "AppleMaxVerticalPixels", 1668);
	}

	root = IOService_getServiceRoot();
	if (root)
		IOService_attach(obj, root);
	IOService_registerService(obj, 0);
	IOLog("LAPIS iomfb: published instance\n");
}

void lapis_iomfb_register(void)
{
	struct lapis_mc iomfb, surface, hid, aks;

	if (register_mc(&iomfb, "IOMobileFramebuffer") == 0)
		IOLog("LAPIS iomfb: metaclass IOMobileFramebuffer ok\n");
	if (register_mc(&surface, "IOSurfaceRoot") == 0)
		IOLog("LAPIS iomfb: metaclass IOSurfaceRoot ok\n");
	if (register_mc(&hid, "IOHIDEventService") == 0)
		IOLog("LAPIS iomfb: metaclass IOHIDEventService ok\n");
	/* containermanagerd matches AppleKeyStore before creating system containers */
	if (register_mc(&aks, "AppleKeyStore") == 0)
		IOLog("LAPIS iomfb: metaclass AppleKeyStore ok\n");

	publish_one(&iomfb, 0);
	publish_one(&surface, 0);
	publish_one(&hid, 1);
	publish_one(&aks, 0);
}
