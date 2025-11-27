# AccDump Tools

PC-side utilities for extracting accelerometer/gyroscope logs from M5Stick and M5Stack Core2 devices.

Note: Tools auto-detect baud from a fast-to-slow list
([1500000, 921600, 460800, 230400, 115200]). Use the latest firmware
to enable high-speed dump; default firmware baud is set to 1500000.

## GUI Usage

1. Install Python and dependencies:
   ```bash
   python -m pip install -r requirements.txt
   ```
2. Run the GUI:
   ```bash
   python AccDumpGUI.py
   ```
3. Select the serial port and press **DUMP**. Choose a destination
   directory when prompted.
4. If *CSVへ変換* is checked, a `.csv` file will be produced next to the
   downloaded `ACCLOG.bin`.

The log window shows the selected baud rate when connected. INFO output includes board/IMU/format and LSB metadata when firmware >=0x0201.

## CLI Usage

Dump from a specific port:

```bash
python accdump_cli.py --port COM5 --out logs/ --csv
```

Dump from all available ports:

```bash
python accdump_cli.py --all --out logs/
```

Convert an existing binary log:

```bash
python decoder.py ACCLOG.bin --csv
```

The CSV file contains columns:

- v1 logs: `n`, `t_sec`, `ax_g`, `ay_g`, `az_g`
- v2+ logs (firmware 0x0200+): adds `gx_dps`, `gy_dps`, `gz_dps`

The decoder auto-detects the format version from the 64-byte header and
parses accordingly. Scaling uses header metadata: `gyro_range_dps` (0x0200) and, if present (0x0201), `lsb_per_g` / `lsb_per_dps` and `imu_type`.
