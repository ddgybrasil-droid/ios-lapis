#include "../include/kern.h"
#define LOG(fmt, ...) klog("LapisBatt: " fmt "\n", ##__VA_ARGS__)
/* SM8650 SPMI master (PMK8550 etc). Base from DT — HYPOTHESIS 0x0C400000. */
#define SPMI_PA 0x0C400000ull
#define SPMI_CMD 0x00
#define SPMI_STATUS 0x08
#define SPMI_RDATA0 0x18
#define SPMI_WDATA0 0x10

static vm_offset_t sp;
static uint32_t R(uint32_t o){return rd32(sp+o);}
static void W(uint32_t o,uint32_t v){wr32(sp+o,v);}

/* SPMI extended register read: sid + addr → data. Protocol simplified. */
static int spmi_read(uint8_t sid, uint16_t addr, uint8_t *out, unsigned n)
{
	for (unsigned i=0;i<n;i++){
		W(SPMI_CMD, (1u<<31) /* GO */ | (0u<<27) /* EXT_READ */ | ((uint32_t)sid<<20) | addr+i);
		for (int t=0;t<1000;t++){ if(!(R(SPMI_STATUS)&1)) break; IODelay(1); }
		out[i]=(uint8_t)R(SPMI_RDATA0);
	}
	return 0;
}

static void batt_thread(void *a,int w){(void)a;(void)w;
	int mode=drv_bootarg("lapis_batt_mode",0);
	uint64_t pa=SPMI_PA; PE_parse_boot_argn("lapis_spmi", &pa, sizeof(pa));
	sp=ml_io_map((vm_offset_t)pa, 0x1000);
	if(!sp){LOG("SPMI map fail");return;}
	LOG("SPMI @ %#llx STATUS %#x", pa, R(SPMI_STATUS));
	if(mode==0){LOG("probe-only");return;}
	/* PMIC FG: common QG/FG registers on SID 0 — SOC at various offsets per PMICs.
	 * Read a window and log; interpret as SOC once map confirmed on hw. */
	uint8_t buf[16]={0};
	if(spmi_read(0, 0x4000, buf, 16)) LOG("spmi_read fail");
	else LOG("FG window[0..7] %02x%02x%02x%02x %02x%02x%02x%02x (raw; SOC decode HYPOTHESIS)",
	         buf[0],buf[1],buf[2],buf[3],buf[4],buf[5],buf[6],buf[7]);
}
void lapis_batt_start(void){drv_thread(batt_thread,NULL);}
