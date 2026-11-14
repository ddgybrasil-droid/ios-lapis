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

Запуск этого файла в QEMU 11.0.0, машина virt-8.2, восемь CPU, таймер 1 ГГц.
UFS и полный rootfs не подключались. Планшет не использовался. iPadOS не
загрузилась.

Обычный запуск три раза дошёл до паники обработчика прерывания. В одном
журнале: type 2, vector 0, handler 0xfffffe00075781b0, 1247 мкс при пороге
851 мкс. В таблице символов этого ядра тот адрес — `_rtclock_intr`.
Сообщения `failed to find the interrupt-controller` и об отсутствии
`cpu-debug-interface` панику не объясняют: ядро после них продолжало работу.
Восемь процессоров в журнале не доказывают, что все восемь ядер исполнялись.
[гипотеза] Тайм-аут связан со скоростью программной эмуляции, а не с тем,
что на планшете повторится та же ошибка.

Запуск с `-icount` прошёл таймер и остановился на панике с форматом
`image4 interface not available @%s:%d`. На входе в `panic` указатели
`img4if` и `amfi` были равны нулю. Отладчик остановил ядро до печати
паник-лога, поэтому это аргумент вызова. Интерфейсы к этой проверке не
зарегистрированы. Комплект 23F84 с этим ядром не совпал по версии;
совместимость не установлена. `-icount` не воспроизводит реальное время.

Внутри Image есть небольшой ramdisk примерно на 1,98 МиБ с `launchd` и
`/usr/lib/dyld`, без dyld shared cache. До его монтирования и до полного
rootfs запуск не дошёл. У `IOService::setRootMedia` третья инструкция —
RET, а установка кадра начинается следом; в исходниках этой ветки такой
no-op снят, но этот Image собран раньше и байты сохраняет.

Старый отдельный лог доходил до строки load_init_program. Этот прогон до
неё не дошёл. На планшете файл не запускался.

Termux, после tsu и echo 1 > /proc/sys/vm/overcommit_memory:

Проверенный запуск использовал -smp 8 и таймер 1 ГГц. Команда ниже только
показывает форму вызова; -smp 1 тем журналам не соответствует.

qemu-system-aarch64 -nodefaults -display none -serial mon:stdio \
  -M virt,virtualization=off,secure=off,gic-version=3,memory-backend=ram0 \
  -m 43G -object memory-backend-ram,id=ram0,size=43G,reserve=off \
  -cpu max -smp 8 -kernel Image

В fastboot этот файл не писать.

QEMU 11 + lva=off (chunk 5): qemu/build-qemu11-lva.sh builds qemu-system-aarch64 with a new
`lva` property for -cpu max. Use CPU=max,pauth-impdef=on,lva=off QEMU=<built binary> tools/emu_run_gdb.sh ...
Sources (VARange [19:16] = 0b0000, 48-bit VA): Arm Cortex-X4 TRM 102484 r0p3, Cortex-A720 TRM 102530,
Cortex-A520 TRM 102517, section ID_AA64MMFR2_EL1.
