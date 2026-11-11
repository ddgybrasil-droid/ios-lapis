Образ для QEMU. Это не сборка текущей ветки и не образ для fastboot.

Файл: Image
Размер: 21 807 408 байт
SHA-256: 52ba8b831abdf7b3436f0ed3e1934383bcb41e0f6f41603eaa70523d85364202

Откуда: архив ios-lapis-round0014-compact.zip, SHA-256 архива
63d0f9fef996fcce8a5daadb194340ec92734ceb2dd44a274c592ff052ce9065.
Внутри архива это project/src/github-v4/out-verified/Image от 2026-11-10.
Тот же файл лежит в Android-обёртке images/qemu-emu-CURRENT-NOT-FASTBOOT.img
(SHA-256 62a41ce6ac2b451a5f46406c836459d3044e307d942e965ad5b8a7b0cffdde45).
Обёртка в git не клалась: QEMU с ключом -kernel принимает сам Image.

Заголовок ARM64. Вшитая строка запуска:
debug=0xc cpus=8 keepsyms=1 -v lapis_smp=1 lapis_smp_n=8 lapis_smp_test=1
lapis_psci=0 slto_us=2000000 tlto_us=1000000 -arm64e_preview_abi
bingrade=0x100000c:2 rd=md0

Корень — диск в памяти md0, не UFS-образ на 5 ГиБ. Назначений
unentitled_ios_sim_launch=1, interrupt-masked-debug-mode=0 и
sched_preemption_disable_debug_mode=0 в этой строке нет. Правки записи
UFS из этой ветки в файл не вкомпилированы.

Старый лог QEMU доходил до строки load_init_program про /sbin/launchd и на
ней заканчивался. Повторение этого лога на планшете здесь не делалось.

Termux, после tsu и echo 1 > /proc/sys/vm/overcommit_memory:

qemu-system-aarch64 -nodefaults -display none -serial mon:stdio \
  -M virt,virtualization=off,secure=off,gic-version=3,memory-backend=ram0 \
  -m 43G -object memory-backend-ram,id=ram0,size=43G,reserve=off \
  -cpu max -smp 1 -kernel Image

В fastboot этот файл не писать.
