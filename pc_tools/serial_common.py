import serial
import errno
from serial.tools import list_ports
from pathlib import Path
from typing import Optional, Callable
import json
from info_format import enrich_info_defaults

BAUDRATE = 115_200
"""Default preferred baud rate for high-speed dump."""

# Fallback candidates (fast -> slow). The code will auto-try these.
# memo: M5stickの個体によっては115200しか動作せず、そのため115200に固定
# CANDIDATE_BAUDRATES = [1_500_000, 921_600, 460_800, 230_400, 115_200]
CANDIDATE_BAUDRATES = [115_200]
TIMEOUT = 15
MAX_ATTEMPTS = 2
HEADER_MAX_ATTEMPTS = 4096


def list_serial_ports():
    """Return a list of available serial port device names."""
    return [p.device for p in list_ports.comports()]


def open_serial(port: str, attempts: int = 5, delay_sec: float = 0.5, baudrate: int = BAUDRATE, log_cb: Optional[Callable[[str], None]] = None):
    """Open a serial connection with retries and safe defaults.

    Retries are helpful when the device is in the middle of a reset or the
    OS hasn't finished enumerating the port yet.
    """
    import time
    last_exc: Optional[Exception] = None
    for i in range(1, max(1, attempts) + 1):
        try:
            if log_cb:
                log_cb(f'[open] Attempt {i}/{attempts}: port={port} baud={baudrate}')
            kwargs = dict(timeout=TIMEOUT, write_timeout=TIMEOUT)
            # Strategy: open with a constructed Serial() so we can tweak control
            # lines immediately after open, then allow the device a short settle
            # period (many ESP32 boards reset on open due to DTR/RTS toggling).
            s = serial.Serial()
            s.port = port
            s.baudrate = baudrate
            s.timeout = TIMEOUT
            s.write_timeout = TIMEOUT
            # Disable flow control
            s.dsrdtr = False
            s.rtscts = False
            s.xonxoff = False
            # Best effort: request non-exclusive where supported
            try:
                # pyserial only accepts 'exclusive' at constructor time, but some
                # builds expose it as attribute; ignore if unsupported.
                setattr(s, 'exclusive', False)
            except Exception:
                pass
            # Pre-set control lines low (may be applied only after open on some OS)
            try:
                s.dtr = False
                s.rts = False
            except Exception:
                pass
            s.open()
            # Ensure DTR/RTS are deasserted to avoid boot strap/reset
            try:
                s.dtr = False
                s.rts = False
            except Exception:
                pass
            # Give the MCU time to finish any auto-reset/boot prints
            try:
                import time as _time
                if log_cb:
                    log_cb('[open] Settling 2.0s and draining input')
                _time.sleep(2.0)
                s.reset_input_buffer()
            except Exception:
                pass
            if log_cb:
                try:
                    log_cb(f'[open] Opened OK: in_waiting={getattr(s, "in_waiting", 0)}')
                except Exception:
                    log_cb('[open] Opened OK')
            return s
        except Exception as exc:
            last_exc = exc
            if log_cb:
                log_cb(f'[open] Failed attempt {i}: {exc!r}')
            # Fallback: if Input/output error during DTR handling, open with DTR/RTS disabled
            try:
                msg = str(exc)
                is_eio = getattr(exc, 'errno', None) in (5,) or 'Input/output error' in msg
                if is_eio:
                    if log_cb:
                        log_cb('[open] Using I/O-error fallback path')
                    s = serial.Serial()
                    s.port = port
                    s.baudrate = baudrate
                    s.timeout = TIMEOUT
                    s.write_timeout = TIMEOUT
                    # Disable handshakes
                    s.dsrdtr = False
                    s.rtscts = False
                    s.xonxoff = False
                    # Pre-set control lines low before open to avoid reset
                    try:
                        s.dtr = False
                        s.rts = False
                    except Exception:
                        pass
                    s.open()
                    try:
                        import time as _time
                        if log_cb:
                            log_cb('[open] (fallback) Settling 2.0s and draining input')
                        _time.sleep(2.0)
                        s.reset_input_buffer()
                    except Exception:
                        pass
                    if log_cb:
                        log_cb('[open] (fallback) Opened OK')
                    return s
            except Exception:
                pass
            if i < attempts:
                if log_cb:
                    log_cb(f'[open] Retry after {delay_sec}s')
                time.sleep(delay_sec)
    # All attempts exhausted: raise the last exception with helpful guidance
    assert last_exc is not None
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
    if getattr(last_exc, 'errno', None) in (5,) or 'Input/output error' in msg:
        raise serial.SerialException(
            5,
            (
                f"could not open port {port}: I/O error.\n"
                "Tips: unplug/replug the device; ensure Arduino IDE Serial Monitor is closed;\n"
                "check which process holds the port with 'lsof /dev/ttyUSB*';\n"
                "on Linux, stop ModemManager/brltty if present;\n"
                "if the board resets on open, disabling DTR/RTS (already attempted) usually helps."
            ),
        ) from last_exc
    raise last_exc


