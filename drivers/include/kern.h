/*
 * kern.h - minimal freestanding view of the xnu-12377 (CI kernel.development.lapis)
 * kernel API used by the lapis built-in drivers.  Every function below is a real
 * kernel symbol; addresses are bound at link time from the kernel's own
 * __LINKEDIT symbol table (drivers/syms.txt -> build.sh --defsym).  C++ methods
 * are called through their mangled symbol with `this` as the first argument
 * (Itanium/arm64 ABI, non-arm64e kernel: no PAC on vtables).
 */
#pragma once
typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;
typedef signed int         int32_t;
typedef signed long long   int64_t;
typedef unsigned long      uintptr_t;
typedef unsigned long      size_t;
typedef unsigned long      vm_offset_t;
typedef uint64_t           pmap_paddr_t;
typedef int                kern_return_t;
typedef int                IOReturn;
typedef int                bool_t;
#define NULL ((void *)0)
#define true 1
#define false 0

#define kIOReturnSuccess      0
#define kIOReturnError        ((IOReturn)0xe00002bc)
#define kIOReturnNoMemory     ((IOReturn)0xe00002bd)
#define kIOReturnUnsupported  ((IOReturn)0xe00002c7)
#define kIOReturnTimeout      ((IOReturn)0xe00002d6)
#define kIOInterruptTypeEdge  0
#define kIOInterruptTypeLevel 1

typedef void *OSObject_t;     /* opaque C++ objects */
typedef void (*IOInterruptHandler)(void *target, void *refCon, void *nub, int source);
typedef IOInterruptHandler IOInterruptAction;

/* iokit/IOKit/IOInterruptController.h (64 bytes, offsets checked against
 * IOInterruptController::enableInterrupt disassembly: vector = vectors + n<<6) */
typedef struct IOInterruptVector {
	volatile char interruptActive;
	volatile char interruptDisabledSoft;
	volatile char interruptDisabledHard;
	volatile char interruptRegistered;
	void         *interruptLock;
	void         *nub;
	int           source;
	void         *target;
	IOInterruptHandler handler;
	void         *refCon;
	void         *sharedController;
} IOInterruptVector;

/* object layout facts (from disassembly of the CI kernel) */
#define IOINTC_OFF_VECTORS          136   /* IOInterruptController::vectors */
#define IOINTC_OFF_LOCK             144   /* IOInterruptController::controllerLock */
#define IOSERVICE_OFF_INTSOURCES    128   /* IOService::_interruptSources */
#define IOINTC_VTABLE_SLOTS         298   /* incl. offset-to-top + RTTI */
/* byte offsets from the vtable address point */
#define VT_registerInterrupt        2176
#define VT_getInterruptType         2192
#define VT_enableInterrupt          2200
#define VT_disableInterrupt         2208
#define VT_causeInterrupt           2216
#define VT_getInterruptHandlerAddress 2224
#define VT_handleInterrupt          2232
#define VT_vectorCanBeShared        2240
#define VT_initVector               2248
#define VT_getVectorType            2256
#define VT_disableVectorHard        2264
#define VT_enableVector             2272
#define VT_causeVector              2280

typedef struct { void *interruptController; void *vectorData; } IOInterruptSource;

/* libkern / osfmk */
/* NOTE: kernel variadics use the Darwin ABI (varargs on the stack). Never call IOLog/
 * snprintf/... with arguments from this blob; use klog() (common/klog.c). */
