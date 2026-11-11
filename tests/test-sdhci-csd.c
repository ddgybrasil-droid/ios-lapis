#include <stdio.h>
#include <string.h>
#include "../drivers/sdhci/sdhci_csd.h"

static int fail;
static void expect(int cond, const char *msg)
{
	if (!cond) { fprintf(stderr, "FAIL %s\n", msg); fail = 1; }
}

static void stuff(unsigned csd[4], unsigned start, unsigned size, unsigned val)
{
	unsigned b;
	for (b = 0; b < size; b++) {
		unsigned bit = start + b;
		unsigned off = 3u - bit / 32u;
		unsigned sh = bit & 31u;
		if ((val >> b) & 1u) csd[off] |= 1u << sh;
	}
}

static void to_regs(const unsigned csd[4], unsigned regs[4])
{
	unsigned raw[4];
	raw[0] = csd[0] >> 8;
	raw[1] = ((csd[0] & 0xffu) << 24) | (csd[1] >> 8);
	raw[2] = ((csd[1] & 0xffu) << 24) | (csd[2] >> 8);
	raw[3] = ((csd[2] & 0xffu) << 24) | (csd[3] >> 8);
	regs[0] = raw[3];
	regs[1] = raw[2];
	regs[2] = raw[1];
	regs[3] = raw[0];
}

int main(void)
{
	unsigned csd[4], regs[4], blocks = 0;

	memset(csd, 0, sizeof(csd));
	stuff(csd, 126, 2, 1u);
	stuff(csd, 48, 22, 16383u);
	expect(lapis_sd_blocks_from_csd(csd, &blocks) == 0 && blocks == 16777216u, "csd v2");
	to_regs(csd, regs);
	blocks = 0;
	expect(lapis_sd_csd_blocks(regs, &blocks) == 0 && blocks == 16777216u, "sdhci v2");

	memset(csd, 0, sizeof(csd));
	stuff(csd, 80, 4, 9u);
	stuff(csd, 62, 12, 100u);
	stuff(csd, 47, 3, 0u);
	expect(lapis_sd_blocks_from_csd(csd, &blocks) == 0 && blocks == 404u, "csd v1");
	to_regs(csd, regs);
	expect(lapis_sd_csd_blocks(regs, &blocks) == 0 && blocks == 404u, "sdhci v1");

	memset(csd, 0, sizeof(csd));
	expect(lapis_sd_blocks_from_csd(csd, &blocks) != 0, "empty csd");
	memset(regs, 0, sizeof(regs));
	expect(lapis_sd_csd_blocks(regs, &blocks) != 0, "empty response");
	if (fail) return 1;
	printf("PASS sdhci-csd\n");
	return 0;
}
