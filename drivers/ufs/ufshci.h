/* UFSHCI host controller interface - JEDEC JESD223 (UFSHCI 2.x/3.x/4.0) register map
 * and descriptors; cross-checked with Linux drivers/ufs/core (include/ufs/ufshci.h,
 * ufs.h) and QEMU 10.0 include/block/ufs.h (UfsReg / UtpTransferReqDesc / UtpUpiu*). */
#pragma once
#define UFS_CAP       0x00   /* [4:0] NUTRS-1, [18:16] NUTMRS-1, [24] 64AS, [30] MCQS */
#define UFS_MCQCAP    0x04
#define UFS_VER       0x08
#define UFS_HCPID     0x10
#define UFS_HCMID     0x14
#define UFS_AHIT      0x18
#define UFS_IS        0x20   /* write-1-to-clear */
#define UFS_IE        0x24
#define UFS_HCS       0x30
#define UFS_HCE       0x34
#define UFS_UECPA     0x38
#define UFS_UECDL     0x3c
#define UFS_UECN      0x40
#define UFS_UECT      0x44
#define UFS_UECDME    0x48
#define UFS_UTRIACR   0x4c
#define UFS_UTRLBA    0x50
#define UFS_UTRLBAU   0x54
#define UFS_UTRLDBR   0x58
#define UFS_UTRLCLR   0x5c
#define UFS_UTRLRSR   0x60
#define UFS_UTRLCNR   0x64
#define UFS_UTMRLBA   0x70
#define UFS_UTMRLBAU  0x74
#define UFS_UTMRLDBR  0x78
#define UFS_UTMRLCLR  0x7c
#define UFS_UTMRLRSR  0x80
#define UFS_UICCMD    0x90
#define UFS_UCMDARG1  0x94
#define UFS_UCMDARG2  0x98
#define UFS_UCMDARG3  0x9c

#define CAP_64AS      (1u << 24)
#define CAP_MCQS      (1u << 30)
#define IS_UTRCS      (1u << 0)
#define IS_UE         (1u << 2)
#define IS_ULSS       (1u << 8)
#define IS_UCCS       (1u << 10)
/* DFES 11, UTPES 12, HCFES 16, SBFES 17 (Linux ufshci.h DEVICE_FATAL_ERROR/UTP_ERROR/
 * CONTROLLER_FATAL_ERROR/SYSTEM_BUS_FATAL_ERROR) */
#define IS_FATAL      ((1u << 11) | (1u << 12) | (1u << 16) | (1u << 17))
#define HCS_DP        (1u << 0)
#define HCS_UTRLRDY   (1u << 1)
#define HCS_UTMRLRDY  (1u << 2)
#define HCS_UCRDY     (1u << 3)
#define HCE_ENABLE    (1u << 0)

#define UIC_DME_GET          0x01
#define UIC_DME_LINK_STARTUP 0x16

/* UTRD (32 bytes, little endian) */
#define UTRD_CT_UFS_STORAGE  (1u << 28)   /* DW0[31:28] command type */
#define UTRD_DD_NONE         (0u << 25)
#define UTRD_DD_WRITE        (1u << 25)   /* host -> device */
#define UTRD_DD_READ         (2u << 25)   /* device -> host */
#define UTRD_INTR            (1u << 24)
#define OCS_SUCCESS          0x0
#define OCS_INVALID          0xf
struct utrd {
	uint32_t dw0, dunl, dw2_ocs, dunu;
	uint32_t ucdba, ucdbau;
	uint16_t resp_len_dw, resp_off_dw;
	uint16_t prdt_len, prdt_off_dw;
};
struct prdt { uint32_t addr_lo, addr_hi, rsvd, dbc; };   /* dbc = byte count - 1 */

/* UPIU (big endian fields) */
#define UPIU_NOP_OUT    0x00
#define UPIU_COMMAND    0x01
#define UPIU_QUERY_REQ  0x16
#define UPIU_NOP_IN     0x20
#define UPIU_RESPONSE   0x21
#define UPIU_QUERY_RSP  0x36
#define UPIU_REJECT     0x3f
#define UPIU_FLAG_READ  0x40
#define UPIU_FLAG_WRITE 0x20
#define QUERY_FUNC_READ  0x01
#define QUERY_FUNC_WRITE 0x81
#define QUERY_OP_READ_DESC 0x01
#define QUERY_OP_READ_FLAG 0x05
#define QUERY_OP_SET_FLAG  0x06
#define QUERY_FLAG_FDEVICEINIT 0x01
#define DESC_IDN_DEVICE    0x00

/* UCD layout we use (Linux ufshcd uses the same 512-byte aligned split) */
#define UCD_CMD_OFF   0x000
#define UCD_RSP_OFF   0x200
#define UCD_PRDT_OFF  0x400
#define UCD_SIZE      0x800
