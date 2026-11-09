
/*
 * Act as iBoot for xnu-12377 on lapis.
 *
 * Payload discovery (TB520FU / Android 13+ GKI):
 *   Stock boot.img is kernel-only; generic ramdisk lives in init_boot.
 *   Qualcomm ABL `fastboot boot` loads our kernel Image but typically leaves
 *   linux,initrd-* pointing at the on-flash init_boot (gzip cpio), NOT at a
 *   ramdisk embedded in the temporary boot.img. So LAPISXNU is appended to
 *   the Linux Image (after _end) and image_size is patched so ABL loads it.
 *   FDT initrd is only a fallback when its bytes already start with LAPISXNU.
 *
 * CRITICAL: xnu-12377 uses BOOT_LINE_LENGTH=1024 and sizeof(boot_args)=1152.
 */
#include <stdint.h>
#include "console.h"
#include "fdt.h"
#include "loader.h"

#define BOOT_LINE_LENGTH	1024
#define PAGE_16K		0x4000UL

struct payload_seg {
	uint64_t vmaddr, vmsize, file_off, file_size;
};

struct payload {
	char magic[8];
	uint32_t version, nsegs;
	uint64_t virt_base, entry_va, adt_off, adt_size, kernel_end;
	uint64_t phys_base, mem_size;
	char cmdline[BOOT_LINE_LENGTH];
	struct payload_seg segs[];
};

struct boot_video {
	uint64_t v_baseAddr, v_display, v_rowBytes, v_width, v_height, v_depth;
};

struct boot_args {
	uint16_t Revision, Version;
	uint64_t virtBase, physBase, memSize, topOfKernelData;
	struct boot_video Video;
	uint32_t machineType;
	uint64_t deviceTreeP;
	uint32_t deviceTreeLength;
	char CommandLine[BOOT_LINE_LENGTH];
	uint64_t bootFlags, memSizeActual;
};

_Static_assert(__builtin_offsetof(struct payload, segs) == 0x448, "payload header layout");
_Static_assert(__builtin_offsetof(struct boot_args, Video) == 40, "boot_args layout");
_Static_assert(__builtin_offsetof(struct boot_args, CommandLine) == 108, "boot_args layout");
_Static_assert(sizeof(struct boot_args) == 1152, "boot_args layout");

/* Linker / Image symbols (shim.ld + start.S). */
extern char _head[];
extern char _end[];

static void copy64(uintptr_t dst, uintptr_t src, uint64_t n)
{
	volatile uint64_t *d = (volatile uint64_t *)dst;
	volatile const uint64_t *s = (volatile const uint64_t *)src;
	for (uint64_t i = 0; i < (n + 7) / 8; i++)
		d[i] = s[i];
}

static void zero64(uintptr_t dst, uint64_t n)
{
	volatile uint64_t *d = (volatile uint64_t *)dst;
	for (uint64_t i = 0; i < n / 8; i++)
		d[i] = 0;
}

/* Compare byte-wise so .rodata does not contain a contiguous "LAPISXNU"
 * that would false-positive host-side Image scans. */
static int magic_ok(const char *m)
{
	return m[0] == 'L' && m[1] == 'A' && m[2] == 'P' && m[3] == 'I' &&
	       m[4] == 'S' && m[5] == 'X' && m[6] == 'N' && m[7] == 'U';
}

/* Paint the first 8 bytes at addr as hex (no scan; single aligned load). */
static void con_peek8(const char *label, uintptr_t addr)
{
	volatile const uint8_t *p = (volatile const uint8_t *)addr;
	char buf[20];
	static const char hex[] = "0123456789ABCDEF";
	for (int i = 0; i < 8; i++) {
		buf[i * 2] = hex[(p[i] >> 4) & 0xf];
		buf[i * 2 + 1] = hex[p[i] & 0xf];
	}
	buf[16] = 0;
	con_str(label, buf);
}

static uint64_t image_size_field(void)
{
	/* Linux arm64 Image header: u64 image_size @ offset 16. */
	return *(volatile uint64_t *)(uintptr_t)(_head + 16);
}

/*
 * Embedded payload sits at _end inside the loaded Image. ABL must have loaded
 * image_size bytes covering [_head, _head+image_size). Refuse if _end is
 * outside that window (avoids reading past what ABL mapped).
 */
static const struct payload *try_embedded(void)
{
	uintptr_t load = (uintptr_t)_head;
	uintptr_t end = (uintptr_t)_end;
	uint64_t isz = image_size_field();
	uintptr_t limit = load + (uintptr_t)isz;

	con_str("PATH", "embedded@_end");
	con_hex("HEAD", load);
	con_hex("END", end);
	con_hex("IMGSZ", isz);

	if (isz < 0x40 || end < load || end + 64 > limit) {
		con_str("EMB", "out of image_size");
		return 0;
	}
	con_peek8("EMB8", end);
	const struct payload *p = (const struct payload *)end;
	if (!magic_ok(p->magic)) {
		con_str("EMB", "bad magic at _end");
		return 0;
	}
	/* Header must fit inside the loaded Image. */
	if (end + 0x448 + 32 > limit) {
		con_str("EMB", "header past image");
		return 0;
	}
	return p;
}

