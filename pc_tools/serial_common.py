import serial
from serial.tools import list_ports
from pathlib import Path

BAUDRATE = 115200
TIMEOUT = 5
MAX_ATTEMPTS = 10
HEADER_MAX_ATTEMPTS = 256


def list_serial_ports():
    """Return a list of available serial port device names."""
    return [p.device for p in list_ports.comports()]


def open_serial(port: str):
    """Open a serial connection with standard settings."""
    return serial.Serial(port, BAUDRATE, timeout=TIMEOUT)


def dump_bin(port: str, out_path: Path, progress_cb=None, log_cb=None):
    """Dump binary log from device connected to *port* into *out_path*.

    Parameters
    ----------
    port : str
        Serial port name.
    out_path : Path
        File path where downloaded binary will be written.
    progress_cb : callable, optional
        Called as ``progress_cb(read_bytes, total_bytes)`` during transfer.
    log_cb : callable, optional
        Called as ``log_cb(message: str)`` for debug logging.
    """
    with open_serial(port) as ser:
        if log_cb:
            log_cb(f'Opened {port} at {BAUDRATE} baud')
        ser.reset_input_buffer()
        ser.write(b'DUMP\n')
        ser.flush()
        if log_cb:
            log_cb('Sent DUMP command, waiting for response')
        first_line = ser.readline().decode('ascii', errors='ignore').strip()
        if log_cb:
            log_cb(f'Received line: {first_line!r}')
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
            # the binary payload. Read byte-by-byte until the full 8-byte
            # header magic appears so the saved file always starts with
            # the exact bytes from the device ("ACCLOG\0\0").
            if remaining > 0:
                magic = b'ACCLOG\0\0'  # full 8-byte magic from firmware header
                window = bytearray()
                preamble = bytearray()
                if log_cb:
                    log_cb('Waiting for ACCLOG header')
                for attempt in range(1, HEADER_MAX_ATTEMPTS + 1):
                    b = ser.read(1)
                    if not b:
                        if log_cb:
                            log_cb(f'Header read attempt {attempt} yielded no data')
                        continue
                    window += b
                    if len(window) > len(magic):
                        preamble.append(window[0])
                        del window[0]
                    if window.endswith(magic):
                        break
                else:
                    if log_cb:
                        log_cb(
                            f'Timeout while waiting for header after {HEADER_MAX_ATTEMPTS} attempts'
                        )
                    raise RuntimeError('Timeout while waiting for header')
                if preamble and log_cb:
                    log_cb(f'Skipped preamble bytes: {preamble.hex()}')
                if log_cb:
                    log_cb('Found header, starting transfer')
                # Write the exact bytes we just matched (8-byte magic)
                f.write(window)
                remaining -= len(window)
                if progress_cb:
                    progress_cb(total - remaining, total)

            while remaining > 0:
                chunk = ser.read(min(4096, remaining))
                if not chunk:
                    if log_cb:
                        log_cb('Timeout while receiving data')
                    raise RuntimeError('Timeout while receiving data')
                f.write(chunk)
                remaining -= len(chunk)
                if log_cb:
                    log_cb(f'Received {len(chunk)} bytes, {remaining} remaining')
                if progress_cb:
                    progress_cb(total - remaining, total)
        # read trailing DONE (some firmware versions send extra newlines)
        if log_cb:
            log_cb('Waiting for DONE trailer')
        tail = ''
        for attempt in range(1, MAX_ATTEMPTS + 1):
            tail = ser.readline().decode('ascii', errors='ignore').strip()
            if log_cb:
                log_cb(f'Trailer read attempt {attempt}: {tail!r}')
            if tail:
                break
        else:
            if log_cb:
                log_cb(f'Timeout waiting for DONE after {MAX_ATTEMPTS} attempts')
            raise RuntimeError('Timeout waiting for DONE')
        if tail != 'DONE':
            raise RuntimeError(f'Unexpected trailer: {tail!r}')
        if progress_cb:
            progress_cb(total, total)
        if log_cb:
            log_cb('Dump complete')


__all__ = ['list_serial_ports', 'open_serial', 'dump_bin']
