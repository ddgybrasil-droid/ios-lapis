/*
 * lapis_pthread: in-kernel pthread provider for the ios-lapis XNU 12377.121.6
 * image (stand-in for pthread.kext). Port of libpthread-539.100.4
 * kern/kern_support.c (APSL 2.0) without psynch. Kernel entry points are
 * called at fixed runtime addresses from the image's own symbol table
 * (ksyms); pthread_kern callbacks are read from _pthread_callbacks.
 * Freestanding, no writable globals (lives in kernel text).
 */
typedef unsigned long u64; typedef unsigned int u32; typedef int i32;
typedef long i64; typedef unsigned short u16; typedef unsigned long size_t;
typedef u64 user_addr_t; typedef void *proc_t; typedef void *thread_t;
typedef void *vm_map_t; typedef void *task_t; typedef int kern_return_t;
typedef u32 mach_port_name_t; typedef _Bool bool;
#define NULL ((void*)0)

#include "ksyms.h"   /* K_<name> runtime addresses, generated */

#define KF(name, ret, ...) ((ret (*)(__VA_ARGS__))K_##name)

struct pthread_callbacks_s {
	int version; u32 config_thread_max;
	u32 (*get_task_threadmax)(void);
	u64 (*proc_get_register)(proc_t);
	void (*proc_set_register)(proc_t);
	user_addr_t (*proc_get_threadstart)(proc_t);
	void (*proc_set_threadstart)(proc_t, user_addr_t);
	user_addr_t (*proc_get_wqthread)(proc_t);
	void (*proc_set_wqthread)(proc_t, user_addr_t);
	int (*proc_get_pthsize)(proc_t);
	void (*proc_set_pthsize)(proc_t, int);
	thread_t (*task_findtid)(task_t, u64);
	void (*thread_deallocate_safe)(thread_t);
	bool (*proc_get_jit_entitled)(proc_t);
	void (*proc_set_dispatchqueue_offset)(proc_t, u64);
	bool (*proc_get_pthread_jit_allowlist2)(proc_t, bool *);
	void *u0, *u1;
	void *psynch_wait_prepare, *psynch_wait_update_complete, *psynch_wait_complete,
	     *psynch_wait_cleanup, *psynch_wait_wakeup, *psynch_wait_update_owner;
	void *(*proc_get_pthhash)(proc_t);
	void (*proc_set_pthhash)(proc_t, void *);
	void *u2, *u3, *u4, *u5;
	void *(*uthread_get_uukwe)(void *);
	void *u6;
	void (*uthread_set_returnval)(void *, int);
	int (*uthread_is_cancelled)(void *);
	void *(*task_get_ipcspace)(task_t);
	void *u7;
	void *vm_map_page_info;
	mach_port_name_t (*ipc_port_copyout_send_pinned)(void *, void *);
	kern_return_t (*thread_set_wq_state32)(thread_t, void *);
	kern_return_t (*thread_set_wq_state64)(thread_t, void *);
	void (*thread_exception_return)(void);
	void (*thread_bootstrap_return)(void);
	void (*abandon_preemption_disable_measurement)(void);
	void *u8, *u9;
	void *(*get_bsdthread_info)(thread_t);
	void *u10, *u11, *u12;
	kern_return_t (*thread_policy_set_internal)(thread_t, int, void *, u32);
	void *u13;
	void (*unix_syscall_return)(int);
	void *u14, *u15, *u16;
	void (*__pthread_testcancel)(int);
	kern_return_t (*mach_port_deallocate)(void *, mach_port_name_t);
	kern_return_t (*semaphore_signal_internal_trap)(mach_port_name_t);
	vm_map_t (*current_map)(void);
	void *u17, *u18;
	kern_return_t (*thread_resume)(thread_t);
	int (*kevent_workq_internal)(proc_t, user_addr_t, int, user_addr_t, int,
	    user_addr_t, u64 *, unsigned int, i32 *);
	void *u19, *u20, *u21;
	void (*proc_set_workqueue_quantum_offset)(proc_t, u64);
	u32 (*proc_get_pthread_tsd_offset)(proc_t);
	void (*proc_set_pthread_tsd_offset)(proc_t, u32);
	kern_return_t (*thread_set_tsd_base)(thread_t, u64);
	int (*proc_usynch_get_requested_thread_qos)(void *);
	u64 (*proc_get_mach_thread_self_tsd_offset)(proc_t);
	void (*proc_set_mach_thread_self_tsd_offset)(proc_t, u64);
	kern_return_t (*thread_policy_get)(thread_t, int, void *, u32 *, int *);
	int (*qos_main_thread_active)(void);
	kern_return_t (*thread_set_voucher_name)(mach_port_name_t);
	void *qos_add_override, *qos_remove_override, *u22, *u23;
	u16 (*thread_set_tag)(thread_t, u16);
	u16 (*thread_get_tag)(thread_t);
	void *u24;
	kern_return_t (*thread_terminate_pinned)(thread_t);
	void *(*convert_thread_to_port_pinned)(thread_t);
	user_addr_t (*proc_get_stack_addr_hint)(proc_t);
	void (*proc_set_stack_addr_hint)(proc_t, user_addr_t);
	kern_return_t (*thread_create_immovable)(task_t, thread_t *);
	void (*proc_set_return_to_kernel_offset)(proc_t, u64);
	void *u25;
	void (*thread_will_park_or_terminate)(thread_t);
	void *u26;
	user_addr_t (*proc_get_user_stack)(proc_t);
	void *u27;
	void *_pad[69];
};
_Static_assert(__builtin_offsetof(struct pthread_callbacks_s, _pad) +
    69 * 8 == sizeof(struct pthread_callbacks_s), "cb");

