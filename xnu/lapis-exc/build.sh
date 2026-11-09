#!/bin/sh
# assemble exc_text.S, linked at its kernel VA (0xfffffe000756e788) -> exc_text.bin
set -e; cd "$(dirname "$0")"
aarch64-linux-gnu-gcc -c -nostdlib -o exc_text.o exc_text.S
aarch64-linux-gnu-ld -Ttext=0xfffffe000756e788 -e _start --defsym=k_vectors=0xfffffe000756a000 --defsym=k_lapis_early_mark=0xfffffe0007567d80 --defsym=k_lapis_early_putc=0xfffffe0007567b44 --defsym=k_lapis_early_panic_body=0xfffffe0007567f90 --defsym=k_doprnt=0xfffffe00073b6b94 --defsym=k_printf_body=0xfffffe00073b6c68 --defsym=k_kprintf_body=0xfffffe0007b71500 --defsym=k_pe_cd_inherit=0xfffffe0007b6f810 --defsym=k_serial_init=0xfffffe0007b7162c --defsym=k_pe_init_plat=0xfffffe0007b70748 --defsym=k_ec64_target=0xfffffe0007b71a0c --defsym=k_machine_startup=0xfffffe0007571474 --defsym=k_early_disabled=0xfffffe0007ccaf04 --defsym=k_pe_init_printf=0xfffffe0007b6f0f8 --defsym=k_img4if=0xfffffe0007277420 --defsym=k_amfi=0xfffffe0007277430 --defsym=k_trust_cache_init_body=0xfffffe0007919958 --defsym=k_g_crypto_funcs=0xfffffe0007277400 --defsym=k_prng_ready=0xfffffe00071a40c8 --defsym=k_read_random_body=0xfffffe0007548bb8 --defsym=k_read_erandom_generate=0xfffffe0007548f6c --no-warn-rwx-segments -o exc_text.elf exc_text.o
aarch64-linux-gnu-objcopy -O binary -j .text exc_text.elf exc_text.bin
ls -l exc_text.bin
# the blob is placed at 0xfffffe000756e788: link address must match exactly
aarch64-linux-gnu-nm exc_text.elf | grep -q '^fffffe000756e788 T _start$' || { echo "exc_text: _start not at 0x756e788"; exit 1; }
