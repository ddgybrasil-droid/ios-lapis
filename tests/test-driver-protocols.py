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
    for src in TESTS:
        bin_path = os.path.join("/tmp", os.path.basename(src) + ".bin")
        subprocess.check_call(["gcc", "-Wall", "-Wextra", "-Werror", "-std=c11",
                               os.path.join(ROOT, src), "-o", bin_path])
        subprocess.check_call([bin_path])
    print("PASS driver protocols")

if __name__ == "__main__":
    main()
