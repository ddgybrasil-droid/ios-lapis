/*
 * lapis_iomfb: OSMetaClass + published IOService stubs for display/surface/HID.
 *
 * Metaclass-only registration (pth21) lets matching dictionaries name the class,
 * but IOMFB_FDR_Loader / backboardd need a live registry instance. We construct
 * plain IOService objects tagged with our metaclasses and registerService() them.
 * String literals stay in this TU so .rodata.str merges into the ramfs .text blob.
 *
 * Clients then IOServiceOpen → IOService::newUserClient. Stock IOService returns
 * kIOReturnUnsupported for the common overloads. arm64e vtables are PAC-signed, so
 * we do NOT rewrite vtable slots. Image build statically B-patches the
 * IOService::newUserClient entry points (see tools/patch_kernel_ramfs_replace.py)
 * onto lapis_new_user_client{,_simple}, which allocate a real IOUserClient.
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
extern int IOService_start(void *, void *) __asm__("_ZN9IOService5startEPS_");
extern void IOService_registerService(void *, unsigned int)
	__asm__("_ZN9IOService15registerServiceEj");
extern void *IOService_getServiceRoot(void) __asm__("_ZN9IOService14getServiceRootEv");
extern void *IOService_getResourceService(void)
	__asm__("_ZN9IOService18getResourceServiceEv");
extern void IOService_publishResource(const char *, void *)
	__asm__("_ZN9IOService15publishResourceEPKcP8OSObject");
extern void *IOService_serviceMatching(const char *, void *)
	__asm__("_ZN9IOService15serviceMatchingEPKcP12OSDictionary");
extern void *IOService_nameMatching(const char *, void *)
	__asm__("_ZN9IOService12nameMatchingEPKcP12OSDictionary");
extern void *IOService_resourceMatching(const char *, void *)
	__asm__("_ZN9IOService16resourceMatchingEPKcP12OSDictionary");
extern void *IOService_copyMatchingService(void *)
	__asm__("_ZN9IOService19copyMatchingServiceEP12OSDictionary");
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
extern void *OSDictionary_getObject_cstr(void *, const char *)
	__asm__("_ZNK12OSDictionary9getObjectEPKc");
extern const char *OSString_getCStringNoCopy(void *)
	__asm__("_ZNK8OSString16getCStringNoCopyEv");
extern void *IORegistryEntry_copyProperty(void *, const char *)
	__asm__("_ZNK15IORegistryEntry12copyPropertyEPKc");
extern void *OSArray_withCapacity(unsigned int)
	__asm__("_ZN7OSArray12withCapacityEj");
extern int OSArray_setObject(void *, const void *)
	__asm__("_ZN7OSArray9setObjectEPK15OSMetaClassBase");
extern void *OSCollectionIterator_withCollection(const void *)
	__asm__("_ZN20OSCollectionIterator14withCollectionEPK12OSCollection");
extern void OSObject_release(void *) __asm__("_ZNK8OSObject7releaseEv");
extern void *IOBufferMemoryDescriptor_withCapacity(unsigned long, unsigned int, unsigned char)
	__asm__("_ZN24IOBufferMemoryDescriptor12withCapacityEmjb");

/*
 * Trampolines overwritten by patch_kernel_ramfs_replace.py:
 *   <saved prologue stp>; b orig+4
 */
void *lapis_copyMatchingService_orig(void *dict);
void *lapis_getMatchingServices_orig(void *dict);
__asm__(
	".text\n"
	".align 2\n"
	".global lapis_copyMatchingService_orig\n"
	".type lapis_copyMatchingService_orig, %function\n"
	"lapis_copyMatchingService_orig:\n"
	"\tnop\n"
	"\tnop\n"
	"\tret\n"
	".global lapis_getMatchingServices_orig\n"
	".type lapis_getMatchingServices_orig, %function\n"
	"lapis_getMatchingServices_orig:\n"
	"\tnop\n"
	"\tnop\n"
	"\tret\n"
);

