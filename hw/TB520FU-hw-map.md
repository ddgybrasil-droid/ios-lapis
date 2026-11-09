# TB520FU (Lapis / SM8650 / pineapple) — hardware map for XNU drivers

**Source dump:** `/workspace/lapis_dump/lapis_dump/` (PixelOS, root, 2026-11-09 ~13:18 MSK)  
**Live FDT:** `fdt.dtb` (1 122 401 bytes, magic `d00dfeed`)  
**Also used:** `iomem.txt`, `interrupts.txt`, `lsmod.txt`, `cmdline.txt`, `props.txt`, `dt_tree.txt`, `vendor_firmware/`, `firmware_mnt/`, `vendor_dlkm_modules/`

**Legend:** **VERIFIED** = taken from this dump (path/node cited). **HYPOTHESIS** = inference, not proven on XNU/Gunyah. No addresses invented.

`/soc` `#address-cells/#size-cells` = **1/1**. Root / reserved-memory = **2/2**.

Device identity (props.txt): `ro.boot.name=TB520FU`, `ro.boot.hwboardid=SM8650P_12+256_12`, `ro.boot.ddrsize=12GB`, `ro.boot.product.vendor.sku=pineapple`, `ro.boot.bootdevice=1d84000.ufshc`, `ro.boot.hypervisor.protected_vm.supported=true`.

---

## 1. GIC

### VERIFIED

| Item | Value | Source |
|------|-------|--------|
| Compatible | `arm,gic-v3` | FDT `/soc/interrupt-controller@17100000` |
| `#interrupt-cells` | 3 | same |
| GICD | **0x17100000** size **0x10000** | FDT `reg`; iomem `17100000-1710ffff : GICD` |
| GICR | **0x17180000** size **0x200000** (→ 0x1737FFFF) | FDT `reg`; iomem `17180000-1737ffff : GICR` |
| `#redistributor-regions` | 1 | FDT |
| `redistributor-stride` | **0x40000** (256 KiB) | FDT (`[0, 262144]`) |
| # redistributors | 0x200000 / 0x40000 = **8** (= 8 CPUs) | derived from VERIFIED sizes |
| ITS | **0x17140000** size **0x20000**, `arm,gic-v3-its`, `#msi-cells=1` | FDT `.../msi-controller@17140000` |
| Maintenance IRQ | PPI 9, level (`interrupts = <1 9 4>`) | FDT |
| PDC (wakeup) | `qcom,pineapple-pdc` / `qcom,pdc` @ **0x0B220000** (+ second window @ 0x174200F0 size 0x64) | FDT `/soc/interrupt-controller@b220000` |
| GIC class routing | class0 CPUs `{0,1}`, class1 `{2..7}` | FDT `/soc/gic-interrupt-router` (`qcom,gic-intr-routing`) |
| Active controller in Linux | **GICv3** (not GICv2) | `interrupts.txt` labels |

### Arch timer

| Item | Value | Source |
|------|-------|--------|
| Compatible | `arm,armv8-timer` | FDT `/soc/timer` |
| `clock-frequency` | **19 200 000** | FDT |
| `always-on` | yes | FDT |
| PPI list (type,id,flags) | secure-phys **13**, nonsecure-phys **14**, **virt 11**, hyp **12** (flags cell `0xFF08`) | FDT `interrupts` |
| Which timer Linux uses | **Virtual timer**, Linux hwirq **27** (= PPI 11), Level, name `arch_timer` | `interrupts.txt` |
| Mem-mapped timer | `arm,armv7-timer-mem` @ **0x17420000**, IRQ **38** (`arch_mem_timer`) | FDT `/soc/timer@17420000`; interrupts.txt |
| Gunyah qtimer | `qcom,gh-qtmr` @ **0x17425000**, SPI 10 | FDT `/soc/qcom,gh-qtimer@17425000` |

### Gunyah vs vGIC

| Item | Value | Source |
|------|-------|--------|
| Hypervisor node | `/hypervisor` compatible `qcom,gunyah-hypervisor-1.0`, `qcom,gunyah-hypervisor`, `simple-bus` | FDT |
| VM id | `qcom,gunyah-vm`, `qcom,vmid = <3>`, vendor Qualcomm | FDT |
| Watchdog | `qcom,gh-watchdog` | FDT |
| Hyp reserved RAM | `gunyah_hyp_region@80000000` **0x80000000–0x80E00000** no-map | FDT reserved-memory; matches shim DTB memmap |
| Gunyah IRQs seen live | SPI **960** `gh_msgq_tx`, **961** `gh_msgq_rx` | interrupts.txt |
| Explicit vGIC / virt-GIC node | **None** in FDT | FDT walk |
| Guest GIC MMIO | Same physical GICD/GICR/ITS as above (Stage-2 1:1 for Primary VM — **HYPOTHESIS**, typical Gunyah; addresses themselves are VERIFIED in guest iomem) | iomem shows GICD/GICR |

