from pathlib import Path
import struct
import numpy as np
import pandas as pd

HEADER_FMT = '<8sHQQHHII26s'
HEADER_SIZE = struct.calcsize(HEADER_FMT)


def parse_header(data: bytes) -> dict:
    if len(data) < HEADER_SIZE:
        raise ValueError('header too short')
    (magic, fmt_ver, device_uid, start_ms, odr, range_g,
     total_samples, dropped, _reserved) = struct.unpack(HEADER_FMT, data[:HEADER_SIZE])
    if not magic.startswith(b'ACCLOG'):
        raise ValueError('invalid magic')
    return {
        'format_ver': fmt_ver,
        'device_uid': device_uid,
        'start_unix_ms': start_ms,
        'odr_hz': odr,
        'range_g': range_g,
        'total_samples': total_samples,
        'dropped_samples': dropped,
    }


def bin_to_csv(bin_path: Path, csv_path: Path | None = None):
    """Convert binary log file to CSV.

    Returns
    -------
    header : dict
        Parsed header information.
    df : pandas.DataFrame
        Converted dataframe with columns ``n,t_sec,ax_g,ay_g,az_g``.
    """
    bin_path = Path(bin_path)
    with open(bin_path, 'rb') as f:
        header = parse_header(f.read(HEADER_SIZE))
        payload = f.read()
    # Ensure even number of bytes (int16-aligned); drop any trailing odd byte
    if len(payload) % 2 != 0:
        payload = payload[:-1]
    # Firmware writes MSB first (big-endian) for each int16
    raw = np.frombuffer(payload, dtype='>i2')
    # Ensure complete [ax,ay,az] triplets; drop any trailing incomplete values
    if raw.size % 3 != 0:
        raw = raw[: (raw.size // 3) * 3]
    data = raw.reshape(-1, 3)
    lsb_per_g = 32768 / header['range_g']
    acc_g = data / lsb_per_g
    n = np.arange(len(acc_g), dtype=np.int64)
    t_sec = n / header['odr_hz']
    df = pd.DataFrame({'n': n, 't_sec': t_sec,
                       'ax_g': acc_g[:, 0],
                       'ay_g': acc_g[:, 1],
                       'az_g': acc_g[:, 2]})
    if csv_path:
        df.to_csv(csv_path, index=False)
    return header, df


if __name__ == '__main__':
    import argparse
    p = argparse.ArgumentParser(description='Convert ACCLOG.BIN to CSV')
    p.add_argument('bin_file', type=Path, help='input .bin file')
    p.add_argument('--csv', action='store_true', help='also output CSV file')
    args = p.parse_args()
    out_csv = args.bin_file.with_suffix('.csv') if args.csv else None
    header, _df = bin_to_csv(args.bin_file, out_csv)
    for k, v in header.items():
        print(f'{k}: {v}')
    if out_csv:
        print(f'CSV written: {out_csv}')
