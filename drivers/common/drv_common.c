/* drv_common.c - shared helpers + the single entry point of the lapis built-in drivers. */
#include "../include/kern.h"
#include "../include/blk.h"

void *drv_dt_entry(const char *path)
{
	return k_IORE_fromPath(path, k_gIODTPlane, NULL, NULL, NULL);
}

const void *drv_dt_prop(void *entry, const char *key, unsigned *len)
{
	void *o = entry ? k_IORE_getProperty(entry, key) : NULL;
	if (!o || !k_safeMetaCast(o, k_OSData_meta))
		return NULL;
	if (len)
		*len = k_OSData_getLength(o);
	return k_OSData_getBytesNoCopy(o);
}

int drv_dt_u32(void *entry, const char *key, uint32_t *out)
{
	unsigned len; const uint32_t *p = drv_dt_prop(entry, key, &len);
	if (!p || len < 4) return 0;
	*out = p[0]; return 1;
}

int drv_dt_u64(void *entry, const char *key, unsigned idx, uint64_t *out)
{
	unsigned len; const uint8_t *p = drv_dt_prop(entry, key, &len);
	if (!p || len < 8 * (idx + 1)) return 0;
	uint64_t v = 0; for (int i = 7; i >= 0; i--) v = (v << 8) | p[8 * idx + i];
	*out = v; return 1;
}

void *drv_dt_service(const char *path, unsigned wait_ms)
{
	for (unsigned t = 0;; t += 10) {
		void *e = drv_dt_entry(path);
		if (e && k_safeMetaCast(e, k_IOService_meta)) return e;
		if (t >= wait_ms) return NULL;
		IOSleep(10);
	}
}

int drv_bootarg(const char *name, uint32_t dflt)
{
	uint32_t v = dflt;
	if (!PE_parse_boot_argn(name, &v, sizeof(v))) v = dflt;
	return (int)v;
}

void drv_thread(thread_continue_t fn, void *arg)
{
	void *th = NULL;
	if (kernel_thread_start(fn, arg, &th) == 0 && th) thread_deallocate(th);
	else klog("LAPIS drv: kernel_thread_start failed\n");
}

int  lapis_gic_start(void *platform);
void lapis_ufs_start(void);
void lapis_sdhci_start(void);
void lapis_fb_start(void);
void lapis_smp_start(void);
void lapis_touch_start(void);
void lapis_batt_start(void);
void lapis_wifi_start(void);
void lapis_gpu_start(void);
void lapis_audio_start(void);
void blk_start(void);

void lapis_drivers_start(void *pe)
{
	klog("LAPIS drv: built-in drivers v2 (GIC,UFS,Blk,SDHCI,FB,SMP,Touch,Batt,WiFi,GPU,Audio)\n");
	blk_start();
	if (drv_bootarg("lapis_gic", 1))   lapis_gic_start(pe);
	if (drv_bootarg("lapis_ufs", 1))   lapis_ufs_start();
	if (drv_bootarg("lapis_fb", 1))    lapis_fb_start();
	/* HW-only / risky: off by default. Enable with lapis_*=1 on the tablet. */
	if (drv_bootarg("lapis_sdhci", 0)) lapis_sdhci_start();
	if (drv_bootarg("lapis_smp", 0))   lapis_smp_start();
	if (drv_bootarg("lapis_touch", 0)) lapis_touch_start();
	if (drv_bootarg("lapis_batt", 0))  lapis_batt_start();
	if (drv_bootarg("lapis_wifi", 0))  lapis_wifi_start();
	if (drv_bootarg("lapis_gpu", 0))   lapis_gpu_start();
	if (drv_bootarg("lapis_audio", 0)) lapis_audio_start();
}
