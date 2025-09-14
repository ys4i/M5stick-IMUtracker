M5-IMUtracker (M5Stick Accelerometer/Gyro Logger)
=================================================

A simple firmware + PC tools set to record IMU data on M5Stick devices and dump it to your computer as a binary and CSV file.

- Firmware logs IMU samples to the device’s filesystem with a compact 64‑byte header followed by raw samples.
- PC tools (GUI/CLI) download `ACCLOG.bin` over serial and optionally convert it to CSV.
- Latest firmware logs both accelerometer and gyroscope. Older logs (accel‑only) are still supported.

Features
--------

- Button‑controlled recording on the M5Stick
- Fixed ODR and ranges configured in `config.h`
- Robust serial dump (handles stray preambles, tolerates timing)
- Decoder auto‑detects log format (v1 accel‑only, v2 accel+gyro)
- CSV output ready for analysis in Python, Excel, etc.

Hardware
--------

- M5StickC / M5StickC Plus family (SH200Q IMU via M5.IMU)

Repository Layout
-----------------

- `firmware_m5_multi_acc_logger/` — Arduino firmware for M5Stick devices
- `pc_tools/` — PC‑side tools (GUI/CLI/decoder)
- `docs/` — additional documentation (if any)

Firmware
--------

1. Open `firmware_m5_multi_acc_logger/firmware_m5_multi_acc_logger.ino` in Arduino IDE.
2. Select your board (e.g., M5StickC/M5StickC Plus) and a suitable partition scheme.
3. Build and upload.
4. Recording:
   - Press Button A to start/stop logging.
   - A binary file `/ACCLOG.BIN` is written in the device filesystem.

Configuration (edit in `config.h`):
- `ODR_HZ` — sampling rate (e.g., 200 Hz)
- `RANGE_G` — accelerometer full scale (2/4/8/16 g)
- `GYRO_RANGE_DPS` — gyroscope full scale (250/500/1000/2000 dps)
- `SERIAL_BAUD` — serial speed for dump/commands (default 115200)

Serial protocol (for tooling):
- `PING` → `PONG`\n
- `INFO` → one‑line JSON (ODR, ranges, file size)
- `DUMP` → `OK <filesize>` then raw file bytes, finally `\nDONE\n`
- `ERASE` → deletes `/ACCLOG.BIN`
- `START` / `STOP` → start/stop logging

Data Format
-----------

Header (64 bytes, little‑endian):
- magic[8]: `"ACCLOG\0\0"` (older v1 may be `"ACCLOG\0"`)
- format_ver: uint16 (v1: 0x0100 accel‑only, v2+: 0x0200 accel+gyro)
- device_uid: uint64
- start_unix_ms: uint64 (optional)
- odr_hz: uint16
- range_g: uint16
- v2+: gyro_range_dps: uint16 (stored where v1 had reserved)
- total_samples: uint32 (0xFFFFFFFF while recording)
- dropped_samples: uint32
- reserved: zero‑padded to 64 bytes total

Payload:
- v1: repeating `[ax i16][ay i16][az i16]` (big‑endian words, MSB first)
- v2+: repeating `[ax][ay][az][gx][gy][gz]` (each int16 big‑endian)

CSV columns:
- v1: `n, t_sec, ax_g, ay_g, az_g`
- v2+: `n, t_sec, ax_g, ay_g, az_g, gx_dps, gy_dps, gz_dps`

PC Tools
--------

Requirements (Python 3.10+ recommended):

```
python -m pip install -r pc_tools/requirements.txt
```

GUI (Tkinter)
-------------

```
python pc_tools/AccDumpGUI.py
```

- Select your serial port and click DUMP.
- Check “CSVへ変換” to automatically generate a CSV next to `ACCLOG.bin`.

CLI
---

Dump from a specific port and convert to CSV:

```
python pc_tools/accdump_cli.py --port /dev/ttyUSB0 --out logs/ --csv
```

Dump from all available ports:

```
python pc_tools/accdump_cli.py --all --out logs/
```

Convert an existing log file to CSV:

```
python pc_tools/decoder.py logs/ACCLOG.bin --csv
```

Troubleshooting
---------------

- Device busy (`could not open port …: Device or resource busy`):
  - Close other serial monitors (Arduino Serial Monitor, screen/minicom/picocom, VS Code extensions).
  - On Linux, stop ModemManager if it probes ports: `sudo systemctl stop ModemManager`
  - Ensure your user is in `dialout` group; re‑login after adding.
- Timeout waiting for header / partial dumps:
  - The PC tool searches for the `ACCLOG` header and can fall back to raw copy.
  - If the header is not at the very start due to device preambles, the decoder scans for it.
  - If issues persist, power‑cycle the device and try again.
- CSV conversion errors (invalid magic):
  - Re‑dump with the latest firmware and tools.
  - Share the `.bin` size and the first 64 bytes (hex) if you need help.
- Slow/unstable link:
  - Tools use a 10‑second serial timeout and read in 4 KB chunks; re‑try if transient errors occur.

Building Stand‑alone Binaries (Optional)
---------------------------------------

Install PyInstaller and build:

```
python -m pip install -r pc_tools/requirements.txt pyinstaller
pyinstaller --onefile --windowed pc_tools/AccDumpGUI.py
pyinstaller --onefile pc_tools/accdump_cli.py
```

Artifacts are created under `dist/`.

Notes
-----

- New firmware (format 0x0200) adds gyroscope channels; the decoder remains backward‑compatible with v1 logs.
- Payload words are written MSB first by the firmware; the decoder interprets them as big‑endian int16.

