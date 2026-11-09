# Lapis built-in drivers — status (drv2)

Images:
- **hw** `out/lapis-xnu-drv2.img` sha256 `4e2ddfd0236dcd2167b2b74ef9c76234078d2da5e6df1a26217103ff65f969ee`
- **emu** `out-emu/lapis-xnu-drv2-emu.img` sha256 `22e42a7f3788da57d86415a19e634c33d87196802e9ff6e490d8ce6e411b4182`

Evidence (emu): `/tmp/drv2-emu5/msgbuf.txt` — UFS GPT + LapisBlk ROOT + lapisfs mount from `disk0s2` + launchd load.

| Driver | Status | Notes |
|---|---|---|
| LapisGICv3 | VERIFIED-emu (+ prior VERIFIED-hw) | selftest + UFS IRQs |
| LapisUFS | VERIFIED-emu (hw: prior probe-only; reuse mode untested on hw) | full on QEMU PCI; hw default `reuse` |
| LapisBlk | VERIFIED-emu | `/dev/disk*` + GPT + ROOT locator; no IOMedia |
| lapisfs ← UFS | VERIFIED-emu | mounted `disk0s2` LAPISFS1, launchd load |
| LapisFB | VERIFIED-emu | splash banner at 0xD5100000, no DSI |
| LapisSMP | compiled-only | PSCI SMC/HVC probe; `ml_set_max_cpus` stays 1 |
| LapisSDHCI | compiled-only | probe-only; data path NOT DONE |
| LapisTouch | compiled-only | stub |
| LapisBatt | compiled-only | stub |
| LapisWiFi | compiled-only | PARF/BAR probe; **PIL/SCM BLOCKER** at EL1/Gunyah |
| LapisGPU | compiled-only | KGSL probe; **ZAP/SCM BLOCKER** at EL1/Gunyah |
| LapisAudio | compiled-only | LPASS probe; **ADSP PIL/SCM BLOCKER** at EL1/Gunyah |
| s11 launchd+AMFI stubs | VERIFIED-emu | merged |

Expected hw log (with drivers enabled in cmdline):
```
LAPIS drv: built-in drivers v2 (...)
LapisGICv3: registered ...
LapisUFS: probing qcom,ufshc @ 0x1d84000 (mode 2) ...
LapisFB: splash fb 0xd5100000 mapped ...
LapisSDHCI: HC @ 0x8804000 ...
LapisSMP: PSCI version ...
LapisWiFi: PCIe0 PARF ...
LapisGPU: KGSL ...
LapisAudio: LPASS_CC ...
```

See `IPSW-BOOT.md` for the install/boot procedure.
