import threading
import tkinter as tk
from tkinter import ttk, filedialog, messagebox
from pathlib import Path

from serial_common import list_serial_ports, dump_bin
import decoder


class AccDumpGUI(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title('AccDumpGUI')
        self.resizable(False, False)
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

    def _dump_worker(self, port: str, out_dir: Path):
        try:
            out_file = out_dir / 'ACCLOG.bin'
            def cb(read_bytes, total_bytes):
                self.progress['maximum'] = total_bytes
                self.progress['value'] = read_bytes
            dump_bin(port, out_file, progress_cb=cb)
            self.log.insert(tk.END, f'DUMP完了: {out_file}\n')
            if self.csv_var.get():
                csv_path = out_file.with_suffix('.csv')
                decoder.bin_to_csv(out_file, csv_path)
                self.log.insert(tk.END, f'CSV変換完了: {csv_path}\n')
        except Exception as e:
            self.log.insert(tk.END, f'エラー: {e}\n')


if __name__ == '__main__':
    app = AccDumpGUI()
    app.mainloop()