#define PK ((const struct pthread_callbacks_s *)K__pthread_callbacks)

/* kernel functions (runtime addresses from the image symbol table) */
#define current_task       KF(_current_task, task_t, void)
#define current_thread     KF(_current_thread, thread_t, void)
#define thread_reference   KF(_thread_reference, void, thread_t)
#define thread_deallocate  KF(_thread_deallocate, void, thread_t)
#define thread_terminate   KF(_thread_terminate, kern_return_t, thread_t)
#define thread_policy_set  KF(_thread_policy_set, kern_return_t, thread_t, int, void *, u32)
#define thread_tid         KF(_thread_tid, u64, thread_t)
#define copyin             KF(_copyin, int, user_addr_t, void *, size_t)
#define copyout            KF(_copyout, int, const void *, user_addr_t, size_t)
#define proc_is64bit       KF(_proc_is64bit, int, proc_t)
#define proc_is64bit_data  KF(_proc_is64bit_data, int, proc_t)
/* Darwin arm64 ABI packs stack args by natural size: cur@[sp], max@[sp+4], inh@[sp+8].
 * GCC uses 8-byte slots, so pass cur|max<<32 as one u64 and inh as the next. */
#define mach_vm_map        KF(_mach_vm_map_external, kern_return_t, vm_map_t, u64 *, u64, u64, int, void *, u64, int, u64, u64)
#define DARWIN_PROT_PAIR(cur, max) ((u64)(u32)(cur) | ((u64)(u32)(max) << 32))
#define mach_vm_allocate   KF(_mach_vm_allocate_external, kern_return_t, vm_map_t, u64 *, u64, int)
#define mach_vm_protect    KF(_mach_vm_protect, kern_return_t, vm_map_t, u64, u64, int, int)
#define mach_vm_deallocate KF(_mach_vm_deallocate, kern_return_t, vm_map_t, u64, u64)
#define mach_vm_behavior_set KF(_mach_vm_behavior_set, kern_return_t, vm_map_t, u64, u64, int)
#define kprintf            KF(_printf, int, const char *, ...)
#define kpanic             KF(_panic, void, const char *, ...)

