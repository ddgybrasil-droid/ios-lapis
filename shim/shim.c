/*
 * lapis M1 shim: paint a banner, then hand off to the LAPISXNU payload
 * in the ABL ramdisk (MMU stays off for the jump).
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
	con_str("SHIM", "ios-lapis M1 loader (xnu-12377)");
	con_hex2("DTB", "at=", (uint32_t)dtb, "magic=",
		 dtb ? __builtin_bswap32(rd32(dtb)) : 0);
	con_hex2("FB", "base=", (uint32_t)FB_BASE, "str=", FB_STRIDE);
	con_hex2("RES", "w=", FB_W, "h=", FB_H);
	con_skip();

	const char *why = loader_boot((uintptr_t)dtb);
	con_str("LOADER", why);
	con_str("HINT", "pack a LAPISXNU payload as the ramdisk");
	for (;;)
		__asm__ volatile("wfe");
}
