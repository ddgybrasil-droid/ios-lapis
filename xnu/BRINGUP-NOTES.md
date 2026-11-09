# XNU bring-up notes (s10, 2026-11-09)

Emulator (QEMU virt, emu profile) boot now reaches the root-device wait:
`Waiting on <dict>IOProviderClass=IOMedia, Content=Apple_HFS</dict>` at ~40.8 s.
At ~100.8 s (60 s later) NVRAM `boot-picker-bringup-reason` is written. That is the
root-not-found path. Still idle and not panicked at 240 s.

## Walls fixed in s10 (all shared emu+hw: binpatches / ADT)
1. Boot thread asleep in ml_wait_max_cpus (kperf_init_early). ml_set_max_cpus is
   normally called by the platform kext's IOCPUInterruptController. Fix: call
   ml_set_max_cpus(1) from LapisPlatformExpert::start (`lapispe-set-max-cpus`).
   The same stall happens on hardware (s9 photo).
2. "pthread kernel extension not loaded": a built-in pthread_functions stub
   (xnu/lapis-exc/aux_text.S). Init fns are no-ops and the syscalls return ENOTSUP.
3. g_crypto_funcs NULL (tcp_init TFO key AES -> data abort): a built-in corecrypto
   stand-in, xnu/lapis-crypto/lapis_crypto.c. It provides MD5/SHA1/SHA2, HMAC,
   AES ECB/CBC and ccrng, and passes the host test vectors. It is linked over
   the sysctl-only `_vm_parameter_validation_kern_test` (entry now returns ENOTSUP).
   crypto_init=1. The CRSTUB random fallbacks now also check each table slot.
4. sdt_init assert: ADT /chosen `kernel-ctrr-to-be-enabled = 0` (no CTRR/KTRR lock).

## Known gaps / not secure
- The pthread stub means no userland threads. The real fix is to build libpthread kern/ in.
- lapis_crypto is not constant-time, and has no GCM/CTR/XTS/DES/RSA or crypto_digest API (slots 17-36, 38-55
  are NULL). Fill these when they get hit.
- AMFI/AppleImage4 are stubbed and the trust cache is skipped, so there is NO code-signing enforcement.
- IOKitInitializeTime waits 30 s for IORTC ("no monotonic clock"), so an RTC driver is needed.
- Hardware: no interrupt-controller driver ("failed to find the interrupt-controller").
  A GIC (via Gunyah), CPU/IOPMGR drivers, timers and IPIs are needed before anything SMP or IRQ-driven.
- Root device: needs a block driver (UFS on SM8650), or a ramdisk/`rd=md0` via the shim, for the next stage.

## Kext infrastructure (no kernelcache)
- Built-in IOKit classes need their personalities in gIOCatalogue->addDrivers.
- arm64 has no runtime kext linking. The real route is an offline MH_FILESET boot
  kernel collection that the shim loads. AuxKC via kernelmanagerd needs AMFI, Image4 and corecrypto.
