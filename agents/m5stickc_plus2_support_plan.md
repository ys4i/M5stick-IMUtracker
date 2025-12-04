# M5StickC Plus2 対応改修案

## 対象範囲
- 既存 M5Stick 系 IMU ロガー（FW v2＋PCツール）を M5StickC Plus2 でも同等機能（加速度＋ジャイロ記録、BIN/CSVダンプ、簡易UI）で動作させる。

## ボード判定
- Arduino ボードマクロで自動判別する（例: `ARDUINO_M5Stick_Plus2` / `M5STICK_C_PLUS2` → Plus2 HAL、`ARDUINO_M5STACK_Core2` → Core2 HAL、`ARDUINO_M5Stick_C` → StickC HAL、それ以外はビルドエラー）。

## ファームウェア作業
- ボードHAL: `board_hal.h` 相当で `M5StickC.h` / `M5Core2.h` / Plus2 用ヘッダの切替、ボタン/タッチ入力、LCD・バックライト・電源レール制御を抽象化。
- IMU方針分岐: Plus2 は MPU6886 を前提とし、M5Unified 公式ライブラリ経由で実装。SH200Q 搭載ボードは従来どおりレジスタ直叩き（生int16読み、ODR/レンジ直接設定）。IMU未確定時はビルド時に明示選択またはエラー。
- ヘッダ/バージョン: 新フォーマット 0x0201 を流用し、`imu_type` / `lsb_per_g` / `lsb_per_dps` / `device_model` を格納。64バイト固定を維持しつつ 0x0200 と後方互換（デコーダは両対応）。
- UI/操作: 入力を変数化し、StickC 系と同挙動（短押し=画面ウェイク、長押し=録画開始/停止、長押しでジャイロキャリブ）。画面サイズ・回転・バックライト処理を調整し、自動画面OFF動作を維持。未記録状態が10分継続し、かつ記録中/DUMP中でない場合は自動電源OFFする挙動を追加。
- 設定: デフォルト値は既存 StickC/Core2 と共通を採用（ODR/レンジ/DLPF/シリアルボーレート）。ビルド時のボード選択マクロ自動判別を前提にし、Plus2向けパーティションは 8MB 前提で別CSVを用意。

## Plus2 デフォルト（既存共通設定）
- ODR: 200 Hz
- 加速度レンジ: ±8 g
- ジャイロレンジ: ±2000 dps
- DLPF: 無効（必要なら 50Hz / 92Hz 等に変更可）
- シリアルボーレート: 115200
- 画面/電源: 画面回転なし。その他（輝度、自動消灯時間など）は StickC と同一設定を流用。

## 0x0201 ヘッダレイアウト
- 既存0x0200構造の後ろ（旧reserved内）に追加し、64B固定のまま:
  - `imu_type` : uint16（1=SH200Q, 2=MPU6886, 0=unknown）
  - `device_model` : uint16（1=StickC, 2=StickC Plus, 3=StickC Plus2, 10=Core2 16MB, 11=Core2 unknown, 0=unknown）
  - `lsb_per_g` : float32 (LE) — 1gあたりのLSB。SH200Qは `32768 / range_g`、MPU6886は M5Unified の実効スケール値を記録。
  - `lsb_per_dps` : float32 (LE) — 1dpsあたりのLSB。SH200Qは `32768 / gyro_range_dps`、MPU6886は M5Unified の実効スケール値を記録。
- 使用サイズ: 40B（従来）+12B（追加）=52B。残り12Bはreservedのままゼロ埋め。
- 0x0200は従来構造のまま。デコーダは format_ver で追加フィールドを読む/スキップ。

## PCツール
- デコーダ: ヘッダにIMU種/LSBがあればそれを優先し、0x0200 は従来スケーリングでフォールバック。
- INFO/DUMP: Board/IMU/ODR/レンジ/FS/ヘッダ種別をログに露出しつつ後方互換を維持（既存表示を踏襲）。
- ドキュメント: `README_AccDump` に Plus2 注意点や必要ならスクリーンショットを追記。
- GUI/CLI表示構成: 既存の Core2/StickC と同じフォーマットを踏襲（Board/IMU/ODR/Accel/Gyro/FS/Format/UID/Offset など）。

## INFO拡張
- 追加キー: `board` (`plus2`/`stickc`/`core2`/`unknown`), `imu` (`mpu6886`/`sh200q`), `format` (`0x0200/0x0201`), `lsb_per_g`, `lsb_per_dps`, `device_model`, `header_found`（bool）, `header_offset`（int）。
- 既存フィールドは維持（`uid`, `odr`, `range_g`, `gyro_dps`, `file_size`, `fs_total`, `fs_used`, `fs_free`, `fs_used_pct`, `baud` など）。

