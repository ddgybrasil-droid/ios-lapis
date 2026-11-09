/*
 * LapisGPU - Adreno 750 (KGSL) probe.
 *
 * Hardware: KGSL 0x3D00000, GPUCC 0x3D90000, chipid 0x43051401 (gen7_9_1),
 * ZAP shader via qcom_scm / TrustZone.  Default = read-only probe of KGSL
 * registers that are documented as safe (HW_VERSION / CORE_STATUS).
 * Anything that would start the GPU (GMU firmware, ZAP, SUBMIT) is behind
 * lapis_gpu_mode=1 and currently refused: ZAP requires SCM, which is a
 * BLOCKER at EL1 under Gunyah.
 */
#include "../include/kern.h"
#define LOG(fmt, ...) klog("LapisGPU: " fmt "\n", ##__VA_ARGS__)
#define KGSL_PA   0x03D00000ull
#define KGSL_SZ   0x10000u
#define GPUCC_PA  0x03D90000ull
#define GPUCC_SZ  0x1000u
/* A6xx/A7xx register offsets (from mesa/freedreno / Linux kgsl) */
#define REG_HW_VERSION   0x0
#define REG_HW_STATUS    0x4
#define REG_RBBM_STATUS  0x218

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
	if (cc) LOG("GPUCC @ %#llx raw@0 %#x", GPUCC_PA, rd32(cc));
	LOG("Adreno750 expected chipid 0x43051401; ZAP via SCM: BLOCKER at EL1 under Gunyah");
	if (mode == 0) { LOG("probe-only (safe default)"); return; }
	LOG("mode=1: refusing GMU/ZAP/submit without SCM (would hang/abort)");
}
void lapis_gpu_start(void) { drv_thread(gpu_thread, NULL); }
