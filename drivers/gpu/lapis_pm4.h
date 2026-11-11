/* Adreno PM4 type-7 header, payload dword count excludes the header.
 * Format cross-checked against Qualcomm KGSL adreno_pm4types.h. This does not
 * establish GPU readiness, DMA visibility, or command authorization. */
#ifndef LAPIS_PM4_H
#define LAPIS_PM4_H
static inline unsigned lapis_pm4_odd_parity(unsigned value)
{
    value ^= value >> 16; value ^= value >> 8;
    value ^= value >> 4; value &= 15;
    return (0x9669u >> value) & 1u;
}
static inline int lapis_pm4_type7(unsigned opcode, unsigned count, unsigned *out)
{
    if (!out || opcode > 0x7fu || count > 0x3fffu) return -1;
    *out = 0x70000000u | count | (lapis_pm4_odd_parity(count) << 15) |
           (opcode << 16) | (lapis_pm4_odd_parity(opcode) << 23);
    return 0;
}
#endif
