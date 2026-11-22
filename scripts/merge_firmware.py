#!/usr/bin/env python3
# ⬡ CK42X · ck42x.com
# merge_firmware: join bootloader, partitions, and firmware into one esp32-s3 flash image
# gold on black. sting once.
"""guarantees: writes ck42x-t-embed-passvault-merged.bin and prints its path on success.
rejects: exits 1 listing missing build artifacts before esptool runs.
units: esp32-s3 flash offsets 0x0 bootloader / 0x8000 partitions / 0x10000 firmware.
"""
from pathlib import Path
import subprocess
import sys

# TRAP: ROOT assumes this script sits one level below the repo root; moving it retargets .pio silently.
ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / ".pio" / "build" / "lilygo_t_embed_cc1101"
OUT = BUILD / "ck42x-t-embed-passvault-merged.bin"

# INVARIANT: offsets are the hardcoded esp32-s3 boot map, not a per-board setting.
parts = [
    ("0x0", BUILD / "bootloader.bin"),
    ("0x8000", BUILD / "partitions.bin"),
    ("0x10000", BUILD / "firmware.bin"),
]
# WHY: list every missing artifact at once instead of failing piecemeal inside esptool.
missing = [str(path) for _, path in parts if not path.exists()]
if missing:
    print("missing build artifacts:", *missing, sep="\n  ", file=sys.stderr)
    sys.exit(1)

# WHY: prefer platformio's penv python so esptool matches the environment that built the artifacts.
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

# SAFETY: check_call raises on nonzero esptool exit; the printed path only appears after a full merge.
subprocess.check_call(cmd)
print(OUT)
