# Arduino IDEでのM5系ボード判別マクロ規則まとめ

## 1. 基本ルール

- 各ボード定義ファイル `boards.txt` には、ボードごとに  
  `xxx.build.board = YYY`  
  という行がある。
- コンパイル時に、この `YYY` に `ARDUINO_` を付けたマクロが自動定義される。

例（Arduino UNO）  
- `uno.build.board = AVR_UNO`  
  → 定義されるマクロ: `ARDUINO_AVR_UNO`

---

## 2. M5Stack公式ボードパッケージの場合

M5Stack公式パッケージ（`M5Stack by M5Stack`）では、`boards.txt` におおよそ次のような定義がある（イメージ）:

- `m5stack_core.build.board        = M5STACK_CORE`
- `m5stack_fire.build.board        = M5STACK_FIRE`
- `m5stack_core2.build.board       = M5STACK_CORE2`
- `m5stack_stickc_plus2.build.board= M5STACK_STICKC_PLUS2`
- `m5stack_atoms3.build.board      = M5STACK_ATOMS3`
- …など

→ 定義されるマクロは以下のようになる：

- `M5STACK_CORE`        → `ARDUINO_M5STACK_CORE`
- `M5STACK_FIRE`        → `ARDUINO_M5STACK_FIRE`
- `M5STACK_CORE2`       → `ARDUINO_M5STACK_CORE2`
- `M5STACK_STICKC_PLUS2`→ `ARDUINO_M5STACK_STICKC_PLUS2`
- `M5STACK_ATOMS3`      → `ARDUINO_M5STACK_ATOMS3`
- …（＝ `build.board` の値に `ARDUINO_` を付けたもの）

---

## 3. コード側での判別パターン例(参考)  

```cpp
#if defined(ARDUINO_M5STACK_CORE) || defined(ARDUINO_M5STACK_FIRE)
// M5Stack Core / Fire 用
#elif defined(ARDUINO_M5STACK_CORE2) || defined(ARDUINO_M5STACK_TOUGH)
// Core2 / Tough 用
#elif defined(ARDUINO_M5STACK_STICKC_PLUS2)
// StickC Plus2 用
#endif