# M5Stack Core2 対応改修案

## 対象範囲
- 既存 M5Stick 系 IMU ロガー（FW v2＋PCツール）を M5Stack Core2 でも同等機能（加速度＋ジャイロ記録、BIN/CSVダンプ、簡易UI）で動作させる。

## ボード判定
- Arduino ボードマクロで自動判別する（例: `ARDUINO_M5STACK_Core2` / `M5STACK_M5Core2` → Core2 HAL、`ARDUINO_M5Stick_C` → StickC HAL、それ以外はビルドエラー）。

## ファームウェア作業
- ボードHAL: `board_hal.h` 相当で `M5StickC.h` / `M5Core2.h` の切替、ボタン/タッチ入力、LCD・バックライト・電源レール制御を抽象化。
- IMU方針分岐: SH200Q 搭載デバイスは既知の不具合回避のため現行どおりレジスタ直叩き（生int16読み、ODR/レンジ直接設定）。SH200Q 以外の IMU を搭載するボード（Core2 の MPU6886 など）は M5Unified 公式ライブラリ経由の実装を踏襲し、Unified が提供するスケール/ODR設定を利用する。
- ヘッダ/バージョン: 新フォーマット 0x0201 を追加し、`imu_type` / `lsb_per_g` / `lsb_per_dps` / `device_model` を格納。64バイト固定を維持しつつ 0x0200 と後方互換にする（デコーダは両対応）。
- UI/操作: 入力を変数化し、StickC系では BtnA(GPIO37) を長押し=開始/停止、短押し=画面ウェイクとする（長押し閾値は現行どおり約800ms）。Core2 ではタッチパネル全域のタップ/長押しで同等挙動を割当てる。画面サイズ・回転・バックライト処理を調整し、自動画面OFF動作は維持。未記録状態が10分継続し、かつ記録中/DUMP中でない場合は自動電源OFFする挙動を追加。
- 設定: ビルド時のボード選択マクロ自動判別を前提にし、Core2向けパーティションは以下を採用。
  - **推奨デフォルト（Core2=16MB前提）**: No OTA 系カスタムCSVで「APP 2MB / LittleFS 13MB」程度を同梱し、最大記録容量を優先。
  - **フォールバック**: 容量不明やカスタムCSV不可の場合は IDE 既定の「No OTA (1MB APP / 3MB SPIFFS)」を選択肢として記載。

## Core2 デフォルト（全て可変、下記は初期値）
- ODR: 200 Hz
- 加速度レンジ: ±8 g
- ジャイロレンジ: ±2000 dps
- DLPF: Digital Low-Pass Filter（内部ローパス）設定値。ノイズ低減と帯域設定用。デフォルトは「無効」（レジスタ値 0x00、必要に応じて 50Hz / 92Hz などに変更）。
- シリアルボーレート: 115200
- 画面/電源: 画面回転なし。その他（輝度、自動消灯時間など）は StickC と同一設定を流用。

## 0x0201 ヘッダレイアウト
- 既存0x0200構造の後ろ（旧reserved内）に追加し、64B固定のまま:
  - `imu_type` : uint16（1=SH200Q, 2=MPU6886, 0=unknown）
  - `device_model` : uint16（1=StickC, 2=StickC Plus, 3=StickC Plus2, 10=Core2 16MB, 11=Core2 unknown, 0=unknown）
  - `lsb_per_g` : float32 (LE) — 1gあたりのLSB。SH200Qは `32768 / range_g` を記録、M5Unified経由のIMUはライブラリの実効スケール値を書く。
  - `lsb_per_dps` : float32 (LE) — 1dpsあたりのLSB。SH200Qは `32768 / gyro_range_dps` を記録、M5Unified経由のIMUはライブラリの実効スケール値を書く。
- 使用サイズ: 40B（従来）+12B（追加）=52B。残り12Bはreservedのままゼロ埋め。
- 0x0200は従来構造のまま。デコーダは format_ver で追加フィールドを読む/スキップ。

## PCツール
- デコーダ: ヘッダにIMU種/LSBがあればそれを優先し、0x0200 は従来スケーリングでフォールバック。
- INFO/DUMP: ボード/IMUメタ情報をログに露出しつつ後方互換を維持。
- ドキュメント: `README_AccDump` に Core2 注意点や必要ならスクリーンショットを追記。
- GUI/CLI表示構成:
  - Board / IMU: `Core2` / `StickC/Plus/Plus2`, `MPU6886` / `SH200Q`（不明は `unknown`）
  - 収録条件: `ODR (Hz)`, `Accel ±g`, `Gyro ±dps`
  - FS情報: `File size` (`ACCLOG.BIN` サイズ), `FS used %` (`used / total`)
  - ヘッダ種別: `Format` (`0x0200` / `0x0201`), `Device UID`, `Header found` (`yes/no`), `offset`
  - 転送メタ（DUMP時）: `Baud`（INFOが返す場合）, `Device now(ms)` / `PC OK recv time`（デバッグ用）
  - GUI: 上部に Board/IMU/ODR/Accel/Gyro を1行表示、FS・ヘッダ詳細はログ欄に出す。CLI: INFOは1行JSONを維持しつつ、ログ行で `Board=<...> IMU=<...> ODR=<...> Accel=±8g Gyro=±2000dps FS=12%` など整形表示。

