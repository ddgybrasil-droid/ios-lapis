# TB520FU (lapis) — железо из прошивки

Источник: PixelOS 17 unofficial `PixelOS_lapis-17.0-20261105-0814.zip` (патч 2026-10-01, SDK 37, sha256 a12bae42…ba742). Из payload вытянуты по HTTP-range только нужные разделы → `firmware/`. Это **статические** DTB/DTBO сборки, не live FDT устройства.

- SoC: SM8650 **Pineapple v2** (msm-id 557, rev 0x20000) → базовый DTB `firmware/dtb/05_…Pineapple_v2_SoC.dts`.
- Плата: DTBO #37 «Lapis QRD», board-id 0x2000B → `firmware/dtbo/37.dts`. (#35 Kirby — соседний Lenovo, не наш.)
- vendor_boot: header v4, page 0x1000, cmdline `video=vfb:640x400,bpp=32,memsize=3072000 nosoftlockup bootconfig`, bootconfig `androidboot.hardware=qcom`, usb `a600000.dwc3`. boot.img: v4, только kernel (35 МБ), ramdisk в init_boot.

## Ядро платформы
| Блок | Адрес / значение |
|---|---|
| RAM `/memory` | `reg = 0` — **заполняет ABL**, нужен live FDT |
| GICv3 Distributor | 0x17100000 (0x10000) |
| GICv3 Redistributors | 0x17180000 (0x200000), stride 0x40000, 1 регион (8 CPU) |
| GIC ITS | 0x17140000 (0x20000); maintenance IRQ PPI 9 |
| Arch timer | 19.2 МГц (0x124F800); PPI 13/14/11/12 (= INTID 29/30/27/26), level-low |
| MMIO timer | 0x17420000, frame0 0x17421000 (SPI 8, 6) |
| PSCI | `arm,psci-1.0`, method **smc**; CPU MPIDR 0x0,0x100…0x700 (8 ядер, `qcom,kryo`) |
| Gunyah | `gunyah_hyp_region@80000000` — ядро запускается **под гипервизором Gunyah** (EL1), учесть в shim |

## Зарезервированная память (главное)
0x80000000 gunyah (0xE00000) · 0x81D00000 smem · 0x82800000 qdss (32 МБ) · 0x86B00000–0x9B000000 modem · 0x9B300000 camera … 0x9DF00000 adsp (0x4080000) · 0xA2000000 ramoops (4 МБ) · 0xD4E23000 (0x2DD000) · **0xD5100000 splash** · 0xD8000000 tz (8 МБ) · 0xF3800000 trust_ui_vm · 0xF7C00000 oem_vm · 0xFF800000 llcc_lpi. Динамический `non_secure_display_region` 0xA400000 (CMA).

## Экран
- `cont_splash_region` (DTBO frag@73): **PA 0xD5100000, size 0x2B00000 (43 МБ)**.
- Панели: `lapis_boe_nt36532_dsc_3k_video` и `lapis_tianma_nt36532_dsc_3k_video` (какая стоит — из live FDT/cmdline `msm_drm.dsi_display0`).
- Dual DSI (topology 2:2:2), на каждый DSI 1472×1840 → **2944×1840 @120 Гц, video mode**, DSI bpp 30 (10 бит), DSC 8bpp/10bpc, slice 736×20.
- MDP 0xAE00000, DSI0 0xAE94000 / PHY 0xAE95500, DSI1 0xAE96000 / PHY 0xAE97500.
- Подсветка: 2× KTZ8866 (I2C 0x11), hwen TLMM 82.

## Ввод
- Тач: `novatek,NVT-ts-spi` (NT36532, стилус) на **QUPv3 SE4 SPI @ 0xA90000** (IRQ SPI 357), CS0, 9.6 МГц; reset TLMM 161, irq TLMM 162 (falling).
- Кнопки: Vol+ — PM8550 GPIO6 (gpio-keys, active-low); Power и Vol− — PMK8550 PON @0x1300 (`pwrkey` code 116, `resin` code 114).
- Также есть Lenovo keyboard (UART, frag@38) и hall-датчик.

## UFS / PMIC
- UFS host 0x1D84000 (0x3000), ICE 0x1D88000, MCQ 0x1DA5000/0x1DA4000; IRQ SPI 265; PHY 0x1D80000, 2 lane, QMP v4 pineapple.
- SPMI PMIC arbiter: bus0 cnfg 0xC42D000, bus1 0xC432000; core 0xC400000, chnls 0xC500000, obsrvr 0xC440000, intr 0xC4C0000/0xC4D0000. PMIC: PMK8550, PM8550 и др.

## Нужно из live FDT устройства (`/sys/firmware/fdt`)
1. `/memory` — реальные банки RAM (тут нули).
2. Итоговый splash/framebuffer после ABL: подтвердить 0xD5100000, stride, формат; какая панель (BOE/Tianma).
3. `/chosen` (cmdline, kaslr-seed, bootconfig), применённые overlay и `/proc/iomem`.
4. Подтвердить, что на устройстве именно Pineapple v2 + Lapis QRD, а не PRC-вариант после LTBox.

## Live /proc/iomem (2026-11-08, с планшета)
- Splash FB подтверждён: d5100000-d7bfffff reserved (0x2B00000).
- GICD 0x17100000, GICR 0x17180000-0x1737ffff, arch_mem_timer 0x17421000, UFS 0x1D84000 (+MCQ 0x1DA0000) — совпадает.
- Высокий банк RAM: 0x8C0000000–0xAEFA5FFFF свободен (~7.9 GiB) — кандидат под payload/XNU (аналог upper bank у sweet).
- Также 0x880000000–0x8AF8FFFFF, 0x8B0000000–0x8BA2FFFFF.
- Wi-Fi: cnss по PCIe @0x1C00000.

## M0 аппаратный тест — отчёт 8 ноября 2026, 21:05–21:06 МСК

По переданному отчёту ABL принял temporary boot; framebuffer 0xD5100000,
XRGB8888 без R/B swap, **2944×1840 landscape**, stride **11776** подтверждены
линейкой/диагональю. **Поворота нет:** боком была снята фотография. CurrentEL=EL1.
Это не проверка MMU/PAC/GIC/IRQ/PSCI/высокой RAM. Полный DTB x0 не установлен
(передано лишь 0x...86FEF000). См. m0/VERIFICATION.md.

Арифметика iomem: [0x8C0000000,0xAEFA60000) = 0x22FA60000 байт,
~8,7445 GiB; старая оценка 7,9 GiB неточна. Доступность под M1 требует
проверки текущего merged FDT и всех динамических carve-outs.
