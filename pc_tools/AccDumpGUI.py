import threading
import tkinter as tk
from tkinter import ttk, filedialog, messagebox
from pathlib import Path
import traceback
import subprocess
import sys

from serial_common import list_serial_ports, dump_bin, get_info
import decoder


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

    def _dump_worker(self, port: str, out_dir: Path):
        try:
            # Query INFO and log FS usage + estimated remaining time
            try:
                info = get_info(port)
                odr = int(info.get('odr', 200))
                fs_total = int(info.get('fs_total', 0))
                fs_used = int(info.get('fs_used', 0))
                fs_free = int(info.get('fs_free', 0))
                fs_used_pct = int(info.get('fs_used_pct', 0))
                # 6 channels * int16 = 12 bytes per sample (acc+gyro)
                bytes_per_sec = 12 * max(1, odr)
                est_sec = fs_free / bytes_per_sec if bytes_per_sec > 0 else 0
                est_min = est_sec / 60.0
                self._append_log(
                    f'FS used {fs_used_pct}% ({fs_used}B / {fs_total}B), '
                    f'est {est_min:.1f} min at {odr}Hz'
                )
            except Exception as e:
                self._append_log(f'INFO取得失敗: {e}')
            out_file = out_dir / 'ACCLOG.bin'
            def cb(read_bytes, total_bytes):
                self.progress['maximum'] = total_bytes
                self.progress['value'] = read_bytes
            dump_bin(port, out_file, progress_cb=cb, log_cb=self._append_log)
            self._append_log(f'DUMP完了: {out_file}')
            if self.csv_var.get():
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
