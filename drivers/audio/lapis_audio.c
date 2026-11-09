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
 *   lapis_audio_mode=1  log GLINK/GPR/APM graph plan; no SMC
 *   lapis_audio_mode=2  PIL load ADSP (needs mdt+segments staged + lapis_scm_pas=1)
 *   lapis_audio_mode=3  after PIL: emit GPR OPEN / APM graph + PCM write plan
 */
#include "../include/kern.h"
#include "../scm/qcom_scm.h"
#define LOG(fmt, ...) klog("LapisAudio: " fmt "\n", ##__VA_ARGS__)
#define ADSP_RPROC_PA  0x03000000ull
#define RX_MACRO_PA    0x06AC0000ull
#define ADSP_CARVEOUT  0x9df00000ull
#define ADSP_CARVE_SZ  0x4080000ull

/* GPR packet header (Linux include/linux/soc/qcom/apr.h + q6apm) — packed LE */
struct gpr_hdr {
	uint32_t version_hdr_size; /* ver:4 | hdr_size:4 | pkt_size:24 */
	uint32_t src_domain:8, src_port:24;
	uint32_t dst_domain:8, dst_port:24;
	uint32_t token;
	uint32_t opcode;
};

#define GPR_DOMAIN_APPS  0x1
#define GPR_DOMAIN_ADSP  0x2
#define APM_MODULE_INSTANCE_ID 0x00000001
#define APM_CMD_GRAPH_OPEN  0x01001000
#define APM_CMD_GRAPH_START 0x01001002
#define APM_CMD_GRAPH_CLOSE 0x01001001
#define DATA_CMD_WR_SH_MEM_EP_DATA_V2 0x0100100B

static void audio_thread(void *a, int w)
{
	(void)a;(void)w;
	int mode = drv_bootarg("lapis_audio_mode", 0);
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
		LOG("APM graph: OPEN(0x%08x) → START(0x%08x) → PCM WR_SH_MEM(0x%08x)",
		    APM_CMD_GRAPH_OPEN, APM_CMD_GRAPH_START, DATA_CMD_WR_SH_MEM_EP_DATA_V2);
	}

	int pil_ok = 0;
	if (mode >= 2) {
		pmap_paddr_t mdt_pa = 0; uint32_t mdt_len = 0;
		PE_parse_boot_argn("lapis_adsp_mdt_pa", &mdt_pa, sizeof(mdt_pa));
		PE_parse_boot_argn("lapis_adsp_mdt_len", &mdt_len, sizeof(mdt_len));
		if (!mdt_pa || !mdt_len) {
			LOG("mode>=2 needs lapis_adsp_mdt_pa/len and adsp.bNN staged into carveout");
			LOG("request blobs: adsp.b00..b55 from firmware_mnt/image/");
		} else {
			vm_offset_t mva = ml_io_map((vm_offset_t)mdt_pa, mdt_len);
			if (!mva) { LOG("mdt map fail"); }
			else {
				int rc = lapis_pil_load_mdt(PAS_ADSP, (void *)mva, mdt_len,
				                           ADSP_CARVEOUT, ADSP_CARVE_SZ, NULL, NULL);
				LOG("ADSP PIL -> %d", rc);
				pil_ok = (rc == 0);
			}
		}
	}

	if (mode >= 3) {
		if (!pil_ok) {
			LOG("mode>=3: GPR/PCM gated on successful ADSP PIL");
			return;
		}
		/* Build a GPR OPEN header in a DMA buffer — actual GLINK TX needs smem mailbox. */
		struct gpr_hdr *h = IOMallocAligned(0x1000, 0x1000);
		if (!h) { LOG("gpr buf alloc fail"); return; }
		bzero(h, 0x1000);
		uint32_t pkt = sizeof(*h);
		h->version_hdr_size = (0x1u << 28) | (sizeof(*h) << 16) | pkt; /* rough packing */
		h->src_domain = GPR_DOMAIN_APPS; h->src_port = 0x2;
		h->dst_domain = GPR_DOMAIN_ADSP; h->dst_port = APM_MODULE_INSTANCE_ID;
		h->token = 1; h->opcode = APM_CMD_GRAPH_OPEN;
		LOG("GPR OPEN hdr built pa %#llx — GLINK/smem TX NOT DONE (need mailbox + intent pool)",
		    kvtophys((vm_offset_t)h));
		LOG("PCM write path: DATA_CMD_WR_SH_MEM_EP_DATA_V2 after graph START — NOT DONE");
	}
}
void lapis_audio_start(void) { drv_thread(audio_thread, NULL); }