void *memset(void *d, int c, size_t n)
{
	volatile unsigned char *p = d;
	while (n--) *p++ = (unsigned char)c;
	return d;
}
void *memcpy(void *d, const void *s, size_t n)
{
	volatile unsigned char *p = d; const unsigned char *q = s;
	while (n--) *p++ = *q++;
	return d;
}

#define EINVAL 22
#define ENOMEM 12
#define EMFILE 24
#define ENOTSUP 45
#define EJUSTRETURN (-2)
#define PAGE_SZ 0x4000UL            /* 16K user and kernel pages */
#define PAGE_MASK (PAGE_SZ - 1)
#define PTH_DEFAULT_STACKSIZE (512UL * 1024)
#define MAX_PTHREAD_SIZE (64 * 1024)
#define PTHREAD_T_OFFSET (12 * 1024)
#define SHARED_REGION_BASE_ARM64 0x180000000UL
#define THREAD_TAG_MAINTHREAD 0x1
#define THREAD_TAG_PTHREAD 0x10
#define PTHREAD_START_CUSTOM 0x01000000
#define PTHREAD_START_SETSCHED 0x02000000
#define PTHREAD_START_QOSCLASS 0x08000000
#define PTHREAD_START_TSD_BASE_SET 0x10000000
#define PTHREAD_START_SUSPENDED 0x20000000
#define PTHREAD_START_QOSCLASS_MASK 0x00ffffff
#define THREAD_EXTENDED_POLICY 1
#define THREAD_PRECEDENCE_POLICY 3
#define THREAD_QOS_POLICY 9
#define THREAD_QOS_POLICY_COUNT 2
#define THREAD_QOS_LEGACY 4
#define VM_FLAGS_ANYWHERE 1
#define VM_MEMORY_STACK 30
#define VM_MAKE_TAG(t) ((t) << 24)
#define VM_BEHAVIOR_REUSABLE 8
#define WQ_SETUP_FIRST_USE 1
#define WQ_SETUP_EXIT_THREAD 8
#define WQ_FLAG_THREAD_KEVENT 0x00080000
#define WQ_FLAG_THREAD_TSD_BASE_SET 0x00200000
#define WQ_KEVENT_LIST_LEN 16
#define WQ_KEVENT_DATA_SIZE (32 * 1024)
#define KEVENT_QOS_SIZE 72
#define KEVENT_FLAG_IMMEDIATE 0x1
#define KEVENT_FLAG_STACK_DATA 0x8
#define KEVENT_FLAG_PARKING 0x800
#define WORKQ_EXIT_THREAD_NKEVENT (-1)
/* PTHREAD_FEATURE_SUPPORTED: DISPATCHFUNC|FINEPRIO|BSDTHREADCTL|SETSELF|
 * QOS_MAINTENANCE|QOS_DEFAULT|KEVENT|WORKLOOP|COOPERATIVE_WORKQ */
#define PTHREAD_FEATURE_SUPPORTED (0x01|0x02|0x04|0x08|0x10|0x40000000|0x40|0x80|0x100)
/* psynch is not implemented: ask libpthread for ulock-based mutexes */
#define MUTEX_DEFAULT_POLICY 0x100   /* _PTHREAD_REG_DEFAULT_USE_ULOCK */

struct reg_data {
	u64 version, dispatch_queue_offset, main_qos;
	u32 tsd_offset, return_to_kernel_offset, mach_thread_self_offset;
	u64 stack_addr_hint;
	u32 mutex_default_policy, joinable_offset_bits, wq_quantum_expiry_offset;
} __attribute__((packed));
_Static_assert(sizeof(struct reg_data) == 56, "regdata");

struct arm_thread_state64 {
	u64 x[29], fp, lr, sp, pc; u32 cpsr, flags;
};

