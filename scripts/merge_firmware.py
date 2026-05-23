#!/usr/bin/env python3
"""Create a single merged ESP32 flash image for browser/manual flashers."""
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / ".pio" / "build" / "lilygo_t_embed_cc1101"
OUT = BUILD / "ck42x-t-embed-passvault-merged.bin"

parts = [
    ("0x0", BUILD / "bootloader.bin"),
    ("0x8000", BUILD / "partitions.bin"),
    ("0x10000", BUILD / "firmware.bin"),
]
missing = [str(path) for _, path in parts if not path.exists()]
if missing:
    print("missing build artifacts:", *missing, sep="\n  ", file=sys.stderr)
    sys.exit(1)

pio_python = Path.home() / ".platformio" / "penv" / "bin" / "python"
python_exe = str(pio_python) if pio_python.exists() else sys.executable
cmd = [python_exe, "-m", "esptool"]

cmd.extend([
    "--chip",
    "esp32s3",
    "merge-bin",
    "-o",
    str(OUT),
])
for addr, path in parts:
    cmd.extend([addr, str(path)])

subprocess.check_call(cmd)
print(OUT)