**HYPOTHESIS:** Under Gunyah EL1 the guest programs the real GICv3 MMIO; there is no separate vGIC base. XNU should use 0x17100000 / 0x17180000 / stride 0x40000.

---

## 2. CPUs / PSCI

### VERIFIED

| CPU node | MPIDR `reg` | capacity-dmips-mhz | cluster (cpu-map) |
|----------|-------------|--------------------|-------------------|
| `cpu@0` | 0x000 | 1024 | cluster0/core0 |
| `cpu@100` | 0x100 | 1024 | cluster0/core1 |
| `cpu@200` | 0x200 | 1792 | cluster1/core0 |
| `cpu@300` | 0x300 | 1792 | cluster1/core1 |
| `cpu@400` | 0x400 | 1792 | cluster1/core2 |
| `cpu@500` | 0x500 | 1792 | cluster2/core0 |
| `cpu@600` | 0x600 | 1792 | cluster2/core1 |
| `cpu@700` | 0x700 | 1894 | cluster3/core0 |

- Count: **8** CPUs (also 8 columns in interrupts.txt).  
- `compatible`: `qcom,kryo` for all.  
- `enable-method`: **`psci`** for all.  
- Clusters: **4** (2+3+2+1) via `/cpus/cpu-map`.  
- PSCI: `/soc/psci` — `compatible = "arm,psci-1.0"`, **`method = "smc"`**.  
- Idle: `/idle-states` present; per-CPU `cpu-idle-states` + `local-timer-stop` on some states (dt_tree).

**HYPOTHESIS:** SMC PSCI is handled by Gunyah (not raw EL3 from EL1). Secondary bring-up for XNU must use PSCI CPU_ON with these MPIDRs.

---

## 3. UFS

### VERIFIED — controller

| Item | Value | Source |
|------|-------|--------|
| Node | `/soc/ufshc@1d84000` | FDT |
| Compatible | `qcom,ufshc` | FDT |
| status | ok | FDT |
| Boot device | `1d84000.ufshc` | props `ro.boot.bootdevice` / `ro.boot.boot_devices` |
| IRQ | SPI **265** → Linux hwirq **297** `ufshcd` | FDT; interrupts.txt |
| `lanes-per-direction` | 2 | FDT |
| PHY phandle name | `ufsphy` → `/soc/ufsphy_mem@1d80000` | FDT |
| Quirks | `qcom,broken-ahit-wa`, `qcom,bypass-pbl-rst-wa` | FDT |
| Resets | `rst` via GCC; `reset-gpios` present | FDT |
| ICE | yes (`ufs_ice`, `ufs_ice_hwkm` reg-names); crypto via `ufshcd_crypto_qti` + `crypto_qti` modules | FDT; lsmod |
| MCQ | yes (`mcq`, `mcq_vs`, `mcq_sqd` in iomem) | iomem / FDT |
| MSI | `msi-parent` → ITS | FDT |

**Registers (FDT `reg` / `reg-names` + iomem):**

| Name | Base | Size | iomem |
|------|------|------|-------|
| ufs_mem | **0x01D84000** | 0x3000 | `01d84000-01d86fff` |
| ufs_ice | **0x01D88000** | 0x8000 | `01d88000-01d8ffff` |
| ufs_ice_hwkm | **0x01D90000** | 0x9000 | `01d90000-01d98fff` |
| mcq | **0x01DA0000** | 0x240 | iomem only (not in named FDT reg list) |
| mcq_vs | **0x01DA4000** | 0x10 | iomem |
| mcq_sqd | **0x01DA5000** | 0x2000 | iomem |

**Clocks (names):** `core_clk`, `bus_aggr_clk`, `iface_clk`, `core_clk_unipro`, `core_clk_ice`, `ref_clk`, `tx_lane0_sync_clk`, `rx_lane0_sync_clk`, `rx_lane1_sync_clk`.

