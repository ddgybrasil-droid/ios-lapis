
/*
 * lapis M1 shim: paint staged progress on the splash FB, then hand off to
 * the LAPISXNU payload embedded in the Linux Image (GKI-safe).
 */
#include <stdint.h>
#include "console.h"
#include "loader.h"

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

void shim_main(uint64_t dtb)
{
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

