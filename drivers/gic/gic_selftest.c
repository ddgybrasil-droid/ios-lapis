/* LapisGICv3 self-tests, run in a kernel thread after registration.
 *  A) /lapis-pl031 (emulator ADT only): QEMU virt PL031 RTC alarm -> SPI 2 (INTID 34),
 *     level.  A real device interrupt through GICD -> sleh_irq -> LapisGICv3 -> the
 *     IOService::registerInterrupt handler of the ADT nub.  Registers per ARM DDI 0224
 *     (PL031 TRM): DR 0x00, MR 0x04, LR 0x08, CR 0x0C, IMSC 0x10, RIS 0x14, MIS 0x18, ICR 0x1C.
 *  B) /lapis-gic-selftest (emulator ADT only): IOService::causeInterrupt -> GICD_ISPENDR
 *     on an otherwise unused SPI, edge.
 *  C) always: report how the vtimer INTID is configured on the boot CPU. */
#include "../include/kern.h"
#include "gicv3.h"
#include "gic_priv.h"

extern uint32_t k_lapis_vtimer_intid;
#define LOG(fmt, ...) klog("LapisGICv3: selftest: " fmt "\n", ##__VA_ARGS__)

static volatile uint32_t rtc_hits, sw_hits;
static volatile uint64_t rtc_ns;
static vm_offset_t rtc;

static void rtc_isr(void *target, void *refCon, void *nub, int source)
{
	(void)target; (void)refCon; (void)nub; (void)source;
	wr32(rtc + 0x1c, 1);          /* RTCICR: clear */
	wr32(rtc + 0x10, 0);          /* RTCIMSC: mask further alarms */
	rtc_ns = now_ns();
	rtc_hits++;
}
static void sw_isr(void *target, void *refCon, void *nub, int source)
{ (void)target; (void)refCon; (void)nub; (void)source; sw_hits++; }

static void test_pl031(struct gic *g)
{
	void *nub = drv_dt_service("/lapis-pl031", 2000);
	uint64_t pa, sz; uint32_t ispec[2] = {0, 0}; unsigned len;
	if (!nub) return;
	if (!drv_dt_u64(nub, "reg", 0, &pa) || !drv_dt_u64(nub, "reg", 1, &sz)) { LOG("pl031: no reg"); return; }
	const uint32_t *ip = drv_dt_prop(nub, "interrupts", &len);
	if (ip && len >= 8) { ispec[0] = ip[0]; ispec[1] = ip[1]; }
	rtc = ml_io_map(pa, sz);
	LOG("pl031 @%#llx PeriphID %02x%02x CellID %02x interrupts=<%u %u> IOInterruptControllers=%s",
	    pa, rd32(rtc + 0xfe4) & 0xff, rd32(rtc + 0xfe0) & 0xff, rd32(rtc + 0xff0) & 0xff, ispec[0], ispec[1],
	    k_IORE_getProperty(nub, "IOInterruptControllers") ? "yes" : "NO");
	wr32(rtc + 0x10, 0); wr32(rtc + 0x1c, 1);
	IOReturn r = k_IOService_registerInterrupt(nub, 0, nub, rtc_isr, NULL);
	if (r) { LOG("pl031: registerInterrupt -> %#x", r); return; }
	r = k_IOService_enableInterrupt(nub, 0);
	uint32_t base = g->count[ispec[0]];
	uint32_t dr = rd32(rtc + 0x00);
	wr32(rtc + 0x04, dr + 2);     /* alarm in ~2 s (RTC counts seconds) */
	uint64_t t0 = now_ns();
	wr32(rtc + 0x10, 1);          /* RTCIMSC: unmask -> raw match drives the SPI line */
	for (int i = 0; i < 600 && !rtc_hits; i++) IOSleep(10);
	uint32_t ris = rd32(rtc + 0x14);
	if (rtc_hits)
		LOG("PASS pl031 alarm: INTID %u dispatched %u time(s) via IOKit nub handler, %llu ms after arming "
		    "(DR %u -> MR %u), RIS now %#x, vector count %u", ispec[0], rtc_hits, (rtc_ns - t0) / 1000000,
		    dr, dr + 2, ris, g->count[ispec[0]] - base);
	else
		LOG("FAIL pl031 alarm: no interrupt in 6 s (enable %#x RIS %#x MIS %#x GICD_ISPENDR1 %#x)",
		    r, ris, rd32(rtc + 0x18), rd32(g->gicd + GICD_ISPENDR + 4));
	k_IOService_disableInterrupt(nub, 0);
}

static void test_cause(struct gic *g)
{
	void *nub = drv_dt_service("/lapis-gic-selftest", 2000);
	unsigned len;
	if (!nub) return;
	const uint32_t *ip = drv_dt_prop(nub, "interrupts", &len);
	uint32_t n = (ip && len >= 4) ? ip[0] : 0;
	IOReturn r = k_IOService_registerInterrupt(nub, 0, nub, sw_isr, NULL);
	if (!r) r = k_IOService_enableInterrupt(nub, 0);
	if (r) { LOG("cause: register/enable -> %#x", r); return; }
	for (int k = 0; k < 3; k++) {
		uint32_t before = sw_hits;
		k_IOService_causeInterrupt(nub, 0);
		for (int i = 0; i < 100 && sw_hits == before; i++) IOSleep(1);
	}
	LOG("%s causeInterrupt: INTID %u (edge) delivered %u/3 via GICD_ISPENDR -> sleh_irq -> handler",
	    sw_hits == 3 ? "PASS" : "FAIL", n, sw_hits);
}

static void selftest_thread(void *arg, int wr)
{
	(void)arg; (void)wr;
	struct gic *g = lapis_gic_state();
	if (!g) return;
	uint32_t ti = k_lapis_vtimer_intid;
	if (ti >= 16 && ti < 32) {
		uint32_t grp = rd32(g->gicr_cpu + GICR_IGROUPR0), en = rd32(g->gicr_cpu + GICR_ISENABLER0);
		uint64_t ctl; __asm__ volatile("mrs %0, cntv_ctl_el0" : "=r"(ctl));
		LOG("vtimer INTID %u: Group%u, %s at boot-CPU redistributor, CNTV_CTL %#llx (handled by sleh_irq before LapisGICv3)",
		    ti, (grp >> ti) & 1, ((en >> ti) & 1) ? "enabled" : "DISABLED", ctl);
	}
	test_pl031(g);
	test_cause(g);
	LOG("done: spurious %u unhandled %u", g->spurious, g->unhandled);
}

void lapis_gic_selftest_start(void)
{
	drv_thread(selftest_thread, NULL);
}