**Supplies:** `vcc`, `vccq`, `vdd-hba`, plus `qcom,vddp-ref-clk-supply`, `qcom,vccq-parent-supply`, `qcom,vccq-shutdown-supply`.

### VERIFIED — PHY

| Item | Value | Source |
|------|-------|--------|
| Node | `/soc/ufsphy_mem@1d80000` | FDT |
| Compatible | **`qcom,ufs-phy-qmp-v4-pineapple`** | FDT |
| reg | **0x01D80000** size 0x2000 (`phy_mem`) | FDT; iomem |
| GDSC supply name | `vdd-phy-gdsc-supply` | FDT |
| Module live | `phy_qcom_ufs_qmp_v4_pineapple` (+ `phy_qcom_ufs`) | lsmod |

### GDSCs (UFS)

| Name | Reg | Source |
|------|-----|--------|
| `gcc_ufs_phy_gdsc` | **0x00177004** | FDT `/soc/qcom,gdsc@177004` |
| `gcc_ufs_mem_phy_gdsc` | **0x0019E000** | FDT `/soc/qcom,gdsc@19e000` |

### Partitions

**VERIFIED (names only):** dynamic partitions; AVB-verified `system`, `system_ext`, `system_dlkm`, `product`, `odm`, `vendor`, `vendor_dlkm`. A/B slot `_a`. Firmware mounts: `vendor.firmware_mnt` → `sde6`, `vendor.bt_firmware` → `sde7` (props).  
**NOT in dump:** exact by-name sizes for `userdata` / `super` (no `ls -l /dev/block/by-name` capture).  
**HYPOTHESIS:** GPT on UFS LU0; need a blockdev listing for sizes.

**UFS firmware blob:** none in dump — PHY is register-init only. See `hw/firmware/ufs/README.txt`.

**SCM/TZ:** UFS crypto / ICE paths use `qcom_scm` (lsmod: `ufs_qcom`, `ufshcd_crypto_qti`, `crypto_qti` → `qcom_scm`). Expect SMC for key slots.

---

## 4. PCIe + Wi-Fi

### VERIFIED — PCIe RC0 (Wi-Fi)

| Item | Value | Source |
|------|-------|--------|
| Node | `/soc/qcom,pcie@1c00000` | FDT |
| Compatible | `qcom,pci-msm` | FDT |
| `cell-index` / domain | 0 | FDT |
| Endpoint child | `pcie0_rp/cnss_pci0` (memory-region → `cnss_wlan_region`) | FDT |
| PARF | **0x01C00000** size 0x3000 | FDT reg-names `parf` |
| PHY | **0x01C06000** size 0x2000 | FDT `phy` |
| DBI / dm_core | **0x60000000** | FDT `dm_core` |
| ELBI | **0x60000F20** | FDT |
| iATU | **0x60001000** | FDT |
| Config space window | **0x60100000** size 0x100000 | FDT `conf` |
| PCIe SM | **0x01D07000** size 0x7000 | FDT `pcie_sm` (cmdline also `pci-msm-drv.pcie_sm_regs=0x1D07000,...`) |
| CPU MMIO window (ranges) | config→0x60100000; mem→**0x60200000** size 0x3CF0000 | FDT `ranges` |
| Live BAR (cnss) | **0x60400000–0x605FFFFF** on `0000:01:00.0` | iomem |
| Parent window in iomem | `60300000-63ffffff : qcom,pcie@1c00000` | iomem |
| IRQs | global SPI **140**; INTA..D SPI 149–152 | FDT |
| PERST / WAKE GPIOs | present | FDT |
| GDSCs | `gcc_pcie_0_gdsc` **0x0016B004**, `gcc_pcie_0_phy_gdsc` **0x0016C000** | FDT gdsc nodes |
| Module | `pci_msm_drv`, `pcie_pdc` | lsmod |

### VERIFIED — PCIe RC1

| Item | Value | Source |
|------|-------|--------|
| Node | `/soc/qcom,pcie@1c08000` | FDT |
| status | **`disabled`** | FDT |
| `qcom,drv-name` | `lpass` | FDT |
| PARF | 0x01C08000 | FDT |

Not used for Wi-Fi on this board.

### VERIFIED — CNSS / WLAN chip

