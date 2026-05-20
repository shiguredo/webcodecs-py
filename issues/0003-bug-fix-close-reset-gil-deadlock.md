# Encoder / Decoder の close と reset の binding で GIL 保持デッドロックが発生する

- Priority: High
- Created: 2026-05-20
- Model: Opus 4.7
- Branch: feature/fix-close-reset-gil-deadlock

## 目的

`VideoEncoder` / `AudioEncoder` / `VideoDecoder` / `AudioDecoder` の `close()` および `reset()` を呼び出した際に、メインスレッドが GIL を保持したまま `worker_thread_.join()` を待つことで発生する相互待ちデッドロックを解消する。

## 優先度根拠

High。

- 発火すると Python プロセス全体が応答しなくなり、外部からの SIGTERM 以外で復帰できない
- pytest-timeout の thread interrupt (`thread.interrupt_main`) は次の Python bytecode 評価まで届かないため、C 拡張内 GIL 保持中の本デッドロックには効かない
- 利用側 (Python アプリケーション側) では回避不可能で、ライブラリ側で塞ぐ以外の手段がない
- `close()` は実利用シナリオで必ず通過する経路なので、callback タイミング次第で flaky に発火する

## 現状

### 該当 binding (GIL release 未付与)

| ファイル | 行 | binding |
| ---- | ---- | ---- |
| `src/bindings/video_encoder.cpp` | 940 | `.def("reset", &VideoEncoder::reset, ...)` |
| `src/bindings/video_encoder.cpp` | 941 | `.def("close", &VideoEncoder::close, ...)` |
| `src/bindings/audio_encoder.cpp` | 460 | `.def("reset", &AudioEncoder::reset, ...)` |
| `src/bindings/audio_encoder.cpp` | 461 | `.def("close", &AudioEncoder::close, ...)` |
| `src/bindings/video_decoder.cpp` | 806 | `.def("reset", &VideoDecoder::reset, ...)` |
| `src/bindings/video_decoder.cpp` | 807 | `.def("close", &VideoDecoder::close, ...)` |
| `src/bindings/audio_decoder.cpp` | 383 | `.def("reset", &AudioDecoder::reset, ...)` |
| `src/bindings/audio_decoder.cpp` | 384 | `.def("close", &AudioDecoder::close, ...)` |

`flush` と `encode` (および `decode`) には `nb::call_guard<nb::gil_scoped_release>()` が付与済み (例: `video_encoder.cpp:883`, `938`) なため同経路は緩和されているが、`close` / `reset` だけ漏れている。

### C++ 実装側の経路

各 codec とも以下の同型の経路を持つ:

- `close()` → `stop_worker()` → `worker_thread_.join()`
- `reset()` → `stop_worker()` → `worker_thread_.join()`

ワーカースレッドは `worker_loop()` 内で `nb::gil_scoped_acquire gil` を取得して Python 側 callback を呼び出す。GIL を取得する箇所の例:

- `video_encoder.cpp`: 320, 377, 426, 690, 818 行
- `audio_encoder.cpp`: 168, 199, 436 行
- `video_decoder.cpp`: 133, 147, 176, 232, 568, 587, 742, 780 行
- `audio_decoder.cpp`: 130, 148, 358 行

### デッドロックの成立条件

メインスレッドが GIL を保持したまま `close()` / `reset()` を呼んだ瞬間に、ワーカースレッドが callback の `nb::gil_scoped_acquire gil` の直前まで進んでいると、

1. メインスレッド: GIL 保持で `worker_thread_.join()` を待つ
2. ワーカースレッド: `nb::gil_scoped_acquire gil` で GIL 取得を待つ

の相互待ちが成立して永久にデッドロックする。`flush` / `encode` 系には GIL release が入っているため、本デッドロックは `close` / `reset` 経路に限定される。

### 影響範囲

- `VideoEncoder` は実際にデッドロックを観測している (`worker_thread_.join()` の前で main thread が停止していることを確認済み)
- `AudioEncoder` / `VideoDecoder` / `AudioDecoder` は同じ binding 漏れと同じ C++ 経路を持つため、callback タイミング次第で同様のデッドロックが発火し得る。今日まで発火していないのは確率的な偶然と判定する

## 設計方針

### 修正方針

該当 8 箇所の binding に `nb::call_guard<nb::gil_scoped_release>()` を付与する。例として `video_encoder.cpp:940-941` は以下に変更する:

```cpp
.def("reset", &VideoEncoder::reset,
     nb::call_guard<nb::gil_scoped_release>(),
     nb::sig("def reset(self, /) -> None"))
.def("close", &VideoEncoder::close,
     nb::call_guard<nb::gil_scoped_release>(),
     nb::sig("def close(self, /) -> None"))
```

書式は既存の `flush` の binding (`nb::call_guard<nb::gil_scoped_release>()` を `nb::sig(...)` より前に置く) に合わせる。

修正は 4 ファイルそれぞれに同形パターンを適用するだけで、C++ 実装側 (`VideoEncoder::close` 本体等) には手を入れない。

### 採用しなかった案

- **C++ 実装側で `gil_scoped_release` を関数内に置く**: 採用しない。実装側に nanobind 依存が漏れる。binding 層で完結する方が層分離として明快
- **`close` だけ修正して `reset` は触らない**: 採用しない。両者とも同じ `stop_worker()` 経由なので、漏らすと同じデッドロックが reset 経由でも残る

### テスト追加

#### 単体テスト (4 件)

`tests/test_<codec>_close_deadlock.py` の形で各 codec に 1 件ずつ regression テストを追加する。

概形:

```python
def test_close_does_not_deadlock_with_callback_in_flight():
    started = threading.Event()
    release = threading.Event()

    def on_output(chunk, metadata):
        started.set()
        release.wait(timeout=5)

    encoder = VideoEncoder(on_output)
    encoder.configure(...)
    encoder.encode(make_frame())
    assert started.wait(timeout=10)

    closer = threading.Thread(target=encoder.close)
    closer.start()
    closer.join(timeout=10)
    assert not closer.is_alive()
    release.set()
```

- callback 内で `threading.Event.wait()` を入れて、ワーカースレッドが GIL 取得待ち状態に到達させる
- 別スレッドから `close()` を呼び、N 秒以内に return することを `Thread.join(timeout=N)` で確認する
- binding に GIL release が無いと `closer.join(timeout=10)` が timeout し `closer.is_alive()` が True になる
- 4 codec ぶん (`VideoEncoder` / `AudioEncoder` / `VideoDecoder` / `AudioDecoder`) を作る。callback で受け取る型と `configure` 引数は codec ごとに調整する

CLAUDE.md の規則に従い、テスト本体は `def` で書き class は使わない。lambda も使わない。モックとスタブは使わず、本物の codec を起動する。`pytest --timeout=10` を必ず付ける。

## 完了条件

- 該当 8 箇所の binding に `nb::call_guard<nb::gil_scoped_release>()` が付与されている
- 4 codec それぞれに「callback 実行中の `close()` で hang しない」 regression テストが追加され、`make develop` 後に `NO_UV_SYNC=1 uv run pytest --timeout=10` で PASS する
- `make format` 適用後の差分が無い
- `uv run ty check` が PASS する
- `CHANGES.md` の `## develop` セクションに `[FIX]` エントリが追加されている

## 解決方法
