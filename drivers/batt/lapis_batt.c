/* LapisBatt - PMIC fuel-gauge read-only probe. */
#include "../include/kern.h"
#define LOG(fmt, ...) klog("LapisBatt: " fmt "\n", ##__VA_ARGS__)
/* SM8650 PMIC via SPMI. Without an SPMI controller driver we only log the intent. */
static void batt_thread(void *a, int w) {
	(void)a;(void)w;
	LOG("PMIC fuel gauge: SPMI controller NOT DONE; read-only path deferred");
	LOG("boot-arg lapis_batt_mode=1 reserved for future SPMI reads");
}
void lapis_batt_start(void) { drv_thread(batt_thread, NULL); }
