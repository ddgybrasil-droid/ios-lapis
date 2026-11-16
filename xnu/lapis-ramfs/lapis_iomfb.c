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
extern void *OSDictionary_withCapacity(unsigned int)
	__asm__("_ZN12OSDictionary12withCapacityEj");
extern int OSDictionary_setObject_cstr(void *, const char *, const void *)
	__asm__("_ZN12OSDictionary9setObjectEPKcPK15OSMetaClassBase");
extern void *OSDictionary_getObject_cstr(void *, const char *)
	__asm__("_ZNK12OSDictionary9getObjectEPKc");
extern void *OSString_withCString(const char *)
	__asm__("_ZN8OSString11withCStringEPKc");
extern const char *OSString_getCStringNoCopy(void *)
	__asm__("_ZNK8OSString16getCStringNoCopyEv");
extern void *IORegistryEntry_copyProperty(void *, const char *)
	__asm__("_ZNK15IORegistryEntry12copyPropertyEPKc");
extern void *OSArray_withCapacity(unsigned int)
	__asm__("_ZN7OSArray12withCapacityEj");
extern int OSArray_setObject(void *, const void *)
	__asm__("_ZN7OSArray9setObjectEPK15OSMetaClassBase");
extern void *OSSet_withCapacity(unsigned int)
	__asm__("_ZN5OSSet12withCapacityEj");
extern int OSSet_setObject(void *, const void *)
	__asm__("_ZN5OSSet9setObjectEPK15OSMetaClassBase");
extern void *OSCollectionIterator_withCollection(const void *)
	__asm__("_ZN20OSCollectionIterator14withCollectionEPK12OSCollection");
/* withCollection leaves valid=false until reset(); in-kernel getNextObject
 * (addMatchingNotification existing drain) returns NULL without it. Userland
 * IOUserIterator wraps GMS and calls reset — which is why GMS looked fine
 * while CES→invokeNotifier never saw our stub (boot99/100 FDR hang). */
extern void OSCollectionIterator_reset(void *)
	__asm__("_ZN20OSCollectionIterator5resetEv");
extern void *OSCollectionIterator_getNextObject(void *)
	__asm__("_ZN20OSCollectionIterator13getNextObjectEv");
extern void OSObject_retain(void *) __asm__("_ZNK8OSObject6retainEv");
extern void OSObject_release(void *) __asm__("_ZNK8OSObject7releaseEv");
extern void *IOBufferMemoryDescriptor_withCapacity(unsigned long, unsigned int, unsigned char)
	__asm__("_ZN24IOBufferMemoryDescriptor12withCapacityEmjb");
extern void *IOBufferMemoryDescriptor_withOptions(unsigned int, unsigned long, unsigned long)
	__asm__("_ZN24IOBufferMemoryDescriptor11withOptionsEjmm");
/* HID EventServicePlugin maps an IODataQueue via clientMemoryForType(0).
 * Plain withCapacity buffers hit vm_map_remap_extract("kernel_object"). */
extern void *IOSharedDataQueue_withCapacity(unsigned int)
	__asm__("_ZN17IOSharedDataQueue12withCapacityEj");
extern void *IOSharedDataQueue_getMemoryDescriptor(void *)
	__asm__("_ZN17IOSharedDataQueue19getMemoryDescriptorEv");
extern int IOSharedDataQueue_enqueue(void *, void *, unsigned int)
	__asm__("_ZN17IOSharedDataQueue7enqueueEPvj");
extern void IODataQueue_setNotificationPort(void *, void *)
	__asm__("_ZN11IODataQueue19setNotificationPortEP8ipc_port");
/* kIODirectionOutIn|kIOMemoryKernelUserShared — shareable to userspace. */
#define LAPIS_IOMEM_KUS 0x00000803u
#define LAPIS_HID_QUEUE_SIZE 0x4000u
/* boot151 panic: no .bss in ramfs blob (objcopy -j .text). Stash queue on UC. */
static void *lapis_hid_queue_for(void *owner);
/* boot105/106: deferred wake after AMN returns so FDR's mach port is listening.
 * NOTE: getNextObject @0x7b11668 is a poison stub (brk #0xbffc); the real
 * drain entry is copyNextObject @0x7b11678 (boot105 panic IOUserClient.cpp:1094). */
extern void *IOServiceUserNotification_copyNextObject(void *)
	__asm__("_ZN25IOServiceUserNotification14copyNextObjectEv");