| Item | Value | Source |
|------|-------|--------|
| Node | `/soc/qcom,cnss-kiwi@b0000000` | FDT |
| Compatible | **`qcom,cnss-kiwi`** | FDT |
| `qcom,wlan` | true | FDT |
| `qcom,wlan-rc-num` | **0** (PCIe0) | FDT |
| `supported-ids` | **0x1107** | FDT |
| Board data files | `bdwlan_lapis.elf`, `bdwlan_row_lapis.elf`, `bdwlan_prc_lapis.elf`; also `bdwlan_wcn785x_2p0_ncm865*.elf` | firmware_mnt |
| Chip family | **WCN785x / Kiwi** (not ath12k SoftMAC) | filenames + modules |
| WLAN enable GPIO | TLMM gpio 16 (and sw-ctrl 18/19, bt-en 17) | FDT |
| Firmware search path | `.../image/kiwi`, `.../wlan/qca_cld/kiwi_v2` | cmdline `firmware_class.path` |
| Driver version prop | `vendor.wlan.driver.version=5.2.1.93C` | props |
| cnss DMA pool | `cnss_wlan_region` shared-dma-pool, size 32 MiB | FDT reserved-memory |

### VERIFIED — which Wi-Fi stack Lenovo uses

**Not ath12k.** Live modules (`lsmod.txt`):

- **`qca_cld3_kiwi_v2`** (12 447 744 bytes) — Qualcomm CLD3 STA/AP stack  
- **`cnss2`** — PCIe/MHI bring-up, firmware loader  
- helpers: `cnss_utils`, `cnss_prealloc`, `wlan_firmware_service`, `mhi`, `cfg80211`, `mac80211`  
- ko present but **not** the active path: `icnss2.ko` exists under vendor_dlkm; **no `ath12k` module** in lsmod or vendor_dlkm listing  

IRQs live: many `ITS-MSI` `pci0_wlan_ce_*` / `pci0_wlan_grp_dp_*` / `wlan_wake_irq`.

### Firmware files (copied → `hw/firmware/wifi-kiwi/`)

See § Firmware manifest. Essentials: `amss.bin`, `amss20.bin`, `phy_ucode.elf`, `phy_ucode20.elf`, `regdb.bin`, `Data.msc`, `Data20.msc`, `bdwlan_lapis.elf` (+ row/prc / wcn785x variants).  
**Note:** no separate `m3.bin` in this kiwi image set (unlike older cnss); M3 is rolled into amss/phy_ucode.

**HYPOTHESIS for XNU:** porting Wi-Fi means either (a) a large CNSS2+CLD3 port, or (b) a much thinner MHI/PCIe stub + userspace — ath12k Linux sources are the wrong binary target but useful for WCN7850 register/MHI docs.

---

## 5. GPU (Adreno / KGSL)

### VERIFIED

| Item | Value | Source |
|------|-------|--------|
| Node | `/soc/qcom,kgsl-3d0@3d00000` | FDT |
| Compatible | **`qcom,adreno-gpu-gen7-9-1`**, `qcom,kgsl-3d0` | FDT |
| Model string | **`Adreno750v2`** | FDT `qcom,gpu-model` |
| Chipid | **0x43051401** | FDT `qcom,chipid` |
| IRQ | SPI **300** → Linux **332** `kgsl_3d0_irq` | FDT; interrupts.txt |
| status | ok | FDT |
| Module | `msm_kgsl` | lsmod |
| GPUCC | `/soc/clock-controller@3d90000` `qcom,pineapple-gpucc` @ **0x03D90000** size 0x9800 | FDT; iomem |
| SMMU | `kgsl-smmu@3da0000` **0x03DA0000–0x03DDFFFF** | iomem |

**reg-names / bases (FDT):**

| Name | Base | Size |
|------|------|------|
| `kgsl_3d0_reg_memory` | **0x03D00000** | 0x40000 |
| `cx_dbgc` | **0x03D6A000** | 0x3000 |
| `rscc` | **0x03D59000** | 0x10000 |
| `cx_misc` | **0x03DA7000** | 0x2000 |
| `qdss_gfx` | 0x10900000 | 0x80000 |
| `qdss_etr` | 0x10048000 | 0x8000 |
| `qdss_tmc` | 0x10B04000 | 0x1000 |

**HYPOTHESIS:** GMU MMIO lives inside/`near` the A7xx block; Linux kgsl maps GMU relative to `kgsl_3d0_reg_memory` / cx domain. Confirm offsets from msm kgsl gen7 sources before poking — wrong access without GDSC → SError.

### GDSCs (GPU)