def _try_ping(ser: serial.Serial, log_cb: Optional[Callable[[str], None]] = None, attempts: int = 5, timeout_sec: float = 1.0) -> bool:
    """Try PING/PONG handshake to confirm link is alive at this baud."""
    import time as _time
    # Temporarily shorten timeout for snappy handshake
    try:
        orig_timeout = ser.timeout
        ser.timeout = timeout_sec
    except Exception:
        orig_timeout = None
    try:
        for i in range(1, max(1, attempts) + 1):
            try:
                ser.reset_input_buffer()
            except Exception:
                pass
            if log_cb:
                log_cb(f'[ping] Attempt {i}/{attempts}')
            try:
                ser.write(b'PING\n')
                ser.flush()
            except Exception as exc:
                if log_cb:
                    log_cb(f'[ping] write failed: {exc!r}')
                _time.sleep(0.1)
                continue
            line = ser.readline().decode('ascii', errors='ignore').strip()
            if log_cb:
                log_cb(f'[ping] Received: {line!r}')
            if line == 'PONG':
                return True
            _time.sleep(0.2)
        return False
    finally:
        try:
            if orig_timeout is not None:
                ser.timeout = orig_timeout
        except Exception:
            pass


def _dump_bin_impl(port: str, out_path: Path, baud: int, progress_cb=None, log_cb=None):
    """Single-baud dump implementation. Returns metadata dict on success."""
    with open_serial(port, baudrate=baud, log_cb=log_cb) as ser:
        if log_cb:
            log_cb(f'[dump] Opened {port} at {baud} baud')
        ser.reset_input_buffer()
        if log_cb:
            log_cb('[dump] Input buffer reset')
        # Handshake to confirm link
        if not _try_ping(ser, log_cb=log_cb):
            if log_cb:
                log_cb('[dump] PING failed – treating this baud as unusable')
            raise RuntimeError('No PONG at this baud')
        # Debug: request file head hex if supported
        try:
            ser.write(b'HEAD\n')
            ser.flush()
            head_line = ser.readline().decode('ascii', errors='ignore').strip()
            if log_cb:
                if head_line.startswith('HEAD'):
                    log_cb(f'[dump] Device HEAD: {head_line[5:]}')
                else:
                    log_cb(f'[dump] HEAD response: {head_line!r}')
        except Exception:
            pass
        ser.write(b'DUMP\n')
        ser.flush()
        if log_cb:
            log_cb('[dump] Sent DUMP, waiting for OK <size> [now_ms]')
        # Read first response line and record PC receipt time
        first_line = ser.readline().decode('ascii', errors='ignore').strip()
        import time as _time
        pc_ok_rx_time = _time.time()
        if log_cb:
            log_cb(f'[dump] First line: {first_line!r}')
        if not first_line.startswith('OK'):
            raise RuntimeError(f'Unexpected response: {first_line!r}')
        # Accept both: "OK <size>" and "OK <size> <now_ms>"
        parts = first_line.split()
        device_now_ms = None
        try:
            total = int(parts[1])
            if len(parts) >= 3:
                try:
                    device_now_ms = int(parts[2])
                except ValueError:
                    device_now_ms = None
        except Exception as exc:
            raise RuntimeError(f'Failed to parse size from: {first_line!r}') from exc
        if log_cb:
            log_cb(f'[dump] total_bytes={total} device_now_ms={device_now_ms}')

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
                    log_cb('[dump] Waiting for ACCLOG header (ACCLOG\0\0)')
                for attempt in range(1, HEADER_MAX_ATTEMPTS + 1):
                    b = ser.read(1)
                    if not b:
                        if log_cb and (attempt == 1 or attempt % 64 == 0):
                            log_cb(f'[dump] Header read attempt {attempt}: no data yet')
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
                            log_cb('[dump] Found header, starting transfer')
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
                            f'[dump] Timeout waiting for header after {HEADER_MAX_ATTEMPTS} attempts; '
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
                            log_cb(f'[dump] Wrote {len(aligned)} bytes from preamble/window, {remaining} remaining')
                        if progress_cb:
                            progress_cb(total - remaining, total)




            READ_CHUNK = 32768  # larger chunk reduces syscall overhead
            while remaining > 0:
                chunk = ser.read(min(READ_CHUNK, remaining))
                if not chunk:
                    if log_cb:
                        log_cb('[dump] Timeout while receiving data')
                    raise RuntimeError('Timeout while receiving data')
                f.write(chunk)
                remaining -= len(chunk)
                if log_cb:
                    log_cb(f'[dump] Received {len(chunk)} bytes, {remaining} remaining')
                if progress_cb:
                    progress_cb(total - remaining, total)
        # read trailing DONE (some firmware versions send extra newlines)
        if log_cb:
            log_cb('[dump] Waiting for DONE trailer')
        tail = ''
        for attempt in range(1, MAX_ATTEMPTS + 1):
            tail = ser.readline().decode('ascii', errors='ignore').strip()
            if log_cb:
                log_cb(f'[dump] Trailer read attempt {attempt}: {tail!r}')
            if tail:
                break
        else:
            if log_cb:
                log_cb(f'[dump] Timeout waiting for DONE after {MAX_ATTEMPTS} attempts')
            raise RuntimeError('Timeout waiting for DONE')
        if tail != 'DONE':
            raise RuntimeError(f'Unexpected trailer: {tail!r}')
        if progress_cb:
            progress_cb(total, total)
        if log_cb:
            log_cb('[dump] Dump complete')
        # Return metadata for timestamp estimation
        return {
            'total_bytes': total,
            'device_now_ms': device_now_ms,
            'pc_ok_rx_time': pc_ok_rx_time,
            'baud': baud,
        }


