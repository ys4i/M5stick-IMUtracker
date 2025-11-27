M5-IMUtracker（M5Stick/Core2 加速度・ジャイロロガー） / English Follows
========================================================================

概要（日本語）
--------------

M5Stick 系および M5Stack Core2 デバイスで IMU（加速度・ジャイロ）を記録し、PC にバイナリ／CSVとして吸い出すためのファームウェア＋PCツールのセットです。

- ファームウェアは64バイトのヘッダ＋生データのシンプルなログ形式で `/ACCLOG.BIN` に記録します。
- PCツール（GUI/CLI）はシリアル経由で `ACCLOG.BIN` をダウンロードし、必要に応じてCSVへ変換します。
- 新ファーム（v2/0x0200）は加速度＋ジャイロを同時記録。旧ログ（加速度のみ v1/0x0100）も自動判別して対応します。
- 拡張ヘッダ（0x0201）では IMU 種別/機種 ID/スケール情報を含み、Core2 など M5Unified 利用ボードでもスケーリングをメタ経由で正しく行います。

特長
----

- M5Stick本体のボタン操作で記録開始／停止
- 取得レートやレンジは `config.h` で設定
- シリアルDUMPはプリアンブル混入やタイミング差にロバスト
- デコーダはログ形式（v1/v2）を自動判別しCSV出力

IMUドライバ方針（IMU別）
--------------------

- SH200Q 搭載デバイス（StickC系のSH200Qモデルなど）は既知の挙動差・スケールばらつき回避のため、`imu_sh200q.h` でレジスタ直叩きし、生の int16 を記録する。
- それ以外のIMU（例: MPU6886 搭載の Core2 や Stick 系バリエーション）は不具合なしとみなし、M5Unified（公式ライブラリ）経由の設定・取得を採用する。ヘッダ 0x0201 に `imu_type` / `device_model` / `lsb_per_g` / `lsb_per_dps` を記録し、PC側で正しくスケーリングする。
- いずれもデバイス内後処理を最小化し、PC側で統一解析する方針。将来的にライブラリ側で生値取得が安定した場合は再評価する。

対応ハードウェア
---------------

- SH200Q 搭載モデル（例: M5StickC/Plus/Plus2 のSH200Q版） — レジスタ直叩き
- それ以外のIMU搭載モデル（例: M5Stack Core2 の MPU6886、将来の非SH200Q Stick系など） — M5Unified 経由

リポジトリ構成
--------------

- `firmware_m5_multi_acc_logger/` — M5Stick 向け Arduino ファームウェア
- `pc_tools/` — PC側ツール（GUI/CLI/decoder）
- `docs/` — 追加ドキュメント（任意）

ファームウェア（使い方）
----------------------

1. Arduino IDE で `firmware_m5_multi_acc_logger/firmware_m5_multi_acc_logger.ino` を開く
2. ボード（M5StickC / M5StickC Plus / M5StickC Plus2 など）を選択
3. ビルドして書き込み
4. 記録操作：
   - 本体ボタンAで開始／停止
   - デバイス内に `/ACCLOG.BIN` が生成されます

パーティション設定（重要）
- Arduino IDE メニューの「ツール」→「Partition Scheme」で、次を推奨します。
  - Core2（16MB想定）: 同梱 `tools/partitions_core2_16mb.csv` の No OTA (APP 2MB / LittleFS 13MB 目安)
  - StickC 系ほか容量不明: `No OTA (1MB APP/3MB SPIFFS)` をフォールバック選択
- この設定によりフラッシュをファイルシステム（LittleFS領域）に広く割り当てます。表示名は「SPIFFS」ですが、実装は LittleFS を使用します（同一FS領域を共有）。

設定（`config.h`）
- `ODR_HZ` サンプリングレート（例: 128 Hz、SH200Qの対応値に自動丸め）
- `RANGE_G` 加速度レンジ（2/4/8/16 g）
- `GYRO_RANGE_DPS` ジャイロレンジ（250/500/1000/2000 dps、既定2000）
- `SERIAL_BAUD` シリアル速度（既定 115200）

シリアルプロトコル（抜粋）
- `PING` → `PONG`\n
- `INFO` → 1行JSON（ODR/レンジ/ファイルサイズ/FS使用率など）
- `HEAD` → 先頭64バイトのヘッダを16進で表示
- `DUMP` → `OK <filesize> <millis>` の後に生データ、本体は最後に `\nDONE\n`
- `ERASE` → `/ACCLOG.BIN` 削除
- `START` / `STOP` → 記録開始／停止

