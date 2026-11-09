
#pragma once
#include <stdint.h>

/* Boot the LAPISXNU payload found in the ABL ramdisk; returns only on error. */
const char *loader_boot(uintptr_t dtb);


#define LAPIS_PHYS_BASE 0x8C0000000UL
extern uint64_t g_dtb_ram_end;
extern char g_mem_strip[3][248];
