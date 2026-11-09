/*
 * LapisWiFi — WCN785x / Kiwi (cnss2 + qca_cld3_kiwi_v2), NOT ath12k.
 *
 * Hardware (dtb/dump VERIFIED):
 *   PCIe0 PARF 0x1C00000, PHY 0x1C06000, conf 0x60100000
 *   WLAN BAR window 0x60400000 (within pcie ranges 0x60300000+)
 *   Firmware: kiwi/amss.bin (+ amss20), bdwlan_lapis.elf, phy_ucode*.elf, regdb.bin
 *
 * Bring-up steps (each gated):
 *   mode 0: probe PARF + BAR (default, safe)
 *   mode 1: dump PCIe link status / PARF registers (read-mostly)
 *   mode 2: attempt MHI BHI base probe + log firmware download prerequisites
 *           (amss.bin not shipped — will log NOT DONE / request blob)
 *   No PCIe core reset / PERST toggle unless lapis_wifi_reset=1.
 */
#include "../include/kern.h"
#define LOG(fmt, ...) klog("LapisWiFi: " fmt "\n", ##__VA_ARGS__)
#define PARF_PA   0x01C00000ull
#define PARF_SZ   0x3000u
#define PHY_PA    0x01C06000ull
#define BAR_PA    0x60400000ull
#define BAR_SZ    0x10000u
#define CONF_PA   0x60100000ull

/* PARF offsets (qcom pcie) */
#define PARF_DEVICE_TYPE   0x1000
#define PARF_LTSSM         0x1B0
#define PARF_PM_CTRL       0x20
#define PARF_SLV_ADDR_SPACE_SIZE 0x358

/* MHI MMIO (relative to BAR0) — BHI */
#define MHI_BHI_VERSION    0x00
#define MHI_BHI_IMGADDR_LOW  0x08
#define MHI_BHI_IMGADDR_HIGH 0x0c
#define MHI_BHI_IMGSIZE    0x10
#define MHI_BHI_IMGTXDB    0x18
#define MHI_BHI_INTVEC     0x20
#define MHI_BHI_EXECENV    0x28
#define MHI_BHI_STATUS     0x2C

static void wifi_thread(void *a, int w)
{
	(void)a;(void)w;
	int mode = drv_bootarg("lapis_wifi_mode", 0);
	vm_offset_t parf = ml_io_map((vm_offset_t)PARF_PA, PARF_SZ);
	vm_offset_t bar  = ml_io_map((vm_offset_t)BAR_PA, BAR_SZ);
	vm_offset_t conf = ml_io_map((vm_offset_t)CONF_PA, 0x1000);
	if (!parf) { LOG("PARF map failed"); return; }

	uint32_t ltssm = rd32(parf + PARF_LTSSM);
	uint32_t dtype = rd32(parf + PARF_DEVICE_TYPE);
	LOG("PCIe0 PARF @ %#llx LTSSM %#x DEVICE_TYPE %#x", PARF_PA, ltssm, dtype);
	if (conf) {
		uint32_t vend = rd32(conf + 0x00);
		uint32_t link = rd32(conf + 0x80); /* PCI Express Link Status/Control often @0x80 in capability */
		LOG("RC conf @ %#llx id %#x linkish %#x", CONF_PA, vend, link);
	}
	if (bar) {
		uint32_t b0 = rd32(bar);
		LOG("WLAN BAR @ %#llx raw@0 %#x", BAR_PA, b0);
	} else {
		LOG("WLAN BAR map failed (link down or SMMU stream)");
	}
	LOG("expect cnss2 + qca_cld3_kiwi_v2; firmware: amss.bin, bdwlan_lapis.elf, phy_ucode, regdb");

	if (mode == 0) { LOG("probe-only (safe)"); return; }

	if (mode >= 1) {
		LOG("mode>=1: PARF PM_CTRL %#x SLV_ADDR_SPACE %#x",
		    rd32(parf + PARF_PM_CTRL), rd32(parf + PARF_SLV_ADDR_SPACE_SIZE));
		if (drv_bootarg("lapis_wifi_reset", 0)) {
			LOG("lapis_wifi_reset=1 refused in this build without SMMU SID setup (would hang link)");
		}
	}

	if (mode >= 2 && bar) {
		/* MHI BHI typically lives at a fixed offset into the endpoint BAR;
		 * for WCN785x/cnss2 the BHI base is often at BAR+0. Probe version. */
		uint32_t bhi_ver = rd32(bar + MHI_BHI_VERSION);
		uint32_t bhi_st  = rd32(bar + MHI_BHI_STATUS);
		uint32_t ee      = rd32(bar + MHI_BHI_EXECENV);
		LOG("MHI BHI VERSION %#x STATUS %#x EXECENV %#x", bhi_ver, bhi_st, ee);
		LOG("firmware download needs kiwi/amss.bin (7.2 MiB) staged + SMMU stream for PCIe EP");
		LOG("MHI channel bring-up / WMI / scan: NOT DONE without amss.bin (request blob)");
		if (drv_bootarg("lapis_wifi_fw", 0)) {
			pmap_paddr_t fw_pa = 0; uint32_t fw_sz = 0;
			PE_parse_boot_argn("lapis_wifi_fw_pa", &fw_pa, sizeof(fw_pa));
			PE_parse_boot_argn("lapis_wifi_fw_sz", &fw_sz, sizeof(fw_sz));
			if (!fw_pa || !fw_sz) {
				LOG("lapis_wifi_fw=1 needs lapis_wifi_fw_pa/sz");
			} else {
				LOG("would program BHI_IMGADDR=%#llx IMGSIZE=%u then ring IMGTXDB (gated — not ringing without hw confirm)",
				    fw_pa, fw_sz);
				/* Intentionally do NOT write doorbell here unless explicitly forced */
				if (drv_bootarg("lapis_wifi_fw_doorbell", 0)) {
					wr32(bar + MHI_BHI_IMGADDR_LOW, (uint32_t)fw_pa);
					wr32(bar + MHI_BHI_IMGADDR_HIGH, (uint32_t)(fw_pa >> 32));
					wr32(bar + MHI_BHI_IMGSIZE, fw_sz);
					wr32(bar + MHI_BHI_IMGTXDB, 1);
					LOG("IMGTXDB rung; STATUS now %#x", rd32(bar + MHI_BHI_STATUS));
				}
			}
		}
	}
}
void lapis_wifi_start(void) { drv_thread(wifi_thread, NULL); }
