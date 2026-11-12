/*
 * LapisBatt — battery properties over PMIC Glink.
 *
 * The tablet's fuel gauge is the ADSP battery-charger service, not a raw SPMI
 * window. This thread maps the SPMI core only to log one raw word. It encodes
 * the three GET requests and does not send them: the glink channel is not open,
 * and a missing answer is not a state of charge.
 *
 *   lapis_batt_mode=0  map SPMI and stop (default)
 *   lapis_batt_mode=1  log the unsent GET frames
 *   lapis_spmi=<pa>    override the core base (default 0xc400000)
 */
#include "../include/kern.h"
#include "pmic_glink_batt.h"
#define LOG(fmt, ...) klog("LapisBatt: " fmt "\n", ##__VA_ARGS__)
#define SPMI_PA 0x0C400000ull

static void log_req(const char *name, unsigned property)
{
	unsigned char msg[LAPIS_BC_MSG_LEN];
	unsigned i;
	if (lapis_bc_get_req(property, msg)) {
		LOG("%s encode failed", name);
		return;
	}
	LOG("%s GET not sent (%u bytes):", name, LAPIS_BC_MSG_LEN);
	for (i = 0; i < LAPIS_BC_MSG_LEN; i += 8)
		LOG("  %02x %02x %02x %02x %02x %02x %02x %02x",
		    msg[i], msg[i+1], msg[i+2], msg[i+3],
		    msg[i+4], msg[i+5], msg[i+6], msg[i+7]);
}

static void batt_thread(void *a, int w)
{
	(void)a;(void)w;
	int mode = drv_bootarg("lapis_batt_mode", 0);
	uint64_t pa = SPMI_PA;
	vm_offset_t sp;
	PE_parse_boot_argn("lapis_spmi", &pa, sizeof(pa));
	sp = ml_io_map((vm_offset_t)pa, 0x1000);
	if (!sp) { LOG("SPMI map fail"); return; }
	LOG("SPMI core @ %#llx raw0 %#x", pa, rd32(sp));
	LOG("fuel gauge is PMIC Glink owner %u opcode %#x; ADSP channel is not open",
	    LAPIS_BC_OWNER, LAPIS_BC_GET);
	if (mode == 0) { LOG("probe-only"); return; }
	log_req("capacity", LAPIS_BC_CAPACITY);
	log_req("voltage", LAPIS_BC_VOLT_NOW);
	log_req("current", LAPIS_BC_CURR_NOW);
	LOG("no state of charge: the requests were not transmitted");
}
void lapis_batt_start(void){drv_thread(batt_thread,NULL);}
