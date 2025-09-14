# Build Instructions

The GUI and CLI tools are pure Python scripts. To distribute stand-alone binaries
for Windows or macOS, [PyInstaller](https://pyinstaller.org/) is used.

## Setup

```bash
python -m pip install -r requirements.txt pyinstaller
```

## Build GUI

```bash
pyinstaller --onefile --windowed AccDumpGUI.py
```

This command produces `dist/AccDumpGUI.exe` on Windows or `dist/AccDumpGUI.app`
on macOS. Distribute the resulting file to end users.

## Build CLI

```bash
pyinstaller --onefile accdump_cli.py
```

The output executable will appear under `dist/`.