struct qos_policy { int qos_tier; int tier_importance; };

static inline u32 pp_make(int qos)
{
	return qos ? (1u << (8 + qos - 1)) | 0xff : 0;   /* relpri 0 -> 0xff */
}
static int pp_qos(u32 pp)
{
	if ((pp & 0x20000000u) || !(pp & 0x3f00u)) return 0;
	return __builtin_ffs((int)((pp & 0x3f00u) >> 8));
}
static int pp_relpri(u32 pp)
{
	if ((pp & 0x20000000u) || !(pp & 0x3f00u)) return 0;
	return (signed char)(pp & 0xff) + 1;
}

static u64 stack_addr_hint(proc_t p)
{
	u64 top = PK->proc_get_user_stack ? PK->proc_get_user_stack(p) : 0;
	if (proc_is64bit(p) && top)
		return top & ~PAGE_MASK;
	return SHARED_REGION_BASE_ARM64 - 64 * PTH_DEFAULT_STACKSIZE;
}

int lp_bsdthread_register2(proc_t p, user_addr_t threadstart, user_addr_t wqthread,
    u32 flags, user_addr_t pthread_init_data, user_addr_t pthread_init_data_size,
    u32 dispatchqueue_offset, u32 tsd_offset_unused, i32 *retval)
{
	struct reg_data data;
	u32 max_tsd_offset; size_t sz = 0;
	int pthsize = (int)flags;     /* xnu passes pthsize in the flags slot */
	(void)tsd_offset_unused;
	memset(&data, 0, sizeof(data));
	if (pthsize < 0 || pthsize > MAX_PTHREAD_SIZE) return EINVAL;
	if (pthread_init_data) {
		if (pthread_init_data_size < 8) return EINVAL;
		sz = pthread_init_data_size < sizeof(data) ? pthread_init_data_size : sizeof(data);
		int r = copyin(pthread_init_data, &data, sz);
		if (r) return r;
		if (data.version != pthread_init_data_size) return EINVAL;
	} else {
		data.dispatch_queue_offset = dispatchqueue_offset;
	}
	u64 stackaddr = stack_addr_hint(p);
	PK->proc_set_stack_addr_hint(p, stackaddr);
	if (PK->proc_get_register(p) != 0) return EINVAL;
	PK->proc_set_threadstart(p, threadstart);
	PK->proc_set_wqthread(p, wqthread);
	PK->proc_set_pthsize(p, pthsize);
	PK->proc_set_register(p);
	u32 slot = proc_is64bit(p) ? 8 : 4;
	if ((u32)pthsize >= slot && data.tsd_offset <= (u32)(pthsize - slot)) {
		max_tsd_offset = (u32)pthsize - data.tsd_offset - slot;
	} else {
		data.tsd_offset = 0; max_tsd_offset = 0;
	}
	PK->proc_set_pthread_tsd_offset(p, data.tsd_offset);
	if (data.dispatch_queue_offset > max_tsd_offset) data.dispatch_queue_offset = 0;
	PK->proc_set_dispatchqueue_offset(p, data.dispatch_queue_offset);
	if (PK->proc_set_return_to_kernel_offset) {
		if (data.return_to_kernel_offset > max_tsd_offset) data.return_to_kernel_offset = 0;
		PK->proc_set_return_to_kernel_offset(p, data.return_to_kernel_offset);
	}
	if (PK->proc_set_mach_thread_self_tsd_offset) {
		if (data.mach_thread_self_offset > max_tsd_offset) data.mach_thread_self_offset = 0;
		PK->proc_set_mach_thread_self_tsd_offset(p, data.mach_thread_self_offset);
	}
	if (PK->proc_set_workqueue_quantum_offset) {
		if (data.wq_quantum_expiry_offset > max_tsd_offset) data.wq_quantum_expiry_offset = 0;
		PK->proc_set_workqueue_quantum_offset(p, data.wq_quantum_expiry_offset);
	}
	if (pthread_init_data) {
		data.version = sizeof(data);
		data.main_qos = 0;
		if (PK->qos_main_thread_active()) {
			struct qos_policy q; u32 n = THREAD_QOS_POLICY_COUNT; int gd = 0;
			kern_return_t kr = PK->thread_policy_get(current_thread(), THREAD_QOS_POLICY, &q, &n, &gd);
			if (kr != 0 || q.qos_tier == 0) {
				q.qos_tier = THREAD_QOS_LEGACY; q.tier_importance = 0;
				kr = PK->thread_policy_set_internal(current_thread(), THREAD_QOS_POLICY, &q, THREAD_QOS_POLICY_COUNT);
			}
			if (kr == 0) data.main_qos = pp_make(q.qos_tier);
		}
		data.stack_addr_hint = stackaddr;
		data.mutex_default_policy = MUTEX_DEFAULT_POLICY;
		if (copyout(&data, pthread_init_data, sz)) return EINVAL;
	}
	*retval = PTHREAD_FEATURE_SUPPORTED;
	return 0;
}