## 数値レンジのガード
- ODR: デバイスごとのサポート値に最近傍で丸める（SH200Q: {8,16,32,64,128,256,512,1024}、MPU6886: {12.5,25,50,100,200,400,800,1600}相当で近似）。無効値はビルド警告/ログに出す。
- 加速度レンジ: {±2, ±4, ±8, ±16} 以外は最近傍に丸め、ヘッダには丸め後を書き込む。
- ジャイロレンジ: {±250, ±500, ±1000, ±2000} 以外は最近傍に丸め、ヘッダには丸め後を書き込む。
- DLPF: 「無効」以外を選ぶ場合は許容値テーブル外なら「無効」にフォールバックし、INFO/ログに出す。

## パーティションCSV
- 推奨: `tools/partitions_plus2_8mb.csv`（例: app0=1.5MB, littlefs=5.5MB, nvs/phy等は最小構成）を同梱。
- フォールバック: IDE既定の「No OTA (1MB APP / 3MB SPIFFS)」を README に記載。

## キャリブレーション方針（Stick系踏襲）
- 起動後は未キャリブ状態で開始し、BtnA の長押し（Plus2でも同位置の物理ボタン）でジャイロ静止キャリブを実行（数百サンプル平均）。
- キャリブ完了後は長押しで録画開始/停止をトグル。短押しは画面ウェイクのみ。
- 加速度オフセット補正は行わず、ソフトバイアスはジャイロのみ（RAM保持。再起動でリセット）。

## ドキュメント
- ルート README に Plus2 対応と Arduino IDE のボード・パーティション設定を追記。
- FW README（存在する場合）に IMU 差異とデフォルト設定、制約を明記。
- 既存ユーザ向け移行メモ（新ヘッダを使わない限り追加作業不要である旨）。

## テスト/検証
- Plus2 へビルド/書き込みし、INFO/REGS/DUMP を確認。ヘッダ内容と CSV スケーリングが実機挙動と一致することをチェック。
- 退行確認: M5StickC 系のビルドが通り、デコーダが混在ログを処理できること。

## 成果物
- 新HAL・IMUドライバ、スケッチ/設定の更新、デコーダ調整、関連ドキュメント更新（`agents` 配下でトラッキング）。

## 実装計画（概要）
- HAL 分離: `board_hal.h/.cpp` で LCD/電源/入力/IMU 初期化を一本化し、StickC(SH200Q直叩き)/Plus2(MPU6886, M5Unified)/Core2(M5Unified)をコンパイル時切替。
- IMU レイヤ: `imu_sh200q.h` 現行維持。`imu_mpu6886_unified.h` を Plus2/Core2 で共有し、M5Unified経由で ODR/レンジ/DLPF(off/50/92Hz)設定・raw取得・ジャイロバイアスを提供。
- ヘッダ/INFO: 0x0201 に `imu_type/device_model/lsb_per_g/lsb_per_dps` を追記し、64B固定・0x0200後方互換を維持。INFO でも同情報を返す。
- 入力/UI: HAL 経由で StickC/Plus2=BtnA長押し/短押し、Core2=全画面タップ/長押し。同挙動を踏襲し、未記録10分＆非DUMP時に自動電源OFF。画面/電源処理は既存挙動を流用。
- パーティション: `tools/partitions_plus2_8mb.csv` を追加し、推奨(1.5MB APP / 5.5MB LittleFS)とフォールバック(No OTA 1MB/3MB)を README に記載。
- PCツール: INFO/表示を Board/IMU/ODR/レンジ/FS/ヘッダ種別で整理。デコーダは0x0200/0x0201両対応、LSB優先を実装。
- テスト: StickC/SH200Q と Plus2/Core2(MPU6886) の両方で INFO/REGS/DUMP/CSV スケールと電源OFF挙動を実機確認。

## タスクリスト
1. HAL に Plus2 を追加し、`firmware_m5_multi_acc_logger.ino` の呼び出しを HAL 化（StickC/Plus2/Core2 切替）。
2. `imu_mpu6886_unified.h` を Plus2/Core2 共有の実装として用意し、M5Unified で ODR/レンジ/DLPF(off/50/92)設定と raw 取得を実装。
3. 0x0201 ヘッダ書き込み・デコーダ/INFO 読取対応（imu_type/device_model/lsb_per_g/lsb_per_dps, device_model=3 を追加）。
4. 入力/UI ロジックを HAL に統合し、未記録10分オートパワーオフ（記録中/DUMP中は除外）を Plus2 にも適用。
5. パーティション CSV `tools/partitions_plus2_8mb.csv` を追加し、README に Plus2 ボード選択とパーティション設定を追記。
6. PCツール: INFO出力・GUI/CLI表示を既存フォーマットで整理し、デコーダで LSB 優先を実装。
7. 実機テスト（StickC/SH200Q、Plus2/Core2=MPU6886）で INFO/REGS/DUMP/CSV スケールと電源OFFを確認。
