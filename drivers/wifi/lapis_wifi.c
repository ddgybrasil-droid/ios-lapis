/*
 * LapisWiFi — WCN785x / Kiwi (cnss2 + qca_cld3_kiwi_v2), NOT ath12k.
 *
 * Hardware (dtb/dump VERIFIED):
 *   PCIe0 PARF 0x1C00000, PHY 0x1C06000, conf 0x60100000
 *   WLAN BAR window 0x60400000 (within pcie ranges 0x60300000+)
 *   Firmware: kiwi/amss.bin (+ amss20), bdwlan_lapis.elf, phy_ucode*.elf, regdb.bin
 *
 * Bring-up (Linux drivers/bus/mhi + drivers/net/wireless/ath/ath12k qmi ids):
 *   mode 0: probe PARF + BAR (default, safe)
 *   mode 1: dump PCIe link / PARF
 *   mode 2: MHI BHI/BHIe state. A BHI image needs a non-zero session id.
 *           WLFW HOST_CAP (QMI 0x0034) is not sent: its TLV body is not built
 *           and no QRTR channel is open.
 *   No PCIe core reset / PERST unless lapis_wifi_reset=1, and that path is refused.
 */
#include "../include/kern.h"
#include "mhi_boot.h"
#include "qmi_wlfw.h"
#define LOG(fmt, ...) klog("LapisWiFi: " fmt "\n", ##__VA_ARGS__)
#define PARF_PA   0x01C00000ull
#define PARF_SZ   0x3000u
#define BAR_PA    0x60400000ull
#define BAR_SZ    0x10000u
#define CONF_PA   0x60100000ull

#define PARF_DEVICE_TYPE   0x1000
#define PARF_LTSSM         0x1B0
#define PARF_PM_CTRL       0x20
#define PARF_SLV_ADDR_SPACE_SIZE 0x358

#define MHI_BHI_VERSION      0x00
#define MHI_BHI_IMGADDR_LOW  0x08
#define MHI_BHI_IMGADDR_HIGH 0x0c
#define MHI_BHI_IMGSIZE      0x10
#define MHI_BHI_IMGTXDB      0x18
#define MHI_BHI_INTVEC       0x20
#define MHI_BHI_EXECENV      0x28
#define MHI_BHI_STATUS       0x2C
#define MHI_BHI_ERRCODE      0x30
#define MHI_BHI_ERRDBG1      0x34
#define MHI_BHI_ERRDBG2      0x38
#define MHI_BHI_ERRDBG3      0x3C
#define MHI_BHI_SERIALNU     0x40
#define MHI_BHI_SBLANTIROLLVER 0x44
#define MHI_BHI_NUMSEG       0x48
#define MHI_BHI_MSMHWID_OFFS 0x4C
#define MHI_BHI_OEMPKHASH_OFFS 0x64
#define MHI_BHI_STATUS_MASK  0xC0000000
#define MHI_BHI_STATUS_SHIFT 30
#define MHI_BHIE_TXVECSTATUS 0x44
#define MHI_BHIE_TXVECDB     0x3c
#define MHI_BHIE_TXVECADDR_LOW_OFFS  0x2c
#define MHI_BHIE_TXVECADDR_HIGH_OFFS 0x30
#define MHI_BHIE_TXVECSIZE_OFFS      0x34

#define MHIREGLEN   0x100
#define MHIVER      0x108
#define MHICFG      0x110
#define CHDBOFF     0x118
#define ERDBOFF     0x120
#define BHIOFF      0x128
#define BHIEOFF     0x12C
#define MHICTRL     0x138
#define MHISTATUS   0x148