データ形式
----------

ヘッダ（64バイト, little-endian）
- magic[8]: `"ACCLOG\0\0"`（古いv1では `"ACCLOG\0"`）
- format_ver: uint16（v1: 0x0100 加速度のみ, v2: 0x0200 加速度+ジャイロ, 拡張: 0x0201 メタ追加）
- device_uid: uint64
- start_unix_ms: uint64（任意）
- odr_hz: uint16
- range_g: uint16
- v2+: gyro_range_dps: uint16（v1のreserved領域を再利用）
- v2.1+: imu_type, device_model, lsb_per_g, lsb_per_dps（旧reserved内に追加）
- total_samples: uint32（記録中は 0xFFFFFFFF）
- dropped_samples: uint32
- reserved: 64バイトにゼロ詰め（上記以外）

ペイロード（MSB first の int16 配列）
- v1: `[ax][ay][az]` の繰り返し
- v2+: `[ax][ay][az][gx][gy][gz]` の繰り返し

CSV列
- v1: `n, t_sec, ax_g, ay_g, az_g`
- v2+: `n, t_sec, ax_g, ay_g, az_g, gx_dps, gy_dps, gz_dps`

PCツール
-------

要件（Python 3.10+ 推奨）：

```
python -m pip install -r pc_tools/requirements.txt
```

GUI（Tkinter）

```
python pc_tools/AccDumpGUI.py
```

- シリアルポートを選択して DUMP をクリック
- 「CSVへ変換」にチェックで、`ACCLOG.BIN` と同じ場所にCSVを自動生成

CLI

特定ポートからDUMPしてCSVへ変換：

```
python pc_tools/accdump_cli.py --port /dev/ttyUSB0 --out logs/ --csv
```

利用可能な全ポートからDUMP：

```
python pc_tools/accdump_cli.py --all --out logs/
```

既存のログをCSVへ変換：

```
python pc_tools/decoder.py logs/ACCLOG.BIN --csv
```

トラブルシューティング
--------------------

- デバイスが使用中（`Device or resource busy`）
  - 他のシリアルモニタ（Arduino Serial Monitor / screen / minicom / VS Code拡張など）を終了
  - Linuxでは ModemManager がポートを触る場合があります：`sudo systemctl stop ModemManager`
  - `dialout` グループ所属を確認し、追加後は再ログイン
- ヘッダ待ちタイムアウト／途中で止まる
  - PC側は `ACCLOG` を探索し、見つからない場合はロウコピーにフォールバック
  - 先頭にプリアンブルがある場合でも decoder が自動でヘッダ位置へ同期
  - 解決しない場合はデバイスの電源再投入、再試行
- CSV変換エラー（invalid magic など）
  - 最新のファーム／ツールで再DUMP
  - `.bin`サイズと先頭64バイトのhexを共有いただければ解析可能
- 通信が不安定／遅い
  - ツールは10秒のシリアルタイムアウトと4KBチャンクで受信。再試行推奨

スタンドアロンビルド（任意）
----------------------------

PyInstaller で配布用バイナリを作成：

```
python -m pip install -r pc_tools/requirements.txt pyinstaller
pyinstaller --onefile --windowed pc_tools/AccDumpGUI.py
pyinstaller --onefile pc_tools/accdump_cli.py
```

`dist/` に成果物が生成されます。

注意事項
------

- 新ファーム（format 0x0200）はジャイロを追加。decoder は v1 と後方互換
- ファームは MSB first で int16 を書き込み、decoder は big-endian として解釈します


English (Summary)
=================

Overview
--------

Firmware + PC tools to log IMU (accelerometer/gyroscope) on M5Stick devices and dump as binary/CSV to your computer.

- Firmware stores `/ACCLOG.BIN` with a 64‑byte header followed by raw samples.
- PC tools (GUI/CLI) download `ACCLOG.BIN` over serial and optionally convert to CSV.
- Latest firmware (v2) logs accel+gyro; older accel‑only logs (v1) are supported.

Features
--------

- Button‑controlled recording on device
- Configurable ODR/ranges in `config.h`
- Robust serial dump (handles preambles/timing)
- Decoder auto‑detects format (v1 accel‑only, v2 accel+gyro)

