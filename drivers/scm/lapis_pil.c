/*
 * LapisPIL — Qualcomm Peripheral Image Loader (PAS).
 *
 * Flow (Linux qcom_q6v5_pas / qcom_mdt_loader):
 *   1. Parse ELF32 .mdt (phdrs + hash segment)
 *   2. Copy metadata (ELF hdr + hash) to a physically contiguous buffer → init_image
 *   3. Load each PT_LOAD segment into the reserved-memory carveout
 *   4. mem_setup(carveout_pa, size)
 *   5. auth_and_reset(pas_id)   ← gated by lapis_scm_pas=1
 *
 * TB520FU carveouts (dtb VERIFIED):
 *   ADSP  adspslpi_region @ 0x9df00000 size 0x4080000
 *   ADSP  q6_adsp_dtb     @ 0x9de80000 size 0x80000
 *   GPU   microcode/zap   @ 0x9b09a000 size 0x2000
 *
 * Boot-args:
 *   lapis_pil=0          idle (default)
 *   lapis_pil=1          run auth with lapis_pil_meta/mem/msz
 *   lapis_pil=2          parse mdt at lapis_pil_mdt_pa and load into carveout
 *   lapis_scm_pas=1      allow auth_and_reset (DANGEROUS — can hang SoC)
 *   lapis_pil_pas=<id>   default 1 (ADSP)
 */
#include "qcom_scm.h"

#define LOG(fmt, ...) klog("LapisPIL: " fmt "\n", ##__VA_ARGS__)

#define ELF_MAGIC 0x464c457f
#define PT_LOAD   1
#define PT_NULL   0
#define MAX_PHDRS 64

struct elf32_hdr {
	uint32_t magic; uint8_t cls, data, ver, osabi, abiver, pad[7];
	uint16_t type, machine; uint32_t version;
	uint32_t entry, phoff, shoff, flags;
	uint16_t ehsize, phentsize, phnum, shentsize, shnum, shstrndx;
};
struct elf32_phdr {
	uint32_t type, offset, vaddr, paddr, filesz, memsz, flags, align;
};

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
	if (!drv_bootarg("lapis_scm_pas", 0)) {
		LOG("  auth_and_reset SKIPPED (need lapis_scm_pas=1)");
		return -1;
	}
	LOG("PAS %u: auth_and_reset", pas_id);
	rc = qcom_scm_pas_auth_and_reset(pas_id);
	LOG("  auth_and_reset -> %d", rc);
	return rc;
}

/* Stage ELF metadata (ehdr + phdrs + hash/non-LOAD segments that live in the mdt)
 * into a freshly allocated contiguous page-aligned buffer; return its PA. */
static pmap_paddr_t stage_metadata(const uint8_t *mdt, uint32_t mdt_len, void **out_va, uint32_t *out_len)
{
	uint32_t sz = (mdt_len + 0xfffu) & ~0xfffu;
	uint8_t *va = IOMallocAligned(sz, 0x1000);
	if (!va) return 0;
	bzero(va, sz);
	memcpy(va, mdt, mdt_len);
	/* clean for TZ DMA */
	for (uintptr_t a = (uintptr_t)va & ~63ul; a < (uintptr_t)va + sz; a += 64)
		__asm__ volatile("dc civac, %0" :: "r"(a) : "memory");
	dsb_sy();
	*out_va = va; *out_len = sz;
	return kvtophys((vm_offset_t)va);
}

