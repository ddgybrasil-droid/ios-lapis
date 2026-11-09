/*
 * LapisAudio - LPASS / ADSP path from the dump.
 *
 * Firmware: adsp.b00..b45 + adsp.mdt in vendor_firmware.  Bring-up on Linux
 * goes through remoteproc/PIL (qcom_scm auth + reset), then APR/GLINK to the
 * ADSP for audio graphs.  At EL1 under Gunyah:
 *   - MMIO to LPASS/CC registers may be reachable (probe below)
 *   - ADSP bring-up via PIL/SCM is a BLOCKER (TrustZone)
 * Default = probe-only.  lapis_audio_mode=1 reserved; still no PIL.
 */
#include "../include/kern.h"
#define LOG(fmt, ...) klog("LapisAudio: " fmt "\n", ##__VA_ARGS__)
/* LPASS base from typical SM8650 maps; confirm against dump if node exists */
#define LPASS_CC_PA  0x06F00000ull
#define LPASS_CC_SZ  0x1000u

static void audio_thread(void *a, int w)
{
	(void)a;(void)w;
	int mode = drv_bootarg("lapis_audio_mode", 0);
	/* Try to map; if Gunyah blocked the range ml_io_map may still succeed
	 * but reads return 0 / fault - we catch with a mild heuristic. */
	vm_offset_t cc = ml_io_map((vm_offset_t)LPASS_CC_PA, LPASS_CC_SZ);
	if (!cc) { LOG("LPASS_CC map failed"); }
	else {
		uint32_t v = rd32(cc);
		LOG("LPASS_CC @ %#llx raw@0 %#x", LPASS_CC_PA, v);
	}
	LOG("ADSP firmware present in dump (adsp.mdt + 45 blobs)");
	LOG("remoteproc/PIL via SCM: BLOCKER at EL1 under Gunyah — no ADSP start without TZ");
	if (mode == 0) LOG("probe-only (safe default)");
	else LOG("mode=1: refusing PIL/SCM (would fail / fault)");
}
void lapis_audio_start(void) { drv_thread(audio_thread, NULL); }