| Name | Reg | Source |
|------|-----|--------|
| `gx_clkctl_gx_gdsc` | **0x03D68024** | FDT |
| `gpu_cc_gx_gdsc` | **0x03D9905C** | FDT |
| `gpu_cc_cx_gdsc` | **0x03D99108** | FDT |

### Firmware (copied → `hw/firmware/gpu/`)

Preferred for this chip (**gen71100** present alongside older gen70900 on vendor partition):

| File | Size | Role |
|------|------|------|
| `gen71100_gmu.bin` | 80680 | GMU |
| `gen71100_sqe.fw` | 67348 | SQE |
| `gen71100_zap.mbn` | 12088 | ZAP (TZ-signed; needs SCM to load) |
| `gen70900_*` / `gmu_gen70900.bin` | — | also on device; keep as fallback |

Zap-shader node: `/soc/qcom,kgsl-3d0@3d00000/zap-shader` → `memory-region` (gpu microcode carveout in reserved-memory).

**SCM/TZ:** ZAP load and some KGSL paths require `qcom_scm` (lsmod dependency).

---

## 6. Display

### VERIFIED

| Item | Value | Source |
|------|-------|--------|
| MDSS/SDE | `/soc/qcom,mdss_mdp@ae00000` `qcom,sde-kms` | FDT |
| MDP phys | **0x0AE00000** size 0x84000 | FDT |
| VBIF | 0x0AEB0000 | FDT |
| IRQ | SPI **83** | FDT |
| DSI ctrl0 | `qcom,dsi-ctrl-hw-v2.8` @ **0x0AE94000** | FDT |
| DSI ctrl1 | @ **0x0AE96000** | FDT |
| DSI PHY0/1 | `qcom,dsi-phy-v5.2` @ 0x0AE95500 / 0x0AE97500 | FDT |
| Active panel (cmdline) | **`qcom,mdss_dsi_panel_lapis_tianma_nt36532_dsc_3k_video`** | cmdline.txt |
| Alternate panel in DT | `..._lapis_boe_nt36532_dsc_3k_video` | FDT |
| Dual-DSI | `qcom,dsi-ctrl-num = <0 1>`, topology `<2 2 2>` | FDT panel |
| Timing size per ctrl | 1472×1840 @ 120 Hz (DSC); **full FB 2944×1840** | FDT timing; matches S4probe |
| Physical mm | 274×171 | FDT |
| DFPS list | 120, 144, 90, 60, 30 | FDT |
| Module | `msm_drm` | lsmod |

### Splash / cont_splash FB

| Item | Value | Source |
|------|-------|--------|
| reserved-memory | `/reserved-memory/splash_region` label `cont_splash_region` | FDT |
| Range | **0xD5100000 size 0x2B00000** (→ 0xD7C00000) | FDT |
| iomem | `d5100000-d7bfffff : reserved` under System RAM `d5100000-d7ffffff` | iomem |
| Matches XNU early FB | 0xD5100000, 2944×1840, stride 11776 | prior S4probe / shim (cross-check) |

### Display GDSCs

| Name | Reg |
|------|-----|
| `disp_cc_mdss_core_gdsc` | **0x0AF09000** |
| `disp_cc_mdss_core_int2_gdsc` | **0x0AF0B000** |

MDSS already clocked by ABL for splash — safe to use FB without re-init (**VERIFIED** by XNU console on device). Full modeset still needs DISPCC/GDSC sequencing.

---

## 7. Power / clocks / SCM

### VERIFIED GDSCs of interest

| Domain | Name | GDSCR |
|--------|------|-------|
| UFS | `gcc_ufs_phy_gdsc` | 0x00177004 |
| UFS mem PHY | `gcc_ufs_mem_phy_gdsc` | 0x0019E000 |
| PCIe0 | `gcc_pcie_0_gdsc` | 0x0016B004 |
| PCIe0 PHY | `gcc_pcie_0_phy_gdsc` | 0x0016C000 |
| PCIe1 | `gcc_pcie_1_gdsc` | 0x0018D004 |
| PCIe1 PHY | `gcc_pcie_1_phy_gdsc` | 0x0018E000 |
| GPU GX | `gpu_cc_gx_gdsc` / `gx_clkctl_gx_gdsc` | 0x03D9905C / 0x03D68024 |
| GPU CX | `gpu_cc_cx_gdsc` | 0x03D99108 |
| Display | `disp_cc_mdss_core_gdsc` (+ int2) | 0x0AF09000 / 0x0AF0B000 |

