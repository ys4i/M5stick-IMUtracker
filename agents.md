# m5stick\_multi\_imu\_logger.md

**目的**: 複数台の **M5StickC 系**（C / C PLUS / C PLUS2）で**内蔵IMUの加速度**を記録し、**計測後にPCへUSB有線で吸い出す**。
**開発条件**: **Arduino IDE で書き込み**。データ吸い出しツールは**初心者向けGUI**（macOS/Windows対応）。

---

## ゴール / 受入基準（Acceptance Criteria）

1. 各M5が**内蔵IMU加速度3軸・int16（6B/サンプル）**を**一定ODR**でロギング。
2. 記録は\*\*デバイス内Flash（LittleFS）\*\*に単一ファイルとして保存（ヘッダ+生データ）。
3. PC側GUIツールから**USBシリアル経由でワンクリック吸い出し**できる（macOS/Windows）。
4. `.bin → .csv` 変換機能をGUI内で提供（列：`n, t_sec, ax_g, ay_g, az_g`）。
5. 10分連続記録時の**実効ODR誤差 ≤ ±1%**、**サンプル数の欠落0**（ログに0件）。
6. 4MB機で約**1.5MB**の実効データ領域（デフォルトパーティション）。PLUS2（8MB）では**\~4–6MB**を確保（後述の簡易切替で実現）。

> デフォルト想定：**ODR=200 Hz**, **±4 g**, **加速度のみ**。必要に応じ`config.h`で変更。

---

## 生成してほしいファイル構成

```
/firmware_m5_multi_acc_logger/                      # Arduino IDEで開けるスケッチ
  firmware_m5_multi_acc_logger.ino
  config.h
  fs_format.h                   # LittleFS操作の薄いラッパ
  imu_sh200q.h                  # 内蔵IMU(SH200Q)用の最小ドライバ
  serial_proto.h                # シリアル簡易プロトコル
  README_ArduinoIDE.md          # 初心者向けセットアップ手順（Windows/macOS）
  partitions_8MB.csv            # (PLUS2向け) 8MB時の推奨パーティション(任意)

 /pc_tools/                     # 初心者向けGUI + CLI
  AccDumpGUI.py                 # Tkinter製GUI（mac/Win対応）
  accdump_cli.py                # CLI版（上級者向け）
  serial_common.py              # ポート列挙・通信共通
  decoder.py                    # .bin→.csv 変換ロジック
  requirements.txt              # pyserial / numpy / pandas
  BUILD.md                      # PyInstallerによるmac/Win用配布物の作り方
  README_AccDump.md             # 使い方（初心者向け）

/docs/
  PROTOCOL.md                   # シリアルコマンド・バイナリ仕様
  CAPACITY.md                   # 保存時間の見積もり表と式
  TROUBLESHOOT.md               # ドライバ・接続の詰まりどころ
```

---

## ファームウェア仕様（Arduino IDE）

### 1) ボード/ライブラリ前提

