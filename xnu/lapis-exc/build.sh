#!/bin/sh
# assemble exc_text.S, linked at its kernel VA (0xfffffe000756e788) -> exc_text.bin
set -e; cd "$(dirname "$0")"
aarch64-linux-gnu-gcc -c -nostdlib -o exc_text.o exc_text.S
aarch64-linux-gnu-ld -Ttext=0xfffffe000756e788 -e _start --defsym=k_img4if=0xfffffe0007277420 --defsym=k_amfi=0xfffffe0007277430 --defsym=k_trust_cache_init_body=0xfffffe0007919958 --defsym=k_g_crypto_funcs=0xfffffe0007277400 --defsym=k_prng_ready=0xfffffe00071a40c8 --defsym=k_read_random_body=0xfffffe0007548bb8 --defsym=k_read_erandom_generate=0xfffffe0007548f6c --no-warn-rwx-segments -o exc_text.elf exc_text.o
aarch64-linux-gnu-objcopy -O binary -j .text exc_text.elf exc_text.bin
ls -l exc_text.bin
# the blob is placed at 0xfffffe000756e788: link address must match exactly
aarch64-linux-gnu-nm exc_text.elf | grep -q '^fffffe000756e788 T _start$' || { echo "exc_text: _start not at 0x756e788"; exit 1; }
