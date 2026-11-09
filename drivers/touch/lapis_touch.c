#include "../include/kern.h"
#define LOG(fmt, ...) klog("LapisTouch: " fmt "\n", ##__VA_ARGS__)
/* TB520FU: Novatek NT36532 on QUPv3 SPI. From DT: seeromi / spi@... — use known QUP base. */
#define QUPV3_SE_BASE 0x00a80000ull   /* HYPOTHESIS: verify against dtb se_spi */
#define GENI_STATUS      0x40
#define GENI_TX_FIFO     0x100
#define GENI_RX_FIFO     0x200
#define GENI_TX_WATERMARK 0x50c
#define GENI_RX_WATERMARK 0x510
#define GENI_M_CMD0      0x600
#define GENI_M_IRQ_STATUS 0x610
#define GENI_TX_FIFOn    0x700
#define GENI_RX_FIFOn    0x780
#define GENI_FORCE_DEFAULT 0x14

/* Novatek host SPI protocol: 0x71 / 0x70 read/write prefixes (common NT36xxx) */
static vm_offset_t se;
static uint32_t R(uint32_t o){return rd32(se+o);}
static void W(uint32_t o,uint32_t v){wr32(se+o,v);}

static int geni_spi_xfer(const uint8_t *tx, uint8_t *rx, unsigned n)
{
	/* Minimal GENI SPI: wait idle, load TX FIFO, issue TX+RX command, drain RX.
	 * Full clock/CS setup needs GCC + GENI FW; behind mode>=2. */
	for (int i=0;i<1000;i++){ if(!(R(GENI_STATUS)&1)) break; IODelay(10); }
	for (unsigned i=0;i<n;i+=4){
		uint32_t w=0;
		for (unsigned b=0;b<4&&i+b<n;b++) w|=(uint32_t)tx[i+b]<<(8*b);
		W(GENI_TX_FIFOn, w);
	}
	W(GENI_M_CMD0, (0x1<<27) /* OPCODE_TX_RX */ | (n & 0xffffff));
	for (int i=0;i<10000;i++){
		uint32_t st=R(GENI_M_IRQ_STATUS);
		if (st & (1u<<0)) { W(GENI_M_IRQ_STATUS, st); break; }
		IODelay(10);
	}
	for (unsigned i=0;i<n;i+=4){
		uint32_t w=R(GENI_RX_FIFOn);
		for (unsigned b=0;b<4&&i+b<n;b++) if(rx) rx[i+b]=(uint8_t)(w>>(8*b));
	}
	return 0;
}

static void touch_thread(void *a,int w){(void)a;(void)w;
	int mode=drv_bootarg("lapis_touch_mode",0);
	uint64_t pa=QUPV3_SE_BASE;
	PE_parse_boot_argn("lapis_touch_se", &pa, sizeof(pa));
	se=ml_io_map((vm_offset_t)pa, 0x4000);
	if(!se){LOG("QUP SE map fail");return;}
	LOG("QUPv3 SE @ %#llx STATUS %#x (Novatek NT36532 SPI)", pa, R(GENI_STATUS));
	if(mode==0){LOG("probe-only");return;}
	if(mode==1){
		/* Identify: read chip id via NT protocol */
		uint8_t tx[4]={0x71,0x00,0x00,0x00}, rx[4]={0};
		if(geni_spi_xfer(tx,rx,4)) LOG("xfer fail");
		else LOG("NT id bytes %02x %02x %02x %02x", rx[0],rx[1],rx[2],rx[3]);
		return;
	}
	LOG("mode>=2: continuous touch IRQ on gpio 162 NOT DONE (needs GIC SPI + event pipe)");
}
void lapis_touch_start(void){drv_thread(touch_thread,NULL);}
