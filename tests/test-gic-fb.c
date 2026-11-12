#include <stdint.h>
#include <stdio.h>
#include "../drivers/gic/gic_decode.h"
#include "../drivers/fb/fb_geom.h"

static int fail;
static void expect(int cond, const char *msg)
{
	if (!cond) { fprintf(stderr, "FAIL %s\n", msg); fail = 1; }
}

int main(void)
{
	uint64_t frames[2], sgi = 0, off = 0, bytes = 0;
	expect(lapis_gic_intid_count(0x37a0208u) == 288u, "qemu typer");
	expect(lapis_gic_intid_count(0x1fu) == 1020u, "intid cap");
	expect(lapis_gic_intid_count(0) == 32u, "sgi ppi only");
	expect(lapis_gic_arch_ok(0x30u) == 1 && lapis_gic_arch_ok(0x40u) == 1, "arch");
	expect(lapis_gic_arch_ok(0x20u) == 0 && lapis_gic_arch_ok(0) == 0, "arch reject");
	expect(lapis_gic_affinity(0x700) == 0x700u, "aff0");
	expect(lapis_gic_affinity(0x100000001ull) == 0x01000001u, "aff3");
	frames[0] = (uint64_t)0x100u << 32;
	frames[1] = (uint64_t)0x700u << 32;
	expect(lapis_gic_redist_find(0x700, frames, 2, 0x20000) == 0x20000, "redist");
	frames[0] |= 1ull << 4;
	expect(lapis_gic_redist_find(0x700, frames, 2, 0x20000) == -1, "last stops");
	expect(lapis_gic_redist_find(0x100, frames, 2, 0x10000) == -1, "stride");
	expect(lapis_gic_edge(1) && lapis_gic_edge(2) && !lapis_gic_edge(4) && !lapis_gic_edge(8) &&
	       !lapis_gic_edge(3), "trigger");
	expect(lapis_gic_sgi1r(0x3, 1, &sgi) == 0 && sgi == 0x01000008ull, "sgi");
	expect(lapis_gic_sgi1r(0x10, 1, &sgi) != 0, "sgi aff0");
	expect(lapis_gic_irouter(0x101000005ull) == 0x100000005ull, "irouter");
	expect(lapis_gic_is_spurious(1023) && lapis_gic_is_spurious(1020) && !lapis_gic_is_spurious(1019),
	       "spurious");
	expect(lapis_gic_redist_awake(0) == 1 && lapis_gic_redist_awake(0x4) == 0, "waker");
	expect(lapis_fb_geometry(2944, 1840, 11776, &bytes) == 0 && bytes == 11776ull * 1840ull,
	       "splash geometry");
	expect(lapis_fb_offset(1, 2, 2944, 1840, 11776, &off) == 0 && off == 2ull * 11776ull + 4ull,
	       "pixel");
	expect(lapis_fb_offset(2944, 0, 2944, 1840, 11776, &off) != 0, "pixel x");
	expect(lapis_fb_geometry(2944, 1840, 11772, &bytes) != 0, "short stride");
	if (fail) return 1;
	printf("PASS gic-fb\n");
	return 0;
}
