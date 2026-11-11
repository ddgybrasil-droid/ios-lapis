
#pragma once
#include "../include/kern.h"
#include "scm_wire.h"
/* ARM SMCCC / QCOM SIP owner — convention A, SMC32 FAST (Linux qcom_scm-smc.c) */
#define ARM_SMCCC_OWNER_SIP          2
#define ARM_SMCCC_FAST_CALL          (1u << 31)
#define ARM_SMCCC_SMC_32             (0u << 30)
#define QCOM_SCM_SVC_PIL             0x02
#define QCOM_SCM_PIL_PAS_INIT_IMAGE  0x01
#define QCOM_SCM_PIL_PAS_MEM_SETUP   0x02
#define QCOM_SCM_PIL_PAS_AUTH_AND_RESET 0x05
#define QCOM_SCM_PIL_PAS_SHUTDOWN    0x06
#define QCOM_SCM_PIL_PAS_IS_SUPPORTED 0x07
#define QCOM_SCM_SVC_INFO            0x06
#define QCOM_SCM_INFO_IS_CALL_AVAIL  0x01
#define QCOM_SCM_SVC_MP              0x0c
#define QCOM_SCM_MP_CP_RESUME        0x0a

/*
 * SM8650 / pineapple PAS ids — Linux qcom_q6v5_pas table defaults.
 * TB520FU dtb: remoteproc-adsp has NO qcom,pas-id. DT ids 0x1c/0x22/0x23 are
 * Gunyah secure VM loaders (trustedvm/oemvm/cpusys_vm), not ADSP/CDSP.
 * Confirm every id with pas_supported() on hardware before auth_and_reset.
 */
#define PAS_ADSP    1
#define PAS_MPSS    2
#define PAS_SLPI    3
#define PAS_CDSP    5
#define PAS_WPSS    6
#define PAS_GPU_ZAP 9
#define PAS_GH_TVM  0x1c
#define PAS_GH_OEM  0x22
#define PAS_GH_CPU  0x23

static inline uint32_t qcom_scm_fn(uint32_t svc, uint32_t cmd, int arginfo)
{
	(void)arginfo;
	return lapis_scm_query_id(svc,cmd);
}

struct qcom_scm_res { int64_t a0, a1, a2, a3; };

int  qcom_scm_call(uint32_t svc, uint32_t cmd, uint32_t arginfo,
                   uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3,
                   struct qcom_scm_res *res);
int  qcom_scm_is_call_available(uint32_t svc, uint32_t cmd);
int  qcom_scm_pas_supported(uint32_t pas_id);
int  qcom_scm_pas_init_image(uint32_t pas_id, pmap_paddr_t meta_pa);
int  qcom_scm_pas_mem_setup(uint32_t pas_id, pmap_paddr_t addr, uint64_t size);
int  qcom_scm_pas_auth_and_reset(uint32_t pas_id);
int  qcom_scm_pas_shutdown(uint32_t pas_id);
void lapis_scm_start(void);

/* PIL. seg_read addresses an individual split .bNN file: offset is zero,
 * len is the exact p_filesz; the callback must reject truncated input. */
int  lapis_pil_auth(uint32_t pas_id, pmap_paddr_t meta_pa, pmap_paddr_t mem_pa, uint64_t mem_sz);
int  lapis_pil_load_mdt(uint32_t pas_id, const void *mdt, uint32_t mdt_len,
                        pmap_paddr_t carveout_pa, uint64_t carveout_sz,
                        int (*seg_read)(unsigned idx, void *dst, uint32_t off, uint32_t len, void *ctx),
                        void *ctx);
void lapis_pil_start(void);