* **ボードマネージャURL**（Arduino IDE）：
  `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
  → 「ESP32 by Espressif Systems」を導入。
* **対象ボード**：

  * M5StickC：`M5Stick-C`（または`ESP32 PICO Kit`系でも可）
  * M5StickC PLUS / PLUS2：対応ボードを選択（`board listall`参照）
* **ライブラリ**：標準の `Wire.h` を使用。IMUは**最小限のレジスタ直叩き**（依存減）。

### 2) 記録仕様

* **センサ**：内蔵IMU（MPU6886相当）**加速度のみ**。
* **ODR**：`config.h`で 100 / 200 / 400 / 1000 Hz から選択（デフォルト200 Hz）。
* **フルスケール**：±2/4/8/16 g（デフォルト±4 g）。
* **LPF/DLPF**：固定（`config.h`に定数、解析一貫性維持）。
* **取得方式**：タイマ駆動ポーリング（割り込み未配線機でも安定）。
* **記録形式（LittleFS, 単一ファイル）**：

  * 先頭64Bヘッダ
  * 以降 `int16 ax, ay, az` が連結（タイムスタンプは保存せず、`n/ODR`で復元）
* **ファイル名**：`ACCLOG.BIN`（新規記録時に上書き確認あり）。
* **開始/停止**：ボタンA短押しでトグル。LCDに状態表示（REC / IDLE / 残容量）。
* **保護**：満容量時は自動停止し、LCDに通知。
* **メタ情報**：起動時に\*\*Device UID（MAC下位64bit）\*\*を取得し、ヘッダに格納。ドロップカウンタもメタとして記録。

### 3) シリアルプロトコル（簡易）

* ボーレート：**921600 bps**（高速転送）
* コマンド（テキスト）

  * `PING` → `PONG\n`
  * `INFO` → JSON1行（UID, ODR, range, file\_size など）
  * `DUMP` → `OK <filesize>\n` の後、**64Bヘッダ + 生データ**を一括送出、最後に `\nDONE\n`
  * `ERASE` → 確認プロンプト後にファイル削除
  * `STOP` → 記録停止、`START` → 記録開始（PCからの遠隔操作向け、任意）
* 転送は**ブロッキング一括**。PC側はCRC無しで受信（簡易・高速）。タイムアウト時は再試行。

### 4) パーティション

* **4MB機（C / C PLUS）**：Arduino既定の「Default 4MB with spiffs」を前提（実効データ領域 ≈1.5MB）。
* **8MB機（PLUS2）**：`partitions_8MB.csv`を同梱（初心者でもIDEの「Partition Scheme」から選択できる形に）。

  * 例：`app0: 2MB, spiffs(littlefs): 5MB` など

---

## データフォーマット詳細

**ヘッダ（64B固定）**

* `magic[8]` : `"ACCLOG\0"`
* `format_ver` : `uint16`（例 0x0100）
* `device_uid` : `uint64`
* `start_unix_ms` : `uint64`（0可；未利用時）
* `odr_hz` : `uint16`
* `range_g` : `uint16`
* `total_samples` : `uint32`（停止時に追記、収集中は0xFFFFFFFF）
* `dropped_samples` : `uint32`
* `reserved[...]` : ゼロ埋め（合計64Bに揃える）

**データ部**

* 連続配列：`[ax int16][ay int16][az int16] ...`
* 物理量変換：`g = raw / LSB_PER_G`（±4 g 時のLSBは`config.h`に定義）

---

## PC側ツール（初心者向けGUI / macOS & Windows）

### AccDumpGUI（Tkinter）

**機能**

* 起動時に**接続ポート自動列挙**。
* デバイスごとに**INFO取得**して一覧表示（UID / ODR / ログ容量）。
* 「選択→**DUMP**」ボタンで保存先フォルダを指定し、`.bin`を吸い出し。
* DUMP後に\*\*「CSVへ変換」チェック\*\*がONなら `.csv` を自動生成。
* 進捗バー、エラー表示、ログ（テキスト）。

**操作手順（README\_AccDump.md に記載）**

1. M5を**記録停止**状態でUSB接続。
2. GUIを起動 → ポートを選ぶ → `DUMP`。
3. 必要なら「CSVへ変換」をON。
4. 完了後、保存フォルダを開くボタンで確認。

**依存**：`pyserial`, `numpy`, `pandas`（`requirements.txt`）。
**配布**（BUILD.md）

* **Windows**：`pyinstaller --onefile --windowed AccDumpGUI.py` → `AccDumpGUI.exe`
* **macOS**：`pyinstaller --onefile --windowed AccDumpGUI.py` → `AccDumpGUI.app`

  * Gatekeeper回避手順をREADMEに記載（右クリック→開く）。

### CLI（上級者）

* `python accdump_cli.py --port COM5 --out logs/ --csv`
* 全ポート一括吸い出し：`--all` オプション。
* 変換のみ：`python decoder.py input.bin --csv`

---

## 初心者向けセットアップ（Arduino IDE）

`/firmware_m5_multi_acc_logger/README_ArduinoIDE.md` に以下を記載して生成：

1. **Arduino IDE インストール**（mac/Win）。
2. **ESP32ボードの追加**（上記URL）。
3. **ボード選択**（M5StickC/PLUS/PLUS2）。
4. **シリアルドライバ**

   * Windowsで認識されない場合：デバイスマネージャでポート確認。未認識なら\*\*USB-UARTドライバ（CP210x等）\*\*をベンダ提供から導入。
   * macOS：通常は不要。未認識時のみドライバ導入を案内。
5. **スケッチを開く** → `firmware_m5_multi_acc_logger.ino`。
6. **シリアルポート選択** → **書き込み**。
7. **ボタンA** で記録開始/停止。LCDに状態表示。
8. 記録後はPCツールで吸い出し。

---

## タイムスタンプ運用（agents）

目的：agents がファイルを変更するたびに、ファイルの先頭コメントへ「最終更新時刻（秒まで）」を明記し、書き込みビルドを人間の目で確実に区別できるようにする。

対象ファイル：`firmware_m5_multi_acc_logger/firmware_m5_multi_acc_logger.ino`

やること（手動）

- スケッチ先頭に以下のコメントブロックを配置する（まだ無い場合は追加）。
- agents が当該ファイルを変更したときに、日時の行を「PCローカル時刻の秒精度」で更新する。

コメントテンプレート（例）

```cpp
// Build Marker: 2025-09-15 19:12:34 (Local, Last Updated)
// Note: Update this timestamp whenever agents modifies this file.
```

推奨ルール

- 形式は `YYYY-MM-DD HH:MM:SS`（24時間表記、ローカルタイム）を使用。
- 1行目のラベルは必ず `Build Marker:` とする（grepや差分で追いやすくするため）。
- agents が変更したタイミングで必ず秒まで更新する（Arduino IDEの自動保存の有無に関わらず明示更新）。
- チーム共有時もこのコメントを残し、ビルドの世代識別に用いる。

補足（任意）

- 日本時間表記にしたい場合は末尾に `JST` を付けてもよい。
  例: `// Build Marker: 2025-09-15 19:12:34 JST`
