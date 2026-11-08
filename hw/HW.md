# lapis hardware notes (TB520FU / SM8650)

> Reconstructed for M1 because `uploads/HW.md` was not mounted into the agent VM.
> Replace any placeholder with the verified on-device facts before relying on a boot.

## Boot environment

- Entry: EL1 under Qualcomm Gunyah (same contract as Linux/`fastboot boot`)
- ABL passes a live FDT in `x0` (Linux Image protocol)
- Boot method: `fastboot boot` only — never flash boot/init_boot

## Interrupt controller (GICv3)

From upstream `sm8650.dtsi` (confirm against live `/proc/iomem`):

| Region | Address | Size |
|--------|---------|------|
| GICD   | `0x17100000` | `0x10000` |
| GICR   | `0x17180000` | `0x200000` (8× stride `0x40000`) |

`GICR_PE_SIZE` / redistributor stride on SM8650 is **`0x40000`**, not the `0x20000` used by ios-sweet/VMAPPLE.

## Timer

`arm,armv8-timer` with standard PPIs → virtual timer **INTID 27**.

## PSCI

`arm,psci-1.0`, method `smc`.

## Framebuffer (M0-verified per task)

| Field | Value |
|-------|-------|
| Base | `0xD5100000` |
| Format | XRGB8888 |
| Stride | 11776 bytes |
| Resolution | 2944×1840 landscape |
| Rotation | none |

## RAM

Placeholder kernel-managed window for the ADT/loader:

- `phys_base = 0x100000000`
- `mem_size = 0x80000000` (2 GiB), with 1 MiB panic log carved from the top

Reconcile against the free ranges from the real `HW.md` / `iomem.txt` before on-device XNU boots.