extern void   IOLog(const char *fmt, ...);
void klog(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
int  ksnprintf(char *buf, size_t n, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
extern void   IOSleep(unsigned ms);
extern void   IODelay(unsigned us);
extern void  *IOMallocZero(size_t);
extern void   IOFree(void *, size_t);
extern void  *IOMallocAligned(size_t, size_t);
extern void  *IOLockAlloc(void);
extern void   IOLockLock(void *);
extern void   IOLockUnlock(void *);
extern void  *memcpy(void *, const void *, size_t);
extern void  *memset(void *, int, size_t);
extern void   bzero(void *, size_t);
extern int    strcmp(const char *, const char *);
extern size_t strlen(const char *);
extern vm_offset_t  ml_io_map(vm_offset_t phys, vm_offset_t size);
extern pmap_paddr_t kvtophys(vm_offset_t va);
extern uint64_t mach_absolute_time(void);
extern void   absolutetime_to_nanoseconds(uint64_t abstime, uint64_t *ns);
typedef void (*thread_continue_t)(void *param, int wresult);
extern kern_return_t kernel_thread_start(thread_continue_t, void *param, void **thread);
extern void   thread_deallocate(void *thread);
extern void   ml_install_interrupt_handler(void *nub, int source, void *target, IOInterruptHandler handler, void *refCon);
extern int    ml_set_interrupts_enabled(int);
extern vm_offset_t pe_arm_get_soc_base_phys(void);
extern int    PE_parse_boot_argn(const char *arg, void *ptr, int max_len);
extern void  *OSObject_typed_operator_new(void *ktv, size_t size);

/* C++ (mangled) entry points, `this` first */
extern char   k_IOInterruptController_ktv[];
extern char   k_IOInterruptController_meta[];
extern char   k_IOInterruptController_vtable[];
extern char   k_IOService_meta[];
extern char   k_OSData_meta[];
extern void  *k_IOInterruptController_ctor(void *self, const void *meta);
extern void  *k_safeMetaCast(const void *obj, const void *meta);
extern int    k_IOService_init(void *self, void *dict);
extern int    k_IOService_attach(void *self, void *provider);
extern void   k_IOService_registerService(void *self, unsigned opts);
extern void  *k_IOService_getPlatform(void);
extern IOReturn k_IOService_registerInterrupt(void *self, int source, void *target, IOInterruptAction h, void *refCon);
extern IOReturn k_IOService_enableInterrupt(void *self, int source);
extern IOReturn k_IOService_disableInterrupt(void *self, int source);
extern IOReturn k_IOService_causeInterrupt(void *self, int source);
extern IOReturn k_IOPE_registerIntController(void *pe, void *name, void *ctrl);
extern void  *k_IODTInterruptControllerName(void *entry);
extern void  *k_IORE_fromPath(const char *path, const void *plane, char *resid, int *residlen, void *from);
extern void  *k_IORE_getProperty(const void *self, const char *key);
extern void   k_IORE_setName(void *self, const char *name, const void *plane);
extern int    k_IORE_setPropertyNum(void *self, const char *key, uint64_t v, unsigned bits);
extern int    k_IORE_setPropertyStr(void *self, const char *key, const char *v);
extern int    k_IORE_setPropertyBytes(void *self, const char *key, const void *bytes, unsigned len);
extern const void *k_OSData_getBytesNoCopy(const void *self);
extern unsigned k_OSData_getLength(const void *self);
extern void  *k_gIODTPlane;   /* const IORegistryPlane * */

/* helpers */
static inline void dsb_sy(void) { __asm__ volatile("dsb sy" ::: "memory"); }
static inline void isb(void) { __asm__ volatile("isb" ::: "memory"); }
static inline uint32_t rd32(vm_offset_t a) { uint32_t v = *(volatile uint32_t *)a; __asm__ volatile("dsb ld" ::: "memory"); return v; }
static inline void wr32(vm_offset_t a, uint32_t v) { __asm__ volatile("dsb st" ::: "memory"); *(volatile uint32_t *)a = v; }
static inline uint64_t rd64(vm_offset_t a) { uint64_t v = *(volatile uint64_t *)a; __asm__ volatile("dsb ld" ::: "memory"); return v; }
#define VCALL(obj, off, rt, ...) ((rt (*)())(*(void ***)(obj))[(off) / 8])(obj, ##__VA_ARGS__)

static inline uint64_t now_ns(void) { uint64_t ns; absolutetime_to_nanoseconds(mach_absolute_time(), &ns); return ns; }

/* ADT (IODeviceTree plane) helpers implemented in common/drv_common.c */
void *drv_dt_entry(const char *path);                        /* IORegistryEntry* or NULL */
const void *drv_dt_prop(void *entry, const char *key, unsigned *len);
int  drv_dt_u32(void *entry, const char *key, uint32_t *out);
int  drv_dt_u64(void *entry, const char *key, unsigned idx, uint64_t *out);
void *drv_dt_service(const char *path, unsigned wait_ms);   /* IOService* nub (waits) */
int  drv_bootarg(const char *name, uint32_t dflt);
void drv_thread(thread_continue_t fn, void *arg);