GCC block: `clock-controller@100000` (iomem `00100000-002f41ff`).  
RPMh: `qcom_rpmh`, `clk_rpmh`, `rpmh_regulator`, `cmd_db` loaded (lsmod); AOP cmd DB region in reserved-memory @ 0x81C60000 (prior shim map).

### SCM / TZ (**VERIFIED** presence)

- FDT `/firmware/qcom_scm` — `qcom,scm-v1.1`, `qcom,scm`  
- Live IRQ: SPI **962** `qcom-scm`  
- Module `qcom_scm` heavily depended on (GPU zap, UFS crypto, SMMU, PIL, Gunyah helpers, etc.)  
- SCMI: `/soc/qcom,scmi` + shmem @ 0x17D09400  

**HYPOTHESIS:** Any ICE key install, ZAP authenticate, or subsystem PIL from XNU needs a working SMC→Gunyah→TZ path; without it those features must be stubbed.

---

## 8. Touch / battery / audio (brief)

### Touch — VERIFIED

| Item | Value | Source |
|------|-------|--------|
| Controller | **Novatek NVT-ts** on SPI | FDT |
| Node | `/soc/qcom,qupv3_1_geni_se@ac0000/spi@a90000/novatek@0` | FDT |
| Compatible | `novatek,NVT-ts-spi` | FDT |
| SPI max | 9.6 MHz | FDT |
| IRQ GPIO | TLMM **162** (active-low flags 8193) | FDT; interrupts.txt `NVT-ts` |
| Reset GPIO | TLMM **161** | FDT |
| Pen support | yes | FDT |
| Module | `nvt_36xxx` (also `nt36xxx_i2c` present) | lsmod |
| Fingerprint (not touch) | `goodix,fingerprint` `/soc/goodix_fp` | FDT; `goodix_ts`/`goodix_fp` modules |

### Battery / charger — VERIFIED

- `/soc/qcom,pmic_glink/qcom,battery_charger` compatible `qcom,battery-charger`  
- Modules: `qti_battery_charger`, `charger_ulog_glink`, `pmic_glink`  
- Transport: PMIC Glink (not a simple I2C gauge).

### Audio — VERIFIED

- LPASS CDC under `/soc/spf_core_platform/lpass-cdc`  
- Codec: **`qcom,wcd939x-codec`** (+ rx/tx slaves on SoundWire)  
- Amps: **`qcom,wsa884x`** / `wsa884x_2` (nodes exist; several `status=disabled` in DT — check which are enabled on lapis SKU)  
- Macros: rx@0x6AC0000, tx@0x6AE0000, va@0x6D44000, wsa@0x6B00000, wsa2@0x6AA0000  
- Modules: `lpass_cdc_*_macro_dlkm`, `wcd939x_dlkm`, `machine_dlkm`, `aw882xx_dlkm` (AW amp FW `aw882xx_acf.bin` in vendor_firmware)  
- **HYPOTHESIS:** Argon’s 0x06D00000 LPAIF poke is unsafe until GCC LPASS GDSC/clocks confirmed from this DT (use lpass-cdc / vote_lpass_* nodes, not guessed bases).

---

## Firmware manifest

Blobs copied to `src/hw/firmware/` (gitignored). Trackable copy of hashes: `src/hw/firmware-MANIFEST.sha256` and `src/hw/firmware/MANIFEST.sha256`.

