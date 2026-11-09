/*
 * LapisWiFi - WCN785x / Kiwi (cnss2 + qca_cld3_kiwi_v2) over PCIe0.
 *
 * Hardware (dump): PCIe0 PARF 0x1C00000, WLAN BAR 0x60400000, MHI, firmware
 * amss.bin / amss20.bin / phy_ucode*.elf / bdwlan_lapis.elf / regdb.bin in the
 * PixelOS vendor_firmware tree.
 *
 * Default = probe-only: map PARF + BAR, dump IDs, do not touch RC/endpoint.
 * Full bring-up (PCIe link train, MHI channels, firmware via PIL/SCM, cnss2
 * state machine) needs TrustZone SCM + remoteproc and is a BLOCKER at pure
 * EL1 under Gunyah for the PIL pieces.  Boot-arg lapis_wifi_mode=1 enables
 * the experimental register poke path (still no firmware load).
 */
#include "../include/kern.h"
#define LOG(fmt, ...) klog("LapisWiFi: " fmt "\n", ##__VA_ARGS__)
#define PARF_PA  0x01C00000ull
#define PARF_SZ  0x2000u
#define BAR_PA   0x60400000ull
#define BAR_SZ   0x1000u          /* probe window only */

static void wifi_thread(void *a, int w)
{
	(void)a;(void)w;
	int mode = drv_bootarg("lapis_wifi_mode", 0);
	vm_offset_t parf = ml_io_map((vm_offset_t)PARF_PA, PARF_SZ);
	vm_offset_t bar  = ml_io_map((vm_offset_t)BAR_PA, BAR_SZ);
	if (!parf) { LOG("PARF map failed"); return; }
	uint32_t vendor = rd32(parf + 0x00);   /* best-effort; PARF layout is qcom-specific */
	LOG("PCIe0 PARF @ %#llx mapped; raw@0 = %#x", PARF_PA, vendor);
	if (bar) LOG("WLAN BAR probe window @ %#llx raw@0 = %#x", BAR_PA, rd32(bar));
	else LOG("WLAN BAR map failed (link down or SMMU?)");
	LOG("firmware blobs present in dump: amss.bin, phy_ucode*, bdwlan_lapis.elf, regdb.bin");
	LOG("MHI + cnss2 + PIL/SCM firmware path: BLOCKER at EL1 under Gunyah (needs TZ)");
	if (mode == 0) { LOG("probe-only (safe default)"); return; }
	LOG("mode=1: no further writes this build (refusing to train PCIe without SMMU/IOMMU setup)");
}
void lapis_wifi_start(void) { drv_thread(wifi_thread, NULL); }