- 将来的に自動化する場合は、プリプロセッサ定義（`__DATE__`, `__TIME__`）で画面表示へ埋め込む等の拡張も可能だが、まずは上記の手動運用で十分。

---

## 実装のポイント（コード方針）

* **安定優先**：Wi-Fi無効、タイマで一定周期読み取り。
* **I²C**：400kHz。再初期化・エラーカウント（`dropped_samples`）を実装。
* **バッファ**：RAMリング→LittleFSへ**4KB単位**で書き込み（摩耗分散）。
* **LCD**：REC中は赤、IDLEは白など簡易表示。容量ゲージ（%）。
* **時刻**：`n/ODR`で復元。`start_unix_ms`は未使用可（0）。必要時はPCから設定コマンドを拡張。

---

## CAPACITY（保存時間の概算；/docs/CAPACITY.md に表で収録）

* データ率：**6 B/サンプル**
* 保存時間：`T[s] = (有効バイト) / (6 × ODR)`

  * 4MB機（有効1.5MB）：100 Hz→約41.7分、200 Hz→約20.8分、400 Hz→約10.4分、1000 Hz→約4.2分
  * 8MB機（有効6MB）：100 Hz→約2.78h、200 Hz→約1.39h、400 Hz→約41.7分、1000 Hz→約16.7分

---

## テスト計画

1. **ODR検証**：200 Hzで10分記録し、総サンプル数/時間から誤差計算（≤±1%）。
2. **ドロップ検証**：`dropped_samples == 0` を確認。
3. **転送検証**：Windows/macOSそれぞれでDUMP→CSVを3回連続成功。
4. **満容量時動作**：容量超過で自動停止・LCD通知・吸い出し可能。

---

## PROTOCOL（/docs/PROTOCOL.md に詳細）

* `INFO` 応答例（JSON, 1行）：

  ```json
  {"uid":"0xA1B2C3D4E5F6","odr":200,"range_g":4,"file_size":1536000,"dropped":0}
  ```
* `DUMP` 応答順：

  1. `OK <filesize>\n`
  2. 64Bヘッダ
  3. 生データ（<filesize>-64バイト）
  4. `\nDONE\n`

---

## 既定値と変更点

* 既定：**ODR=200 Hz**, **±4 g**, **加速度のみ**, **単一ファイル方式**, **LittleFS**。
* 変更は\*\*`config.h`\*\*の定数で可能（ODR, RANGE, ファイル名, ボーレート）。

---

## 想定される詰まりどころ（/docs/TROUBLESHOOT.md）

* **ポートが見えない**：ケーブル変更、USBハブ回避、ドライバ導入。
* **書き込み失敗**：ボード選択／ポート誤り、他アプリのシリアル掴み、リセット。
* **ログが短い**：容量不足→PLUS2でパーティション拡張。
* **CSVがNaN**：壊れたファイル→再DUMP（転送時の切断対策として再試行あり）。

---

## （任意）将来拡張

* 6軸対応（ジャイロ追加; 12B/サンプル）
* 複数ファイルローテーション／日付別保存
* PCからの**遠隔START/STOP**（USB接続中に操作）
* 圧縮（差分+LZ4）で容量倍増を狙う

---

## 本仕様で未固定のパラメータ（暫定既定で生成してよい）

* ODR：200 Hz / RANGE：±4 g
* 記録時間目安：**20分**
* LittleFS実効領域：4MB機で**\~1.5MB** / 8MB機で**\~6MB**
* GUIはシリアルポートを**単一選択**、将来「全ポート一括」オプション追加

> 変更が必要な場合は、`config.h` と `/pc_tools/README_AccDump.md` の記述をあわせて修正すれば運用できます。
