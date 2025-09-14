# AccDump Tools

PC-side utilities for extracting accelerometer logs from the M5Stick devices.

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
parses accordingly. Gyro scaling uses `gyro_range_dps` embedded in the header.
