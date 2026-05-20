# VideoDecoder::reset() で state を UNCONFIGURED に遷移するよう WebCodecs 仕様準拠に修正する

- Priority: Medium
- Created: 2026-05-20
- Model: Opus 4.7
- Branch: feature/fix-video-decoder-reset-unconfigured-state

## 目的

`VideoDecoder::reset()` が WebCodecs 仕様に従って state を `UNCONFIGURED` に遷移するよう修正する。

現実装は state を変更しない (`CONFIGURED` のまま) ため、 W3C WebCodecs 仕様の Reset VideoDecoder algorithm `Set [[state]] to "unconfigured".` (https://www.w3.org/TR/webcodecs/#dom-videodecoder-reset の手順) に違反している。 加えて他コーデックの reset (AudioEncoder / AudioDecoder / VideoEncoder) は内部で `close()` を呼んで state を `UNCONFIGURED` に戻しており、 VideoDecoder だけ挙動が非対称。

## 優先度根拠

Medium。

- 仕様違反: WebCodecs 仕様で reset 後の state は `unconfigured` と明記されている。
- 実害: state を確認するアプリは reset 後の値が他コーデックと違うため期待外れの挙動になる。 ただし state を読まないアプリには影響しない。
- 同型 issue (0003 / 0004) と一緒に整理しておきたい技術的負債。
- 緊急度は High ほどではないが、 仕様準拠の方向に揃えるのが望ましい。

## 現状

### 該当箇所

`src/bindings/video_decoder.cpp:270-305` の `VideoDecoder::reset()`:

```cpp
void VideoDecoder::reset() {
  if (state_ == CodecState::CLOSED) {
    throw std::runtime_error("Decoder is closed");
  }

  // ワーカースレッドを停止
  stop_worker();

  // キューをクリア
  {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    while (!decode_queue_.empty()) {
      decode_queue_.pop();
    }
    pending_tasks_ = 0;
  }

  // 出力バッファをクリア
  {
    std::lock_guard<std::mutex> lock(output_mutex_);
    output_buffer_.clear();
    next_output_sequence_ = 0;
  }

  // シーケンス番号をリセット
  next_sequence_number_ = 0;

  // デコーダーをリセット
  if (decoder_context_) {
    cleanup_decoder();
    init_decoder();
  }

  // ワーカースレッドを再開
  start_worker();
}
```

state を変更していないため、 reset 後も `CodecState::CONFIGURED` のまま。

### 他コーデックとの比較

- `VideoEncoder::reset()` (`video_encoder.cpp:495-524`): 内部で `close()` を呼び、 その後 `state_ = CodecState::UNCONFIGURED;` で UNCONFIGURED に戻す。
- `AudioEncoder::reset()` (`audio_encoder.cpp:234-263`): 同上。
- `AudioDecoder::reset()` (`audio_decoder.cpp:159-188`): 同上。
- `VideoDecoder::reset()` のみ `cleanup_decoder()` → `init_decoder()` の経路で再初期化し、 state は変えない。

### WebCodecs 仕様

W3C WebCodecs ( https://www.w3.org/TR/webcodecs/#dom-videodecoder-reset ) の Reset VideoDecoder algorithm:

1. (前略)
2. (前略)
3. `Set [[state]] to "unconfigured".`
4. (後略)

仕様では reset() 後の state は明確に `unconfigured` と規定されている。

## 設計方針

### 修正方針

`VideoDecoder::reset()` の挙動を他コーデックと揃え、 内部で `cleanup_decoder()` を呼んで state を `UNCONFIGURED` に戻す。 ユーザーが再度 `configure()` を呼んで利用を再開するフローに合わせる。

例:

```cpp
void VideoDecoder::reset() {
  if (state_ == CodecState::CLOSED) {
    throw std::runtime_error("Decoder is closed");
  }

  // ワーカースレッドを停止
  stop_worker();

  // キューと出力バッファをクリア (現状通り)
  // ...

  // シーケンス番号をリセット (現状通り)
  next_sequence_number_ = 0;

  // デコーダーを完全に解放 (init_decoder() は呼ばない)
  if (decoder_context_) {
    cleanup_decoder();
  }

  state_ = CodecState::UNCONFIGURED;

  // ワーカースレッドを再開
  start_worker();
}
```

`init_decoder()` を呼ばず `cleanup_decoder()` のみ呼ぶ。 reset 後の利用は `configure()` → `decode()` のフローを必須とする。

### 後方互換性

WebCodecs API はまだ正式リリースされておらず、 CLAUDE.md に「後方互換性は考慮しない」 と明記されているため、 reset 後の利用フロー変更は許容する。

### テスト追加

- `tests/test_video_decoder_close_reset_deadlock.py` の reset 用テストに state アサート (`assert decoder.state == CodecState.UNCONFIGURED`) を追加する。 (issue 0003 対応時に削除したものを復活させる)
- `tests/test_video_decoder_reset.py` (新規) で「reset 後に再 configure → decode できること」 を検証する。

## 完了条件

- `VideoDecoder::reset()` 末尾で `state_ = CodecState::UNCONFIGURED;` に遷移している
- `cleanup_decoder()` のみを呼び `init_decoder()` を呼ばないように変更されている
- `tests/test_video_decoder_close_reset_deadlock.py` の reset 用テストに state アサートが追加されている
- `tests/test_video_decoder_reset.py` (新規) で「reset → configure → decode」 のフローが PASS する
- `make develop` 後に `NO_UV_SYNC=1 uv run pytest --timeout=10` で既存テスト含め全 PASS する
- `make format` 適用後、 差分が出ない
- `uv run ty check` が PASS する
- `CHANGES.md` の `## develop` セクションに以下の `[FIX]` エントリを追加する:

  ```
  - [FIX] VideoDecoder.reset() を WebCodecs 仕様に準拠させ、 state を UNCONFIGURED に遷移するよう修正する
    - @<担当者>
  ```

## 解決方法