extern const void *IOUserClient_metaClass __asm__("_ZN12IOUserClient9metaClassE");
extern void *IOUserClient_nw(unsigned long) __asm__("_ZN12IOUserClientnwEm");
extern void IOUserClient_ctor_meta(void *, const void *)
	__asm__("_ZN12IOUserClientC1EPK11OSMetaClass");
extern int IOUserClient_initWithTask_dict(void *, void *, void *, unsigned int, void *)
	__asm__("_ZN12IOUserClient12initWithTaskEP4taskPvjP12OSDictionary");
extern int IOUserClient_initWithTask(void *, void *, void *, unsigned int)
	__asm__("_ZN12IOUserClient12initWithTaskEP4taskPvj");

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

#define kIOReturnNoMemory     0xe00002bd
#define kIOReturnBadArgument  0xe00002c2
#define kIOReturnUnsupported  0xe00002c7

static int streq(const char *a, const char *b)
{
	if (!a || !b)
		return 0;
	while (*a && *a == *b) {
		a++;
		b++;
	}
	return *a == *b;
}

static int is_lapis_match_name(const char *name)
{
	return streq(name, "IOGPU") || streq(name, "IOGPUDevice") ||
	       streq(name, "IOMobileFramebuffer") || streq(name, "IOSurfaceRoot") ||
	       streq(name, "IOCoreSurfaceRoot") ||
	       streq(name, "IOHIDEventService") || streq(name, "AppleKeyStore") ||
	       streq(name, "AppleCLCD") || streq(name, "AGXAccelerator") ||
	       streq(name, "IOAccelerator");
}

/*
 * boot90/91: serviceMatching/nameMatching fail (hand-rolled metaclass metaCast).
 * resourceMatching finds IOResources (the publisher), NOT our stub — so
 * IOServiceOpen would hit IOResources::newUserClient (always Unsupported).
 * Return the publishResource object via copyProperty so open hits our NUC.
 * Image B-patches IOService::copyMatchingService onto this.
 */
static void *lapis_published_stub(const char *name)
{
	void *root, *s;

	if (!is_lapis_match_name(name))
		return 0;
	root = IOService_getResourceService();
	if (!root)
		return 0;
	s = IORegistryEntry_copyProperty(root, name);
	/* boot93: runningboardd name-matches IOCoreSurfaceRoot after IOGPU. */
	if (!s && streq(name, "IOCoreSurfaceRoot"))
		s = IORegistryEntry_copyProperty(root, "IOSurfaceRoot");
	if (s)
		IOLog("LAPIS iomfb: match via resource\n");
	return s;
}

static const char *lapis_match_dict_name(void *dict)
{
	void *val;

	if (!dict)
		return 0;
	val = OSDictionary_getObject_cstr(dict, "IOProviderClass");
	if (!val)
		val = OSDictionary_getObject_cstr(dict, "IONameMatch");
	if (!val)
		val = OSDictionary_getObject_cstr(dict, "IOResourceMatch");
	if (!val)
		return 0;
	return OSString_getCStringNoCopy(val);
}

void *lapis_copyMatchingService(void *dict)
{
	void *s;
	const char *name;

	if (!dict)
		return lapis_copyMatchingService_orig(dict);

	name = lapis_match_dict_name(dict);
	if (name) {
		s = lapis_published_stub(name);
		if (s)
			return s;
	}
	return lapis_copyMatchingService_orig(dict);
}

/*
 * boot93: getMatchingServices (plural) does not call copyMatchingService — it
 * walks the registry (metaCast/name miss). Return a one-shot iterator over our
 * published stub so IONameMatch("IOCoreSurfaceRoot") etc. succeed.
 */
void *lapis_getMatchingServices(void *dict)
{
	void *s, *arr, *it;
	const char *name;

	if (!dict)
		return lapis_getMatchingServices_orig(dict);

	name = lapis_match_dict_name(dict);
	if (name) {
		s = lapis_published_stub(name);
		if (s) {
			arr = OSArray_withCapacity(1);
			if (arr && OSArray_setObject(arr, s)) {
				it = OSCollectionIterator_withCollection(arr);
				OSObject_release(s);
				OSObject_release(arr);
				if (it)
					return it;
			} else {
				OSObject_release(s);
				if (arr)
					OSObject_release(arr);
			}
		}
	}
	return lapis_getMatchingServices_orig(dict);
}

