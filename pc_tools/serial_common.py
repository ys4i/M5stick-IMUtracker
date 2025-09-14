import serial
from serial.tools import list_ports
from pathlib import Path

BAUDRATE = 115200
TIMEOUT = 5


def list_serial_ports():
    """Return a list of available serial port device names."""
    return [p.device for p in list_ports.comports()]


def open_serial(port: str):
    """Open a serial connection with standard settings."""
    return serial.Serial(port, BAUDRATE, timeout=TIMEOUT)


def dump_bin(port: str, out_path: Path, progress_cb=None):
    """Dump binary log from device connected to *port* into *out_path*.

    Parameters
    ----------
    port : str
        Serial port name.
    out_path : Path
        File path where downloaded binary will be written.
    progress_cb : callable, optional
        Called as ``progress_cb(read_bytes, total_bytes)`` during transfer.
    """
    with open_serial(port) as ser:
        ser.reset_input_buffer()
        ser.write(b'DUMP\n')
        ser.flush()
        first_line = ser.readline().decode('ascii', errors='ignore').strip()
        if not first_line.startswith('OK'):
            raise RuntimeError(f'Unexpected response: {first_line!r}')
        try:
            total = int(first_line.split()[1])
        except Exception as exc:
            raise RuntimeError(f'Failed to parse size from: {first_line!r}') from exc

        remaining = total
        out_path.parent.mkdir(parents=True, exist_ok=True)
        with open(out_path, 'wb') as f:
            # Some firmware revisions prepend extra text or blank lines before
            # the binary payload. Read byte-by-byte until the header magic
            # appears so the saved file always starts with "ACCLOG\0".
            if remaining > 0:
                magic = b'ACCLOG\0'
                window = bytearray()
                while True:
                    b = ser.read(1)
                    if not b:
                        raise RuntimeError('Timeout while waiting for header')
                    window += b
                    if len(window) > len(magic):
                        del window[0]
                    if window.endswith(magic):
                        break
                f.write(magic)
                remaining -= len(magic)
                if progress_cb:
                    progress_cb(total - remaining, total)

            while remaining > 0:
                chunk = ser.read(min(4096, remaining))
                if not chunk:
                    raise RuntimeError('Timeout while receiving data')
                f.write(chunk)
                remaining -= len(chunk)
                if progress_cb:
                    progress_cb(total - remaining, total)
        # read trailing DONE (some firmware versions send extra newlines)
        tail = ''
        while tail == '':
            tail = ser.readline().decode('ascii', errors='ignore').strip()
        if tail != 'DONE':
            raise RuntimeError(f'Unexpected trailer: {tail!r}')
        if progress_cb:
            progress_cb(total, total)


__all__ = ['list_serial_ports', 'open_serial', 'dump_bin']
