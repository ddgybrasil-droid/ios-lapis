#!/usr/bin/env python3
"""Compile and run the host checks for the touch, battery and SD parsers."""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TESTS = (
    "tests/test-nvt-nt36532.c",
    "tests/test-pmic-glink-batt.c",
    "tests/test-sdhci-csd.c",
    "tests/test-wifi-audio-wire.c",
    "tests/test-mdt-hfi.c",
    "tests/test-gic-fb.c",
)

def main():
    sdhci = open(os.path.join(ROOT, "drivers/sdhci/lapis_sdhci.c"), encoding="utf-8").read()
    start = sdhci.index("static int sdhci_xfer")
    body = sdhci[start:sdhci.index("static void sdhci_thread")]
    if "if (write) return -1;" not in body:
        sys.exit("sdhci write path is not refused")
    if "lapis_sd_csd_blocks" not in sdhci or "disk not published" not in sdhci:
        sys.exit("sdhci publishes a disk without a decoded CSD")
    touch = open(os.path.join(ROOT, "drivers/touch/lapis_touch.c"), encoding="utf-8").read()
    if "0x71" in touch or "0x1000" in touch:
        sys.exit("touch still uses the guessed Novatek address")
    batt = open(os.path.join(ROOT, "drivers/batt/lapis_batt.c"), encoding="utf-8").read()
    if "soc~" in batt or "0x4000" in batt:
        sys.exit("battery still decodes a guessed SPMI window")
    wifi = open(os.path.join(ROOT, "drivers/wifi/lapis_wifi.c"), encoding="utf-8").read()
    if "MHI_BHI_IMGTXDB, 1)" in wifi:
        sys.exit("wifi still rings BHI with a fixed session id")
    audio = open(os.path.join(ROOT, "drivers/audio/lapis_audio.c"), encoding="utf-8").read()
    if "graph payload is empty" not in audio:
        sys.exit("audio no longer refuses an empty APM graph")
    gpu = open(os.path.join(ROOT, "drivers/gpu/lapis_gpu.c"), encoding="utf-8").read()
    if "queue is not allocated" not in gpu or "CP submission refused" not in gpu:
        sys.exit("gpu no longer refuses HFI and CP submission")
    mdt = open(os.path.join(ROOT, "drivers/scm/mdt_image.c"), encoding="utf-8").read()
    if "smc #" in mdt.lower() or "qcom_scm_" in mdt:
        sys.exit("mdt parser issues an SMC")
    fb = open(os.path.join(ROOT, "drivers/fb/lapis_fb.c"), encoding="utf-8").read()
    if 'drv_bootarg("lapis_fb", 1)' in fb or "FB OK" in fb:
        sys.exit("framebuffer still paints by default or claims the panel is up")
    gic = open(os.path.join(ROOT, "drivers/gic/lapis_gic.c"), encoding="utf-8").read()
    if "lapis_gic_intid_count" not in gic or "lapis_gic_redist_awake" not in gic:
        sys.exit("gic no longer uses the decoded INTID count and WAKER check")
    for src in TESTS:
        bin_path = os.path.join("/tmp", os.path.basename(src) + ".bin")
        cmd = ["gcc", "-Wall", "-Wextra", "-Werror", "-std=c11",
               os.path.join(ROOT, src), "-o", bin_path]
        if src.endswith("test-mdt-hfi.c"):
            cmd.insert(-2, os.path.join(ROOT, "drivers/scm/mdt_image.c"))
        subprocess.check_call(cmd)
        subprocess.check_call([bin_path])
    print("PASS driver protocols")

if __name__ == "__main__":
    main()