int lp_bsdthread_register(proc_t p, user_addr_t threadstart, user_addr_t wqthread,
    int pthsize, user_addr_t init_data, user_addr_t init_size, u64 dq_off, i32 *retval)
{
	return lp_bsdthread_register2(p, threadstart, wqthread, (u32)pthsize,
	    init_data, init_size, (u32)dq_off, 0, retval);
}

int lp_bsdthread_create(proc_t p, user_addr_t user_func, user_addr_t user_funcarg,
    user_addr_t user_stack, user_addr_t user_pthread, u32 flags, user_addr_t *retval)
{
	kern_return_t kret; int error = 0; thread_t th;
	mach_port_name_t th_thport; task_t ctask = current_task();
	bool start_suspended = (flags & PTHREAD_START_SUSPENDED) != 0;
	u32 tsd_offset;

	if (PK->proc_get_register(p) == 0) return EINVAL;
	kret = PK->thread_create_immovable(ctask, &th);
	if (kret != 0) return ENOMEM;
	thread_reference(th);
	PK->thread_set_tag(th, THREAD_TAG_PTHREAD);
	void *sright = PK->convert_thread_to_port_pinned(th);
	th_thport = PK->ipc_port_copyout_send_pinned(sright, PK->task_get_ipcspace(ctask));
	if (th_thport == 0 || th_thport == ~0u) { error = EMFILE; goto out; }
	if ((flags & PTHREAD_START_CUSTOM) == 0) { error = EINVAL; goto out; }

	tsd_offset = PK->proc_get_pthread_tsd_offset(p);
	if (tsd_offset) {
		if (PK->thread_set_tsd_base(th, user_pthread + tsd_offset) == 0)
			flags |= PTHREAD_START_TSD_BASE_SET;
	}
	flags &= ~PTHREAD_START_SUSPENDED;

	struct arm_thread_state64 st;
	memset(&st, 0, sizeof(st));
	st.pc = PK->proc_get_threadstart(p);
	st.x[0] = user_pthread; st.x[1] = th_thport; st.x[2] = user_func;
	st.x[3] = user_funcarg; st.x[4] = user_stack; st.x[5] = flags;
	st.sp = user_stack;
	(void)PK->thread_set_wq_state64(th, &st);

	if (flags & PTHREAD_START_SETSCHED) {
		int ext = (((flags >> 16) & 0xff) == 1 /* POLICY_TIMESHARE */);
		int imp = (int)(flags & 0xffff) - 31;
		thread_policy_set(th, THREAD_EXTENDED_POLICY, &ext, 1);
		thread_policy_set(th, THREAD_PRECEDENCE_POLICY, &imp, 1);
	} else if (flags & PTHREAD_START_QOSCLASS) {
		u32 pp = flags & PTHREAD_START_QOSCLASS_MASK;
		struct qos_policy q = { pp_qos(pp), pp_relpri(pp) };
		if (q.qos_tier == 0 || q.tier_importance > 0 || q.tier_importance < -15) {
			error = EINVAL; goto out;
		}
		PK->thread_policy_set_internal(th, THREAD_QOS_POLICY, &q, THREAD_QOS_POLICY_COUNT);
	}
	if (PK->proc_get_mach_thread_self_tsd_offset) {
		u64 off = PK->proc_get_mach_thread_self_tsd_offset(p);
		if (off && tsd_offset) {
			u64 v = th_thport;
			error = copyout(&v, user_pthread + tsd_offset + off, 8);
			if (error) goto out;
		}
	}
	if (!start_suspended) {
		if (PK->thread_resume(th) != 0) { error = EINVAL; goto out; }
	}
	thread_deallocate(th);
	*retval = user_pthread;
	return 0;
out:
	(void)PK->mach_port_deallocate(PK->task_get_ipcspace(ctask), th_thport);
	if (PK->thread_will_park_or_terminate) PK->thread_will_park_or_terminate(th);
	(void)thread_terminate(th);
	(void)thread_deallocate(th);
	return error;
}

