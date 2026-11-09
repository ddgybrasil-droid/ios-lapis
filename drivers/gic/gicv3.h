/* GICv3 register map - Arm IHI 0069 (GICv3/v4 architecture spec), chapters 12.8
 * (GICD), 12.10/12.11 (GICR RD_base / SGI_base), 12.2 (ICC system registers).
 * Cross-checked with xnu-12377 pexpert/pexpert/arm64/VMAPPLE.h (GICR_TYPER 0x08,
 * GICR_WAKER 0x14, GICR_IGROUPR0 0x10080, GICR_ISENABLER0 0x10100). */
#pragma once
#define GICD_CTLR        0x0000
#define  GICD_CTLR_RWP   (1u << 31)
#define  GICD_CTLR_DS    (1u << 6)
#define  GICD_CTLR_ARE   (1u << 4)    /* ARE_S / ARE (DS=1) / ARE_NS (NS view) */
#define GICD_TYPER       0x0004       /* [4:0] ITLinesNumber: SPIs = 32*(N+1)-32 */
#define GICD_IIDR        0x0008
#define GICD_IGROUPR     0x0080
#define GICD_ISENABLER   0x0100
#define GICD_ICENABLER   0x0180
#define GICD_ISPENDR     0x0200
#define GICD_ICPENDR     0x0280
#define GICD_ISACTIVER   0x0300
#define GICD_IPRIORITYR  0x0400
#define GICD_ICFGR       0x0C00
#define GICD_IGRPMODR    0x0D00
#define GICD_IROUTER     0x6000       /* 64-bit, n*8, n >= 32 */
#define GICD_PIDR2       0xFFE8       /* [7:4] ArchRev: 3 = GICv3, 4 = GICv4 */

#define GICR_CTLR        0x0000
#define  GICR_CTLR_RWP   (1u << 3)
#define GICR_IIDR        0x0004
#define GICR_TYPER       0x0008       /* 64-bit: [63:32] affinity, [4] Last, [1] VLPIS */
#define  GICR_TYPER_LAST  (1ull << 4)
#define  GICR_TYPER_VLPIS (1ull << 1)
#define GICR_WAKER       0x0014
#define GICR_SGI         0x10000      /* SGI_base frame */
#define GICR_IGROUPR0    (GICR_SGI + 0x0080)
#define GICR_ISENABLER0  (GICR_SGI + 0x0100)
#define GICR_ICENABLER0  (GICR_SGI + 0x0180)
#define GICR_ISPENDR0    (GICR_SGI + 0x0200)
#define GICR_ICPENDR0    (GICR_SGI + 0x0280)
#define GICR_IPRIORITYR  (GICR_SGI + 0x0400)
#define GICR_ICFGR0      (GICR_SGI + 0x0C00)
#define GICR_ICFGR1      (GICR_SGI + 0x0C04)
#define GICR_IGRPMODR0   (GICR_SGI + 0x0D00)

#define GIC_INTID_SPURIOUS 1023
#define GIC_MAX_INTID      1020

/* ICC registers (S3_0_C12_...): read with mrs/msr encodings, spec 12.2 */
#define ICC_SGI1R_EL1    "S3_0_C12_C11_5"
#define ICC_IGRPEN1_EL1  "S3_0_C12_C12_7"
#define ICC_PMR_EL1      "S3_0_C4_C6_0"
#define ICC_CTLR_EL1     "S3_0_C12_C12_4"
#define ICC_SRE_EL1      "S3_0_C12_C12_5"
#define ICC_RPR_EL1      "S3_0_C12_C11_3"