extern int IOServiceUserNotification_handler(void *, void *, void *)
	__asm__("_ZN25IOServiceUserNotification7handlerEPvP9IOService");
typedef void (*lapis_thread_call_func_t)(void *, void *);
typedef void *lapis_thread_call_t;
extern lapis_thread_call_t thread_call_allocate(lapis_thread_call_func_t, void *);
extern int thread_call_enter(lapis_thread_call_t);
extern void IOSleep(unsigned int milliseconds);

/*
 * Trampolines overwritten by patch_kernel_ramfs_replace.py:
 *   <saved prologue stp>; b orig+4
 */
void *lapis_copyMatchingService_orig(void *dict);
void *lapis_getMatchingServices_orig(void *dict);
void *lapis_copyExistingServices_orig(void *dict, unsigned int options,
				     unsigned int flags);
void *lapis_addMatchingNotification_orig(void *type, void *matching,
					 void *handler, void *target,
					 void *ref, int priority);
/* boot113/114: is_io_object_conforms_to — hand-rolled metaclasses fail metaCast. */
int lapis_is_io_object_conforms_to_orig(void *obj, const char *name,
					unsigned int *out);
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
	".global lapis_copyExistingServices_orig\n"
	".type lapis_copyExistingServices_orig, %function\n"
	"lapis_copyExistingServices_orig:\n"
	"\tnop\n"
	"\tnop\n"
	"\tret\n"
	".global lapis_addMatchingNotification_orig\n"
	".type lapis_addMatchingNotification_orig, %function\n"
	"lapis_addMatchingNotification_orig:\n"
	"\tnop\n"
	"\tnop\n"
	"\tret\n"
	".global lapis_is_io_object_conforms_to_orig\n"
	".type lapis_is_io_object_conforms_to_orig, %function\n"
	"lapis_is_io_object_conforms_to_orig:\n"
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

/*
 * boot148: empty IOCFPlugInTypes lets CreateCFProperty succeed but CFPlugin
 * never loads IOHIDLib — BB opens IOMFB then never uses the connection.
 * Point classic HID type UUIDs at IOHIDLib.plugin (present on fullroot).
 */
static void set_hid_cfplugin_types(void *obj)
{
	void *dict, *path;

	dict = OSDictionary_withCapacity(4);
	path = OSString_withCString(
		"IOHIDFamily.kext/PlugIns/IOHIDLib.plugin");
	if (!dict || !path)
		return;
	/* kIOHIDDeviceTypeID / event-service / queue / interface families. */
	OSDictionary_setObject_cstr(dict,
		"FA12FA38-6F1A-11D4-BA0C-0005028F18D5", path);
	OSDictionary_setObject_cstr(dict,
		"7DDEECA8-A7B4-11DA-8A0E-0014519758EF", path);
	OSDictionary_setObject_cstr(dict,
		"0B842ADD-C395-4352-95F6-6585EB515623", path);
	OSDictionary_setObject_cstr(dict,
		"40A57A4E-26A0-11D8-9295-000A958A2C78", path);
	IORegistryEntry_setProperty_obj(obj, "IOCFPlugInTypes", dict);
}

static void dict_set_u32(void *dict, const char *key, unsigned int val)
{
	void *n = OSNumber_withNumber(val, 32);

	if (n) {
		OSDictionary_setObject_cstr(dict, key, n);
		OSObject_release(n);
	}
}

/*
 * boot152: after HID map/NOTE64 held, BB CFProperty-gets many HID identity
 * keys (all 0xe00002f0) then opens IOMFB with 0 post-open IOCONN. Seed a
 * built-in display-integrated digitizer so the display path can bind.
 */
