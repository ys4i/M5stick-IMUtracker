"""Helpers for displaying INFO metadata (board/IMU/format/LSB etc.)."""

def format_info_line(info: dict) -> str:
    board = info.get('board', 'unknown')
    imu = info.get('imu', 'unknown')
    fmt = info.get('format', info.get('format_ver', ''))
    odr = info.get('odr') or info.get('odr_hz')
    rg = info.get('range_g')
    gdr = info.get('gyro_dps')
    fs_used_pct = info.get('fs_used_pct')
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
    return ' '.join(parts)

def enrich_info_defaults(info: dict) -> dict:
    info.setdefault('board', 'unknown')
    info.setdefault('imu', 'unknown')
    info.setdefault('format', info.get('format_ver', 'unknown'))
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