## INFO拡張
- 追加キー: `board` (`core2`/`stickc`/`unknown`), `imu` (`mpu6886`/`sh200q`), `format` (`0x0200/0x0201`), `lsb_per_g`, `lsb_per_dps`, `device_model`（短い文字列または数値ID。`board`は系列、`device_model`はより細かい機種IDを指す）, `header_found`（bool）, `header_offset`（int）。
- 既存フィールドは維持（`uid`, `odr`, `range_g`, `gyro_dps`, `file_size`, `fs_total`, `fs_used`, `fs_free`, `fs_used_pct`, `baud` など）。

## 数値レンジのガード
- ODR: デバイスごとのサポート値に最近傍で丸める（SH200Q: {8,16,32,64,128,256,512,1024}, MPU6886: {12.5,25,50,100,200,400,800,1600}相当で近似）。無効値はビルド警告/ログに出す。
- 加速度レンジ: {±2, ±4, ±8, ±16} 以外は最近傍に丸め、ヘッダには丸め後を書き込む。
- ジャイロレンジ: {±250, ±500, ±1000, ±2000} 以外は最近傍に丸め、ヘッダには丸め後を書き込む。
- DLPF: 「無効」以外を選ぶ場合は許容値テーブル外なら「無効」にフォールバックし、INFO/ログに出す。

## パーティションCSV
- 推奨: `tools/partitions_core2_16mb.csv`（例: app0=2MB, littlefs=13MB, nvs/phy等は最小構成）を同梱。
- フォールバック: IDE既定の「No OTA (1MB APP / 3MB SPIFFS)」を README に記載。

## キャリブレーション方針（Stick系踏襲）
- 起動後は未キャリブ状態で開始し、BtnA（Core2ではタッチ長押し）の長押しでジャイロ静止キャリブを実行（数百サンプル平均）。
- キャリブ完了後は長押しで録画開始/停止をトグル。短押し/タップは画面ウェイクのみ。
- 加速度オフセット補正は行わず、ソフトバイアスはジャイロのみ（RAM保持。再起動でリセット）。

## ドキュメント
- ルート README に Core2 対応と Arduino IDE のボード・パーティション設定を追記。
- FW README（存在する場合）に IMU 差異とデフォルト設定、制約を明記。
- 既存ユーザ向け移行メモ（新ヘッダを使わない限り追加作業不要である旨）。

## テスト/検証
- Core2 へビルド/書き込みし、INFO/REGS/DUMP を確認。ヘッダ内容と CSV スケーリングが実機挙動と一致することをチェック。
- 退行確認: M5StickC 系のビルドが通り、デコーダが混在ログを処理できること。

## 成果物
- 新HAL・IMUドライバ、スケッチ/設定の更新、デコーダ調整、関連ドキュメント更新（`agents` 配下でトラッキング）。

## 実装計画（概要）
- HAL 分離: `board_hal.h/.cpp` で LCD/電源/入力/IMU 初期化を一本化し、StickC(SH200Q直叩き)と Core2(M5Unified)をコンパイル時切替。
- IMU レイヤ: `imu_sh200q.h` 現行維持。`imu_mpu6886_unified.h` を追加し、M5Unified経由で ODR/レンジ/DLPF(off/50/92Hz)設定・raw取得・ジャイロバイアスを提供。
- ヘッダ/INFO: 0x0201 に `imu_type/device_model/lsb_per_g/lsb_per_dps` を追記し、64B固定・0x0200後方互換を維持。INFO でも同情報を返す。
- 入力/UI: HAL 経由で StickC=BtnA長押し/短押し、Core2=全画面タップ/長押し。同挙動を踏襲し、未記録10分＆非DUMP時に自動電源OFF。画面/電源処理は既存挙動を流用。
- パーティション: `tools/partitions_core2_16mb.csv` を追加し、推奨(2MB APP / 13MB LittleFS)とフォールバック(No OTA 1MB/3MB)を README に記載。
- PCツール: INFO/表示を Board/IMU/ODR/レンジ/FS/ヘッダ種別で整理。デコーダは0x0200/0x0201両対応、LSB優先でスケーリング。
- テスト: StickC/SH200Q と Core2/M5Unified の両方で INFO/REGS/DUMP/CSV スケールと電源OFF挙動を実機確認。

## タスクリスト
1. HAL 層追加＆ `firmware_m5_multi_acc_logger.ino` の呼び出しを HAL 化。
2. `imu_mpu6886_unified.h` 追加と M5Unified 設定テーブル（ODR/レンジ/DLPF off/50/92）実装。
3. 0x0201 ヘッダ書き込み・デコーダ/INFO 読取対応（imu_type/device_model/lsb_per_g/lsb_per_dps）。
4. 入力/UI ロジックを HAL に統合し、未記録10分オートパワーオフ（記録中/DUMP中は除外）を追加。
5. パーティション CSV 追加と README 追記。
6. PCツール: INFO出力・GUI/CLI表示を整理し、デコーダで LSB 優先を実装。
7. 実機テスト（StickC/SH200Q、Core2/M5Unified）で INFO/REGS/DUMP/CSV スケールと電源OFFを確認。
