# 各コーデックの state_ メンバーを std::atomic にして data race を解消する

- Priority: Medium
- Created: 2026-05-20
- Model: Opus 4.7
- Branch: feature/fix-state-data-race

## 目的

`VideoEncoder` / `AudioEncoder` / `VideoDecoder` / `AudioDecoder` の `state_` メンバー (`CodecState`) を `std::atomic<CodecState>` に置き換え、 close()/reset() が GIL 解放下で `state_` を書き、 state プロパティが GIL 保持下で `state_` を読む際に発生する C++20 data race (UB) を解消する。

## 優先度根拠

Medium。

- issue 0003 で close()/reset() のバインディングに `nb::call_guard<nb::gil_scoped_release>()` を付与した結果、 close()/reset() の本体が GIL を解放した状態で `state_` を書くようになった。
- 一方 state プロパティのバインディング (`def_prop_ro("state", ...)`) は `nb::call_guard` 未指定で、 Python 側は GIL 保持で `state_` を読む。
- 「書き手は GIL なし / 読み手は GIL あり」 という構成では GIL が同期に使えず、 非アトミックメンバーへの並行アクセスは C++20 data race UB に該当。
- 実害として CPython 上では `CodecState` (enum class) の読み書きは多くの ABI でアトミック相当の word アクセスになるため顕在化は稀だが、 標準上は未定義動作で、 Free-Threading ビルドや ThreadSanitizer で検出されうる。
- 緊急度は High ではないが、 issue 0003 で導入した修正の副作用として記録する。

## 現状

### 該当箇所

| ファイル | 行 | 該当 |
| ---- | ---- | ---- |
| `src/bindings/video_encoder.h` | (state_ 宣言) | `CodecState state_;` |
| `src/bindings/audio_encoder.h` | 同上 | 同上 |
| `src/bindings/video_decoder.h` | 同上 | 同上 |
| `src/bindings/audio_decoder.h` | 同上 | 同上 |

`state()` プロパティのバインディング (例 `audio_decoder.cpp:389-390`):

```cpp
.def_prop_ro("state", &AudioDecoder::state,
             nb::sig("def state(self, /) -> CodecState"))
```

`nb::call_guard` 未指定で GIL を保持して `state_` を読む。

`close()` / `reset()` での書き込み (例 `audio_decoder.cpp:216`):

```cpp
state_ = CodecState::CLOSED;
```

issue 0003 で `close()` / `reset()` のバインディングに `nb::call_guard<nb::gil_scoped_release>()` が付いたため、 この書き込みは GIL 解放下で行われる。

### 競合シナリオ

- スレッド A: `encoder.close()` → call_guard で GIL 解放 → C++ で `state_ = CodecState::CLOSED;`
- スレッド B (Python): `encoder.state` → GIL 保持で `state_` 読み

スレッド A は GIL を保持しておらず、 スレッド B は GIL を保持しているが、 両者は同期されていない (GIL は両者の同期に使われない)。 → data race。

## 設計方針

### 修正方針

各コーデックヘッダの `state_` メンバーを `std::atomic<CodecState>` に変更する。

例 (`src/bindings/audio_decoder.h`):

```cpp
#include <atomic>
...
std::atomic<CodecState> state_;
```

`state_` への読み書きはすべて `.load()` / `.store()` 経由にする。 memory_order はデフォルトの `seq_cst` で十分。

`state()` getter も atomic の load に変える:

```cpp
CodecState AudioDecoder::state() const { return state_.load(); }
```

`CodecState` は POD enum なので `std::atomic<CodecState>` は lock-free。

### 採用しなかった案

- **state プロパティの binding に `nb::call_guard<nb::gil_scoped_release>()` を付ける**: 採用しない。 状態読み取りで GIL を解放する意味がなく、 また他スレッドが状態を書き換える間の一貫性も保証できない。
- **`callback_mutex_` (既存) で state も保護**: 採用しない。 callback_mutex は別目的 (コールバック更新の排他) で、 state 読みを毎回 mutex 取得にするとオーバーヘッドが大きい。

## テスト追加

- `tests/test_<codec>_state_atomicity.py` (新規 4 ファイル): 並行 close + state 読みで data race が起きないことを ThreadSanitizer なしでは検証しづらいが、 「複数スレッドから state を読みながら close を呼んでクラッシュしない」 ことを確認する smoke test を追加。

## 完了条件

- 4 コーデックの `state_` メンバーが `std::atomic<CodecState>` になっている
- `state()` getter が `.load()` 経由で読んでいる
- 書き込み箇所がすべて `.store()` 経由になっている
- 既存テストが全 PASS する
- 新規 smoke test が PASS する
- `make format` 適用後、 差分が出ない
- `uv run ty check` が PASS する
- `docs/PYTHON_INTERFACE.md` は更新不要 (内部実装変更のみ)
- `CHANGES.md` の `## develop` セクションに以下の `[FIX]` エントリを追加する:

  ```
  - [FIX] 各コーデックの state_ メンバーを std::atomic にして data race を解消する
    - @<担当者>
  ```

## 解決方法
