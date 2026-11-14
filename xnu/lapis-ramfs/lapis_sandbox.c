/*
 * lapis_sandbox: minimal in-kernel "Sandbox" MAC policy for the iPadOS 26 bring-up kernel
 * (xnu-12377.121.6, no Sandbox.kext in this kernel).
 *
 * Why (chunk 17):
 * - boot55 console (VERIFIED): launchd "denied lookup: ... error = 159: Sandbox restriction"
 *   for UserEventAgent lookups (com.apple.logd / notification_center / commcenter.xpc).
 * - boot55 XPC_LOOKUP_RET (VERIFIED): every traced _xpc_look_up_endpoint return, including
 *   com.apple.containermanagerd.system from backboardd/PerfPowerServices, has x0=0x9f (=159).
 * - launchd authorises mach-lookups with sandbox_check_by_audit_token(..., "mach-lookup", name)
 *   -> sandbox_check_common -> __mac_syscall("Sandbox", 2, args). With no policy named
 *   "Sandbox", ___mac_syscall returns ENOPOLICY (103), sandbox_check returns -1, and launchd
 *   maps that to xpc error 159 -> no demand-launch of containermanagerd.system -> backboardd
 *   BSSystemContainerForCurrentProcessPathProvider gets nil.
 *
 * This policy answers check the way "no sandbox profile applied" does (same bring-up class as
 * lapis_amfi): mpo_policy_syscall call 2 returns 0; the caller's result word is left untouched
 * (pre-zeroed = allowed). Every other Sandbox call still returns ENOPOLICY (sandbox_init etc.
 * unchanged). No label slot, no other hooks.
 *
 * mac_policy_ops layout VERIFIED against apple-oss-distributions xnu-12377.121.6
 * security/mac_policy.h: 335 entries, mpo_policy_syscall at index 117.
 */
#include <stdint.h>
#include <stddef.h>

#define ENOPOLICY 103
#define MAC_OPS_COUNT 335            /* struct mac_policy_ops (security/mac_policy.h, xnu-12377) */
#define MPO_POLICY_SYSCALL 117       /* index of mpo_policy_syscall in it */
#define SANDBOX_CALL_CHECK 2

struct mac_policy_conf {
	const char *mpc_name, *mpc_fullname;
	char const * const *mpc_labelnames;
	unsigned int mpc_labelname_count;
	const void *mpc_ops;
	int mpc_loadtime_flags;
	int *mpc_field_off;
	int mpc_runtime_flags;
	void *mpc_list, *mpc_data;
};
extern int mac_policy_register(struct mac_policy_conf *, unsigned int *, void *);
extern void *IOMallocZero(size_t);
extern void IOLog(const char *, ...);

static int lsb_policy_syscall(void *p, int call, uint64_t arg)
{
	(void)p; (void)arg;
	return call == SANDBOX_CALL_CHECK ? 0 : ENOPOLICY;
}

void lapis_sandbox_register(void)
{
	static const char name[] = "Sandbox", full[] = "Lapis bring-up sandbox (no profiles; check = allow)";
	struct mac_policy_conf *c = IOMallocZero(sizeof(*c));
	void **ops = IOMallocZero(MAC_OPS_COUNT * sizeof(void *));
	unsigned int h = 0;
	int e;
	if (!c || !ops) { IOLog("LAPIS sandbox: alloc failed\n"); return; }
	ops[MPO_POLICY_SYSCALL] = (void *)lsb_policy_syscall;
	c->mpc_name = name; c->mpc_fullname = full; c->mpc_ops = ops;
	e = mac_policy_register(c, &h, 0);
	/* IOLog on this kernel uses the Apple stack-varargs ABI; multi-arg
	 * printf from gcc is garbage (same note as lapis_amfi). One string only. */
	if (e)
		IOLog("LAPIS sandbox: mac_policy_register FAILED\n");
	else
		IOLog("LAPIS sandbox: check-allow policy registered\n");
}
