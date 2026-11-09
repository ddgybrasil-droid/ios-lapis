/*
 * LapisAudio — ADSP via PAS + LPASS, then GLINK/GPR toward a PCM path.
 *
 * Hardware (dtb VERIFIED):
 *   remoteproc-adsp@03000000 compatible qcom,pineapple-adsp-pas
 *   carveout adspslpi_region @ 0x9df00000 size 0x4080000
 *   q6_adsp_dtb_region @ 0x9de80000 size 0x80000
 *   Firmware: adsp.mdt + adsp.b00..b55 (segments NOT shipped in handoff)
 *
 * Boot-args:
 *   lapis_audio_mode=0  probe LPASS/ADSP MMIO (default)
 *   lapis_audio_mode=1  log GLINK/GPR plan; no SMC
 *   lapis_audio_mode=2  PIL load ADSP (needs mdt+segments staged + lapis_scm_pas=1)
 */
#include "../include/kern.h"
#include "../scm/qcom_scm.h"
#define LOG(fmt, ...) klog("LapisAudio: " fmt "\n", ##__VA_ARGS__)
#define LPASS_CC_PA   0x06F00000ull
#define ADSP_RPROC_PA 0x03000000ull
#define ADSP_CARVEOUT 0x9df00000ull
#define ADSP_CARVE_SZ 0x4080000ull

static void audio_thread(void *a, int w)
{
	(void)a;(void)w;
	int mode = drv_bootarg("lapis_audio_mode", 0);
	vm_offset_t cc = ml_io_map((vm_offset_t)LPASS_CC_PA, 0x1000);
	vm_offset_t rp = ml_io_map((vm_offset_t)ADSP_RPROC_PA, 0x1000);
	if (cc) LOG("LPASS_CC @ %#llx raw@0 %#x", LPASS_CC_PA, rd32(cc));
	else LOG("LPASS_CC map failed");
	if (rp) LOG("ADSP rproc @ %#llx raw@0 %#x", ADSP_RPROC_PA, rd32(rp));
	LOG("ADSP carveout %#llx+%#llx; firmware adsp.mdt + b00..b55", ADSP_CARVEOUT, ADSP_CARVE_SZ);

	if (mode == 0) { LOG("probe-only (safe)"); return; }

	if (mode >= 1) {
		LOG("GLINK edge label=adsp transport=smem; GPR channel adsp_apps → audio_prm");
		LOG("PCM path: APR/GPR graph after ADSP up — skeleton only until PIL succeeds");
	}

	if (mode >= 2) {
		pmap_paddr_t mdt_pa = 0; uint32_t mdt_len = 0;
		PE_parse_boot_argn("lapis_adsp_mdt_pa", &mdt_pa, sizeof(mdt_pa));
		PE_parse_boot_argn("lapis_adsp_mdt_len", &mdt_len, sizeof(mdt_len));
		if (!mdt_pa || !mdt_len) {
			LOG("mode=2 needs lapis_adsp_mdt_pa/len and adsp.bNN staged into carveout");
			LOG("request blobs: adsp.b00..b55 from firmware_mnt/image/");
			return;
		}
		vm_offset_t mva = ml_io_map((vm_offset_t)mdt_pa, mdt_len);
		if (!mva) { LOG("mdt map fail"); return; }
		/* seg_read=NULL: will return -2 listing missing segments unless filesz fits in mdt */
		int rc = lapis_pil_load_mdt(PAS_ADSP, (void *)mva, mdt_len,
		                           ADSP_CARVEOUT, ADSP_CARVE_SZ, NULL, NULL);
		LOG("ADSP PIL -> %d", rc);
		if (rc == 0) LOG("next: GLINK open + GPR PCM — NOT DONE this pass");
	}
}
void lapis_audio_start(void) { drv_thread(audio_thread, NULL); }
