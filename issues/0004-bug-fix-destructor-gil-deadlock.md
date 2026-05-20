# VideoEncoder / AudioEncoder / VideoDecoder / AudioDecoder のデストラクタと VideoDecoder::configure 経由で GIL 保持デッドロックが発生する

- Priority: Medium
- Created: 2026-05-20
- Model: Opus 4.7
- Branch: feature/fix-destructor-gil-deadlock

## 目的

issue 0003 でスコープ外とした以下 2 経路のデッドロックを解消する:

- 各コーデックのデストラクタ (`~VideoEncoder()` / `~AudioEncoder()` / `~VideoDecoder()` / `~AudioDecoder()`) からの `stop_worker()` → `worker_thread_.join()`
- `VideoDecoder::configure()` が既存デコーダーを `cleanup_decoder()` する経路の `stop_worker()`

issue 0003 はバインディング層に `nb::call_guard<nb::gil_scoped_release>()` を付与する方針で 4 クラスの `close()` / `reset()` を塞いだ。 デストラクタと再 configure はバインディング関数を経由しないため、 同じ手法では塞げない。 C++ 実装側で GIL 解放区間を作る必要がある。

## 優先度根拠

Medium。

- デストラクタ経路: Python GC で破棄される際に発火するため、 ユーザーが `close()` を明示的に呼んでいれば回避できる。 issue 0003 でも「明示 `close()` を呼ぶ運用」を前提として scope 外にした。 ただしライブラリとして塞ぐべき本質的な問題。
- 再 configure 経路: 同じデコーダーオブジェクトに対する 2 回目以降の `configure()` で発火する。 実用シナリオでは稀。
- 緊急度は issue 0003 ほど高くないが、 同型のデッドロックなので残しておく価値がない。

## 現状

### 該当箇所

#### デストラクタ

| ファイル | 行 | デストラクタ |
| ---- | ---- | ---- |
| `src/bindings/video_encoder.cpp` | 30-33 | `VideoEncoder::~VideoEncoder()` |
| `src/bindings/audio_encoder.cpp` | 43-46 | `AudioEncoder::~AudioEncoder()` |
| `src/bindings/video_decoder.cpp` | 41-44 | `VideoDecoder::~VideoDecoder()` |
| `src/bindings/audio_decoder.cpp` | 43-46 | `AudioDecoder::~AudioDecoder()` |

各デストラクタは `stop_worker();` の後に `close();` を呼ぶ。 `stop_worker()` 内で `worker_thread_.join()` する経路は issue 0003 で詳述した内容と同型。

#### `VideoDecoder::configure` 経由

`src/bindings/video_decoder.cpp:60-110` の `VideoDecoder::configure()` は、既存デコーダーがあれば `cleanup_decoder()` を呼ぶ。 `cleanup_decoder()` (`video_decoder.cpp:469-529`) は内部で `stop_worker()` を呼ぶ (`video_decoder.cpp:471-473`)。

### デッドロックの成立条件

issue 0003 と同型:

1. 呼び出し元スレッド (Python GC や Python から `configure()` 呼び出し): GIL 保持で `worker_thread_.join()` を待つ
2. ワーカースレッド: コールバックの `nb::gil_scoped_acquire` で GIL 取得を待つ

の相互待ち。 Python GC は Python 側コードを実行している (= GIL を保持している) スレッド上で動くため、 `del encoder` や暗黙の参照カウント減少時に発火しうる。

## 設計方針

### 修正方針

各デストラクタおよび `VideoDecoder::cleanup_decoder()` の `stop_worker()` 呼び出しを `nb::gil_scoped_release` で囲む。

例として `VideoEncoder::~VideoEncoder()` (`video_encoder.cpp:30-33`):

```cpp
VideoEncoder::~VideoEncoder() {
  {
    nb::gil_scoped_release release;
    stop_worker();  // ワーカースレッドを停止
  }
  close();
}
```

`close()` も内部で `stop_worker()` を呼ぶが、 デストラクタの先頭で停止しているので `worker_thread_.joinable()` が false で no-op になる。 `close()` 自体は Python オブジェクトに触らないため、 `nb::gil_scoped_release` 配下で呼んでも問題ないが、 範囲を最小化するためデストラクタの `stop_worker()` のみを括る方が層分離として明快。

`VideoDecoder::cleanup_decoder()` (`video_decoder.cpp:469-529`) の `stop_worker()` 呼び出しも同様に `nb::gil_scoped_release` で囲む。 ただし `cleanup_decoder()` は内部で `init_decoder()` 等を呼ぶ経路があり、 関数全体を GIL 解放にすると影響範囲が広い。 `stop_worker()` 単体のみを括る形が安全。

### 検討事項 (実装時に確定)

- `nb::gil_scoped_release` は GIL を保持していない状態で構築すると `PyEval_SaveThread` が不正状態になる可能性がある。 デストラクタが C++ 側から呼ばれた場合 (Python GC 経由ではないケース) はどう扱うか確認する。 nanobind の `nb::gil_scoped_release` の挙動を `_deps/nanobind/include/nanobind/nb_misc.h` で確認のこと。
- Python 3.13t / 3.14t (Free-Threading) では GIL 自体がないため `gil_scoped_release` の意味が異なる。 `nb::ft_mutex` でカバーされる範囲も含めて確認する。

### 採用しなかった案

- **デストラクタ内で `close()` を Python 側から呼んでもらう前提に立つ**: 採用しない。 ライブラリとして「ユーザーが close() を呼び忘れた場合に hang する」のは品質として後退。

## テスト追加

issue 0003 と同形の regression テスト 5 ケースを追加する:

- 4 ファイル / 各 1 ケース: `del encoder` でデストラクタが呼ばれる際にデッドロックしないこと
  - `tests/test_video_encoder_destructor_deadlock.py`
  - `tests/test_audio_encoder_destructor_deadlock.py`
  - `tests/test_video_decoder_destructor_deadlock.py`
  - `tests/test_audio_decoder_destructor_deadlock.py`
- 1 ファイル: `VideoDecoder` を再 configure する時にデッドロックしないこと
  - `tests/test_video_decoder_reconfigure_deadlock.py`

テスト概形は issue 0003 のテスト概形 (`closer = threading.Thread(target=...)` + `closer.join(timeout=3)`) を踏襲する。 デストラクタテストは `closer = threading.Thread(target=lambda: gc.collect())` のように GC 経由で発火させる形で書く (もしくは `target=lambda: ... ; del encoder` のスコープ制御)。

詳細は実装時に確定する。

## 完了条件

- 4 デストラクタおよび `VideoDecoder::cleanup_decoder()` の `stop_worker()` 呼び出しが `nb::gil_scoped_release` で囲まれている
- 上記 5 ファイル / 5 ケースの regression テストが追加され、`make develop` 後に `NO_UV_SYNC=1 uv run pytest <該当ファイル> --timeout=10` で PASS する
- `make format` 適用後、 差分が出ない
- `uv run ty check` が PASS する
- `docs/PYTHON_INTERFACE.md` は更新不要 (内部実装変更のみ)
- `CHANGES.md` の `## develop` セクションに以下の `[FIX]` エントリを追加する:

  ```
  - [FIX] VideoEncoder / AudioEncoder / VideoDecoder / AudioDecoder のデストラクタと VideoDecoder::configure 経由で GIL 保持時にデッドロックする不具合を修正する
    - @<担当者>
  ```

## 解決方法
