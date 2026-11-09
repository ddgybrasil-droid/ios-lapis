/*
 * LapisAudio — ADSP via PAS + LPASS CDC, then GLINK/GPR toward a PCM path.
 *
 * Hardware (dtb VERIFIED on TB520FU):
 *   remoteproc-adsp@03000000 compatible qcom,pineapple-adsp-pas  (NO qcom,pas-id prop)
 *   carveout adspslpi_region @ 0x9df00000 size 0x4080000
 *   q6_adsp_dtb_region @ 0x9de80000 size 0x80000
 *   LPASS CDC macros under spf_core_platform/lpass-cdc:
 *     rx-macro@6AC0000, tx@6AE0000, wsa@6B00000, va@6D44000, lpi_pinctrl@6E80000
 *   DT pas-ids 0x1c/0x22/0x23 are Gunyah secure VMs — NOT ADSP.
 *   Firmware: adsp.mdt + adsp.b00..b55 (segments NOT shipped in handoff)
 *
 * Boot-args:
 *   lapis_audio_mode=0  probe ADSP rproc + one CDC macro (default)
 *   lapis_audio_mode=1  log GLINK/GPR plan; no SMC
 *   lapis_audio_mode=2  PIL load ADSP (needs mdt+segments staged + lapis_scm_pas=1)
 */
#include "../include/kern.h"
#include "../scm/qcom_scm.h"
#define LOG(fmt, ...) klog("LapisAudio: " fmt "\n", ##__VA_ARGS__)
#define ADSP_RPROC_PA  0x03000000ull
#define RX_MACRO_PA    0x06AC0000ull   /* dtb: rx-macro@6AC0000 */
#define ADSP_CARVEOUT  0x9df00000ull
#define ADSP_CARVE_SZ  0x4080000ull

static void audio_thread(void *a, int w)
{
	(void)a;(void)w;
	int mode = drv_bootarg("lapis_audio_mode", 0);
	/* Do NOT poke the old hypothesized LPASS_CC @ 0x06F00000 — absent from iomem/dtb. */
	vm_offset_t rp = ml_io_map((vm_offset_t)ADSP_RPROC_PA, 0x1000);
	vm_offset_t rx = ml_io_map((vm_offset_t)RX_MACRO_PA, 0x1000);
	if (rp) LOG("ADSP rproc @ %#llx raw@0 %#x", ADSP_RPROC_PA, rd32(rp));
	else LOG("ADSP rproc map failed");
	if (rx) LOG("LPASS rx-macro @ %#llx raw@0 %#x", RX_MACRO_PA, rd32(rx));
	else LOG("rx-macro map failed (clocks/GDSC may be off — expected before ADSP up)");
	LOG("ADSP carveout %#llx+%#llx; firmware adsp.mdt + b00..b55", ADSP_CARVEOUT, ADSP_CARVE_SZ);
	LOG("dtb: remoteproc has no qcom,pas-id; PAS_ADSP=%u is Linux pineapple table default", PAS_ADSP);

	if (mode == 0) { LOG("probe-only (safe)"); return; }

	if (mode >= 1) {
		LOG("GLINK edge label=adsp/lpass transport=smem; GPR channel adsp_apps → audio_prm");
		LOG("battery charger also on ADSP pmic_glink (PMIC_RTR_ADSP_APPS)");
		LOG("PCM path: GPR graph after ADSP up — skeleton only until PIL succeeds");
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
		int rc = lapis_pil_load_mdt(PAS_ADSP, (void *)mva, mdt_len,
		                           ADSP_CARVEOUT, ADSP_CARVE_SZ, NULL, NULL);
		LOG("ADSP PIL -> %d", rc);
		if (rc == 0) LOG("next: GLINK open + GPR PCM — NOT DONE this pass");
	}
}
void lapis_audio_start(void) { drv_thread(audio_thread, NULL); }