def dump_bin(port: str, out_path: Path, progress_cb=None, log_cb=None):
    """Dump binary log with auto-baud selection.

    Tries CANDIDATE_BAUDRATES from fastest to slowest until successful.
    """
    last_exc: Optional[Exception] = None
    if log_cb:
        log_cb(f'[dump] Candidate baudrates: {CANDIDATE_BAUDRATES}')
    for baud in CANDIDATE_BAUDRATES:
        try:
            if log_cb:
                log_cb(f'[dump] Trying baud {baud}...')
            meta = _dump_bin_impl(port, out_path, baud, progress_cb, log_cb)
            if log_cb:
                log_cb(f'[dump] Succeeded at {baud} baud')
            return meta
        except Exception as exc:
            last_exc = exc
            if log_cb:
                log_cb(f'[dump] Failed at {baud} baud: {exc!r}')
            continue
    assert last_exc is not None
    raise last_exc


__all__ = ['list_serial_ports', 'open_serial', 'dump_bin', 'get_info']

def _get_info_impl(port: str, baud: int, log_cb: Optional[Callable[[str], None]] = None) -> dict:
    if log_cb:
        log_cb(f'[info] Opening {port} at {baud} baud')
    with open_serial(port, baudrate=baud, log_cb=log_cb) as ser:
        ser.reset_input_buffer()
        if log_cb:
            log_cb('[info] Input buffer reset')
        # Handshake to confirm link
        if not _try_ping(ser, log_cb=log_cb):
            if log_cb:
                log_cb('[info] PING failed – treating this baud as unusable')
            raise RuntimeError('No PONG at this baud')
        if log_cb:
            log_cb('[info] Sending INFO')
        # Use shorter timeout for snappy fallback when baud mismatched
        try:
            orig_timeout = ser.timeout
            ser.timeout = 2.0
        except Exception:
            orig_timeout = None
        line = ''
        try:
            for i in range(1, 4):
                ser.write(b'INFO\n')
                ser.flush()
                line = ser.readline().decode('ascii', errors='ignore').strip()
                if log_cb:
                    log_cb(f'[info] Attempt {i} received: {line!r}')
                if line.startswith('{') and line.endswith('}'):
                    break
                # small wait before retry
                import time as _time
                _time.sleep(0.2)
        finally:
            try:
                if orig_timeout is not None:
                    ser.timeout = orig_timeout
            except Exception:
                pass
        if log_cb:
            log_cb(f'[info] Received: {line!r}')
    data = json.loads(line)
    data['baud'] = baud
    return enrich_info_defaults(data)


def get_info(port: str, log_cb: Optional[Callable[[str], None]] = None) -> dict:
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
    last_exc: Optional[Exception] = None
    if log_cb:
        log_cb(f'[info] Candidate baudrates: {CANDIDATE_BAUDRATES}')
    for baud in CANDIDATE_BAUDRATES:
        try:
            if log_cb:
                log_cb(f'[info] Trying baud {baud}...')
            info = _get_info_impl(port, baud, log_cb=log_cb)
            if log_cb:
                log_cb(f'[info] Succeeded at {baud} baud')
            return info
        except Exception as exc:
            last_exc = exc
            if log_cb:
                log_cb(f'[info] Failed at {baud} baud: {exc!r}')
            continue
    assert last_exc is not None
    raise last_exc


__all__ = ['list_serial_ports', 'open_serial', 'dump_bin', 'get_info']
