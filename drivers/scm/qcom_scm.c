/*
 * qcom_scm - SMCCC client for Qualcomm Secure Channel (port of Linux
 * drivers/firmware/qcom/qcom_scm-smc.c convention A, 32-bit SIP).
 *
 * On TB520FU under Gunyah, SMCs are forwarded to TZ (stock Android does the
 * same from EL1). Default: only is_call_available probes. Real PAS/PIL
 * auth_and_reset behind lapis_scm_pas=1.
 */
#include "qcom_scm.h"

#define LOG(fmt, ...) klog("LapisSCM: " fmt "\n", ##__VA_ARGS__)

static void smc_full(uint64_t fn, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5,
                     struct qcom_scm_res *res)
{
	register uint64_t r0 __asm__("x0") = fn;
	register uint64_t r1 __asm__("x1") = a1;
	register uint64_t r2 __asm__("x2") = a2;
	register uint64_t r3 __asm__("x3") = a3;
	register uint64_t r4 __asm__("x4") = a4;
	register uint64_t r5 __asm__("x5") = a5;
	__asm__ volatile("smc #0" : "+r"(r0), "+r"(r1), "+r"(r2), "+r"(r3), "+r"(r4), "+r"(r5)
	                 : : "x6","x7","memory");
	if (res) { res->a0 = (int64_t)r0; res->a1 = (int64_t)r1; res->a2 = (int64_t)r2; res->a3 = (int64_t)r3; }
}

int qcom_scm_call(uint32_t svc, uint32_t cmd, uint32_t arginfo,
                  uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3,
                  struct qcom_scm_res *res)
{
	uint32_t fn = qcom_scm_fn(svc, cmd, arginfo);
	struct qcom_scm_res r;
	smc_full(fn, arginfo, a0, a1, a2, a3, &r);
	/* SMCCC V1 atomic: r0 = SMCCC return; for QCOM, r0 can be QUEUED and need retry.
	 * Simplified: treat negative as error. */
	int tries = 0;
	while ((int64_t)r.a0 == 1 /* QCOM_SCM_INTERRUPTED */ && tries++ < 8)
		smc_full(fn, arginfo, a0, a1, a2, a3, &r);
	if (res) *res = r;
	return (int)r.a0;
}

int qcom_scm_is_call_available(uint32_t svc, uint32_t cmd)
{
	/* SVC_INFO / IS_CALL_AVAIL with fn encoded as first arg */
	uint32_t fn = qcom_scm_fn(svc, cmd, 0);
	struct qcom_scm_res r;
	int rc = qcom_scm_call(QCOM_SCM_SVC_INFO, QCOM_SCM_INFO_IS_CALL_AVAIL,
	                       0x1 /* 1 VAL arg */, fn, 0, 0, 0, &r);
	if (rc) return 0;
	return (int)r.a1; /* available flag in a1 on success */
}

int qcom_scm_pas_supported(uint32_t pas_id)
{
	struct qcom_scm_res r;
	int rc = qcom_scm_call(QCOM_SCM_SVC_PIL, QCOM_SCM_PIL_PAS_IS_SUPPORTED,
	                       0x1, pas_id, 0, 0, 0, &r);
	return rc == 0 && r.a1 != 0;
}
int qcom_scm_pas_init_image(uint32_t pas_id, pmap_paddr_t meta_pa)
{
	struct qcom_scm_res r;
	return qcom_scm_call(QCOM_SCM_SVC_PIL, QCOM_SCM_PIL_PAS_INIT_IMAGE,
	                     0x21 /* VAL + RO buf */, pas_id, meta_pa, 0, 0, &r);
}
int qcom_scm_pas_mem_setup(uint32_t pas_id, pmap_paddr_t addr, uint64_t size)
{
	struct qcom_scm_res r;
	return qcom_scm_call(QCOM_SCM_SVC_PIL, QCOM_SCM_PIL_PAS_MEM_SETUP,
	                     0x3, pas_id, addr, size, 0, &r);
}
int qcom_scm_pas_auth_and_reset(uint32_t pas_id)
{
	struct qcom_scm_res r;
	return qcom_scm_call(QCOM_SCM_SVC_PIL, QCOM_SCM_PIL_PAS_AUTH_AND_RESET,
	                     0x1, pas_id, 0, 0, 0, &r);
}
int qcom_scm_pas_shutdown(uint32_t pas_id)
{
	struct qcom_scm_res r;
	return qcom_scm_call(QCOM_SCM_SVC_PIL, QCOM_SCM_PIL_PAS_SHUTDOWN,
	                     0x1, pas_id, 0, 0, 0, &r);
}

void lapis_scm_start(void)
{
	/* Safe default: query only. */
	int avail_init = qcom_scm_is_call_available(QCOM_SCM_SVC_PIL, QCOM_SCM_PIL_PAS_INIT_IMAGE);
	int avail_auth = qcom_scm_is_call_available(QCOM_SCM_SVC_PIL, QCOM_SCM_PIL_PAS_AUTH_AND_RESET);
	int avail_info = qcom_scm_is_call_available(QCOM_SCM_SVC_INFO, QCOM_SCM_INFO_IS_CALL_AVAIL);
	LOG("SCM probe: INFO.IS_CALL_AVAIL=%d PIL.INIT_IMAGE=%d PIL.AUTH_AND_RESET=%d",
	    avail_info, avail_init, avail_auth);
	if (!avail_auth) {
		LOG("PIL AUTH_AND_RESET not reported available (HYPOTHESIS: Gunyah filter or wrong convention); will still attempt if lapis_scm_pas=1");
	}
	if (!drv_bootarg("lapis_scm_pas", 0)) {
		LOG("PAS auth_and_reset gated (set lapis_scm_pas=1 to run)");
		return;
	}
	/* PAS ids from SM8650 DT / Linux: ADSP=1, MPSS=2, CDSP etc. — query supported first. */
	static const struct { uint32_t id; const char *name; } pas[] = {
		{1, "adsp"}, {2, "mpss"}, {3, "spare"}, {4, "wlan_cx"}, {6, "cdsp"}, {12, "wpss"},
	};
	for (unsigned i = 0; i < sizeof(pas)/sizeof(pas[0]); i++) {
		int s = qcom_scm_pas_supported(pas[i].id);
		LOG("PAS id %u (%s) supported=%d", pas[i].id, pas[i].name, s);
	}
	LOG("full PIL load (mdt+bNN + mem_setup + auth_and_reset) requires firmware carveouts; see lapis_pil.c");
}