/*
 * ABI matches IOService::newUserClient(task*, void*, uint32, OSDictionary*, IOUserClient**).
 * x0=self, x1=task, x2=security_id, w3=type, x4=properties, x5=out
 * Exported: Image build B-patches stock IOService::newUserClient onto these.
 *
 * NOTE: the ramfs blob lives in RX __TEXT_EXEC — no writable .bss statics.
 * Do not gate on a pointer list stored in this TU; serve any stock open.
 */
int lapis_new_user_client(void *self, void *task, void *sec,
			  unsigned int type, void *props, void **out)
{
	unsigned int sz;
	void *client;
	int ok;

	if (!out)
		return kIOReturnBadArgument;
	*out = 0;

	sz = OSMetaClass_getClassSize(IOUserClient_metaClass);
	client = IOUserClient_nw(sz);
	if (!client) {
		IOLog("LAPIS iomfb: nuc no mem\n");
		return kIOReturnNoMemory;
	}
	IOUserClient_ctor_meta(client, IOUserClient_metaClass);
	if (props)
		ok = IOUserClient_initWithTask_dict(client, task, sec, type, props);
	else
		ok = IOUserClient_initWithTask(client, task, sec, type);
	if (!ok) {
		IOLog("LAPIS iomfb: nuc initWithTask fail\n");
		return kIOReturnUnsupported;
	}
	if (!IOService_attach(client, self)) {
		IOLog("LAPIS iomfb: nuc attach fail\n");
		return kIOReturnUnsupported;
	}
	if (!IOService_start(client, self)) {
		IOLog("LAPIS iomfb: nuc start fail\n");
		return kIOReturnUnsupported;
	}
	*out = client;
	IOLog("LAPIS iomfb: nuc ok\n");
	return 0;
}

/* ABI: newUserClient(task*, void*, uint32, IOUserClient**) — forward with props=NULL. */
int lapis_new_user_client_simple(void *self, void *task, void *sec,
				 unsigned int type, void **out)
{
	return lapis_new_user_client(self, task, sec, type, 0, out);
}

/*
 * boot94: after IOCoreSurfaceRoot open, clients need clientMemoryForType
 * (shared surface root mapping). Stock IOUserClient returns Unsupported.
 * Image B-patches IOUserClient::clientMemoryForType onto this.
 */
int lapis_client_memory_for_type(void *self, unsigned int type,
				 unsigned int *options, void **memory)
{
	void *md;

	(void)self;
	(void)type;
	if (!memory)
		return kIOReturnBadArgument;
	*memory = 0;
	if (options)
		*options = 0;
	/* 64 KiB RW buffer — enough for IOSurfaceRoot header probes. */
	md = IOBufferMemoryDescriptor_withCapacity(0x10000, 3, 0);
	if (!md) {
		IOLog("LAPIS iomfb: cmap no mem\n");
		return kIOReturnNoMemory;
	}
	*memory = md;
	IOLog("LAPIS iomfb: cmap ok\n");
	return 0;
}

/*
 * boot95/98: seed structure outputs from real IOGPU/IOSurface userspace parsers.
 * IOExternalMethodArguments (arm64): structureOutput @88, size @96.
 *
 * IOGPUDeviceCreateWithOptions (iPadOS 26 / 23F77):
 *   sel=2 stout=0x218: event_machine — u64 base@0, u32 count@0x14, u32 offs@0x18.
 *     count < 2 takes the no-malloc path (safe for stubs).
 *   sel=0 stout=0x40: IOGPUDeviceConfigData = IIQQIIQQQII (64 bytes).
 *   sel=4 stout=0x20: trace filter blob.
 *
 * IOSurface ____ioSurfaceConnectInternal (sel=13 stout=0x28 → _ioSurfaceLimits):
 *   success requires *(uint16_t *)(limits + 0x22) == 1; otherwise the connect
 *   falls through to IOServiceClose and IOSurface stays unusable.
 */
