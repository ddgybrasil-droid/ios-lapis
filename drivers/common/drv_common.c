/* drv_common.c - shared helpers + the single entry point of the lapis built-in drivers. */
#include "../include/kern.h"

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

/* top-level ADT nodes become IOPlatformDevice nubs in IODTPlatformExpert::processTopLevel
 * (called from configure() inside IOPlatformExpert::start, i.e. before our hook). */
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

/* gic/lapis_gic.c, ufs/lapis_ufs.c */
int  lapis_gic_start(void *platform);
void lapis_ufs_start(void);

/* Called from LapisPlatformExpert::start (after IODTPlatformExpert::start and
 * registerService) through drv_pe_hook (common/drv_entry.S). */
void lapis_drivers_start(void *pe)
{
	klog("LAPIS drv: built-in drivers v1 (LapisGICv3, LapisUFS)\n");
	if (drv_bootarg("lapis_gic", 1))
		lapis_gic_start(pe);
	if (drv_bootarg("lapis_ufs", 1))
		lapis_ufs_start();
}
