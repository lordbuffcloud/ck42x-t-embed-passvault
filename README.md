# CK42X T-Embed PassVault

CK42X PassVault firmware for the LilyGO T-Embed / T-Embed CC1101 ESP32-S3 device.

This is a standalone ESP32-S3 port of [`flipper-ck42x-passvault`](https://github.com/lordbuffcloud/flipper-ck42x-passvault): a small field password vault with generated passwords, PIN-gated encrypted local storage, and explicit USB HID password typing.

gold on black. sting once.

## Status

MVP firmware for hardware bring-up and community testing.

- Target: LilyGO T-Embed / T-Embed CC1101, ESP32-S3, 16MB flash, 8MB PSRAM, ST7789 320x170 display, rotary encoder.
- Build system: PlatformIO + Arduino ESP32.
- Primary UI: USB serial command shell for setup/provisioning, T-Embed screen/encoder for field selection and explicit typing.
- Storage: ESP32 NVS `Preferences` blob encrypted with AES-256-GCM using a SHA-256 KDF from a user PIN + random salt.
- HID: ESP32-S3 USB HID keyboard types only the selected password after an explicit confirmation action.

This is not an audited password manager. Use test credentials until the firmware has real-world review.

## Build

```bash
pio run
```

Output artifacts:

```text
.pio/build/lilygo_t_embed_cc1101/firmware.bin
.pio/build/lilygo_t_embed_cc1101/bootloader.bin
.pio/build/lilygo_t_embed_cc1101/partitions.bin
.pio/build/lilygo_t_embed_cc1101/ck42x-t-embed-passvault-merged.bin
```

The PlatformIO environment is pinned to the `pioarduino` ESP32 platform because the current upstream PlatformIO `espressif32` package does not include the 16MB flash / 8MB PSRAM ESP32-S3 board ID used here.

## Flash

Probe the chip first. Do not flash if the board is not ESP32-S3.

```bash
pio run -t upload
```

For Windows-hosted devices from WSL, use the guarded PowerShell helper. It probes first and refuses to flash anything other than ESP32-S3:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\windows_flash.ps1 -Port COM11
```

If the board is running firmware that does not expose a serial port, put the T-Embed into ESP32-S3 ROM bootloader mode, then rerun the helper.

## Serial command shell

Open a serial monitor at `115200` baud.

```bash
pio device monitor -b 115200
```

Commands:

```text
HELP                         Show commands
STATUS                       Show lock/setup state
PIN <pin>                    First-run PIN setup, min 4 chars
UNLOCK <pin>                 Unlock existing vault
LOCK                         Lock vault in RAM
ADD account|username|pass    Add or update an entry
GEN account|username|preset  Generate and save a password. Presets: memorable, strict, long, nosymbol
LIST                         List entries with masked passwords
SHOW <index>                 Print one password to serial after unlock
TYPE <index>                 USB-HID type one password after a 3-second countdown
DEL <index>                  Delete one entry
CLEAR confirm                Wipe PassVault NVS data
```

## On-device controls

After unlock:

- Rotate encoder: select an entry.
- Short press: arm the selected entry for HID typing.
- Press again while armed: type the selected password.
- Arm times out automatically.

The screen masks passwords. Serial `SHOW` is intentionally explicit and should be used only in a trusted session.

## Security notes

- The vault is encrypted at rest with AES-GCM, using a random salt and fresh random nonce per save.
- The unlock key only exists in RAM while unlocked.
- Weak PINs, compromised firmware, serial access after unlock, or malicious host USB contexts can still expose secrets.
- HID typing sends keystrokes to whatever machine currently owns the USB focus. Use only on systems you control.

## Community submission targets

There is no single official "T-Embed app catalog" equivalent to the Flipper app catalog. Practical targets:

1. Public GitHub repo + GitHub Release with `firmware.bin`, `bootloader.bin`, `partitions.bin`, optional merged image, and SHA-256 checksums.
2. Bruce App Store is for BruceJS scripts/themes, not standalone replacement firmware. A tiny BruceJS launcher/info card can be submitted later, but this firmware itself does not install as a Bruce app.
3. LILYGO community/forum post linking the repo and release.
4. Optional M5Burner-style publication is only relevant to M5Stack devices/accounts, not the LilyGO T-Embed hardware path.

## License

MIT. See `LICENSE`.
