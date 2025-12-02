import threading
import tkinter as tk
from tkinter import ttk, filedialog, messagebox
from pathlib import Path
import traceback
import subprocess
import sys
import errno
import serial

from serial_common import list_serial_ports, dump_bin, get_info
import decoder
from info_format import format_info_line


class AccDumpGUI(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title('AccDumpGUI')
        self.resizable(False, False)
        self.last_csv_path: Path | None = None
        self._build_ui()
        self.refresh_ports()

    def _build_ui(self):
        frame = ttk.Frame(self, padding=10)
        frame.grid(row=0, column=0)

        ttk.Label(frame, text='Serial Port').grid(row=0, column=0, sticky='w')
        self.port_var = tk.StringVar()
        self.port_combo = ttk.Combobox(frame, textvariable=self.port_var, width=20)
        self.port_combo.grid(row=0, column=1)
        ttk.Button(frame, text='Refresh', command=self.refresh_ports).grid(row=0, column=2, padx=5)

        self.csv_var = tk.BooleanVar(value=True)
        ttk.Checkbutton(frame, text='CSVへ変換', variable=self.csv_var).grid(row=1, column=0, columnspan=3, sticky='w')

        self.progress = ttk.Progressbar(frame, orient='horizontal', length=300)
        self.progress.grid(row=2, column=0, columnspan=3, pady=5)

        self.log = tk.Text(frame, width=50, height=10)
        self.log.grid(row=3, column=0, columnspan=3, pady=5)

        ttk.Button(frame, text='DUMP', command=self.start_dump).grid(row=4, column=0, columnspan=3)
        ttk.Button(frame, text='グラフ表示', command=self.plot_csv).grid(row=5, column=0, columnspan=3, pady=(4, 0))

    def refresh_ports(self):
        ports = list_serial_ports()
        self.port_combo['values'] = ports
        if ports:
            self.port_combo.current(0)

    def start_dump(self):
        port = self.port_var.get()
        if not port:
            messagebox.showerror('Error', 'ポートを選択してください')
            return
        out_dir = filedialog.askdirectory(title='保存先フォルダ')
        if not out_dir:
            return
        self.progress['value'] = 0
        self.log.delete('1.0', tk.END)
        thread = threading.Thread(target=self._dump_worker,
                                  args=(port, Path(out_dir)),
                                  daemon=True)
        thread.start()

    def _append_log(self, msg: str):
        self.log.insert(tk.END, msg + '\n')
        self.log.see(tk.END)

    def _is_busy_serial_error(self, exc: Exception) -> bool:
        msg = str(exc)
        err = getattr(exc, 'errno', None)
        return err in (errno.EBUSY, 16) or 'device or resource busy' in msg.lower() or 'device is busy' in msg.lower()

    def _warn_busy_port(self, port: str, exc: Exception):
        msg = str(exc)
        advice = (
            f'{port} を開けませんでした。他のプロセスが使用中の可能性があります。\n'
            'Arduino IDE のシリアルモニタ、screen/minicom/picocom を閉じてください。\n'
            "占有プロセス確認: lsof /dev/ttyUSB* または fuser /dev/ttyUSB*"
        )
        self._append_log(f'ポートが使用中: {msg}')
        self._append_log(advice.replace('\n', ' '))
        # TkのメッセージボックスはUIスレッドで呼ぶ
        self.after(0, lambda: messagebox.showwarning('ポートが使用中', f'{msg}\n\n{advice}'))

    def _dump_worker(self, port: str, out_dir: Path):
        try:
            # Query INFO and log FS usage + estimated remaining time
            try:
                self._append_log('INFO問い合わせ開始')
                info = get_info(port, log_cb=self._append_log)
                self._append_log(format_info_line(info))
                odr = int(info.get('odr', 200))
                fs_total = int(info.get('fs_total', 0))
                fs_used = int(info.get('fs_used', 0))
                fs_free = int(info.get('fs_free', 0))
                fs_used_pct = int(info.get('fs_used_pct', 0))
                has_head = info.get('has_head')
                info_uid_str = info.get('uid')
                info_uid = None
                if isinstance(info_uid_str, str) and info_uid_str.startswith('0x'):
                    try:
                        info_uid = int(info_uid_str, 16)
                    except ValueError:
                        info_uid = None
                # 6 channels * int16 = 12 bytes per sample (acc+gyro)
                bytes_per_sec = 12 * max(1, odr)
                est_sec = fs_free / bytes_per_sec if bytes_per_sec > 0 else 0
                est_min = est_sec / 60.0
                self._append_log(
                    f'FS used {fs_used_pct}% ({fs_used}B / {fs_total}B), '
                    f'est {est_min:.1f} min at {odr}Hz'
                )
                if 'baud' in info:
                    self._append_log(f'INFO成功: baud={info["baud"]}')
                if has_head is not None:
                    self._append_log(f'has_head={has_head}')
            except serial.SerialException as e:
                if self._is_busy_serial_error(e):
                    self._warn_busy_port(port, e)
                    return
                self._append_log(f'INFO取得失敗: {e}')
            except Exception as e:
                self._append_log(f'INFO取得失敗: {e}')
            out_file = out_dir / 'ACCLOG.bin'
            self._append_log(f'DUMP開始: 出力先={out_file}')
            last_logged_pct = {'p': -1}
            def cb(read_bytes, total_bytes):
                self.progress['maximum'] = total_bytes
                self.progress['value'] = read_bytes
                try:
                    pct = int(100 * read_bytes / total_bytes) if total_bytes else 0
                except Exception:
                    pct = 0
                # ログは1%刻みで出力
                if pct != last_logged_pct['p']:
                    last_logged_pct['p'] = pct
                    self._append_log(f'進捗: {pct}% ({read_bytes}/{total_bytes}B)')
            try:
                meta = dump_bin(port, out_file, progress_cb=cb, log_cb=self._append_log)
            except serial.SerialException as e:
                if self._is_busy_serial_error(e):
                    self._warn_busy_port(port, e)
                    return
                raise
            self._append_log(f'DUMP完了: {out_file}')

            # 推定開始時刻でファイル名をリネーム（補正なしB案）
            try:
                header, df_preview = decoder.bin_to_csv(out_file, None)
                device_start_ms = int(header.get('start_unix_ms', 0))
                odr_hz = int(header.get('odr_hz', 0)) or odr
                uid = int(header.get('device_uid', 0)) or (info_uid or 0)
                fmt_ver = int(header.get('format_ver', 0))
                header_found = bool(header.get('header_found', False))
                header_offset = int(header.get('header_offset', -1))
                device_now_ms = meta.get('device_now_ms') if isinstance(meta, dict) else None
                pc_ok_rx_time = meta.get('pc_ok_rx_time') if isinstance(meta, dict) else None

                # デバッグ出力
                self._append_log(
                    'DEBUG: '
                    f'header_found={header_found} offset={header_offset} '
                    f'fmt_ver=0x{fmt_ver:04X} uid=0x{uid:016X} '
                    f'start_unix_ms={device_start_ms} odr_hz={odr_hz}'
                )
                try:
                    with open(out_file, 'rb') as _f:
                        _head = _f.read(32)
                    self._append_log(f'DEBUG: file_head_hex={_head.hex()}')
                except Exception as _e:
                    self._append_log(f'DEBUG: file_head_hex: read failed: {_e}')
                if device_now_ms is not None and pc_ok_rx_time is not None:
                    self._append_log(
                        f'DEBUG: device_now_ms={device_now_ms} pc_ok_rx_time={pc_ok_rx_time:.6f}'
                    )

                new_base = None
                if device_now_ms is not None and pc_ok_rx_time is not None and device_start_ms > 0:
                    import datetime
                    delta_ms = max(0, device_now_ms - device_start_ms)
                    self._append_log(f'DEBUG: delta_ms(now-start)={delta_ms}')
                    start_epoch = pc_ok_rx_time - (delta_ms / 1000.0)
                    dt = datetime.datetime.fromtimestamp(start_epoch)
                    msec = dt.microsecond // 1000
                    ts = f"{dt:%Y-%m-%d_%H-%M-%S}.{msec:03d}"
                    uid_hex = f"{uid:016X}"
                    new_base = f"ACC_{ts}_UID{uid_hex}_ODR{odr_hz}"
                elif device_now_ms is not None and pc_ok_rx_time is not None and (not header_found or device_start_ms == 0):
                    # フォールバック: ヘッダが無い場合、録画時間=サンプル数/ODRから開始時刻を推定
                    import datetime
                    try:
                        if df_preview is not None and not df_preview.empty:
                            # t_sec は 0 から始まるので末尾が録画時間（秒）
                            duration_sec = float(df_preview['t_sec'].iloc[-1])
                        else:
                            # データ数から（6ch*2B=12B/サンプル）
                            file_size = out_file.stat().st_size
                            # 先頭ヘッダが無い場合、全体がペイロード
                            samples = (file_size // 12)
                            duration_sec = samples / max(1, odr_hz)
                        start_epoch = pc_ok_rx_time - duration_sec
                        dt = datetime.datetime.fromtimestamp(start_epoch)
                        msec = dt.microsecond // 1000
                        ts = f"{dt:%Y-%m-%d_%H-%M-%S}.{msec:03d}"
                        uid_hex = f"{uid:016X}"
                        new_base = f"ACC_{ts}_UID{uid_hex}_ODR{odr_hz}"
                        self._append_log(f'DEBUG: fallback duration_sec={duration_sec:.3f}')
                    except Exception as _fe:
                        self._append_log(f'DEBUG: fallback失敗: {_fe}')
                else:
                    self._append_log('開始時刻推定に必要な情報が不足: 既定名のまま保存')

                if new_base:
                    new_bin = out_dir / f"{new_base}.bin"
                    # 衝突回避
                    counter = 1
                    while new_bin.exists():
                        new_bin = out_dir / f"{new_base}_{counter}.bin"
                        counter += 1
                    out_file.rename(new_bin)
                    out_file = new_bin
                    self._append_log(f'リネーム: {out_file.name}')
            except Exception as e:
                self._append_log(f'リネーム処理失敗: {e}')

            if self.csv_var.get():
                self._append_log('CSV変換開始')
                csv_path = out_file.with_suffix('.csv')
                decoder.bin_to_csv(out_file, csv_path)
                self._append_log(f'CSV変換完了: {csv_path}')
                self.last_csv_path = csv_path
        except Exception:
            self._append_log('エラー:\n' + traceback.format_exc())

    def plot_csv(self):
        try:
            csv_path: Path | None = None
            if self.last_csv_path and self.last_csv_path.exists():
                use_last = messagebox.askyesno('グラフ表示', f'直近のCSVを表示しますか？\n{self.last_csv_path}')
                if use_last:
                    csv_path = self.last_csv_path
            if csv_path is None:
                chosen = filedialog.askopenfilename(
                    title='CSVファイルを選択',
                    filetypes=[('CSV files', '*.csv'), ('All files', '*.*')]
                )
                if not chosen:
                    return
                csv_path = Path(chosen)

            script = Path(__file__).parent / 'plot_csv.py'
            if not script.exists():
                messagebox.showerror('Error', f'plot_csv.py が見つかりません: {script}')
                return
            cmd = [sys.executable, str(script), str(csv_path)]
            subprocess.Popen(cmd)
            self._append_log(f'グラフ起動: {csv_path}')
        except Exception:
            self._append_log('エラー(グラフ表示):\n' + traceback.format_exc())


if __name__ == '__main__':
    app = AccDumpGUI()
    app.mainloop()