int lp_bsdthread_terminate(proc_t p, user_addr_t stackaddr, size_t size,
    u32 kthport, u32 sem, i32 *retval)
{
	thread_t th = current_thread();
	(void)p; (void)retval;
	if (size && stackaddr) {
		vm_map_t m = PK->current_map();
		if (PK->thread_get_tag(th) & THREAD_TAG_MAINTHREAD) {
			u64 fs = (size - 1) & ~PAGE_MASK;
			if (mach_vm_behavior_set(m, stackaddr, fs, VM_BEHAVIOR_REUSABLE) == 0)
				mach_vm_protect(m, stackaddr, fs, 0, 0);
		} else {
			mach_vm_deallocate(m, stackaddr, size);
		}
	}
	if (PK->thread_will_park_or_terminate) PK->thread_will_park_or_terminate(th);
	(void)PK->thread_terminate_pinned(th);
	if (sem) (void)PK->semaphore_signal_internal_trap(sem);
	if (kthport) PK->mach_port_deallocate(PK->task_get_ipcspace(current_task()), kthport);
	PK->thread_exception_return();
	__builtin_unreachable();
}

int lp_thread_selfid(proc_t p, u64 *retval)
{
	(void)p;
	*retval = thread_tid(current_thread());
	return 0;
}

static u64 wq_allocsize(proc_t p)
{
	u64 pts = ((u64)PK->proc_get_pthsize(p) + PTHREAD_T_OFFSET + PAGE_MASK) & ~PAGE_MASK;
	return PAGE_SZ + PTH_DEFAULT_STACKSIZE + pts;
}

int lp_workq_create_threadstack(proc_t p, vm_map_t vmap, u64 *out_addr)
{
	u64 stackaddr = PK->proc_get_stack_addr_hint(p);
	u64 sz = wq_allocsize(p);
	kern_return_t kret = mach_vm_map(vmap, &stackaddr, sz, PAGE_MASK,
	    VM_MAKE_TAG(VM_MEMORY_STACK) | VM_FLAGS_ANYWHERE, NULL, 0, 0, DARWIN_PROT_PAIR(3, 7), 1);
	if (kret != 0)
		kret = mach_vm_allocate(vmap, &stackaddr, sz, VM_MAKE_TAG(VM_MEMORY_STACK) | VM_FLAGS_ANYWHERE);
	if (kret != 0) return kret;
	kret = mach_vm_protect(vmap, stackaddr, PAGE_SZ, 0, 0);
	if (kret != 0) { mach_vm_deallocate(vmap, stackaddr, sz); return kret; }
	if (out_addr) *out_addr = stackaddr;
	return 0;
}

