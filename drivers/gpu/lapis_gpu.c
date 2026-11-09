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
 *   lapis_gpu_mode=3  CP ringbuffer + CP_NOP; optional 2D fill stub
 *   lapis_gpu_gdsc=1  allow GDSC/clock writes (DANGEROUS — refused unless wired)
 *   lapis_gpu_submit=1 allow ringing CP WPTR
 */
#include "../include/kern.h"
#include "../scm/qcom_scm.h"
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
		if (!zap_mdt) {
			LOG("mode>=2: stage gen71100_zap.mbn at a PA and set lapis_gpu_zap_pa/len");
		} else {
			vm_offset_t zva = ml_io_map((vm_offset_t)zap_mdt, zap_len ? zap_len : 0x4000);
			if (!zva) { LOG("zap map fail"); }
			else {
				int rc = lapis_pil_load_mdt(PAS_GPU_ZAP, (void *)zva,
				                            zap_len ? zap_len : 12088,
				                            ZAP_PA, ZAP_SZ, NULL, NULL);
				LOG("ZAP PAS load -> %d", rc);
			}
		}
	}

	if (mode >= 3) {
		if (!drv_bootarg("lapis_gpu_submit", 0)) {
			LOG("mode>=3: ringbuffer submit gated (set lapis_gpu_submit=1 after ZAP OK)");
			return;
		}
		/* CP ring: 4K, CP_NOP + CP_MEM_WRITE fence to scratch, then optional FB fill plan. */
		uint32_t *rb = IOMallocAligned(0x1000, 0x1000);
		uint32_t *scratch = IOMallocAligned(0x1000, 0x1000);
		if (!rb || !scratch) { LOG("rb/scratch alloc fail"); return; }
		bzero(rb, 0x1000); bzero(scratch, 0x1000);
		pmap_paddr_t rb_pa = kvtophys((vm_offset_t)rb);
		pmap_paddr_t sc_pa = kvtophys((vm_offset_t)scratch);
		unsigned i = 0;
		rb[i++] = (CP_NOP << 8) | 1; rb[i++] = 0;
		/* CP_MEM_WRITE: write 0xLAP1 to scratch */
		rb[i++] = (CP_MEM_WRITE << 8) | 3;
		rb[i++] = (uint32_t)sc_pa; rb[i++] = (uint32_t)(sc_pa >> 32); rb[i++] = 0x4c415031;
		rb[i++] = (CP_WAIT_MEM_WRITES << 8) | 0;
		for (uintptr_t a = (uintptr_t)rb & ~63ul; a < (uintptr_t)rb + 0x1000; a += 64)
			__asm__ volatile("dc civac, %0" :: "r"(a) : "memory");
		dsb_sy();
		wr32(kgsl + REG_CP_RB_CNTL, 0x8001);
		wr32(kgsl + REG_CP_RB_BASE, (uint32_t)rb_pa);
		wr32(kgsl + REG_CP_SCRATCH_UADDR, (uint32_t)sc_pa);
		wr32(kgsl + REG_CP_RB_WPTR, i);
		LOG("CP RB base %#llx wptr %u; RBBM %#x scratch[0]=%#x",
		    rb_pa, i, rd32(kgsl + REG_RBBM_STATUS), scratch[0]);
		LOG("2D FB fill: would CP_BLIT to splash 0xD5100000 — NOT DONE without GMU HFI + SQE fw running");
	}
}
void lapis_gpu_start(void) { drv_thread(gpu_thread, NULL); }
