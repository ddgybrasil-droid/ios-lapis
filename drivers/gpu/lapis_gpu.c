
/*
 * LapisGPU — Adreno 750 (gen7_9_1, chipid 0x43051401).
 *
 * Hardware (dtb VERIFIED):
 *   KGSL 0x3D00000 size 0x40000, GPUCC 0x3D90000, GMU 0x3D68000, RSC 0x3D50000
 *   ZAP carveout microcode_region @ 0x9b09a000 size 0x2000
 *   zap-shader mbn: vendor_firmware/gen71100_zap.mbn (12 KiB)
 *
 * Boot-args:
 *   lapis_gpu_mode=0  probe-only (default)
 *   lapis_gpu_mode=1  RBBM/GMU/RSC status dump
 *   lapis_gpu_mode=2  ZAP PAS load via lapis_pil (needs lapis_scm_pas=1)
 *   lapis_gpu_mode=3  type-7 encoding diagnostic; submission is unavailable
 *   lapis_gpu_gdsc=1  allow GDSC/clock writes (DANGEROUS — refused unless wired)
 *   lapis_gpu_submit=1 is refused until the command path is implemented
 */
#include "../include/kern.h"
#include "../scm/qcom_scm.h"
#include "lapis_pm4.h"
#include "lapis_hfi.h"
extern void ml_io_unmap(vm_offset_t, vm_offset_t);
#define LOG(fmt, ...) klog("LapisGPU: " fmt "\n", ##__VA_ARGS__)
#define KGSL_PA    0x03D00000ull
#define KGSL_SZ    0x40000u
#define GPUCC_PA   0x03D90000ull
#define GPUCC_SZ   0x2000u
#define GMU_PA     0x03D68000ull
#define RSC_PA     0x03D50000ull
#define ZAP_PA     0x9b09a000ull
#define ZAP_SZ     0x2000u
#define REG_HW_VERSION   0x0
#define REG_HW_STATUS    0x4
#define REG_RBBM_STATUS  0x218
#define REG_RBBM_INT_0_MASK 0x220
#define REG_CP_RB_BASE   0x980
#define REG_CP_RB_CNTL   0x984
#define REG_CP_RB_RPTR   0x988
#define REG_CP_RB_WPTR   0x98c
#define REG_CP_ME_CNTL   0x0f70
#define REG_CP_SQE_CNTL  0x0f80
#define REG_CP_SCRATCH_UADDR 0x9c0
#define REG_CP_PFP_UCODE_ADDR 0x0f00
#define REG_CP_PFP_UCODE_DATA 0x0f04
#define CP_NOP           0x10
#define CP_SET_DRAW_STATE 0x43
#define CP_MEM_WRITE     0x3d
#define CP_WAIT_MEM_WRITES 0x12
/* GMU (gen7) */
#define GMU_CM3_SYSRESET  0x1b80
#define GMU_PWR_COL_KEEPALIVE 0x1b88
#define GMU_HFI_CTRL_STATUS 0x1c40