int lp_workq_destroy_threadstack(proc_t p, vm_map_t vmap, u64 stackaddr)
{
	return mach_vm_deallocate(vmap, stackaddr, wq_allocsize(p));
}

void lp_workq_markfree_threadstack(proc_t p, thread_t th, vm_map_t vmap, user_addr_t stackaddr)
{
	(void)p; (void)th;
	u64 freeaddr = stackaddr + PAGE_SZ;
	u64 freesize = ((PTH_DEFAULT_STACKSIZE + PAGE_SZ + PTHREAD_T_OFFSET - 1) & ~PAGE_MASK) - PAGE_SZ;
	mach_vm_behavior_set(vmap, freeaddr, freesize, VM_BEHAVIOR_REUSABLE);
}

struct wq_addrs { u64 self, stack_bottom, stack_top; };

static void wq_get_addrs(u64 stackaddr, struct wq_addrs *a)
{
	a->self = stackaddr + PTH_DEFAULT_STACKSIZE + PAGE_SZ + PTHREAD_T_OFFSET;
	a->stack_top = a->self & ~15UL;
	a->stack_bottom = stackaddr + PAGE_SZ;
}

static void wq_set_regs(proc_t p, thread_t th, struct wq_addrs *a,
    mach_port_name_t kport, u64 klist, u32 upcall_flags, int kcount)
{
	u64 fn = PK->proc_get_wqthread(p);
	if (!fn) kpanic("lapis_pthread: wqthread start is NULL");
	struct arm_thread_state64 st;
	memset(&st, 0, sizeof(st));
	st.pc = fn; st.x[0] = a->self; st.x[1] = kport; st.x[2] = a->stack_bottom;
	st.x[3] = klist; st.x[4] = upcall_flags; st.x[5] = (u64)(i64)kcount;
	st.sp = a->stack_top;
	int e = PK->thread_set_wq_state64(th, &st);
	if (e) kpanic("lapis_pthread: thread_set_wq_state64 failed"); /* no varargs: Darwin passes them on the stack */
}

static int wq_kevent(proc_t p, struct wq_addrs *a, u64 elist, int nev, int kflags,
    u64 *klist_out, int *kcount_out)
{
	u64 klist = a->self - WQ_KEVENT_LIST_LEN * KEVENT_QOS_SIZE;
	u64 dbuf = klist - WQ_KEVENT_DATA_SIZE;
	u64 avail = WQ_KEVENT_DATA_SIZE;
	int ret = PK->kevent_workq_internal(p, elist, nev, klist, WQ_KEVENT_LIST_LEN,
	    dbuf, &avail, kflags, kcount_out);
	if (ret != 0 || *kcount_out == -1) { *klist_out = 0; *kcount_out = 0; return ret; }
	a->stack_top = (dbuf + avail) & ~15UL;
	*klist_out = klist;
	return ret;
}

__attribute__((noreturn))
void lp_workq_setup_thread(proc_t p, thread_t th, vm_map_t map, user_addr_t stackaddr,
    mach_port_name_t kport, int th_qos, int setup_flags, int upcall_flags)
{
	struct wq_addrs a; u64 klist = 0; int kcount = 0;
	(void)map; (void)th_qos;
	wq_get_addrs(stackaddr, &a);
	if (setup_flags & WQ_SETUP_FIRST_USE) {
		u32 tsd = PK->proc_get_pthread_tsd_offset(p);
		if (tsd && PK->thread_set_tsd_base(th, a.self + tsd) == 0)
			upcall_flags |= WQ_FLAG_THREAD_TSD_BASE_SET;
	}
	if (setup_flags & WQ_SETUP_EXIT_THREAD) {
		kcount = WORKQ_EXIT_THREAD_NKEVENT;
	} else if (upcall_flags & WQ_FLAG_THREAD_KEVENT) {
		wq_kevent(p, &a, 0, 0, KEVENT_FLAG_STACK_DATA | KEVENT_FLAG_IMMEDIATE, &klist, &kcount);
	}
	wq_set_regs(p, th, &a, kport, klist, (u32)upcall_flags, kcount);
	if (setup_flags & WQ_SETUP_FIRST_USE) PK->thread_bootstrap_return();
	else PK->unix_syscall_return(EJUSTRETURN);
	__builtin_unreachable();
}