/* Only accept FDT initrd when it already begins with LAPISXNU (no scans). */
static const struct payload *try_fdt_initrd(uintptr_t dtb)
{
	uint64_t rd_start = 0, rd_end = 0;

	con_str("PATH", "fdt initrd");
	if (!dtb) {
		con_str("FDT", "null dtb");
		return 0;
	}
	con_hex("DTB", dtb);

	if (!fdt_chosen_u64(dtb, "linux,initrd-start", &rd_start) ||
	    !fdt_chosen_u64(dtb, "linux,initrd-end", &rd_end)) {
		con_str("FDT", "no linux,initrd-*");
		return 0;
	}
	con_hex("RD_LO", rd_start);
	con_hex("RD_HI", rd_end);

	if (rd_end <= rd_start || rd_end - rd_start < 64) {
		con_str("FDT", "initrd range bad");
		return 0;
	}
	/* Refuse absurd sizes (protect against corrupt props). Cap 64 MiB. */
	if (rd_end - rd_start > (64ULL << 20)) {
		con_str("FDT", "initrd too large");
		return 0;
	}
	con_peek8("RD8", (uintptr_t)rd_start);
	const struct payload *p = (const struct payload *)(uintptr_t)rd_start;
	if (!magic_ok(p->magic)) {
		/* Typical on this device: gzip (1f8b) or cpio (070701) from init_boot. */
		con_str("FDT", "not payload (likely init_boot)");
		return 0;
	}
	return p;
}

typedef void (*xnu_entry_t)(uint64_t boot_args_pa);

static const char *boot_payload(const struct payload *p, const char *src)
{
	con_str("SRC", src);
	con_hex2("HDR", "ver=", p->version, "nseg=", p->nsegs);
	if (p->version != 1 || (p->adt_off & 7) || (((uintptr_t)p) & 7))
		return "bad payload header";

	uint64_t pa_of_va_base = p->phys_base - p->virt_base;
	con_str("STEP", "3 phys/virt map");
	con_hex("PHYS", p->phys_base);
	con_hex("MEMSZ", p->mem_size);
	con_hex("VIRT", p->virt_base);
	con_hex("ENTRY", p->entry_va);

	/* Refuse if the loaded Image sits inside the XNU DRAM bank. */
	{
		uintptr_t img_lo = (uintptr_t)_head;
		uintptr_t img_hi = img_lo + (uintptr_t)image_size_field();
		if (img_hi > p->phys_base && img_lo < p->phys_base + p->mem_size)
			return "Image overlaps kernel memory";
	}

	con_str("STEP", "4 copy segments");
	for (uint32_t i = 0; i < p->nsegs; i++) {
		const struct payload_seg *s = &p->segs[i];
		uintptr_t dst = s->vmaddr + pa_of_va_base;
		copy64(dst, (uintptr_t)p + s->file_off, s->file_size);
		if (s->vmsize > s->file_size)
			zero64(dst + ((s->file_size + 7) & ~7UL),
			       s->vmsize - ((s->file_size + 7) & ~7UL));
	}
	con_hex("KEND", p->kernel_end + pa_of_va_base);

	con_str("STEP", "5 place ADT");
	uintptr_t adt_pa = p->kernel_end + pa_of_va_base;
	copy64(adt_pa, (uintptr_t)p + p->adt_off, p->adt_size);
	con_hex("ADT_PA", adt_pa);
	con_hex("ADT_SZ", p->adt_size);

	con_str("STEP", "6 boot_args");
	uintptr_t ba_pa = (adt_pa + p->adt_size + PAGE_16K - 1) & ~(PAGE_16K - 1);
	uintptr_t top = ba_pa + PAGE_16K;

	zero64(ba_pa, PAGE_16K);
	volatile struct boot_args *ba = (volatile struct boot_args *)ba_pa;
	ba->Revision = 2;
	ba->Version = 2;
	ba->virtBase = p->virt_base;
	ba->physBase = p->phys_base;
	ba->memSize = p->mem_size;
	ba->topOfKernelData = top;
	ba->Video.v_baseAddr = FB_BASE;
	ba->Video.v_display = 1;
	ba->Video.v_rowBytes = FB_STRIDE;
	ba->Video.v_width = FB_W;
	ba->Video.v_height = FB_H;
	ba->Video.v_depth = 32;
	ba->deviceTreeP = adt_pa - pa_of_va_base;
	ba->deviceTreeLength = (uint32_t)p->adt_size;
	for (int i = 0; i < BOOT_LINE_LENGTH; i++)
		ba->CommandLine[i] = p->cmdline[i];
	ba->memSizeActual = p->mem_size;
	con_hex("BA_PA", ba_pa);
	con_hex("TOPKD", top);

	uintptr_t entry_pa = p->entry_va + pa_of_va_base;
	con_str("STEP", "7 jump XNU");
	con_hex("JUMP", entry_pa);
	con_str("XNU", "handoff (MMU off)");

	/*
	 * Ensure XNU's copied pages are visible to instruction fetch and that
	 * any speculative D-cache lines covering the destination are cleaned.
	 * ABL may have left caches enabled; we run with MMU off.
	 */
	for (uintptr_t pa = p->phys_base; pa < top; pa += 64)
		__asm__ volatile("dc civac, %0" :: "r"(pa) : "memory");
	__asm__ volatile("dsb sy; ic iallu; dsb sy; isb" ::: "memory");

	/* Green handoff bar: if XNU stage0 white band appears, entry worked. */
	{
		volatile uint32_t *fb = (volatile uint32_t *)FB_BASE;
		for (int y = FB_H - 64; y < FB_H; y++)
			for (int x = 0; x < 256; x++)
				fb[y * (FB_STRIDE / 4) + x] = 0xff00ff00U;
	}
	__asm__ volatile("dsb sy" ::: "memory");

	((xnu_entry_t)entry_pa)(ba_pa);
	return "xnu returned";
}

const char *loader_boot(uintptr_t dtb)
{
	const struct payload *p;

	con_str("STEP", "1 find payload");
	p = try_embedded();
	if (p)
		return boot_payload(p, "embedded");

	con_skip();
	p = try_fdt_initrd(dtb);
	if (p)
		return boot_payload(p, "fdt-initrd");

	return "no payload (embed in Image; GKI ignores boot.img ramdisk)";
}

