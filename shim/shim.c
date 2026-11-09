
/*
 * lapis M1 shim: paint staged progress on the splash FB, then hand off to
 * the LAPISXNU payload embedded in the Linux Image (GKI-safe).
 */
#include <stdint.h>
#include "console.h"
#include "loader.h"
#include "fdt.h"

#define rsr(r) ({ uint64_t _v; __asm__ volatile("mrs %0, " r : "=r"(_v)); _v; })

static inline uint32_t rd32(uintptr_t a) { return *(volatile uint32_t *)a; }

void exc_handler(uint64_t slot, uint64_t *regs)
{
	(void)regs;
	con_clear(BLUE);
	con_str("PANIC", "exception");
	con_hex("SLOT", slot);
	con_hex("ESR", rsr("esr_el1"));
	con_hex("ELR", rsr("elr_el1"));
	con_hex("FAR", rsr("far_el1"));
	for (;;)
		__asm__ volatile("wfe");
}

void secondary_main(uint64_t idx)
{
	(void)idx;
	for (;;)
		__asm__ volatile("wfe");
}

/* ---- s8: DTB DRAM map page + physmap clamp ---- */
static struct fdt_memmap mm;
uint64_t g_dtb_ram_end;		/* 0 = no clamp; read by loader.c */
char g_mem_strip[3][248];	/* bottom-strip summary, drawn by loader before jump */

static char *cat(char *d, const char *s) { while (*s) *d++ = *s++; *d = 0; return d; }
static char *hx(char *d, uint64_t v) { d = cat(d, "0x"); int n = 16; while (n > 9 && !((v >> (4 * (n - 1))) & 0xf)) n--; d = con_fmt_hex(d, v, n); *d = 0; return d; }
static char *rng(char *d, uint64_t b, uint64_t s) { d = hx(d, b); d = cat(d, "-"); d = hx(d, b + s); return d; }

#define PANEL_BG 0xff101030U
static void hold_seconds(unsigned sec)
{
	uint64_t f = rsr("cntfrq_el0"), t0 = rsr("cntpct_el0");
	if (!f || f > 2000000000ULL) f = 19200000;
	while (rsr("cntpct_el0") - t0 < f * sec)
		;
}