int lapis_external_method(void *self, unsigned int selector, void *args,
			  void *dispatch, void *target, void *reference)
{
	uint64_t *scalar_out;
	uint32_t n, i, sz;
	unsigned char *p;
	uint32_t *u32;
	uint16_t *u16;
	uint64_t *u64;

	(void)self;
	(void)dispatch;
	(void)target;
	(void)reference;
	if (!args)
		return 0;
	scalar_out = *(uint64_t **)((char *)args + 72);
	n = *(uint32_t *)((char *)args + 80);
	if (n > 16)
		n = 16;
	if (scalar_out) {
		for (i = 0; i < n; i++)
			scalar_out[i] = 0;
		/* Prefer "yes" for single-scalar probes. */
		if (n >= 1)
			scalar_out[0] = 1;
	}
	*(uint32_t *)((char *)args + 80) = n;
	p = *(unsigned char **)((char *)args + 88);
	sz = *(uint32_t *)((char *)args + 96);
	if (p && sz) {
		if (sz > 0x1000)
			sz = 0x1000;
		for (i = 0; i < sz; i++)
			p[i] = 0;
		u32 = (uint32_t *)p;
		u16 = (uint16_t *)p;
		u64 = (uint64_t *)p;

		/* IOGPU sel=2 event_machine (536): count<2 avoids offset table walk. */
		if (selector == 2 && sz >= 0x18) {
			u64[0] = 0; /* base */
			u32[5] = 0; /* count @0x14 */
		}
		/* IOGPU sel=0 config (64): IOGPUDeviceConfigData IIQQIIQQQII. */
		else if (selector == 0 && sz >= 64) {
			u32[0] = 1;		/* version-ish */
			u32[1] = 1;		/* flags / present */
			u64[1] = 0x10000000ULL; /* mem-ish */
			u64[2] = 0x10000000ULL;
			u32[6] = 1;
			u32[7] = 1;
			u64[4] = 0x4000;
			u64[5] = 0x4000;
			u64[6] = 1;
			u32[14] = 1;
			u32[15] = 1;
		}
		/* IOGPU sel=4 trace filter (32): non-zero present marker. */
		else if (selector == 4 && sz >= 32) {
			u64[0] = 1;
			u32[6] = 0; /* keep [+0x18] path gated */
		}
		/* IOSurface sel=13 limits (40): halfword @0x22 MUST be 1. */
		else if (selector == 13 && sz >= 0x24) {
			u32[0] = 0x10000; /* generous max surfaces */
			u32[1] = 0x10000;
			u32[2] = 0x40000000; /* byte budget */
			u16[0x22 / 2] = 1; /* connect-success gate */
		} else {
			/* Generic presence header for other selectors. */
			if (sz >= 4)
				u32[0] = 1;
			if (sz >= 8)
				u32[1] = selector;
			if (sz >= 12)
				u32[2] = sz;
			if (sz >= 16)
				u32[3] = 1;
		}
		*(uint32_t *)((char *)args + 96) = sz;
	}
	return 0;
}

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

	/* GPU / Metal clients probe these on matched accelerator nodes. */
	if ((m->name[0] == 'I' && m->name[1] == 'O' && m->name[2] == 'G') ||
	    (m->name[0] == 'A' && m->name[1] == 'G' && m->name[2] == 'X') ||
	    (m->name[0] == 'I' && m->name[1] == 'O' && m->name[2] == 'A' &&
	     m->name[3] == 'c')) {
		IORegistryEntry_setProperty_bool(obj, "MetalDevice", 1);
		IORegistryEntry_setProperty_cstr(obj, "MetalPluginName", "IOGPUMetal");
		set_u32(obj, "IOGPUMetalCaps", 1);
		IORegistryEntry_setProperty_cstr(obj, "IOClass", m->name);
	}

	/*
	 * boot88: getMatchingService(IOGPU) returned kIOReturnOffline (0xe00002f0).
	 * Stubs were attached to the service root without start() — IOKit leaves
	 * them offline for matching. Attach to IOResources, start(), then
	 * registerService + publishResource so IOServiceMatching can find them.
	 */
	root = IOService_getResourceService();
	if (!root)
		root = IOService_getServiceRoot();
	if (!root) {
		IOLog("LAPIS iomfb: no provider root\n");
		return;
	}
	if (!IOService_attach(obj, root)) {
		IOLog("LAPIS iomfb: attach fail\n");
		return;
	}
	if (!IOService_start(obj, root)) {
		IOLog("LAPIS iomfb: start fail\n");
		/* still try to publish — some classes start()=false but remain usable */
	}
	IOService_registerService(obj, 0);
	IOService_publishResource(m->name, obj);
	IOLog("LAPIS iomfb: published instance\n");
}

