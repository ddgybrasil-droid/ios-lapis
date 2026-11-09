/*
 * LapisBatt — PMIC fuel gauge over SPMI (read-only).
 *
 * dtb VERIFIED: spmi@c42d000 qcom,spmi-pmic-arb
 *   reg-names cnfg@0xc42d000, core@0xc400000, chnls, obsrvr, intr
 * Dump truth: no qpnp-fg node. High-level path is qcom,battery-charger via
 * ADSP pmic_glink (PMIC_RTR_ADSP_APPS). Direct SPMI SOC is best-effort only and
 * may never work until ADSP is up.
 *
 * Boot-args:
 *   lapis_batt_mode=0  probe SPMI STATUS (default)
 *   lapis_batt_mode=1  EXT_READ a FG window and decode best-effort SOC/voltage
 *   lapis_spmi=<pa>    override core base (default 0xc400000)
 *   lapis_batt_sid=<n> SPMI slave id (default 0)
 */
#include "../include/kern.h"
#define LOG(fmt, ...) klog("LapisBatt: " fmt "\n", ##__VA_ARGS__)
#define SPMI_PA     0x0C400000ull
#define SPMI_CMD    0x00
#define SPMI_STATUS 0x08
#define SPMI_RDATA0 0x18

static vm_offset_t sp;
static uint32_t R(uint32_t o){return rd32(sp+o);}
static void W(uint32_t o,uint32_t v){wr32(sp+o,v);}

static int spmi_read(uint8_t sid, uint16_t addr, uint8_t *out, unsigned n)
{
	for (unsigned i = 0; i < n; i++) {
		uint16_t a = (uint16_t)(addr + i);
		W(SPMI_CMD, (1u << 31) | (0u << 27) | ((uint32_t)sid << 20) | a);
		for (int t = 0; t < 1000; t++) { if (!(R(SPMI_STATUS) & 1)) break; IODelay(1); }
		if (R(SPMI_STATUS) & 1) return -1;
		out[i] = (uint8_t)R(SPMI_RDATA0);
	}
	return 0;
}

static void batt_thread(void *a, int w)
{
	(void)a;(void)w;
	int mode = drv_bootarg("lapis_batt_mode", 0);
	uint64_t pa = SPMI_PA;
	PE_parse_boot_argn("lapis_spmi", &pa, sizeof(pa));
	uint32_t sid = (uint32_t)drv_bootarg("lapis_batt_sid", 0);
	sp = ml_io_map((vm_offset_t)pa, 0x1000);
	if (!sp) { LOG("SPMI map fail"); return; }
	LOG("SPMI core @ %#llx STATUS %#x (arb cnfg is @ 0xc42d000)", pa, R(SPMI_STATUS));
	if (mode == 0) { LOG("probe-only"); return; }

	/* Heuristic FG window: many QG/FG blocks publish SOC at 0x4000+ on SID0.
	 * Also try common PMK/PM8550 FG offsets. Log raw + best-effort decode. */
	static const uint16_t windows[] = { 0x4000, 0x4800, 0x5000, 0x9000 };
	for (unsigned w = 0; w < sizeof(windows)/sizeof(windows[0]); w++) {
		uint8_t buf[16] = {0};
		if (spmi_read((uint8_t)sid, windows[w], buf, 16)) {
			LOG("spmi_read sid=%u addr=%#x FAIL", sid, windows[w]);
			continue;
		}
		unsigned soc = buf[0];                 /* often 0..100 or 0..255 */
		unsigned mv  = buf[2] | (buf[3] << 8); /* millivolts-ish */
		int16_t  ma  = (int16_t)(buf[4] | (buf[5] << 8));
		LOG("FG@%#x raw %02x%02x%02x%02x %02x%02x%02x%02x  decode: soc~%u%% v~%umV i~%dmA (HYPOTHESIS)",
		    windows[w], buf[0],buf[1],buf[2],buf[3], buf[4],buf[5],buf[6],buf[7],
		    soc > 100 ? soc * 100 / 255 : soc, mv, (int)ma);
	}
	LOG("full SOC path may need ADSP pmic_glink once audio/ADSP PIL is up");
}
void lapis_batt_start(void){drv_thread(batt_thread,NULL);}
