import argparse
from pathlib import Path

from serial_common import list_serial_ports, dump_bin
import decoder


def dump_one(port: str, out_dir: Path, do_csv: bool):
    out_dir.mkdir(parents=True, exist_ok=True)
    out_file = out_dir / f'{port.replace("/", "_")}_ACCLOG.bin'
    print(f'Dumping {port} -> {out_file}')
    def cb(read_bytes, total_bytes):
        pct = 100 * read_bytes / total_bytes if total_bytes else 0
        print(f'\r{pct:5.1f}% ({read_bytes}/{total_bytes}B)', end='', flush=True)
    meta = dump_bin(port, out_file, progress_cb=cb)
    baud = meta.get('baud') if isinstance(meta, dict) else None
    print(f"\nDONE" + (f" (baud={baud})" if baud else ""))
    if do_csv:
        csv_path = out_file.with_suffix('.csv')
        decoder.bin_to_csv(out_file, csv_path)
        print(f'CSV written: {csv_path}')


def main():
    p = argparse.ArgumentParser(description='M5Stick ACCLOG dumper')
    p.add_argument('--port', help='serial port to use')
    p.add_argument('--all', action='store_true', help='dump from all available ports')
    p.add_argument('--out', type=Path, default=Path('.'), help='output directory')
    p.add_argument('--csv', action='store_true', help='convert to CSV after dump')
    args = p.parse_args()

    if args.all:
        ports = list_serial_ports()
    elif args.port:
        ports = [args.port]
    else:
        p.error('specify --port or --all')

    for port in ports:
        dump_one(port, args.out, args.csv)


if __name__ == '__main__':
    main()