static void gpu_thread(void *a, int w)
{
	(void)a;(void)w;
	int mode = drv_bootarg("lapis_gpu_mode", 0);
	vm_offset_t kgsl = ml_io_map((vm_offset_t)KGSL_PA, KGSL_SZ);
	vm_offset_t cc   = ml_io_map((vm_offset_t)GPUCC_PA, GPUCC_SZ);
	vm_offset_t gmu  = ml_io_map((vm_offset_t)GMU_PA, 0x4000);
	vm_offset_t rsc  = ml_io_map((vm_offset_t)RSC_PA, 0x2000);
	if (!kgsl) { LOG("KGSL map failed"); return; }
	uint32_t ver = rd32(kgsl + REG_HW_VERSION);
	uint32_t st  = rd32(kgsl + REG_HW_STATUS);
	uint32_t rbbm = rd32(kgsl + REG_RBBM_STATUS);
	LOG("KGSL @ %#llx HW_VERSION %#x STATUS %#x RBBM_STATUS %#x", KGSL_PA, ver, st, rbbm);
	if (ver == 0 || ver == 0xffffffffu)
		LOG("HW_VERSION looks powered-off/clock-gated (expected before GDSC)");
	if (cc) LOG("GPUCC @ %#llx raw@0 %#x", GPUCC_PA, rd32(cc));
	if (gmu) LOG("GMU @ %#llx SYSRESET %#x HFI_STATUS %#x", GMU_PA,
	             rd32(gmu + GMU_CM3_SYSRESET), rd32(gmu + GMU_HFI_CTRL_STATUS));
	if (rsc) LOG("RSC @ %#llx raw@0 %#x", RSC_PA, rd32(rsc));
	LOG("Adreno750v2 expect chipid 0x43051401; ZAP carveout %#llx", ZAP_PA);
	if (drv_bootarg("lapis_gpu_submit", 0))
		LOG("lapis_gpu_submit=1 refused: no HFI queue and no CP submission");

	if (mode == 0) { LOG("probe-only (safe)"); return; }

	if (mode >= 1) {
		LOG("GDSC/clock plan (dtb): gcc_gpu_memnoc_gfx, gpu_cc_ahb, GMU clocks; zap via PAS");
		LOG("ME_CNTL %#x SQE_CNTL %#x", rd32(kgsl + REG_CP_ME_CNTL), rd32(kgsl + REG_CP_SQE_CNTL));
		if (drv_bootarg("lapis_gpu_gdsc", 0))
			LOG("lapis_gpu_gdsc=1: GDSC toggles NOT wired this build (refusing blind writes)");
	}

	if (mode >= 2) {
		pmap_paddr_t zap_mdt = 0; uint32_t zap_len = 0;
		PE_parse_boot_argn("lapis_gpu_zap_pa", &zap_mdt, sizeof(zap_mdt));
		PE_parse_boot_argn("lapis_gpu_zap_len", &zap_len, sizeof(zap_len));
		/* Never infer the size of a mapped/staged binary. */
		if (!zap_mdt || !zap_len || zap_len > 0x100000u ||
		    zap_mdt > ~(pmap_paddr_t)0 - zap_len) {
			LOG("mode>=2: need explicit staged ZAP PA and length (1..1048576)");
		} else {
			vm_offset_t zva = ml_io_map((vm_offset_t)zap_mdt, zap_len);
			if (!zva) LOG("zap map failed");
			else {
				int rc = lapis_pil_load_mdt(PAS_GPU_ZAP, (void *)zva,
				                            zap_len, ZAP_PA, ZAP_SZ, NULL, NULL);
				ml_io_unmap(zva, zap_len);
				LOG("ZAP PAS load -> %d", rc);
			}
		}
	}

	if (mode >= 3) {
		unsigned nop, write, wait, hfi;
		if (lapis_pm4_type7(CP_NOP, 1, &nop) ||
		    lapis_pm4_type7(CP_MEM_WRITE, 3, &write) ||
		    lapis_pm4_type7(CP_WAIT_MEM_WRITES, 0, &wait) ||
		    lapis_hfi_header(LAPIS_HFI_INIT, LAPIS_HFI_INIT_DWORDS, LAPIS_HFI_CMD, 1, &hfi)) {
			LOG("PM4/HFI encoding failed; submission refused");
			return;
		}
		LOG("PM4 type7 headers (not submitted): NOP=%#x MEM_WRITE=%#x WAIT=%#x",
		    nop, write, wait);
		LOG("HFI INIT header %#x; queue is not allocated, so it is not written", hfi);
		/* The previous path submitted invalid packets and used CPU physical
		 * addresses as GPU addresses even if PAS had failed. Do not touch CP
		 * registers until GMU/HFI, SQE, ZAP and the GPU address domain exist. */
		LOG("CP submission refused: GMU/HFI, SQE and GPU address mapping not implemented");
		return;
	}

}
void lapis_gpu_start(void) { drv_thread(gpu_thread, NULL); }


