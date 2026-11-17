/*
 * lapis_sandbox: minimal in-kernel "Sandbox" MAC policy for the iPadOS 26 bring-up kernel
 * (xnu-12377.121.6, no Sandbox.kext in this kernel).
 *
 * Why (chunk 17, VERIFIED with a user-space trace): every launchd lookup made by backboardd /
 * PerfPowerServices (_xpc_look_up_endpoint, launchd routine 0x324) came back with error 159 =
 * "Sandbox restriction" (libxpc __errors[159-106]). launchd authorises mach-lookups with
 * sandbox_check_by_audit_token(..., "mach-lookup", name) -> libsystem_sandbox
 * sandbox_check_common -> __mac_syscall("Sandbox", 2, args). With no policy named "Sandbox"
 * registered, xnu's __mac_syscall returns ENOPOLICY, sandbox_check returns -1 (error), and
 * launchd treats that as a denial, so containermanagerd.system (and everything else looked up
 * that way) was never resolved / demand-launched -> backboardd got no system container.
 *
 * This policy implements the sandbox check call the way "no sandbox profile applied" answers it
 * (this kernel enforces no sandbox profiles at all - same bring-up policy class as the AMFI
 * provider): call 2 (check) returns 0 and leaves the caller's result word untouched (0 =
 * allowed). Every other Sandbox call keeps its previous behaviour (ENOPOLICY), so
 * sandbox_init/extension/etc. users see exactly what they saw before.
 * The policy has no label slot and no other hooks.
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
	IOLog("LAPIS sandbox: mac_policy_register -> %d (handle %u)\n", e, h);
}
