
/*
 * Just enough flattened-device-tree parsing to find what ABL put in
 * /chosen (initrd location). Big-endian, 4-byte aligned tokens.
 */
#include "fdt.h"

#define FDT_MAGIC	0xd00dfeed
#define FDT_BEGIN_NODE	1
#define FDT_END_NODE	2
#define FDT_PROP	3
#define FDT_NOP		4
#define FDT_END		9

static uint32_t be32(uintptr_t a)
{
	return __builtin_bswap32(*(volatile uint32_t *)a);
}

static int streq(const char *a, const char *b)
{
	while (*a && *a == *b)
		a++, b++;
	return *a == *b;
}

static uintptr_t align4(uintptr_t a)
{
	return (a + 3) & ~3UL;
}

static uintptr_t skip_str(uintptr_t p)
{
	/* byte loads are fine on Device memory as long as they are aligned to 1 */
	while (*(volatile char *)p)
		p++;
	return p + 1;
}

int fdt_chosen_u64(uintptr_t fdt, const char *prop, uint64_t *out)
{
	if (!fdt || be32(fdt) != FDT_MAGIC)
		return 0;
	uintptr_t p = fdt + be32(fdt + 8);		/* off_dt_struct */
	uintptr_t strings = fdt + be32(fdt + 12);	/* off_dt_strings */
	int depth = 0, in_chosen = 0;

	for (;;) {
		uint32_t tok = be32(p);
		p += 4;
		switch (tok) {
		case FDT_BEGIN_NODE: {
			const char *name = (const char *)p;
			depth++;
			in_chosen = depth == 2 && streq(name, "chosen");
			p = align4(skip_str(p));
			break;
		}
		case FDT_END_NODE:
			if (in_chosen)
				return 0;
			depth--;
			break;
		case FDT_PROP: {
			uint32_t len = be32(p), nameoff = be32(p + 4);
			uintptr_t val = p + 8;
			if (in_chosen && streq((const char *)(strings + nameoff), prop)) {
				if (len == 4)
					*out = be32(val);
				else if (len == 8)
					*out = (uint64_t)be32(val) << 32 | be32(val + 4);
				else
					return 0;
				return 1;
			}
			p = align4(val + len);
			break;
		}
		case FDT_NOP:
			break;
		default:	/* FDT_END or garbage */
			return 0;
		}
	}
}


/* ---------------- s8: /memory + /reserved-memory ---------------- */
static int prefix(const char *s, const char *p)
{
	while (*p)
		if (*s++ != *p++)
			return 0;
	return *s == 0 || *s == '@';
}

static uint64_t cells(uintptr_t v, uint32_t n)
{
	uint64_t r = 0;
	for (uint32_t i = 0; i < n; i++)
		r = r << 32 | be32(v + 4 * i);
	return r;
}

int fdt_memmap(uintptr_t fdt, struct fdt_memmap *m)
{
	uint8_t *z = (uint8_t *)m;
	for (unsigned i = 0; i < sizeof(*m); i++)
		z[i] = 0;
	m->root_ac = 2; m->root_sc = 1; m->rsv_ac = 2; m->rsv_sc = 2;
	if (!fdt || be32(fdt) != FDT_MAGIC)
		return 0;
	/* /memreserve/ entries: pairs of be64, terminated by 0,0 */
	for (uintptr_t r = fdt + be32(fdt + 16); m->nrsvhdr < 16; r += 16) {
		uint64_t b = cells(r, 2), s = cells(r + 8, 2);
		if (!b && !s)
			break;
		m->rsvhdr[m->nrsvhdr].base = b;
		m->rsvhdr[m->nrsvhdr++].size = s;
	}
	uintptr_t p = fdt + be32(fdt + 8);
	uintptr_t strings = fdt + be32(fdt + 12);
	int depth = 0, in_mem = 0, in_rsvnode = 0, cur = -1;

	for (int guard = 0; guard < 4000000; guard++) {
		uint32_t tok = be32(p);
		p += 4;
		switch (tok) {
		case FDT_BEGIN_NODE: {
			const char *name = (const char *)p;
			depth++;
			if (depth == 2) {
				in_mem = prefix(name, "memory");
				in_rsvnode = prefix(name, "reserved-memory");
			}
			cur = -1;
			if (depth == 3 && in_rsvnode) {
				if (m->nrsv < FDT_MAX_RSV) {
					cur = m->nrsv++;
					int i = 0;
					while (i < 27 && name[i])
						m->rsv[cur].name[i] = name[i], i++;
				} else
					m->rsv_trunc = 1;
			}
			p = align4(skip_str(p));
			break;
		}
		case FDT_END_NODE:
			if (depth == 2)
				in_mem = in_rsvnode = 0;
			cur = -1;
			depth--;
			if (depth == 0)
				return 1;
			break;
		case FDT_PROP: {
			uint32_t len = be32(p), nameoff = be32(p + 4);
			uintptr_t val = p + 8;
			const char *pn = (const char *)(strings + nameoff);
			if (depth == 1 && streq(pn, "#address-cells")) m->root_ac = be32(val);
			if (depth == 1 && streq(pn, "#size-cells")) m->root_sc = be32(val);
			if (depth == 2 && in_rsvnode && streq(pn, "#address-cells")) m->rsv_ac = be32(val);
			if (depth == 2 && in_rsvnode && streq(pn, "#size-cells")) m->rsv_sc = be32(val);
			if (depth == 2 && in_mem && streq(pn, "reg")) {
				uint32_t st = 4 * (m->root_ac + m->root_sc);
				for (uint32_t o = 0; st && o + st <= len; o += st) {
					if (m->nmem >= FDT_MAX_MEM) { m->mem_trunc = 1; break; }
					m->mem[m->nmem].base = cells(val + o, m->root_ac);
					m->mem[m->nmem++].size = cells(val + o + 4 * m->root_ac, m->root_sc);
				}
			}
			if (cur >= 0 && streq(pn, "reg") && len >= 4 * (m->rsv_ac + m->rsv_sc)) {
				m->rsv[cur].base = cells(val, m->rsv_ac);
				m->rsv[cur].size = cells(val + 4 * m->rsv_ac, m->rsv_sc);
				m->rsv[cur].has_reg = 1;
			}
			if (cur >= 0 && streq(pn, "no-map"))
				m->rsv[cur].nomap = 1;
			p = align4(val + len);
			break;
		}
		case FDT_NOP:
			break;
		default:
			return depth == 0;
		}
	}
	return 0;
}