void lapis_iomfb_register(void)
{
	struct lapis_mc iomfb, surface, coresurf, hid, aks, gpu, gpudev, clcd, agx, accel;

	if (register_mc(&iomfb, "IOMobileFramebuffer") == 0)
		IOLog("LAPIS iomfb: metaclass IOMobileFramebuffer ok\n");
	if (register_mc(&surface, "IOSurfaceRoot") == 0)
		IOLog("LAPIS iomfb: metaclass IOSurfaceRoot ok\n");
	/* runningboardd IONameMatch IOCoreSurfaceRoot after IOGPU (boot93). */
	if (register_mc(&coresurf, "IOCoreSurfaceRoot") == 0)
		IOLog("LAPIS iomfb: metaclass IOCoreSurfaceRoot ok\n");
	if (register_mc(&hid, "IOHIDEventService") == 0)
		IOLog("LAPIS iomfb: metaclass IOHIDEventService ok\n");
	/* containermanagerd matches AppleKeyStore before creating system containers */
	if (register_mc(&aks, "AppleKeyStore") == 0)
		IOLog("LAPIS iomfb: metaclass AppleKeyStore ok\n");
	/* runningboardd matches IOGPU before advertising com.apple.runningboard */
	if (register_mc(&gpu, "IOGPU") == 0)
		IOLog("LAPIS iomfb: metaclass IOGPU ok\n");
	/* Userspace Metal / IOGPU.framework often opens IOGPUDevice, not IOGPU. */
	if (register_mc(&gpudev, "IOGPUDevice") == 0)
		IOLog("LAPIS iomfb: metaclass IOGPUDevice ok\n");
	/* display pipeline probes AppleCLCD / IODisplayWrangler-class names */
	if (register_mc(&clcd, "AppleCLCD") == 0)
		IOLog("LAPIS iomfb: metaclass AppleCLCD ok\n");
	/* Some clients match AGX / IOAccelerator family names instead of IOGPU. */
	if (register_mc(&agx, "AGXAccelerator") == 0)
		IOLog("LAPIS iomfb: metaclass AGXAccelerator ok\n");
	if (register_mc(&accel, "IOAccelerator") == 0)
		IOLog("LAPIS iomfb: metaclass IOAccelerator ok\n");

	publish_one(&iomfb, 0);
	publish_one(&surface, 0);
	publish_one(&coresurf, 0);
	publish_one(&hid, 1);
	publish_one(&aks, 0);
	publish_one(&gpu, 0);
	publish_one(&gpudev, 0);
	publish_one(&clcd, 0);
	publish_one(&agx, 0);
	publish_one(&accel, 0);

	/* Self-test: which matching path can see our IOGPU stub? */
	{
		void *d, *s;
		d = IOService_serviceMatching("IOGPU", 0);
		s = d ? IOService_copyMatchingService(d) : 0;
		IOLog(s ? "LAPIS iomfb: self serviceMatching IOGPU ok\n"
			: "LAPIS iomfb: self serviceMatching IOGPU FAIL\n");
		d = IOService_nameMatching("IOGPU", 0);
		s = d ? IOService_copyMatchingService(d) : 0;
		IOLog(s ? "LAPIS iomfb: self nameMatching IOGPU ok\n"
			: "LAPIS iomfb: self nameMatching IOGPU FAIL\n");
		d = IOService_resourceMatching("IOGPU", 0);
		s = d ? IOService_copyMatchingService(d) : 0;
		IOLog(s ? "LAPIS iomfb: self resourceMatching IOGPU ok\n"
			: "LAPIS iomfb: self resourceMatching IOGPU FAIL\n");
	}
}