int lapis_pil_load_mdt(uint32_t pas_id, const void *mdt_v, uint32_t mdt_len,
                       pmap_paddr_t carveout_pa, uint64_t carveout_sz,
                       int (*seg_read)(unsigned idx, void *dst, uint32_t off, uint32_t len, void *ctx),
                       void *ctx)
{
	const uint8_t *mdt = (const uint8_t *)mdt_v;
	if (!mdt || mdt_len < sizeof(struct elf32_hdr)) { LOG("bad mdt"); return -1; }
	const struct elf32_hdr *eh = (const struct elf32_hdr *)mdt;
	if (eh->magic != ELF_MAGIC || eh->cls != 1) { LOG("not ELF32"); return -1; }
	if (eh->phnum > MAX_PHDRS || eh->phentsize != sizeof(struct elf32_phdr)) {
		LOG("bad phdrs phnum=%u entsize=%u", eh->phnum, eh->phentsize); return -1;
	}
	LOG("ELF32 machine=%u phnum=%u phoff=%#x mdt_len=%u carveout %#llx+%#llx",
	    eh->machine, eh->phnum, eh->phoff, mdt_len, carveout_pa, carveout_sz);

	pmap_paddr_t min_pa = ~0ull, max_pa = 0;
	unsigned nload = 0;
	for (unsigned i = 0; i < eh->phnum; i++) {
		const struct elf32_phdr *ph = (const struct elf32_phdr *)(mdt + eh->phoff + i * eh->phentsize);
		if (ph->type != PT_LOAD) continue;
		nload++;
		if (ph->paddr < min_pa) min_pa = ph->paddr;
		uint64_t end = (uint64_t)ph->paddr + ph->memsz;
		if (end > max_pa) max_pa = end;
		LOG("  PHDR%u LOAD paddr=%#x filesz=%#x memsz=%#x flags=%#x",
		    i, ph->paddr, ph->filesz, ph->memsz, ph->flags);
	}
	if (!nload) { LOG("no PT_LOAD"); return -1; }
	uint64_t need = max_pa - min_pa;
	LOG("image span %#llx..%#llx (%llu bytes), %u load segs", min_pa, max_pa, need, nload);
	if (min_pa != carveout_pa)
		LOG("NOTE: first paddr %#llx != carveout %#llx (using phdr paddr as reloc base)",
		    min_pa, carveout_pa);
	if (need > carveout_sz) { LOG("carveout too small"); return -1; }

	/* Map carveout and load segments. */
	vm_offset_t cva = ml_io_map((vm_offset_t)min_pa, (vm_offset_t)need);
	if (!cva) { LOG("carveout map failed"); return -1; }

	for (unsigned i = 0; i < eh->phnum; i++) {
		const struct elf32_phdr *ph = (const struct elf32_phdr *)(mdt + eh->phoff + i * eh->phentsize);
		if (ph->type != PT_LOAD) continue;
		uint8_t *dst = (uint8_t *)(cva + (ph->paddr - min_pa));
		bzero(dst, ph->memsz);
		if (ph->filesz == 0) continue;
		/* Prefer bytes embedded in the mdt (offset within mdt_len); else seg_read. */
		if (ph->offset + ph->filesz <= mdt_len) {
			memcpy(dst, mdt + ph->offset, ph->filesz);
		} else if (seg_read) {
			if (seg_read(i, dst, ph->offset, ph->filesz, ctx)) {
				LOG("seg_read PHDR%u failed", i); return -1;
			}
		} else {
			LOG("PHDR%u needs external segment (offset %#x filesz %#x) — NOT DONE without .bNN",
			    i, ph->offset, ph->filesz);
			return -2; /* missing blobs */
		}
		for (uintptr_t a = (uintptr_t)dst & ~63ul; a < (uintptr_t)dst + ph->memsz; a += 64)
			__asm__ volatile("dc civac, %0" :: "r"(a) : "memory");
	}
	dsb_sy();

	void *meta_va = NULL; uint32_t meta_len = 0;
	pmap_paddr_t meta_pa = stage_metadata(mdt, mdt_len, &meta_va, &meta_len);
	if (!meta_pa) { LOG("meta stage failed"); return -1; }
	LOG("metadata staged va=%p pa=%#llx len=%u", meta_va, meta_pa, meta_len);

	int rc = lapis_pil_auth(pas_id, meta_pa, min_pa, need);
	return rc;
}

void lapis_pil_start(void)
{
	int mode = drv_bootarg("lapis_pil", 0);
	uint32_t pas = (uint32_t)drv_bootarg("lapis_pil_pas", PAS_ADSP);
	LOG("PIL ready (mode %d, pas %u)", mode, pas);
	LOG("carveouts: ADSP 0x9df00000+0x4080000, ADSP_DTB 0x9de80000+0x80000, ZAP 0x9b09a000+0x2000");
	LOG("need blobs for full load: adsp.b00..b55, kiwi/amss.bin — request from owner if missing");
	if (!mode) {
		LOG("set lapis_pil=1 (raw PAs) or =2 (mdt@pa) plus lapis_scm_pas=1");
		return;
	}
	if (mode == 1) {
		pmap_paddr_t meta = 0, mem = 0; uint64_t msz = 0;
		PE_parse_boot_argn("lapis_pil_meta", &meta, sizeof(meta));
		PE_parse_boot_argn("lapis_pil_mem", &mem, sizeof(mem));
		PE_parse_boot_argn("lapis_pil_msz", &msz, sizeof(msz));
		if (!meta || !mem || !msz) {
			LOG("need lapis_pil_meta/mem/msz");
			return;
		}
		lapis_pil_auth(pas, meta, mem, msz);
		return;
	}
	if (mode == 2) {
		pmap_paddr_t mdt_pa = 0; uint32_t mdt_len = 0;
		pmap_paddr_t carve = 0x9df00000ull; uint64_t csz = 0x4080000ull;
		PE_parse_boot_argn("lapis_pil_mdt_pa", &mdt_pa, sizeof(mdt_pa));
		PE_parse_boot_argn("lapis_pil_mdt_len", &mdt_len, sizeof(mdt_len));
		PE_parse_boot_argn("lapis_pil_mem", &carve, sizeof(carve));
		PE_parse_boot_argn("lapis_pil_msz", &csz, sizeof(csz));
		if (!mdt_pa || !mdt_len) { LOG("need lapis_pil_mdt_pa/len"); return; }
		vm_offset_t mva = ml_io_map((vm_offset_t)mdt_pa, mdt_len);
		if (!mva) { LOG("mdt map fail"); return; }
		/* Without seg_read, only works if all LOAD filesz fit inside the mdt
		 * (true for tiny ZAP; false for ADSP which needs .bNN). */
		int rc = lapis_pil_load_mdt(pas, (void *)mva, mdt_len, carve, csz, NULL, NULL);
		LOG("load_mdt -> %d", rc);
		return;
	}
	LOG("unknown lapis_pil mode %d", mode);
}
