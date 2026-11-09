# TB520FU (SM8650, Gunyah) — ABL DTB DRAM map, as seen by the s8 shim

Source: s8 shim DTB page, `fastboot boot`, photos s8_hw_1.jpg / s8_hw_2.jpg / s8_hw_4.jpg
(2026-11-09 ~12:50 MSK). Transcribed from photos: digits marked `?` are unreadable/uncertain.
The shim prints `0x` + at least 9 hex digits (so `0x0816E0000` == 0x816E0000).

Header: `dtb=0x0B6FEF000  parse=ok  ac/sc=2/2  rsv ac/sc=2/2`

## /memory banks (green MEM lines), in DTB order
| # | range | note |
|---|-------|------|
| 1 | 0x0816E0000 - 0x081A00000 | confirmed by bottom strip |
| 2 | 0x081CF5000 - 0x081D00000 | confirmed by bottom strip |
| 3 | 0x081F20000 - 0x0824A0000 | confirmed by bottom strip |
| 4 | 0x0?B?00000 - 0x0D8000000 ? | blurry |
| 5 | 0x0E1?00000 - 0x0F7?00000 ? | strip: 0x0?E1?00000-0x0F7?00000 |
| 6 | 0x0FFE00000 - 0x100000000 | |
| 7 | 0x?00000000 - 0x?AF?00000 ? | blurry |
| 8 | 0x?80000000 - 0x?BA?00000 ? | blurry |
| 9 | 0x?00000000 - 0x?00000000 ? | blurry |
|10 | 0x0C0000000 - 0x?00000000 ? | blurry |
|11 | 0x0?020000 - 0x0?4?00000 ? | blurry |
|12 | 0x0A4?00000 - 0x0A6?00000 ? | blurry |
|13 | 0x0A1F00000 - 0x0A2000000 ? | between adspslpi end 0xA1F00000 and ramoops 0xA2000000 |
|14 | 0x0A2400000 - 0x0D4E23000 ? | ends at hwfence-shmem start |
|15 | 0x0D5100000 - 0x0D8000000 ? | starts at splash FB |
Bank holding phys_base: **phys_base 0x8C0000000, bank_end 0xB00000000, safe_end 0xB00000000** (yellow line, readable).
Bottom strip: `RSV>=phys:` **empty** → no reserved-memory with reg at/above 0x8C0000000.
Bottom strip: `XNU RAM: 8C0000000-AEF960000 (profile AEF960000, DTB ok)` → no clamp needed.

## /reserved-memory (R = reg, R! = no-map)
```
R! 0x080000000-0x080E00000 gunyah_hyp_region@80000000
R! 0x080E00000-0x081200000 cpusys_vm_region@80e00000
R! 0x081A00000-0x081C60000 xbl_aop_merged_region@81a00000
R! 0x081C60000-0x081C80000 aop_cmd_db_region@81c60000
R! 0x081C80000-0x081CF4000 aop_tme_uefi_merged_region@...
R! 0x081CF4000-0x081CF5000 chipinfo_region@81cf4000
R! 0x081D00000-0x081F00000 smem_region@81d00000
R! 0x081F00000-0x081F20000 adsp_mhi_region@81f00000
R! 0x0824A0000-0x0825A0000 pvmfw_region@824a0000
R! 0x082600000-0x082700000 global_sync_region@82600000
R! 0x082700000-0x082800000 tz_stat_region@82700000
R  0x082800000-0x084800000 qdss_region@82800000
R! 0x084800000-0x084A00000 qlink_logging_region@84800000
R! 0x086B00000-0x08B400000 mpss_dsm_region@86b00000
R! 0x08B400000-0x08BC00000 mpss_dsm_region_2@8b400000
R! 0x08BC00000-0x09B000000 mpss_region@8bc00000
R! 0x09B000000-0x09B080000 q6_mpss_dtb_region@9b000000
R! 0x09B080000-0x09B090000 ipa_fw_region@9b080000
R! 0x09B090000-0x09B09A000 ipa_gsi_region@9b090000
R  (dynamic, no reg) qseecom_region
R  (dynamic, no reg) qseecom_ta_region
R  (dynamic, no reg) qvc_dma_region
R  (dynamic, no reg) non_secure_display_region
R! 0x09B09A000-0x09B09C000 gpu_microcode_region@9b09a000
R! 0x09B09C000-0x09B0A0000 lost_reg_mem
R! 0x09B0A0000-0x09B280000 spss_region_region@9b0a0000
R! 0x09B280000-0x09B2E0000 spu_secure_shared_memory_re...
R! 0x09B2E0000-0x09B300000 spu_secure_shared_memory_re...
R! 0x09B300000-0x09BB00000 camera_region@9b300000
R! 0x09BB00000-0x09C300000 video_region@9bb00000
R! 0x09C300000-0x09CA00000 cvp_region@9c300000
R! 0x09CA00000-0x09DE00000 cdsp_region@9ca00000
R! 0x09DE00000-0x09DE80000 q6_cdsp_dtb_region@9de00000
R! 0x09DE80000-0x09DF00000 q6_adsp_dtb_region@9de80000
R! 0x09DF00000-0x0A1F00000 adspslpi_region@9df00000
R! 0x0D8000000-0x0D8B00000 tz_merged_region@d8000000   (end digit uncertain)
R! 0x0D4E23000-0x0D5100000 hwfence-shmem
R  (dynamic, no reg) vm_comm_mem_region
R  0x0F3800000-0x0F7C00000 trust_ui_vm_region@f3800000
R  0x0F7C00000-0x0FC800000 oem_vm_region@f7c00000
R! 0x0FF800000-0x0FFE00000 llcc_lpi_region@ff800000
R  (dynamic, no reg) va_md_mem_region, sp_region, cdsp_eva_region, adsp_heap_region,
   linux,cma, secure_cdsp_region
R! (dynamic, no reg) debug_kinfo_region
R! 0x0A2000000-0x0A2400000 ramoops@a2000000
R  (dynamic, no reg) mem_dump_region
R  0x0D5100000-0x0D7C00000 splash_region
R  0x000000000-0x000000000 demura_region_0 / demura_region_1 (empty)
R  (dynamic, no reg) cnss_wlan_region
```
Consequences: XNU's [0x8C0000000, 0xAEFA60000) (incl. /pram 0xAEF960000+1M) is plain DTB RAM with no
reserved overlap. Dynamic (no-reg) regions are placed by Linux at runtime — irrelevant to XNU.
