#include "qcom_scm.h"
#define LOG(fmt, ...) klog("LapisPIL: " fmt "\n", ##__VA_ARGS__)

int lapis_pil_auth(uint32_t pas_id, pmap_paddr_t meta_pa, pmap_paddr_t mem_pa, uint64_t mem_sz)
{
	int rc;
	LOG("PAS %u: init_image meta_pa=%#llx", pas_id, meta_pa);
	rc = qcom_scm_pas_init_image(pas_id, meta_pa);
	LOG("  init_image -> %d", rc);
	if (rc) return rc;
	LOG("PAS %u: mem_setup pa=%#llx sz=%#llx", pas_id, mem_pa, mem_sz);
	rc = qcom_scm_pas_mem_setup(pas_id, mem_pa, mem_sz);
	LOG("  mem_setup -> %d", rc);
	if (rc) return rc;
	LOG("PAS %u: auth_and_reset", pas_id);
	rc = qcom_scm_pas_auth_and_reset(pas_id);
	LOG("  auth_and_reset -> %d", rc);
	return rc;
}

void lapis_pil_start(void)
{
	int mode = drv_bootarg("lapis_pil", 0);
	uint32_t pas = (uint32_t)drv_bootarg("lapis_pil_pas", 1);
	LOG("PIL loader ready (mode %d, default pas %u)", mode, pas);
	LOG("firmware in dump: adsp.mdt+b00..b45, wpss, kiwi; carveouts HYPOTHESIS until DT parse");
	if (!mode) { LOG("set lapis_pil=1 lapis_scm_pas=1 lapis_pil_meta=<pa> to auth"); return; }
	if (!drv_bootarg("lapis_scm_pas", 0)) { LOG("need lapis_scm_pas=1"); return; }
	pmap_paddr_t meta = 0, mem = 0; uint64_t msz = 0;
	PE_parse_boot_argn("lapis_pil_meta", &meta, sizeof(meta));
	PE_parse_boot_argn("lapis_pil_mem", &mem, sizeof(mem));
	PE_parse_boot_argn("lapis_pil_msz", &msz, sizeof(msz));
	if (!meta || !mem || !msz) {
		LOG("NOT DONE without lapis_pil_meta/mem/msz boot-args (mdt must be staged in RAM)");
		LOG("lapis_pil_auth() implemented — callable once PAs known");
		return;
	}
	lapis_pil_auth(pas, meta, mem, msz);
}
