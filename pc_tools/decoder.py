from pathlib import Path
import struct
import numpy as np
import pandas as pd

# Header formats
HEADER_FMT_V1 = '<8sHQQHHII26s'          # accel-only
HEADER_FMT_V2 = '<8sHQQHHHII24s'         # adds gyro_range_dps (uint16)
HEADER_PREFIX_FMT = '<8sHQQHH'           # common prefix up to range_g
HEADER_PREFIX_SIZE = struct.calcsize(HEADER_PREFIX_FMT)
HEADER_SIZE = 64
HEADER_FMT = HEADER_FMT_V2  # use v2 format for unpacking; v1 compatible


def parse_header(data: bytes) -> dict:
    if len(data) < HEADER_SIZE:
        raise ValueError('header too short')
    # common prefix
    magic, fmt_ver, device_uid, start_ms, odr, range_g = struct.unpack(
        HEADER_PREFIX_FMT, data[:HEADER_PREFIX_SIZE]
    )
    if not magic.startswith(b'ACCLOG'):
        raise ValueError('invalid magic')
    if fmt_ver >= 0x0200:
        (magic, fmt_ver, device_uid, start_ms, odr, range_g,
         gyro_range_dps, total_samples, dropped, _reserved) = struct.unpack(
            HEADER_FMT_V2, data[:HEADER_SIZE]
        )
    else:
        (magic, fmt_ver, device_uid, start_ms, odr, range_g,
         total_samples, dropped, _reserved) = struct.unpack(
            HEADER_FMT_V1, data[:HEADER_SIZE]
        )
        gyro_range_dps = 0
    return {
        'format_ver': fmt_ver,
        'device_uid': device_uid,
        'start_unix_ms': start_ms,
        'odr_hz': odr,
        'range_g': range_g,
        'gyro_range_dps': gyro_range_dps,
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
        Converted dataframe:
          - v1: columns n,t_sec,ax_g,ay_g,az_g
          - v2+: above + gx_dps,gy_dps,gz_dps
    """
    bin_path = Path(bin_path)
    with open(bin_path, 'rb') as f:
        buf = f.read()

    # Robust header alignment: search for ACCLOG magic within the file
    patterns = (b'ACCLOG\x00\x00', b'ACCLOG\x00', b'ACCLOG')
    idx = None
    for m in patterns:
        j = buf.find(m)
        if j != -1:
            idx = j if idx is None else min(idx, j)
    if idx is None:
        # Heuristic fallback: try decoding as headerless raw stream
        if len(buf) == 0:
            raise ValueError('empty file: no data and no ACCLOG header')
        # Try to find a small offset (<=4KB) that makes the length
        # compatible with 6ch or 3ch int16 samples.
        best = None
        max_scan = min(4096, len(buf))
        for off in range(max_scan):
            n16 = (len(buf) - off) // 2
            if n16 >= 3 and n16 % 6 == 0:
                best = (off, 6)
                break
            if n16 >= 3 and n16 % 3 == 0 and best is None:
                best = (off, 3)
        if best is None:
            preview = buf[:32].hex()
            raise ValueError(
                'invalid magic: ACCLOG header not found and length not compatible. '
                f'len={len(buf)} preview={preview}'
            )
        off, channels = best
        # Construct a synthetic header with sensible defaults
        header = {
            'format_ver': 0x0200 if channels == 6 else 0x0100,
            'device_uid': 0,
            'start_unix_ms': 0,
            'odr_hz': 200,
            'range_g': 4,
            'gyro_range_dps': 2000,
            'total_samples': 0,
            'dropped_samples': 0,
        }
        payload = buf[off:]
    else:
        if len(buf) - idx < HEADER_SIZE:
            raise ValueError('header too short after aligning to magic')
        header = parse_header(buf[idx: idx + HEADER_SIZE])
        payload = buf[idx + HEADER_SIZE:]

    # Ensure even number of bytes (int16-aligned); drop any trailing odd byte
    if len(payload) % 2 != 0:
        payload = payload[:-1]

    # Firmware writes MSB first (big-endian) for each int16
    raw = np.frombuffer(payload, dtype='>i2')

    # Determine channels per sample: v1=3 (acc), v2+=6 (acc+gyro)
    channels = 6 if header['format_ver'] >= 0x0200 else 3
    if raw.size % channels != 0:
        raw = raw[: (raw.size // channels) * channels]
    data = raw.reshape(-1, channels)

    # Accelerometer scaling
    lsb_per_g = 32768 / header['range_g']
    acc_g = data[:, :3] / lsb_per_g

    # Timebase
    n = np.arange(len(acc_g), dtype=np.int64)
    t_sec = n / header['odr_hz']

    # Build dataframe columns
    cols = {
        'n': n,
        't_sec': t_sec,
        'ax_g': acc_g[:, 0],
        'ay_g': acc_g[:, 1],
        'az_g': acc_g[:, 2],
    }
    if data.shape[1] == 6:
        # Firmware v2 stores gyro values as int16 cast from dps
        gyro_dps = data[:, 3:6].astype(np.float32)
        cols.update({
            'gx_dps': gyro_dps[:, 0],
            'gy_dps': gyro_dps[:, 1],
            'gz_dps': gyro_dps[:, 2],
        })

    df = pd.DataFrame(cols)
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
