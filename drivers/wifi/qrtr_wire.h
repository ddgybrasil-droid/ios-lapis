/* Stateless QRTR codec for protocol versions 1 and 2.
 * Version byte 1 is the 32-byte header. Version byte 3 is the 16-byte v2
 * header plus optlen, matching Linux net/qrtr. No channel or route is opened. */
#ifndef LAPIS_QRTR_WIRE_H
#define LAPIS_QRTR_WIRE_H
#include <stddef.h>
#include <stdint.h>
#define LAPIS_QRTR_DATA 1u
#define LAPIS_QRTR_NEW_SERVER 4u
#define LAPIS_QRTR_RESUME_TX 7u
#define LAPIS_QRTR_CTRL_PORT UINT32_C(0xfffffffe)
struct lapis_qrtr_packet {
    uint32_t src_node, src_port, dst_node, dst_port, type;
    int confirm_rx;
    const uint8_t *payload;
    size_t payload_len;
};
static inline uint16_t lapis_qrtr_u16(const uint8_t *p)
{ return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8)); }
static inline uint32_t lapis_qrtr_u32(const uint8_t *p)
{ return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24); }
static inline void lapis_qrtr_put32(uint8_t *p, uint32_t v)
{ p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24); }
static inline int lapis_qrtr_fields_valid(uint32_t type, uint32_t port, size_t n)
{
    if (!n) return 0;
    if ((type==LAPIS_QRTR_NEW_SERVER || type==LAPIS_QRTR_RESUME_TX) && n<20) return 0;
    return port==LAPIS_QRTR_CTRL_PORT || type==LAPIS_QRTR_DATA || type==LAPIS_QRTR_RESUME_TX;
}
static inline int lapis_qrtr_decode(const void *buffer, size_t length, struct lapis_qrtr_packet *out)
{
    if (!buffer || !out || !length || (length & 3u)) return -1;
    const uint8_t *p=buffer; size_t h, size, padded;
    struct lapis_qrtr_packet q;
    if (p[0]==1) {
        if (length<32 || lapis_qrtr_u32(p)!=1) return -1;
        h=32; q.type=lapis_qrtr_u32(p+4); q.src_node=lapis_qrtr_u32(p+8);
        q.src_port=lapis_qrtr_u32(p+12); q.confirm_rx=!!lapis_qrtr_u32(p+16);
        size=lapis_qrtr_u32(p+20); q.dst_node=lapis_qrtr_u32(p+24); q.dst_port=lapis_qrtr_u32(p+28);
    } else if (p[0]==3) {
        if (length<16) return -1;
        h=16u+p[3]; q.type=p[1]; q.confirm_rx=!!(p[2]&1u); size=lapis_qrtr_u32(p+4);
        q.src_node=lapis_qrtr_u16(p+8); q.src_port=lapis_qrtr_u16(p+10);
        q.dst_node=lapis_qrtr_u16(p+12); q.dst_port=lapis_qrtr_u16(p+14);
        if (q.src_port==0xfffeu) q.src_port=LAPIS_QRTR_CTRL_PORT;
        if (q.dst_port==0xfffeu) q.dst_port=LAPIS_QRTR_CTRL_PORT;
    } else return -1;
    if (q.dst_port==0xffffu) q.dst_port=LAPIS_QRTR_CTRL_PORT;
    if (h>length || size>SIZE_MAX-3u) return -1;
    padded=(size+3u)&~(size_t)3u;
    if (padded!=length-h || !lapis_qrtr_fields_valid(q.type,q.dst_port,size)) return -1;
    q.payload=p+h; q.payload_len=size; *out=q; return 0;
}
static inline int lapis_qrtr_encode_v1(void *buffer, size_t capacity,
        const struct lapis_qrtr_packet *q, size_t *written)
{
    size_t padded;
    if (!buffer || !q || !written || !q->payload || q->payload_len>UINT32_MAX ||
        q->payload_len>SIZE_MAX-3u ||
        !lapis_qrtr_fields_valid(q->type,q->dst_port,q->payload_len)) return -1;
    padded=(q->payload_len+3u)&~(size_t)3u;
    if (capacity<32u || padded>capacity-32u) return -1;
    uint8_t *p=buffer;
    lapis_qrtr_put32(p,1); lapis_qrtr_put32(p+4,q->type);
    lapis_qrtr_put32(p+8,q->src_node); lapis_qrtr_put32(p+12,q->src_port);
    lapis_qrtr_put32(p+16,!!q->confirm_rx); lapis_qrtr_put32(p+20,(uint32_t)q->payload_len);
    lapis_qrtr_put32(p+24,q->dst_node); lapis_qrtr_put32(p+28,q->dst_port);
    for(size_t i=0;i<q->payload_len;i++) p[32+i]=q->payload[i];
    for(size_t i=q->payload_len;i<padded;i++) p[32+i]=0;
    *written=32u+padded; return 0;
}
#endif
