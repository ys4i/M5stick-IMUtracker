"""Helpers for displaying INFO metadata (board/IMU/format/LSB etc.)."""

def format_info_line(info: dict) -> str:
    board = info.get('board', 'unknown')
    imu = info.get('imu', 'unknown')
    fmt = info.get('format', info.get('format_ver', ''))
    odr = info.get('odr') or info.get('odr_hz')
    rg = info.get('range_g')
    gdr = info.get('gyro_dps')
    fs_used_pct = info.get('fs_used_pct')
    lsb_g = info.get('lsb_per_g')
    lsb_dps = info.get('lsb_per_dps')
    parts = []
    parts.append(f"Board={board}")
    parts.append(f"IMU={imu}")
    if fmt:
        parts.append(f"Fmt={fmt}")
    if odr:
        parts.append(f"ODR={odr}Hz")
    if rg:
        parts.append(f"Accel=±{rg}g")
    if gdr:
        parts.append(f"Gyro=±{gdr}dps")
    if fs_used_pct is not None:
        parts.append(f"FS={fs_used_pct}%")
    if lsb_g:
        parts.append(f"LSB/g={lsb_g}")
    if lsb_dps:
        parts.append(f"LSB/dps={lsb_dps}")
    return ' '.join(parts)

def enrich_info_defaults(info: dict) -> dict:
    def _board_from_model(dm: int | None) -> str:
        if dm == 1: return 'stickc'
        if dm == 2: return 'stickc_plus'
        if dm == 3: return 'plus2'
        if dm in (10, 11): return 'core2'
        return 'unknown'

    def _imu_from_type(it: int | None) -> str:
        if it == 1: return 'sh200q'
        if it == 2: return 'mpu6886'
        return 'unknown'

    fmt_ver = info.get('format_ver')
    if 'format' not in info or not info.get('format'):
        if fmt_ver is not None:
            try:
                info['format'] = f"0x{int(fmt_ver):04X}"
            except Exception:
                info['format'] = 'unknown'
        else:
            info['format'] = 'unknown'
    info.setdefault('board', _board_from_model(info.get('device_model')))
    info.setdefault('imu', _imu_from_type(info.get('imu_type')))
    # Fill LSBs if possible
    if 'lsb_per_g' not in info or info.get('lsb_per_g') in (None, 0, 0.0):
        try:
            rg = float(info.get('range_g', 0))
            info['lsb_per_g'] = 32768.0 / rg if rg else None
        except Exception:
            info['lsb_per_g'] = None
    if 'lsb_per_dps' not in info or info.get('lsb_per_dps') in (None, 0, 0.0):
        try:
            gd = float(info.get('gyro_dps', 0))
            info['lsb_per_dps'] = 32768.0 / gd if gd else None
        except Exception:
            info['lsb_per_dps'] = None
    return info