Hardware
--------

- Devices with SH200Q (e.g., StickC/Plus/Plus2 SH200Q models) — register-level driver
- Devices with other IMUs (e.g., Core2 with MPU6886, future non-SH200Q Stick variants) — M5Unified

Driver Rationale
----------------

- SH200Q devices use `imu_sh200q.h` to configure registers directly and keep raw int16 samples for reproducibility and to avoid known quirks.
- Non-SH200Q devices use M5Unified APIs; format 0x0201 stores `imu_type/device_model/lsb_per_g/lsb_per_dps` to ensure correct scaling on PC.
- Direct register control preserves repeatability for offline analysis; once the M5 library officially exposes raw access with configurable ODR/range we can revisit the decision.

Repository Layout
-----------------

- `firmware_m5_multi_acc_logger/` — Arduino firmware
- `pc_tools/` — PC tools (GUI/CLI/decoder)
- `docs/` — extra docs

Firmware
--------

1. Open `firmware_m5_multi_acc_logger/firmware_m5_multi_acc_logger.ino` in Arduino IDE.
2. Select your M5Stick board, set the partition scheme, build and upload.
3. Recording: Button A toggles logging; `/ACCLOG.BIN` is created.

Configuration (`config.h`):
- `ODR_HZ` sampling rate (e.g., 128 Hz; rounded to nearest supported by SH200Q)
- `RANGE_G` accelerometer full scale (2/4/8/16 g)
- `GYRO_RANGE_DPS` gyroscope full scale (250/500/1000/2000 dps, default 2000)
- `SERIAL_BAUD` serial speed (115200 default)

Serial Protocol
- `PING` → `PONG`\n
- `INFO` → JSON line (ODR/ranges/file size/FS usage, etc.)
- `HEAD` → dump first 64-byte header as hex
- `DUMP` → `OK <filesize> <millis>` then raw bytes, then `\nDONE\n`
- `ERASE` → remove `/ACCLOG.BIN`
- `START` / `STOP` → control logging

Data Format
-----------

Header (64 bytes, little‑endian): magic `ACCLOG\0\0` (v1 may be `ACCLOG\0`), version, UID, start time, ODR, ranges, totals, reserved.

Payload (int16, MSB first):
- v1: `[ax][ay][az]`
- v2+: `[ax][ay][az][gx][gy][gz]`

CSV Columns:
- v1: `n, t_sec, ax_g, ay_g, az_g`
- v2+: `n, t_sec, ax_g, ay_g, az_g, gx_dps, gy_dps, gz_dps`

PC Tools
--------

Requirements:

```
python -m pip install -r pc_tools/requirements.txt
```

GUI:

```
python pc_tools/AccDumpGUI.py
```

CLI:

```
python pc_tools/accdump_cli.py --port /dev/ttyUSB0 --out logs/ --csv
python pc_tools/accdump_cli.py --all --out logs/
python pc_tools/decoder.py logs/ACCLOG.BIN --csv
```

Troubleshooting
---------------

- Busy port: close other monitors; stop ModemManager on Linux; ensure `dialout` group.
- Header timeouts/partials: dumper searches for `ACCLOG` and decoder scans/aligns; retry after power‑cycle if needed.
- CSV errors: re‑dump with latest firmware/tools; share `.bin` size and first 64 bytes (hex) for help.
- Unstable link: tools use 10‑second timeout and 4 KB chunks; retry.

Standalone Builds (optional)
----------------------------

```
python -m pip install -r pc_tools/requirements.txt pyinstaller
pyinstaller --onefile --windowed pc_tools/AccDumpGUI.py
pyinstaller --onefile pc_tools/accdump_cli.py
```

Notes
-----

- v2 firmware (0x0200) adds gyro channels; decoder remains backward‑compatible.
- Firmware writes int16 MSB‑first; decoder reads big‑endian.
Partition Scheme (Important)
- In Arduino IDE, go to Tools → Partition Scheme and select:
  - `No OTA (1MB APP/3MB SPIFFS)`
- This grants ~3MB to the filesystem (used by LittleFS) for longer recordings. The default schemes provide a smaller FS and will reduce recording time.
- The menu label mentions SPIFFS, but this project uses LittleFS on the same partition region.
