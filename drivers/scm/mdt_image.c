/* SPDX-License-Identifier: BSD-2-Clause */
/* Qualcomm MDT placement plan. A zero return is an ELF32 layout that fits the
 * carveout. It is not PAS authentication and it does not call SMC. */
#include "mdt_image.h"
static uint32_t u32(const uint8_t *p)
{ return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static uint16_t u16(const uint8_t *p) { return p[0]|(uint16_t)p[1]<<8; }
static int loadable(const uint8_t *p)
{ return u32(p)==1 && (u32(p+24)&0x07000000u)!=0x02000000u && u32(p+20); }
int lapis_mdt_parse(const void *data,size_t size,uint64_t carve,uint64_t capacity,
                    struct lapis_mdt_plan *plan)
{
	const uint8_t *b=data;
	if(!b || !plan || size<52 || size>0x400000 || !carve || !capacity ||
	   carve+capacity<carve || u32(b)!=0x464c457fu || b[4]!=1 || b[5]!=1 ||
	   b[6]!=1 || u32(b+20)!=1 || u16(b+40)!=52 || u16(b+42)!=32)return -1;
	uint32_t off=u32(b+28),n=u16(b+44);
	if(n<2 || n>LAPIS_MDT_MAX_SEGMENTS || off<52 || off>size || n>(size-off)/32)return -1;
	struct lapis_mdt_plan p={0};
	const uint8_t *ph=b+off;
	p.header_size=u32(ph+16);
	if(u32(ph)==1 || p.header_size<off+32*n || p.header_size>size)return -1;
	uint64_t low=~0ULL,high=0;
	for(unsigned i=0;i<n;i++) {
		const uint8_t *h=ph+32*i;
		uint32_t flags=u32(h+24),file=u32(h+16),memory=u32(h+20);
		uint64_t foff=u32(h+4),addr=u32(h+12),end=addr+memory;
		if(file && (foff>size || file>size-foff))p.split=1;
		if(i && (flags&0x07000000u)==0x02000000u) {
			if(p.hash_size || !file)return -1;
			p.hash_size=file;p.hash_offset=(uint32_t)foff;p.hash_index=i;
		}
		if(!loadable(h))continue;
		if(file>memory || end>0x100000000ULL)return -1;
		if(addr<low)low=addr;
		if(end>high)high=end;
		if(flags&0x08000000u)p.relocate=1;
	}
	if(!p.hash_size || p.hash_size>0x400000-p.header_size || low>=high)return -1;
	if(p.header_size+p.hash_size==size)p.hash_offset=p.header_size;
	else if(p.hash_offset>size || p.hash_size>size-p.hash_offset)p.hash_external=1;
	uint64_t rounded=(high+4095)&~4095ULL;
	p.image_size=rounded-low;
	if(p.relocate && p.image_size>capacity)return -1;
	uint64_t base=p.relocate?low:carve;
	for(unsigned i=0;i<n;i++) {
		const uint8_t *h=ph+32*i;
		if(!loadable(h))continue;
		uint64_t addr=u32(h+12),len=u32(h+20);
		if(addr<base || addr-base>capacity || len>capacity-(addr-base))return -1;
		struct lapis_mdt_segment *s=&p.segments[p.count++];
		s->index=i;s->destination=addr-base;s->file_offset=u32(h+4);
		s->file_size=u32(h+16);s->memory_size=(uint32_t)len;
		for(unsigned j=0;j+1<p.count;j++) {
			const struct lapis_mdt_segment *other=&p.segments[j];
			if(s->destination<other->destination+other->memory_size &&
			   other->destination<s->destination+s->memory_size)return -1;
		}
	}
	*plan=p;return 0;
}
