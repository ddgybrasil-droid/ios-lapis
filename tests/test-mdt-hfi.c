#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../drivers/scm/mdt_image.h"
#include "../drivers/gpu/lapis_hfi.h"
#include "../drivers/gpu/lapis_pm4.h"

static int fail;
static void expect(int cond, const char *msg)
{
	if (!cond) { fprintf(stderr, "FAIL %s\n", msg); fail = 1; }
}

static void put16(uint8_t *p, unsigned v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, unsigned v)
{
	p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
	p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
static void phdr(uint8_t *h, unsigned type, unsigned off, unsigned addr,
                 unsigned filesz, unsigned memsz, unsigned flags)
{
	memset(h, 0, 32);
	put32(h + 0, type); put32(h + 4, off); put32(h + 8, addr); put32(h + 12, addr);
	put32(h + 16, filesz); put32(h + 20, memsz); put32(h + 24, flags);
}

/* Three program headers: metadata, one relocatable load, hash. */
static unsigned build_mdt(uint8_t *b, unsigned load_addr, unsigned load_mem,
                          unsigned load_file, unsigned flags)
{
	unsigned hdr = 148, hash = 32, payload = 180;
	memset(b, 0, 256);
	b[0] = 0x7f; b[1] = 'E'; b[2] = 'L'; b[3] = 'F';
	b[4] = 1; b[5] = 1; b[6] = 1;
	put32(b + 20, 1); put32(b + 28, 52);
	put16(b + 40, 52); put16(b + 42, 32); put16(b + 44, 3);
	phdr(b + 52, 0, 0, 0, hdr, 0, 0);
	phdr(b + 84, 1, payload, load_addr, load_file, load_mem, flags);
	phdr(b + 116, 0, hdr, 0, hash, hash, 0x02000000u);
	b[payload] = 0xab;
	return payload + load_file;
}

int main(void)
{
	uint8_t image[256];
	struct lapis_mdt_plan plan;
	unsigned nop = 0, wait = 0, hfi = 0, ack[4];
	unsigned n;
	const uint64_t carve = 0x9b09a000ull;

	n = build_mdt(image, 0x8000, 0x1000, 4, 0x08000005u);
	expect(lapis_mdt_parse(image, n, carve, 0x2000, &plan) == 0, "mdt parse");
	expect(plan.count == 1 && plan.relocate == 1 && plan.split == 0 &&
	       plan.hash_external == 0 && plan.segments[0].destination == 0 &&
	       plan.segments[0].file_size == 4 && plan.image_size == 0x1000,
	       "mdt placement");
	image[5] = 2;
	expect(lapis_mdt_parse(image, n, carve, 0x2000, &plan) != 0, "mdt endian");
	image[5] = 1;
	expect(lapis_mdt_parse(image, n, carve, 0x800, &plan) != 0, "mdt carveout");
	n = build_mdt(image, 0x8000, 0x1000, 0x2000, 0x08000005u);
	expect(lapis_mdt_parse(image, n, carve, 0x2000, &plan) != 0, "mdt filesz");

	memset(image, 0, sizeof(image));
	image[0] = 0x7f; image[1] = 'E'; image[2] = 'L'; image[3] = 'F';
	image[4] = 1; image[5] = 1; image[6] = 1;
	put32(image + 20, 1); put32(image + 28, 52);
	put16(image + 40, 52); put16(image + 42, 32); put16(image + 44, 4);
	phdr(image + 52, 0, 0, 0, 180, 0, 0);
	phdr(image + 84, 1, 0, 0x8000, 0, 0x100, 0x08000005u);
	phdr(image + 116, 1, 0, 0x8000, 0, 0x100, 0x08000005u);
	phdr(image + 148, 0, 180, 0, 32, 32, 0x02000000u);
	expect(lapis_mdt_parse(image, 212, carve, 0x2000, &plan) != 0, "mdt overlap");

	n = build_mdt(image, 0x8000, 0x100, 16, 0x08000005u);
	put32(image + 84 + 4, 0x100000);
	expect(lapis_mdt_parse(image, 148 + 32, carve, 0x2000, &plan) == 0 && plan.split == 1,
	       "mdt split is a plan, not a load");

	expect(lapis_pm4_type7(0x10, 1, &nop) == 0 && nop == 0x70100001u, "pm4 nop");
	expect(lapis_pm4_type7(0x12, 0, &wait) == 0 && wait == 0x70928000u, "pm4 wait");
	expect(lapis_pm4_type7(0x80, 1, &nop) != 0, "pm4 opcode");
	expect(lapis_hfi_header(LAPIS_HFI_INIT, LAPIS_HFI_INIT_DWORDS, LAPIS_HFI_CMD, 1, &hfi) == 0 &&
	       hfi == 0x00100500u, "hfi init");
	expect(lapis_hfi_header(1, 0, 0, 1, &hfi) != 0, "hfi empty");
	expect(lapis_hfi_reserve(0, 0, 16, 5) == 0, "hfi space");
	expect(lapis_hfi_reserve(0, 0, 16, 13) != 0, "hfi pad does not fit");
	expect(lapis_hfi_reserve(0, 1, 16, 1) != 0, "hfi full");
	expect(lapis_hfi_reserve(0, 0, 24, 1) != 0, "hfi size");
	ack[0] = (3u << 8) | (LAPIS_HFI_ACK << 16);
	ack[1] = hfi;
	ack[2] = 0;
	expect(lapis_hfi_ack_ok(ack, 3, 1) == 0, "hfi ack");
	ack[2] = 2;
	expect(lapis_hfi_ack_ok(ack, 3, 1) != 0, "hfi error dword");
	ack[0] = (3u << 8) | LAPIS_HFI_F2H_ERROR;
	ack[2] = 0;
	expect(lapis_hfi_ack_ok(ack, 3, 1) != 0, "hfi firmware error");

	if (fail) return 1;
	printf("PASS mdt-hfi\n");
	return 0;
}
