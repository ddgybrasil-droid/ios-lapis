/*
 * LapisGPU — Adreno 750 (gen7_9_1, chipid 0x43051401).
 *
 * Hardware (dtb VERIFIED):
 *   KGSL 0x3D00000 size 0x40000, GPUCC 0x3D90000, GMU 0x3D68000
 *   ZAP carveout microcode_region @ 0x9b09a000 size 0x2000
 *   zap-shader mbn: vendor_firmware/gen71100_zap.mbn (shipped, 12 KiB)
 *
 * Boot-args:
 *   lapis_gpu_mode=0  probe-only (default)
 *   lapis_gpu_mode=1  read more RBBM/status; log GDSC/clock plan from dtb
 *   lapis_gpu_mode=2  attempt ZAP PAS load via lapis_pil (needs lapis_scm_pas=1)
 *   lapis_gpu_mode=3  after ZAP, program a minimal ringbuffer + CP_NOP (hw only)
 *   lapis_gpu_gdsc=1  allow GDSC/clock writes (DANGEROUS)
 */
#include "../include/kern.h"
#include "../scm/qcom_scm.h"
#define LOG(fmt, ...) klog("LapisGPU: " fmt "\n", ##__VA_ARGS__)
#define KGSL_PA    0x03D00000ull
#define KGSL_SZ    0x40000u
#define GPUCC_PA   0x03D90000ull
#define GPUCC_SZ   0x2000u
#define GMU_PA     0x03D68000ull
#define ZAP_PA     0x9b09a000ull
#define ZAP_SZ     0x2000u
#define REG_HW_VERSION   0x0
#define REG_HW_STATUS    0x4
#define REG_RBBM_STATUS  0x218
#define REG_CP_RB_BASE   0x980
#define REG_CP_RB_CNTL   0x984
#define REG_CP_RB_RPTR   0x988
#define REG_CP_RB_WPTR   0x98c
#define REG_CP_SCRATCH_UADDR 0x9c0
#define CP_NOP           0x10
#define CP_WAIT_MEM_WRITES 0x12

static void gpu_thread(void *a, int w)
{
	(void)a;(void)w;
	int mode = drv_bootarg("lapis_gpu_mode", 0);
	vm_offset_t kgsl = ml_io_map((vm_offset_t)KGSL_PA, KGSL_SZ);
	vm_offset_t cc   = ml_io_map((vm_offset_t)GPUCC_PA, GPUCC_SZ);
	if (!kgsl) { LOG("KGSL map failed"); return; }
	uint32_t ver = rd32(kgsl + REG_HW_VERSION);
	uint32_t st  = rd32(kgsl + REG_HW_STATUS);
	uint32_t rbbm = rd32(kgsl + REG_RBBM_STATUS);
	LOG("KGSL @ %#llx HW_VERSION %#x STATUS %#x RBBM_STATUS %#x", KGSL_PA, ver, st, rbbm);
	if (ver == 0 || ver == 0xffffffffu)
		LOG("HW_VERSION looks powered-off/clock-gated (expected before GDSC)");
	if (cc) LOG("GPUCC @ %#llx raw@0 %#x", GPUCC_PA, rd32(cc));
	LOG("Adreno750v2 expect chipid 0x43051401; ZAP carveout %#llx", ZAP_PA);

	if (mode == 0) { LOG("probe-only (safe)"); return; }

	if (mode >= 1) {
		LOG("GDSC/clock plan (dtb): gcc_gpu_memnoc_gfx, gpu_cc_ahb, GMU clocks; zap via PAS");
		if (drv_bootarg("lapis_gpu_gdsc", 0)) {
			LOG("lapis_gpu_gdsc=1: GDSC toggles NOT wired this build (refusing blind writes)");
		}
	}

	if (mode >= 2) {
		pmap_paddr_t zap_mdt = 0; uint32_t zap_len = 0;
		PE_parse_boot_argn("lapis_gpu_zap_pa", &zap_mdt, sizeof(zap_mdt));
		PE_parse_boot_argn("lapis_gpu_zap_len", &zap_len, sizeof(zap_len));
		if (!zap_mdt) {
			LOG("mode>=2: stage gen71100_zap.mbn at a PA and set lapis_gpu_zap_pa/len");
			LOG("then lapis_scm_pas=1 lapis_pil_pas=%u", PAS_GPU_ZAP);
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
		/* Minimal CP ring: allocate 4K, write CP_NOP packet, set RB regs.
		 * Only safe once clocks+ZAP are up — still experimental. */
		uint32_t *rb = IOMallocAligned(0x1000, 0x1000);
		if (!rb) { LOG("rb alloc fail"); return; }
		bzero(rb, 0x1000);
		/* CP packet: CP_NOP with 1 dword payload */
		rb[0] = (CP_NOP << 8) | 1;
		rb[1] = 0;
		pmap_paddr_t rb_pa = kvtophys((vm_offset_t)rb);
		for (uintptr_t a = (uintptr_t)rb & ~63ul; a < (uintptr_t)rb + 0x1000; a += 64)
			__asm__ volatile("dc civac, %0" :: "r"(a) : "memory");
		dsb_sy();
		wr32(kgsl + REG_CP_RB_CNTL, 0x8001); /* size log2, no RPTR wrap tricks */
		wr32(kgsl + REG_CP_RB_BASE, (uint32_t)rb_pa);
		wr32(kgsl + REG_CP_RB_WPTR, 2);
		LOG("CP RB base %#llx wptr 2; RBBM now %#x (2D fill/blit NOT DONE — needs GMU+submit fw)",
		    rb_pa, rd32(kgsl + REG_RBBM_STATUS));
	}
}
void lapis_gpu_start(void) { drv_thread(gpu_thread, NULL); }
