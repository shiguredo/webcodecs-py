# 並行 close() / reset() による std::thread::join の二重呼び出し UB を解消する

- Priority: Medium
- Created: 2026-05-20
- Model: Opus 4.7
- Branch: feature/fix-concurrent-close-reset

## 目的

`VideoEncoder` / `AudioEncoder` / `VideoDecoder` / `AudioDecoder` の `stop_worker()` が並行実行された際に、 同一の `worker_thread_` インスタンスに対する `std::thread::join()` を二つのスレッドが同時に呼ぶことで発生する未定義動作を解消する。 close()/reset() 間 (および両者と Python GC 経由のデストラクタ) の相互排他を導入する。

## 優先度根拠

Medium。

- issue 0003 で close()/reset() のバインディングに `nb::call_guard<nb::gil_scoped_release>()` を付与した結果、 同じインスタンスに対する複数スレッドからの close()/reset() 呼び出しが CPython GIL によってシリアライズされなくなった。
- `stop_worker()` の構造 (`if (worker_thread_.joinable()) worker_thread_.join();`) は GIL によるシリアライズを暗黙の前提にしていたため、 二つのスレッドが同時に `joinable()` で true を観測し、 両方が `join()` を呼んで UB に陥る可能性がある。
- 実用シナリオ (アプリケーション側で意図的に二重 close を呼ぶ) は稀だが、 Python GC によるデストラクタ呼び出しと明示 close() の race など、 暗黙の経路で発火し得る。
- 緊急度は High ではないが、 issue 0003 で導入した修正の副作用として記録する。

## 現状

### 該当箇所

各コーデックの `stop_worker()`:

| ファイル | 行 | 該当 |
| ---- | ---- | ---- |
| `src/bindings/video_encoder.cpp` | 641-651 | `VideoEncoder::stop_worker()` |
| `src/bindings/audio_encoder.cpp` | 342-352 | `AudioEncoder::stop_worker()` |
| `src/bindings/video_decoder.cpp` | 675-685 | `VideoDecoder::stop_worker()` |
| `src/bindings/audio_decoder.cpp` | 264-274 | `AudioDecoder::stop_worker()` |

構造 (例 `audio_decoder.cpp:264-274`):

```cpp
void AudioDecoder::stop_worker() {
  {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    should_stop_ = true;
  }
  queue_cv_.notify_all();

  if (worker_thread_.joinable()) {
    worker_thread_.join();
  }
}
```

### 競合シナリオ

スレッド A・B が同時に `encoder.close()` を呼ぶ:

1. A: call_guard で GIL 解放 → `stop_worker()` 開始 → `should_stop_ = true` → `queue_cv_.notify_all()`
2. B: call_guard で GIL 解放 (A と並行可能) → `stop_worker()` 開始 → `should_stop_ = true` (冪等) → `queue_cv_.notify_all()` (冪等)
3. A: `if (worker_thread_.joinable())` → true → `worker_thread_.join()` 待機開始
4. B: `if (worker_thread_.joinable())` → true (A はまだ join 完了していない) → `worker_thread_.join()` 待機開始
5. cppreference: 同一 thread に対する concurrent な join() は未定義動作

加えて `reset()` 内では `stop_worker()` 後に `start_worker()` で `worker_thread_ = std::thread(...)` の move 代入が走る。 A の reset と B の close が並行すると、 B が join 中に A が `worker_thread_` を上書きする更に危険な構成になる。

## 設計方針

### 修正方針

各コーデックヘッダに `std::mutex lifecycle_mutex_;` メンバーを追加し、 `close()` / `reset()` の本体先頭で `std::lock_guard<std::mutex> guard(lifecycle_mutex_);` を取って相互排他する。 デストラクタも同様に lifecycle_mutex_ を取得する (issue 0004 のデストラクタ修正と合わせて検討)。

例 (`src/bindings/audio_decoder.cpp`):

```cpp
void AudioDecoder::close() {
  std::lock_guard<std::mutex> guard(lifecycle_mutex_);
  if (state_ == CodecState::CLOSED) {
    return;
  }
  flush();
  stop_worker();
  // 以下既存処理
}

void AudioDecoder::reset() {
  std::lock_guard<std::mutex> guard(lifecycle_mutex_);
  // 既存処理
}
```

`stop_worker()` 自体には lifecycle_mutex_ を取らない。 close()/reset() 内部から呼ばれる前提で、 既に外側が排他済み。

### 注意点

- `AudioDecoder::close()` は内部で `flush()` を呼ぶ。 `AudioDecoder::flush()` は `queue_cv_.wait()` でワーカー進捗を待つため、 close 中に flush 経由で long-wait が発生する。 この間 lifecycle_mutex_ を保持する形になるが、 これは意図通り (close と並行する別 close/reset を待たせる)。
- issue 0004 で C++ 側に `nb::gil_scoped_release` を入れるデストラクタ修正と組み合わせる必要がある。

### 採用しなかった案

- **`stop_worker()` 内部に lifecycle_mutex_ を入れる**: 採用しない。 close()/reset() の他処理 (`state_` 更新、 リソース解放) も含めて排他したいため、 外側で取る方が一貫する。
- **既存の `queue_mutex_` を流用**: 採用しない。 queue_mutex_ はキュー操作の排他に使われており、 long-running な close/reset を queue_mutex_ で覆うと encode/decode の queue 操作が長時間ブロックされる。

## テスト追加

- `tests/test_<codec>_concurrent_close_reset.py` (新規 4 ファイル): 複数スレッドから close()/reset() を同時呼び出ししてもクラッシュしないことを確認。 ThreadSanitizer を使えば UB 検出も可能だが、 まずは smoke test で着地させる。

## 完了条件

- 4 コーデックのヘッダに `std::mutex lifecycle_mutex_;` が追加されている
- 4 コーデックの `close()` / `reset()` 本体先頭で `lifecycle_mutex_` を取っている
- 新規 4 ファイルの smoke test が PASS する
- 既存テストが全 PASS する
- `make format` 適用後、 差分が出ない
- `uv run ty check` が PASS する
- `docs/PYTHON_INTERFACE.md` は更新不要 (内部実装変更のみ)
- `CHANGES.md` の `## develop` セクションに以下の `[FIX]` エントリを追加する:

  ```
  - [FIX] 並行 close() / reset() による std::thread::join の二重呼び出し UB を解消する
    - @<担当者>
  ```

## 解決方法
