# LAPIS M0 — hardware result (reported)

Report received 8 November 2026, 21:05–21:06 MSK (Europe/Moscow).
Source: parent-agent relay of the device test and subsequent user correction.
No photograph or full fastboot transcript was supplied to this executor.
This is reported hardware evidence, not a test executed in this task.

## Reported passes

- ABL accepted temporary `fastboot boot`; payload displayed successfully.
- Framebuffer: `0xD5100000`; XRGB8888, no red/blue swap.
- Landscape **2944 x 1840**, stride **11776 bytes (0x2E00)**.
- Ruler straight and diagonal unbroken.
- **Not rotated**: the photograph was taken sideways; user explicitly corrected it.
- CurrentEL read as **EL1**. Gunyah was identified separately in FDT.
- DTB x0 reported approximately `0x...86FEF000`; full value unknown, never hard-code.

Frame write span is `[0xD5100000, 0xD65AA000)`, length `0x14AA000` = 21,667,840
bytes. Reservation remains `[0xD5100000, 0xD7C00000)`.

## Not established by M0

No MMU/page-size/PAC/GIC/timer/PSCI/high-RAM-write/XNU/userland compatibility
is established by drawing a framebuffer. Android recovery/unchanged slot was
not separately reported in this relay. No new device operations were performed.

Prior conversation identified boot artifact SHA256
`a9e3f672ce3e2837b67391ceb4ce7e925c21e24a043ae839db4efd2009cd7a94`, 28,672 bytes.
The earlier m0 build directory was absent from the current filesystem at task
start; those binary/build-log claims have not been revalidated here. This new
file records the reported test only; it does not recreate the prior artifact.