static void dtb_memmap_page(uint64_t dtb, uint64_t phys_base)
{
	char l[200], *q;
	int ok = fdt_memmap(dtb, &mm);
	con_clear(PANEL_BG);
	int y = 8, x = 16, col = 0;
#define LINE(fg) do { con_text_at(x, y, 2, l, fg, PANEL_BG); y += 32; if (y > FB_H - 96) { y = 8 + 32 * 3; x += 1472; col++; } } while (0)
	q = cat(l, "S8 DTB DRAM MAP  dtb="); q = hx(q, dtb); q = cat(q, ok ? "  parse=ok" : "  parse=FAIL");
	q = cat(q, "  ac/sc="); q = con_fmt_hex(q, mm.root_ac, 1); q = cat(q, "/"); q = con_fmt_hex(q, mm.root_sc, 1);
	q = cat(q, "  rsv ac/sc="); q = con_fmt_hex(q, mm.rsv_ac, 1); q = cat(q, "/"); q = con_fmt_hex(q, mm.rsv_sc, 1); LINE(YELLOW);
	for (int i = 0; i < mm.nmem; i++) { q = cat(l, "MEM "); q = rng(q, mm.mem[i].base, mm.mem[i].size); LINE(GREEN); }
	if (mm.mem_trunc) { cat(l, "MEM list truncated"); LINE(RED); }
	for (int i = 0; i < mm.nrsvhdr; i++) { q = cat(l, "MRSV "); q = rng(q, mm.rsvhdr[i].base, mm.rsvhdr[i].size); LINE(WHITE); }
	for (int i = 0; i < mm.nrsv; i++) {
		q = cat(l, mm.rsv[i].nomap ? "R! " : "R  ");
		if (mm.rsv[i].has_reg) q = rng(q, mm.rsv[i].base, mm.rsv[i].size); else q = cat(q, "(dynamic, no reg)");
		q = cat(q, " "); q = cat(q, mm.rsv[i].name); LINE(WHITE);
	}
	if (mm.rsv_trunc) { cat(l, "RSV list truncated"); LINE(RED); }

	/* clamp: DTB bank containing phys_base (merging adjacent banks), cut at the
	 * first reserved region above phys_base. */
	uint64_t end = 0; int conflict = 0;
	for (int i = 0; i < mm.nmem; i++)
		if (mm.mem[i].base <= phys_base && phys_base < mm.mem[i].base + mm.mem[i].size)
			end = mm.mem[i].base + mm.mem[i].size;
	for (int again = 1; end && again; ) {
		again = 0;
		for (int i = 0; i < mm.nmem; i++)
			if (mm.mem[i].base == end && mm.mem[i].size) { end += mm.mem[i].size; again = 1; }
	}
	uint64_t raw_end = end;
	for (int i = 0; end && i < mm.nrsv + mm.nrsvhdr; i++) {
		uint64_t b, s2;
		if (i < mm.nrsv) { if (!mm.rsv[i].has_reg) continue; b = mm.rsv[i].base; s2 = mm.rsv[i].size; }
		else { b = mm.rsvhdr[i - mm.nrsv].base; s2 = mm.rsvhdr[i - mm.nrsv].size; }
		if (b < end && b + s2 > phys_base) {
			if (b <= phys_base) conflict = 1;
			else end = b;
		}
	}
	g_dtb_ram_end = (end && !conflict) ? end : 0;
	q = cat(l, "phys_base "); q = hx(q, phys_base); q = cat(q, " bank_end "); q = hx(q, raw_end);
	q = cat(q, " safe_end "); q = hx(q, end); q = cat(q, conflict ? " RESERVED@phys_base!" : ""); q = cat(q, end ? "" : " (phys_base NOT in /memory)");
	LINE(conflict || !end ? RED : YELLOW);
	(void)col;

	/* strip line 0: memory banks; line 1: reserved regions in [phys_base, raw_end+) */
	q = cat(g_mem_strip[0], "DTB MEM:");
	for (int i = 0; i < mm.nmem && q < g_mem_strip[0] + 200; i++) { q = cat(q, " "); q = rng(q, mm.mem[i].base, mm.mem[i].size); }
	q = cat(g_mem_strip[1], "RSV>=phys:");
	for (int i = 0; i < mm.nrsv && q < g_mem_strip[1] + 190; i++)
		if (mm.rsv[i].has_reg && mm.rsv[i].base + mm.rsv[i].size > phys_base) {
			q = cat(q, " "); q = rng(q, mm.rsv[i].base, mm.rsv[i].size);
		}
	hold_seconds(ok ? 10 : 3);
#undef LINE
}

void shim_main(uint64_t dtb)
{
	dtb_memmap_page(dtb, LAPIS_PHYS_BASE);
	con_clear(RED);
	con_str("SHIM", "ios-lapis M1 (xnu-12377)");
	con_str("STEP", "0 entry EL1");
	con_hex("DTB", dtb);
	if (dtb)
		con_hex("MAGIC", __builtin_bswap32(rd32(dtb)));
	con_hex("FB", FB_BASE);
	con_hex2("RES", "w=", FB_W, "h=", FB_H);
	con_hex2("STR", "b=", FB_STRIDE, "bpp=", 32);
	con_skip();

	const char *why = loader_boot((uintptr_t)dtb);
	con_clear(BLUE);
	con_str("STOP", "loader returned");
	con_str("WHY", why);
	con_str("HINT", "payload must be in Image after _end");
	con_str("NOTE", "GKI: boot.img ramdisk not used by ABL");
	for (;;)
		__asm__ volatile("wfe");
}