| Path | Bytes | sha256 |
|------|------:|--------|
| `gpu/gen70900_aqe.fw` | 33176 | `25237c161d727b8ea92b4beb837a31651b44ea78bfc34d95672af8f346a45825` |
| `gpu/gen70900_sqe.fw` | 85364 | `f756bafe5f69a87161c7c53385386374a791861a574c244e845baba677993842` |
| `gpu/gen70900_zap.mbn` | 12088 | `12124bbaa8aaaa2ee595bb2b6005ac4cf1ebb9e4715ed006159bf78e65ab5eca` |
| `gpu/gen71100_gmu.bin` | 80680 | `435c7bac9a328b8a100df930b84b65b4d579e42be44785d0e60e3729291bb864` |
| `gpu/gen71100_sqe.fw` | 67348 | `eee95622aa1e487e96de55469a65c5642ce9b8c724f9da768c79fe54df760eff` |
| `gpu/gen71100_zap.mbn` | 12088 | `39ffa0c98e64cd3800b3198015bc1eb86189007b1ad5539cda5d2796067d36a0` |
| `gpu/gmu_gen70900.bin` | 78588 | `9685f62c42543befcaee68d25355e16bf55cdb0d573bddcc9f6633f59eec3f72` |
| `wifi-kiwi/Data.msc` | 1158272 | `cc9279c79dc92f8820183e77564697f3addb3787c24f529371edf2e78b329fcc` |
| `wifi-kiwi/Data20.msc` | 1274052 | `27a99ffef54d3af5958441c54ff27a2db29d38c6403b9f4505c4e8fa2eb54967` |
| `wifi-kiwi/amss.bin` | 7458880 | `c42e06f534c07ffdaec3b265ff1e38b312741e35eb5fb21b5ca77995fab8d688` |
| `wifi-kiwi/amss20.bin` | 6422592 | `b96b0b22a03d5f4157184b9ec3b7c2ebdf6e8027f95ebce550bc7758f0cb31e0` |
| `wifi-kiwi/bdwlan.elf` | 88860 | `9a48987ba0d990d919165a28eabf1d141d0dedf5a04897e54c82acb97c0060b7` |
| `wifi-kiwi/bdwlan_lapis.elf` | 88860 | `4e023ab0e89cf9b91c74d5f73e9d20aef6b4efd7bf511a535474edc87ec19b26` |
| `wifi-kiwi/bdwlan_prc_lapis.elf` | 88860 | `4e023ab0e89cf9b91c74d5f73e9d20aef6b4efd7bf511a535474edc87ec19b26` |
| `wifi-kiwi/bdwlan_row_lapis.elf` | 88860 | `df66377f45bba28c447baa5591df572e02da219dd92f1244a3623b4557e7cfbf` |
| `wifi-kiwi/bdwlan_wcn785x_2p0_ncm825.elf` | 88860 | `24f1b4fefb4c05f813dee38b05ea72a9103f4a3f035af91ec5f359233b71ed37` |
| `wifi-kiwi/bdwlan_wcn785x_2p0_ncm865.elf` | 88860 | `6a6606148e98f9076cae22cb821473994a8c717202a2d6df9b1bc5ae04b257be` |
| `wifi-kiwi/bdwlan_wcn785x_2p0_ncm865a.elf` | 88860 | `95610649bdd51b503900b97df29890e7a45b127dd8367185e83b956ea8af1c86` |
| `wifi-kiwi/phy_ucode.elf` | 299660 | `6b83f3fa654d8e62ff4cfa1ee92ab814828d373394b13e5431d62aa8b873f981` |
| `wifi-kiwi/phy_ucode20.elf` | 299660 | `7f269130c3d0a209e5c51f54c3bf40062e582d72242bc99dc73d45f1475ca270` |
| `wifi-kiwi/regdb.bin` | 24278 | `4fd934da5a0be3f30694515ddca936568b9e57a9450b357e0ab4fa57f4082806` |

---

## Surprises / driver priorities

1. **Wi-Fi is cnss2 + qca_cld3_kiwi_v2 (WCN785x), not ath12k** — Argon/ath12k plans need redirect.  
2. **No vGIC DT node** — use real GICv3 @ 0x17100000; Linux already runs arch **virtual** timer (PPI 11).  
3. **PCIe1 disabled**; Wi-Fi only on **PCIe0**, BAR **0x60400000**.  
4. **Panel is dual-DSI NT36532** (Tianma active); FB width 2944 = 2×1472.  
5. **Touch is Novatek SPI**, not Goodix (Goodix = fingerprint).  
6. **UFS has ICE + MCQ** and SCM-tied crypto — simple UFSHCI bring-up first, defer ICE.  
7. **GPU is Adreno750v2 / gen7_9_1** with **gen71100** FW; ZAP needs TZ.  
8. Dump lacks partition size listing — still need `ls -l /dev/block/by-name` for userdata/super if ramdisk alternative is rejected.

## Suggested XNU driver order (from this map)

1. GICv3 redistributor (8× stride 0x40000) + arch virtual timer PPI 11 @ 19.2 MHz  
2. PSCI SMC CPU_ON for secondaries (optional early)  
3. UFS host @ 0x1D84000 + PHY @ 0x1D80000 (GDSC 0x177004 / 0x19E000) — or keep ramdisk  
4. Only then PCIe0 + CNSS/kiwi, KGSL/Adreno, input, audio
