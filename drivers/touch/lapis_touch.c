/* LapisTouch - Novatek NT36532 SPI touch (gpio 162 IRQ). Probe-only. */
#include "../include/kern.h"
#define LOG(fmt, ...) klog("LapisTouch: " fmt "\n", ##__VA_ARGS__)
/* From dump: SPI via QUPv3, IRQ gpio 162. Full driver needs GENI SPI + firmware. */
static void touch_thread(void *a, int w) {
	(void)a;(void)w;
	int mode = drv_bootarg("lapis_touch_mode", 0);
	LOG("Novatek NT36532 (SPI) probe; gpio-irq 162; mode %d", mode);
	LOG("GENI SPI xfer + firmware download NOT DONE (needs QUPv3 @ EL1)");
	if (mode == 0) LOG("probe-only (safe default)");
}
void lapis_touch_start(void) { drv_thread(touch_thread, NULL); }