static const char *bhi_status_str(uint32_t st)
{
	switch ((st & MHI_BHI_STATUS_MASK) >> MHI_BHI_STATUS_SHIFT) {
	case BHI_STATUS_SUCCESS: return "SUCCESS";
	case BHI_STATUS_RESET: return "RESET";
	case BHI_STATUS_ERROR: return "ERROR";
	default: return "BUSY/OTHER";
	}
}

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
		uint32_t link = rd32(conf + 0x80);
		LOG("RC conf @ %#llx id %#x linkish %#x", CONF_PA, vend, link);
	}
	if (bar) {
		LOG("WLAN BAR @ %#llx raw@0 %#x", BAR_PA, rd32(bar));
	} else {
		LOG("WLAN BAR map failed (link down or SMMU stream)");
	}
	LOG("expect cnss2 + qca_cld3_kiwi_v2; firmware: amss.bin, bdwlan_lapis.elf, phy_ucode, regdb");
	LOG("WLFW service %#x host-cap msg %#x is not sent (%s)",
	    LAPIS_WLFW_SERVICE_ID, LAPIS_WLFW_HOST_CAP,
	    lapis_wlfw_host_cap_body_ok(0) ? "empty body refused" : "body present");

	if (mode == 0) { LOG("probe-only (safe)"); return; }

	if (mode >= 1) {
		LOG("mode>=1: PARF PM_CTRL %#x SLV_ADDR_SPACE %#x",
		    rd32(parf + PARF_PM_CTRL), rd32(parf + PARF_SLV_ADDR_SPACE_SIZE));
		if (drv_bootarg("lapis_wifi_reset", 0))
			LOG("lapis_wifi_reset=1 refused without SMMU SID setup");
	}

	if (mode >= 2 && bar) {
		uint32_t bhi_off = rd32(bar + BHIOFF);
		uint32_t bhie_off = rd32(bar + BHIEOFF);
		struct lapis_bhi_image img;
		uint32_t session;
		pmap_paddr_t fw_pa = 0; uint32_t fw_sz = 0;
		if (!lapis_bhi_window_valid(bhi_off, BAR_SZ)) {
			LOG("invalid/unavailable BHI window %#x in BAR size %#x", bhi_off, BAR_SZ);
			return;
		}
		uint32_t mhictrl = rd32(bar + MHICTRL);
		uint32_t mhist = rd32(bar + MHISTATUS);
		LOG("MHI MHIVER %#x MHICFG %#x CTRL %#x STATUS %#x BHI@%#x BHIe@%#x",
		    rd32(bar + MHIVER), rd32(bar + MHICFG), mhictrl, mhist, bhi_off, bhie_off);
		vm_offset_t bhi = bar + bhi_off;
		uint32_t bhi_ver = rd32(bhi + MHI_BHI_VERSION);
		uint32_t bhi_st  = rd32(bhi + MHI_BHI_STATUS);
		uint32_t ee      = rd32(bhi + MHI_BHI_EXECENV);
		LOG("BHI VERSION %#x STATUS %#x (%s) EXECENV %#x ERR %#x",
		    bhi_ver, bhi_st, bhi_status_str(bhi_st), ee, rd32(bhi + MHI_BHI_ERRCODE));
		LOG("BHI SERIAL %#x NUMSEG %#x", rd32(bhi + MHI_BHI_SERIALNU), rd32(bhi + MHI_BHI_NUMSEG));

		if (!drv_bootarg("lapis_wifi_fw", 0)) {
			LOG("firmware download gated (set lapis_wifi_fw=1 + lapis_wifi_fw_pa/sz + lapis_wifi_bhi_session)");
			LOG("MHI channel bring-up / WMI / scan: NOT DONE without amss.bin");
			return;
		}
		PE_parse_boot_argn("lapis_wifi_fw_pa", &fw_pa, sizeof(fw_pa));
		PE_parse_boot_argn("lapis_wifi_fw_sz", &fw_sz, sizeof(fw_sz));
		session = (uint32_t)drv_bootarg("lapis_wifi_bhi_session", 0);
		if (lapis_bhi_image(fw_pa, fw_sz, session, &img)) {
			LOG("BHI image refused: need a non-zero PA, size at most %u, and a non-zero session",
			    LAPIS_BHI_IMAGE_MAX);
			return;
		}

		wr32(bhi + MHI_BHI_STATUS, 0);
		wr32(bhi + MHI_BHI_IMGADDR_LOW, img.addr_lo);
		wr32(bhi + MHI_BHI_IMGADDR_HIGH, img.addr_hi);
		wr32(bhi + MHI_BHI_IMGSIZE, img.size);
		LOG("BHI programmed IMGADDR=%#llx SIZE=%u session=%#x", fw_pa, fw_sz, img.session);
		if (!drv_bootarg("lapis_wifi_fw_doorbell", 0)) {
			LOG("doorbell gated (set lapis_wifi_fw_doorbell=1 after confirming PA is DMA-visible to EP)");
			return;
		}
		wr32(bhi + MHI_BHI_IMGTXDB, img.session);
		for (int i = 0; i < 500; i++) {
			bhi_st = rd32(bhi + MHI_BHI_STATUS);
			uint32_t s = (bhi_st & MHI_BHI_STATUS_MASK) >> MHI_BHI_STATUS_SHIFT;
			if (s == BHI_STATUS_SUCCESS || s == BHI_STATUS_ERROR) break;
			IOSleep(10);
		}
		LOG("after IMGTXDB STATUS %#x (%s) EXECENV %#x ERR %#x/%#x/%#x",
		    bhi_st, bhi_status_str(bhi_st), rd32(bhi + MHI_BHI_EXECENV),
		    rd32(bhi + MHI_BHI_ERRCODE), rd32(bhi + MHI_BHI_ERRDBG1), rd32(bhi + MHI_BHI_ERRDBG2));
		if (((bhi_st & MHI_BHI_STATUS_MASK) >> MHI_BHI_STATUS_SHIFT) != BHI_STATUS_SUCCESS) {
			LOG("firmware transfer failed or timed out; Wi-Fi remains offline");
			return;
		}
		LOG("BHIe/amss + WMI/scan still need kiwi/amss.bin staged; HOST_CAP is still not sent");
	}
}
void lapis_wifi_start(void) { drv_thread(wifi_thread, NULL); }