int lp_workq_handle_stack_events(proc_t p, thread_t th, vm_map_t map, user_addr_t stackaddr,
    mach_port_name_t kport, user_addr_t events, int nevents, int upcall_flags)
{
	struct wq_addrs a; u64 klist = 0; int kcount = 0, err;
	(void)map;
	wq_get_addrs(stackaddr, &a);
	err = wq_kevent(p, &a, events, nevents,
	    KEVENT_FLAG_STACK_DATA | KEVENT_FLAG_IMMEDIATE | KEVENT_FLAG_PARKING, &klist, &kcount);
	if (err || kcount == 0) return err;
	(void)PK->thread_set_voucher_name(0);
	wq_set_regs(p, th, &a, kport, klist, (u32)upcall_flags, kcount);
	PK->unix_syscall_return(EJUSTRETURN);
	__builtin_unreachable();
}

void lp_void(void) { }
void lp_proc_void(proc_t p) { (void)p; }
int lp_enotsup(void) { return ENOTSUP; }

/* struct pthread_functions_s, xnu-12377 bsd/sys/pthread_shims.h */
__attribute__((section(".text.table"), used))
const void *const lapis_pthread_functions[0x160 / 8 + 83] = {
	[0x00 / 8] = (void *)0,                     /* version 0: xnu calls bsdthread_register (as pthread.kext) */
	[0x08 / 8] = (void *)lp_void,               /* pthread_init */
	[0x40 / 8] = (void *)lp_proc_void,          /* pth_proc_hashinit */
	[0x48 / 8] = (void *)lp_proc_void,          /* pth_proc_hashdelete */
	[0x50 / 8] = (void *)lp_bsdthread_create,
	[0x58 / 8] = (void *)lp_bsdthread_register,
	[0x60 / 8] = (void *)lp_bsdthread_terminate,
	[0x68 / 8] = (void *)lp_thread_selfid,
	[0x80 / 8] = (void *)lp_enotsup,            /* psynch_mutexwait */
	[0x88 / 8] = (void *)lp_enotsup,            /* psynch_mutexdrop */
	[0x90 / 8] = (void *)lp_enotsup,            /* psynch_cvbroad */
	[0x98 / 8] = (void *)lp_enotsup,            /* psynch_cvsignal */
	[0xa0 / 8] = (void *)lp_enotsup,            /* psynch_cvwait */
	[0xa8 / 8] = (void *)lp_enotsup,            /* psynch_cvclrprepost */
	[0xb0 / 8] = (void *)lp_enotsup,            /* psynch_rw_longrdlock */
	[0xb8 / 8] = (void *)lp_enotsup,            /* psynch_rw_rdlock */
	[0xc0 / 8] = (void *)lp_enotsup,            /* psynch_rw_unlock */
	[0xc8 / 8] = (void *)lp_enotsup,            /* psynch_rw_wrlock */
	[0xd0 / 8] = (void *)lp_enotsup,            /* psynch_rw_yieldwrlock */
	[0xe0 / 8] = (void *)lp_bsdthread_register2,
	/* 0x120 pthread_find_owner, 0x128 pthread_get_thread_kwq: NULL (checked) */
	[0x138 / 8] = (void *)lp_workq_handle_stack_events,
	[0x140 / 8] = (void *)lp_workq_create_threadstack,
	[0x148 / 8] = (void *)lp_workq_destroy_threadstack,
	[0x150 / 8] = (void *)lp_workq_setup_thread,
	[0x158 / 8] = (void *)lp_workq_markfree_threadstack,
};
