#!/usr/bin/env python3
"""Ensure the historical no-op pthread provider is not injected anymore."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
patcher = (root / "tools/xnu_binpatches.py").read_text()
build = (root / "xnu/lapis-exc/build.sh").read_text()
aux = (root / "xnu/lapis-exc/aux_text.S").read_text()

assert "pthread_functions-builtin-stub" not in patcher
assert "PATCHES.append(('aux-blob'" not in patcher
assert "aux_text.bin" not in patcher
assert "aux_text.S is retained as a historical prototype only" in build
assert "HISTORICAL PROTOTYPE ONLY — NOT BUILT OR INJECTED" in aux
assert "original NULL table" in patcher
print("PASS: no-op pthread_functions provider is not built or injected; original XNU missing-provider panic remains")
