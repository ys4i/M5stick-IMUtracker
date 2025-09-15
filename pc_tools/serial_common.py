import serial
import errno
from serial.tools import list_ports
from pathlib import Path
from typing import Optional
import json

BAUDRATE = 115200
TIMEOUT = 15
MAX_ATTEMPTS = 10
HEADER_MAX_ATTEMPTS = 4096


def list_serial_ports():
    """Return a list of available serial port device names."""
    return [p.device for p in list_ports.comports()]


def open_serial(port: str, attempts: int = 5, delay_sec: float = 0.5):
    """Open a serial connection with retries and safe defaults.

    Retries are helpful when the device is in the middle of a reset or the
    OS hasn't finished enumerating the port yet.
    """
    import time
    last_exc: Optional[Exception] = None
    for _ in range(max(1, attempts)):
        try:
            kwargs = dict(timeout=TIMEOUT, write_timeout=TIMEOUT)
            try:
                # Linux: avoid exclusive lock to reduce EIO/EBUSY on some setups
                ser = serial.Serial(port, BAUDRATE, exclusive=False, **kwargs)
            except TypeError:
                # Older pyserial without 'exclusive' kwarg
                ser = serial.Serial(port, BAUDRATE, **kwargs)
            return ser
        except Exception as exc:
            last_exc = exc
            time.sleep(delay_sec)
    assert last_exc is not None
    # Provide actionable message for busy device errors
    msg = str(last_exc)
    busy = (
        getattr(last_exc, 'errno', None) in (errno.EBUSY, 16)
        or 'Device or resource busy' in msg
    )
    if busy:
        raise serial.SerialException(
            errno.EBUSY,
            (
                f"could not open port {port}: device is busy.\n"
                "Close other serial monitors (Arduino IDE Serial Monitor, screen, minicom, picocom),\n"
                "or stop services like ModemManager/brltty if present.\n"
                "Tip: check with 'lsof /dev/ttyUSB*' or 'fuser /dev/ttyUSB*'."
            ),
        ) from last_exc
    raise last_exc


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
            # the binary payload. Read byte-by-byte until the header magic
            # appears so the saved file always starts with the exact bytes
            # from the device.
            if remaining > 0:
                magic8 = b'ACCLOG\0\0'  # preferred full 8-byte magic
                magic7 = b'ACCLOG\0'    # accept 7-byte variant for older logs
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
                    if len(window) > len(magic8):
                        preamble.append(window[0])
                        del window[0]
                    # Match either 8-byte or 7-byte magic
                    if window.endswith(magic8) or (len(window) >= 7 and window[-7:] == magic7):
                        if preamble and log_cb:
                            log_cb(f'Skipped preamble bytes: {preamble.hex()}')
                        if log_cb:
                            log_cb('Found header, starting transfer')
                        # Write the exact bytes we just matched (7 or 8 bytes)
                        f.write(window)
                        remaining -= len(window)
                        if progress_cb:
                            progress_cb(total - remaining, total)
                        break
                else:
                    # Fallback: could not detect header; proceed to raw copy
                    if log_cb:
                        log_cb(
                            f'Timeout while waiting for header after {HEADER_MAX_ATTEMPTS} attempts; '
                            'falling back to raw stream copy'
                        )
                    # Bytes consumed during header search
                    consumed = bytes(preamble) + bytes(window)
                    if consumed:
                        # Try to align to header within consumed bytes so file starts at header if present
                        magic8 = b'ACCLOG\x00\x00'
                        magic7 = b'ACCLOG\x00'
                        idx = -1
                        for m in (magic8, magic7):
                            j = consumed.find(m)
                            if j != -1:
                                idx = j
                                break
                        aligned = consumed[idx:] if idx != -1 else consumed
                        f.write(aligned)
                        remaining -= len(aligned)
                        if log_cb:
                            log_cb(f'Wrote {len(aligned)} bytes from preamble/window, {remaining} remaining')
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
def get_info(port: str) -> dict:
    """Query device INFO JSON on the given serial port.

    Returns a dict like:
      {
        'odr': 200,
        'range_g': 4,
        'gyro_dps': 2000,
        'file_size': 0,
        'fs_total': 0,
        'fs_used': 0,
        'fs_free': 0,
        'fs_used_pct': 0,
      }
    """
    with open_serial(port) as ser:
        ser.reset_input_buffer()
        ser.write(b'INFO\n')
        ser.flush()
        line = ser.readline().decode('ascii', errors='ignore').strip()
    try:
        return json.loads(line)
    except json.JSONDecodeError as exc:
        raise RuntimeError(f'Failed to parse INFO response: {line!r}') from exc


__all__ = ['list_serial_ports', 'open_serial', 'dump_bin', 'get_info']
