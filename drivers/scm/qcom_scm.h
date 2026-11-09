#pragma once
#include "../include/kern.h"
/* ARM SMCCC / QCOM SIP owner */
#define ARM_SMCCC_OWNER_SIP          2
#define ARM_SMCCC_FAST_CALL          (1u << 31)
#define ARM_SMCCC_SMC_32             (0u << 30)
#define ARM_SMCCC_SMC_64             (1u << 30)
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

static inline uint32_t qcom_scm_fn(uint32_t svc, uint32_t cmd, int arginfo)
{
	return ARM_SMCCC_FAST_CALL | ARM_SMCCC_SMC_32 | (ARM_SMCCC_OWNER_SIP << 24)
	       | ((svc & 0xff) << 8) | (cmd & 0xff);
}
/* Convention A: arginfo in a1 low bits = nargs | (type<<4)*n */
#define QCOM_SCM_ARGS_IMPL(n, ...)  (((n) & 0xf) | (__VA_ARGS__))
#define QCOM_SCM_ARG_VAL   0
#define QCOM_SCM_ARG_RO    1
#define QCOM_SCM_ARG_RW    2
#define QCOM_SCM_ARG_BUFVAL 3

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
