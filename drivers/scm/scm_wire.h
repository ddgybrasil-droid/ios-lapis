/* SPDX-License-Identifier: BSD-2-Clause */
#pragma once
#ifdef LAPIS_KERNEL
#include "../include/kern.h"
#else
#include <stdint.h>
#endif
#define LAPIS_SCM_INIT_IMAGE_ARGS 0x82u /* two arguments: VAL, RW */
struct lapis_smc_regs { uint64_t x[8]; };
typedef void (*lapis_smc_transport)(struct lapis_smc_regs *,void *);
static inline uint32_t lapis_scm_query_id(unsigned svc,unsigned cmd)
{ return (2u<<24)|((svc&255u)<<8)|(cmd&255u); }
static inline uint32_t lapis_scm_call_id(unsigned svc,unsigned cmd,int wide,int fast)
{ return lapis_scm_query_id(svc,cmd)|(wide?1u<<30:0)|(fast?1u<<31:0); }
/* Resume interrupted calls with x0=1 and the returned Qualcomm x6 context.
 * Arguments x1..x5 remain those of the original request (SMCCC quirk A6). */
static inline int lapis_scm_exchange(lapis_smc_transport call,void *ctx,
    uint32_t fn,uint32_t info,const uint64_t args[4],struct lapis_smc_regs *out)
{
	struct lapis_smc_regs r={{fn,info,args[0],args[1],args[2],args[3],0,0}};
	for(unsigned attempt=0;attempt<64;attempt++) {
		call(&r,ctx);
		if((int64_t)r.x[0]!=1) { *out=r;return (int)r.x[0]; }
		uint64_t saved=r.x[6];
		r=(struct lapis_smc_regs){{1,info,args[0],args[1],args[2],args[3],saved,0}};
	}
	*out=r;return -1; /* bounded failure, never a successful PAS result */
}
static inline int lapis_scm_pas_result(int transport,int64_t result)
{ return transport?transport:(int)result; }