static void set_hid_identity_props(void *obj)
{
	void *pairs, *pair, *transport, *esp;

	set_u32(obj, "PrimaryUsagePage", 0x0d); /* Digitizer */
	set_u32(obj, "PrimaryUsage", 0x04);     /* Touch Screen */
	set_u32(obj, "ReportInterval", 8000);   /* 125 Hz (µs) */
	set_u32(obj, "BatchInterval", 8000);
	/* boot153: BB opens IOMFB then burns CPU in digitizer matching; surface
	 * dims let BKMainDisplayObserver bind HID↔panel (2420×1668). */
	set_u32(obj, "DigitizerSurfaceWidth", 2420);
	set_u32(obj, "DigitizerSurfaceHeight", 1668);
	IORegistryEntry_setProperty_bool(obj, "Built-In", 1);
	IORegistryEntry_setProperty_bool(obj, "DisplayIntegrated", 1);
	transport = OSString_withCString("Apple");
	if (transport) {
		IORegistryEntry_setProperty_obj(obj, "Transport", transport);
		OSObject_release(transport);
	}
	pair = OSDictionary_withCapacity(2);
	pairs = OSArray_withCapacity(1);
	if (pair && pairs) {
		dict_set_u32(pair, "DeviceUsagePage", 0x0d);
		dict_set_u32(pair, "DeviceUsage", 0x04);
		OSArray_setObject(pairs, pair);
		IORegistryEntry_setProperty_obj(obj, "DeviceUsagePairs", pairs);
	}
	if (pair)
		OSObject_release(pair);
	if (pairs)
		OSObject_release(pairs);
	esp = OSDictionary_withCapacity(8);
	if (esp) {
		dict_set_u32(esp, "PrimaryUsagePage", 0x0d);
		dict_set_u32(esp, "PrimaryUsage", 0x04);
		dict_set_u32(esp, "ReportInterval", 8000);
		dict_set_u32(esp, "QueueSize", LAPIS_HID_QUEUE_SIZE);
		dict_set_u32(esp, "DigitizerSurfaceWidth", 2420);
		dict_set_u32(esp, "DigitizerSurfaceHeight", 1668);
		IORegistryEntry_setProperty_obj(obj, "HIDEventServiceProperties",
						esp);
		OSObject_release(esp);
	}
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
	       streq(name, "IOAccelerator") ||
	       /* boot110: GetMainDisplay counts FBs via AppleParavirtGPU.DisplayPortCount */
	       streq(name, "AppleParavirtGPU");
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
	if (s) {
		/* Fixed messages only — avoid printf %s on Apple stack ABI. */
		if (streq(name, "AppleParavirtGPU"))
			IOLog("LAPIS iomfb: match via resource ParavirtGPU\n");
		else if (streq(name, "IOMobileFramebuffer"))
			IOLog("LAPIS iomfb: match via resource IOMFB\n");
		else
			IOLog("LAPIS iomfb: match via resource\n");
	}
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

/*
 * boot137: backboardd AddMatchingNotification uses
 *   { IOProviderClass=IOService, IOPropertyMatch={ HIDServiceSupport=true } }
 * lapis_match_dict_name returns "IOService" (not a stub name) → stock CES
 * empty → IOITER_NEXT 0xe00002c0 → BRK. Resolve property-match to our HID stub.
 */
static const char *lapis_resolve_match_stub(void *dict)
{
	const char *name;
	void *pm, *hs;

	if (!dict)
		return 0;
	name = lapis_match_dict_name(dict);
	if (name && is_lapis_match_name(name))
		return name;
	pm = OSDictionary_getObject_cstr(dict, "IOPropertyMatch");
	if (pm) {
		hs = OSDictionary_getObject_cstr(pm, "HIDServiceSupport");
		if (hs)
			return "IOHIDEventService";
	}
	if (OSDictionary_getObject_cstr(dict, "HIDServiceSupport"))
		return "IOHIDEventService";
	return 0;
}

void *lapis_copyMatchingService(void *dict)
{
	void *s;
	const char *name;

	if (!dict)
		return lapis_copyMatchingService_orig(dict);

	name = lapis_resolve_match_stub(dict);
	if (name) {
		s = lapis_published_stub(name);
		if (s)
			return s;
	}
	return lapis_copyMatchingService_orig(dict);
}

/*
 * Shared: one-shot iterator over a published stub. Used by getMatchingServices
 * and copyExistingServices (notification path). Stock CES wraps an OSSet;
 * boot101 still hung with OSArray+reset, so match stock and peek getNextObject.
 */
static void *lapis_stub_iterator(const char *name)
{
	void *s, *set, *it, *peek;

	s = lapis_published_stub(name);
	if (!s)
		return 0;
	set = OSSet_withCapacity(1);
	if (!set) {
		OSObject_release(s);
		return 0;
	}
	if (!OSSet_setObject(set, s)) {
		OSObject_release(s);
		OSObject_release(set);
		return 0;
	}
	/* set retains s */
	OSObject_release(s);
	it = OSCollectionIterator_withCollection(set);
	if (!it) {
		OSObject_release(set);
		return 0;
	}
	/*
	 * boot121: userspace IOIteratorNext returned null after a successful
	 * GetMatchingServices. Peek+reset here so we never hand out an empty
	 * iterator; keep an extra set retain until after peek in case
	 * withCollection's retain is deferred until reset/first Next.
	 */
	OSCollectionIterator_reset(it);
	peek = OSCollectionIterator_getNextObject(it);
	if (!peek) {
		IOLog("LAPIS iomfb: stub iter EMPTY\n");
		OSObject_release(it);
		OSObject_release(set);
		return 0;
	}
	/*
	 * boot122: peek consumes the sole entry; OSCollectionIterator_reset
	 * did not re-arm for userspace IOIteratorNext (kr=0, null object).
	 * Hand out a fresh iterator over the same set instead.
	 */
	OSObject_release(it);
	it = OSCollectionIterator_withCollection(set);
	if (!it) {
		IOLog("LAPIS iomfb: stub iter rebuild fail\n");
		OSObject_release(set);
		return 0;
	}
	OSCollectionIterator_reset(it);
	IOLog("LAPIS iomfb: stub iter ok\n");
	OSObject_release(set);
	return it;
}

/*
 * boot93: getMatchingServices (plural) does not call copyMatchingService — it
 * walks the registry (metaCast/name miss). Return a one-shot iterator over our
 * published stub so IONameMatch("IOCoreSurfaceRoot") etc. succeed.
 */
void *lapis_getMatchingServices(void *dict)
{
	void *it;
	const char *name;

	if (!dict)
		return lapis_getMatchingServices_orig(dict);

	name = lapis_resolve_match_stub(dict);
	if (name) {
		it = lapis_stub_iterator(name);
		if (it)
			return it;
	}
	return lapis_getMatchingServices_orig(dict);
}

/*
 * boot99: AddMatchingNotification / installNotification calls
 * copyExistingServices (NOT getMatchingServices). Stock CES walks the
 * registry with matchPassive/metaCast — our hand-rolled metaclasses miss.
 * IOMFB_FDR_Loader only uses the notify path, so without this hook it always
 * exits 1 after IOKMATCH notify with no IOSOPEN.
 * ABI: static IOService::copyExistingServices(OSDictionary*, uint, uint) →
 * OSIterator* (stock getMatchingServices returns CES's result directly).
 */
void *lapis_copyExistingServices(void *dict, unsigned int options,
				 unsigned int flags)
{
	void *it;
	const char *name;

	if (!dict)
		return lapis_copyExistingServices_orig(dict, options, flags);

	name = lapis_resolve_match_stub(dict);
	/*
	 * boot108/109: do NOT feed IOMobileFramebuffer through CES. Stock AMN
	 * would queue into the userNotify during MIG (armed=0, no ping); FDR
	 * then either hangs or never opens. Leave CES empty for IOMFB so
	 * deferred ping alone delivers after FDR listens. Other classes keep
	 * the resource CES path (RB/IOGPU notify etc.).
	 */
	if (name && streq(name, "IOMobileFramebuffer")) {
		IOLog("LAPIS iomfb: CES skip IOMFB (deferred)\n");
		return lapis_copyExistingServices_orig(dict, options, flags);
	}
	if (name) {
		/* Re-assert non-inactive matched state before notify invoke. */
		{
			void *s = lapis_published_stub(name);
			if (s) {
				unsigned int *st =
					(unsigned int *)((char *)s + 0x48);
				*st = (*st & ~1u) | 0x1e;
				OSObject_release(s);
			}
		}
		it = lapis_stub_iterator(name);
		if (it) {
			void *peek = OSCollectionIterator_getNextObject(it);
			IOLog(peek ? "LAPIS iomfb: CES via resource\n"
				   : "LAPIS iomfb: CES iter EMPTY\n");
			/* Drain consumed the only entry — re-arm for invoke. */
			OSCollectionIterator_reset(it);
			return it;
		}
	}
	return lapis_copyExistingServices_orig(dict, options, flags);
}

/*
 * boot101–106: CES+reset+force+pre-arm+deferred still left FDR hung (no IOSOPEN).
 * Root cause (RE of IOServiceUserNotification::handler @0x7b1147c):
 * sendPing only when armed && count==0 *before* enqueue. Stock existing-drain
 * runs inside the MIG with armed=0 by design (copyNextObject sets armed=1 only
 * after userland empties the queue). Pre-arm pings during the syscall — before
 * FDR waits on the port — so the mach message is lost. FDR then blocks on the
 * notification port forever (boot99+ hang vs boot98 exit-1 with empty CES).
 *
 * boot105/106: deferred thread_call drains via copyNextObject then re-queues.
 * boot106: ping ran (no panic) but 200ms is still before FDR's mach_msg wait
 * under QEMU spawn storms — message lost again; queue has the stub, nobody
 * wakes to drain. Fix: longer initial sleep + retry pings via the SAME AMN
 * handler/target/ref/notifier (heap ctx — no RX .bss).
 */
typedef int (*lapis_match_handler_t)(void *target, void *ref, void *service,
				     void *notifier);

struct lapis_iomfb_ping_ctx {
	void *target;
	void *handler;
	void *ref;
	void *notifier;
};

static void lapis_deferred_iomfb_ping(void *param0, void *param1)
{
	struct lapis_iomfb_ping_ctx *ctx = param0;
	void *obj, *s, *target;
	lapis_match_handler_t h;
	int i;
	unsigned char armed;

	(void)param1;
	if (!ctx || !ctx->target || !ctx->handler)
		goto out;
	target = ctx->target;
	h = (lapis_match_handler_t)ctx->handler;

	/*
	 * boot106: 200ms fired before FDR listened. boot116: 500ms raced
	 * inside AMN MIG (pings before IOKMATCH_RET). Keep ≥2s so empty
	 * Next can arm, then retry.
	 */
	IOSleep(2000);
	for (i = 0; i < 10; i++) {
		while ((obj = IOServiceUserNotification_copyNextObject(target)))
			OSObject_release(obj);
		armed = *((unsigned char *)target + 0x48);
		s = lapis_published_stub("IOMobileFramebuffer");
		if (!s) {
			IOLog("LAPIS iomfb: deferred ping no stub\n");
			break;
		}
		{
			unsigned int *st = (unsigned int *)((char *)s + 0x48);
			*st = (*st & ~1u) | 0x1e;
		}
		/* Re-arm explicitly in case drain raced with another enqueue. */
		*((unsigned char *)target + 0x48) = 1;
		h(target, ctx->ref, s, ctx->notifier);
		OSObject_release(s);
		IOLog(armed ? "LAPIS iomfb: deferred ping\n"
			    : "LAPIS iomfb: deferred ping (was unarmed)\n");
		if (i + 1 < 8)
			IOSleep(1000);
	}
out:
	if (ctx) {
		if (ctx->target)
			OSObject_release(ctx->target);
		if (ctx->notifier)
			OSObject_release(ctx->notifier);
		/* ctx itself was IOMallocZero — no free API linked; leak ok. */
	}
}

/*
 * boot113: GetMainDisplay/populate drains our IOMFB notify (IOITER_NEXT ok) but
 * never IOSOPEN. Hand-rolled OSMetaClass fails metaCast, so
 * IOObjectConformsTo(service, "IOMobileFramebuffer") returns false and the
 * framework discards the match. Force-true when the object is our published
 * stub for that class name.
 * ABI: is_io_object_conforms_to(obj, className, boolean_t *out) → kern_return_t.
 */
int lapis_is_io_object_conforms_to(void *obj, const char *name,
				  unsigned int *out)
{
	int kr;
	void *stub;

	kr = lapis_is_io_object_conforms_to_orig(obj, name, out);
	if (!out)
		return kr;
	if (*out)
		return kr;
	if (!obj || !name)
		return kr;
	/*
	 * boot147: connection-side class checks for IOMFB UC. Our NUC is a
	 * stock IOUserClient tagged LapisUC + IOClass=…UserClient.
	 */
	if (streq(name, "IOMobileFramebufferUserClient") ||
	    streq(name, "IOHIDEventServiceUserClient") ||
	    streq(name, "IOUserClient")) {
		void *tag = IORegistryEntry_copyProperty(obj, "LapisUC");
		if (tag) {
			*out = 1;
			IOLog("LAPIS iomfb: conformsTo LapisUC\n");
			OSObject_release(tag);
			return kr;
		}
	}
	if (!is_lapis_match_name(name))
		return kr;
	stub = lapis_published_stub(name);
	if (!stub)
		return kr;
	/*
	 * Notify/MIG may pass the same OSObject* as publishResource, or a
	 * distinct retained alias — accept either pointer equality or a
	 * second published lookup hit (same class name). Always force for
	 * lapis match names when a stub exists: metaCast cannot see us.
	 */
	*out = 1;
	IOLog(stub == obj ? "LAPIS iomfb: conformsTo force eq\n"
			  : "LAPIS iomfb: conformsTo force any\n");
	OSObject_release(stub);
	return kr;
}

void *lapis_addMatchingNotification(void *type, void *matching, void *handler,
				    void *target, void *ref, int priority)
{
	void *ret;
	const char *name;
	int ours = 0;
	lapis_thread_call_t tc;
	struct lapis_iomfb_ping_ctx *ctx;

	name = matching ? lapis_match_dict_name(matching) : 0;
	/* Only FDR's IOMobileFramebuffer notify needs the armed/ping fix. */
	ours = name && streq(name, "IOMobileFramebuffer") && target && handler;

	ret = lapis_addMatchingNotification_orig(type, matching, handler,
						 target, ref, priority);
	if (!ret || !ours)
		return ret;

	/*
	 * boot108: force-queue during AMN + 8 deferred pings (getNextObject
	 * fixed) still yielded no FDR IOSOPEN. Stop force-queuing here so
	 * FDR's post-AMN empty IOIteratorNext can set armed; deferred path
	 * alone delivers once the port is listening. CES also skips IOMFB.
	 */

	ctx = IOMallocZero(sizeof(*ctx));
	if (!ctx) {
		IOLog("LAPIS iomfb: deferred ping alloc fail\n");
		return ret;
	}
	ctx->target = target;
	ctx->handler = handler;
	ctx->ref = ref;
	ctx->notifier = ret;
	OSObject_retain(target);
	OSObject_retain(ret);
	tc = thread_call_allocate(lapis_deferred_iomfb_ping, ctx);
	if (tc) {
		thread_call_enter(tc);
		IOLog("LAPIS iomfb: scheduled deferred ping\n");
	} else {
		OSObject_release(target);
		OSObject_release(ret);
		IOLog("LAPIS iomfb: deferred ping alloc fail\n");
	}
	return ret;
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
	/*
	 * boot144/146/153: tag UC by provider. HID opens used to inherit
	 * IOMobileFramebufferUserClient IOClass; keep queue only on HID.
	 */
	{
		void *hid = self ? IORegistryEntry_copyProperty(
					 self, "HIDServiceSupport")
				 : 0;
		if (hid) {
			IORegistryEntry_setProperty_cstr(
				client, "IOClass",
				"IOHIDEventServiceUserClient");
			set_u32(client, "QueueSize", LAPIS_HID_QUEUE_SIZE);
			{
				void *q = lapis_hid_queue_for(client);
				if (q)
					OSObject_release(q);
			}
			OSObject_release(hid);
		} else {
			set_u32(client, "IODisplayWidth", 2420);
			set_u32(client, "IODisplayHeight", 1668);
			set_u32(client, "IODisplayPixelsWide", 2420);
			set_u32(client, "IODisplayPixelsHigh", 1668);
			set_u32(client, "IODisplayFrequency", 120);
			set_u32(client, "canvas-width", 2420);
			set_u32(client, "canvas-height", 1668);
			set_u32(client, "MaxSrcBufferWidth", 2420);
			set_u32(client, "MaxSrcBufferHeight", 1668);
			IORegistryEntry_setProperty_bool(client,
							 "IOMFBMainDisplay", 1);
			IORegistryEntry_setProperty_cstr(
				client, "IOClass",
				"IOMobileFramebufferUserClient");
		}
	}
	IORegistryEntry_setProperty_bool(client, "LapisUC", 1);
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
 * Return retained IOSharedDataQueue for this UC (property LapisHIDQ).
 * No static/BSS — ramfs blob is .text only (boot151 PWTKS on g_hid write).
 */
static void *lapis_hid_queue_for(void *owner)
{
	void *q;

	if (!owner)
		return 0;
	q = IORegistryEntry_copyProperty(owner, "LapisHIDQ");
	if (q)
		return q;
	q = IOSharedDataQueue_withCapacity(LAPIS_HID_QUEUE_SIZE);
	if (!q) {
		IOLog("LAPIS iomfb: hid queue alloc fail\n");
		return 0;
	}
	IORegistryEntry_setProperty_obj(owner, "LapisHIDQ", q);
	IOLog("LAPIS iomfb: hid queue ok\n");
	return q; /* withCapacity retain for caller */
}

/*
 * boot149/150: HID CFPlugin opens UC → sel0 → map type0 → sel1 →
 * set_notification_port_64. Stock registerNotificationPort is Unsupported;
 * boot150 stubbed it to 0 but map still failed → BRK. Wire the port onto
 * the shared HID IODataQueue so enqueue can signal userspace.
 * Image B-patches both jj/jy overloads onto this.
 */
int lapis_register_notification_port(void *self, void *port,
				     unsigned int type, unsigned long long ref)
{
	void *q;
	/* boot157: removing the pulse regressed to BRK right after NOTE64
	 * (elr=0x1f0a1dd44). Restore enqueue; keep w00t MACHMSG2 filter. */
	unsigned char pulse[8];
	int i, enq;

	(void)type;
	(void)ref;
	for (i = 0; i < 8; i++)
		pulse[i] = 0;
	q = lapis_hid_queue_for(self);
	if (q) {
		if (port)
			IODataQueue_setNotificationPort(q, port);
		/* Boolean true(1)=success */
		enq = IOSharedDataQueue_enqueue(q, pulse, 8);
		OSObject_release(q);
		IOLog(enq ? "LAPIS iomfb: regnote ok enq\n"
			  : "LAPIS iomfb: regnote ok enqfail\n");
		return 0;
	}
	IOLog("LAPIS iomfb: regnote ok\n");
	return 0;
}

/*
 * boot94/151/152: clientMemoryForType. HID type0 must be an IOSharedDataQueue
 * mapping (IODataQueueMemory); plain withCapacity hits
 * vm_map_remap_extract rejecting kernel_object (boot150 IOMAPMEM 0xe00002c2).
 * Image B-patches IOUserClient::clientMemoryForType onto this.
 */
int lapis_client_memory_for_type(void *self, unsigned int type,
				 unsigned int *options, void **memory)
{
	void *md = 0;
	void *q;

	if (!memory)
		return kIOReturnBadArgument;
	*memory = 0;
	if (options)
		*options = 0;

	/* type 0: HID EventService queue (also fine for other type0 probes). */
	if (type == 0) {
		q = lapis_hid_queue_for(self);
		if (q) {
			md = IOSharedDataQueue_getMemoryDescriptor(q);
			OSObject_release(q);
		}
		if (md) {
			OSObject_retain(md);
			*memory = md;
			IOLog("LAPIS iomfb: cmap hidq ok\n");
			return 0;
		}
	}

	/* Fallback: KernelUserShared buffer (IOSurface / generic). */
	md = IOBufferMemoryDescriptor_withOptions(LAPIS_IOMEM_KUS, 0x10000, 1);
	if (!md)
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
		/*
		 * Prefer "yes" for single-scalar probes, except IOSurface
		 * connect (sel=32): boot98+ held connect with method kr=0
		 * but scalar[0]=1 may be read as a non-success status /
		 * bogus id by some clients. Leave 0 for sel 32.
		 */
		if (n >= 1 && selector != 32)
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
		}
		/*
		 * boot146/147 RE: kern_GetDisplaySize uses selector 8 with an
		 * 8-byte (two u32 / two doubles after convert) structure out.
		 * Also accept generic 8/16-byte size-shaped outs.
		 */
		else if (selector == 8 || sz == 8 || sz == 16) {
			u32[0] = 2420;
			u32[1] = 1668;
			if (sz >= 16) {
				u32[2] = 1;
				u32[3] = 120;
			}
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
	if (hid_support) {
		IORegistryEntry_setProperty_bool(obj, "HIDServiceSupport", 1);
		IORegistryEntry_setProperty_cstr(obj, "IOClass", "IOHIDEventService");
		/* Minimal identity so clients past empty-match asserts. */
		set_u32(obj, "VendorID", 0x5ac);
		set_u32(obj, "ProductID", 1);
		set_u32(obj, "LocationID", 1);
		/*
		 * boot150: EventServicePlugin CFProperty-gets QueueSize before
		 * IOConnectMapMemory type0; missing → 0xe00002f0 then map BRK.
		 */
		set_u32(obj, "QueueSize", LAPIS_HID_QUEUE_SIZE);
		set_hid_cfplugin_types(obj);
		set_hid_identity_props(obj);
	}
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
		/*
		 * boot147 RE of IOMobileFramebufferOpen: after IOServiceOpen it
		 * CFProperty-gets canvas-width / canvas-height (not IODisplay*).
		 * BB itself raw-opens (we see IOSOPEN without the Open post-path),
		 * but other callers and later size probes use these keys.
		 */
		set_u32(obj, "canvas-width", 2420);
		set_u32(obj, "canvas-height", 1668);
		set_u32(obj, "canvas_width", 2420);
		set_u32(obj, "canvas_height", 1668);
		set_u32(obj, "MaxSrcBufferWidth", 2420);
		set_u32(obj, "MaxSrcBufferHeight", 1668);
		set_u32(obj, "MaxSrcRectWidth", 2420);
		set_u32(obj, "IOMFBMaxSrcPixels", 2420 * 1668);
		/* GetMainDisplay / embedded-panel match (IOMFB.framework). */
		IORegistryEntry_setProperty_bool(obj, "IOMFBMainDisplay", 1);
		set_u32(obj, "IOMFBDisplayExternal", 0);
		/*
		 * boot116 RE of iomfb_match_callback: SearchCFProperty keys are
		 * CFSTR("external") / "dfr" / "AppleTV" (booleans). Missing
		 * "external" already marks main; set it false explicitly so a
		 * parent-plane hit cannot flip the slot to external-only.
		 */
		IORegistryEntry_setProperty_bool(obj, "external", 0);
		IORegistryEntry_setProperty_cstr(obj, "IOClass", "IOMobileFramebuffer");
		/* boot153: BB walks device_type/compatible before IOMFB open. */
		IORegistryEntry_setProperty_cstr(obj, "device_type",
						 "display-subsystem");
		IORegistryEntry_setProperty_cstr(obj, "compatible",
						 "IOMobileFramebuffer");
		{
			void *plug = OSDictionary_withCapacity(0);
			if (plug)
				IORegistryEntry_setProperty_obj(obj, "IOCFPlugInTypes", plug);
		}
	}

	/*
	 * boot109/110 RE of IOMobileFramebufferGetMainDisplay (iPadOS 26):
	 * getParavirtDisplayCount → IOServiceGetMatchingServices(AppleParavirtGPU)
	 * then IORegistryEntryCreateCFProperty(DisplayPortCount). Populate waits
	 * until enumerated FB count == that port count, then opens. Without this
	 * node Ref_FB stays wrong and FDR drains our deferred ping without IOSOPEN.
	 */
	if (m->name[0] == 'A' && m->name[1] == 'p' && m->name[2] == 'p' &&
	    m->name[5] == 'P') {
		set_u32(obj, "DisplayPortCount", 1);
		IORegistryEntry_setProperty_cstr(obj, "IOClass", "AppleParavirtGPU");
		IORegistryEntry_setProperty_cstr(obj, "device_type",
						 "display-subsystem");
		IORegistryEntry_setProperty_cstr(obj, "compatible",
						 "paravirtualizedgraphics,gpu");
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
	/*
	 * boot99: AddMatchingNotification's in-kernel handler skips services
	 * whose +0x48 state bit0 (kIOServiceInactiveState) is set. Hand-rolled
	 * stubs can land inactive; CES then returns them but invoke is skipped
	 * → userspace waits forever (boot99 FDR hang after CES, no IOSOPEN).
	 * Force registered|matched|firstPublish|firstMatch, clear inactive.
	 */
	{
		unsigned int *st = (unsigned int *)((char *)obj + 0x48);
		*st = (*st & ~1u) | 0x1e;
	}
	IOService_publishResource(m->name, obj);
	IOLog("LAPIS iomfb: published instance\n");
}

void lapis_iomfb_register(void)
{
	struct lapis_mc iomfb, surface, coresurf, hid, aks, gpu, gpudev, clcd, agx,
		accel, pgpu;

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
	/* GetMainDisplay getParavirtDisplayCount (boot110). */
	if (register_mc(&pgpu, "AppleParavirtGPU") == 0)
		IOLog("LAPIS iomfb: metaclass AppleParavirtGPU ok\n");

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
	publish_one(&pgpu, 0);

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
