# VideoEncoder / AudioEncoder / VideoDecoder / AudioDecoder の close と reset のバインディングで GIL 保持デッドロックが発生する

- Priority: High
- Created: 2026-05-20
- Model: Opus 4.7
- Branch: feature/fix-close-reset-gil-deadlock

## 目的

`VideoEncoder` / `AudioEncoder` / `VideoDecoder` / `AudioDecoder` の `close()` および `reset()` を呼び出した際に、メインスレッドが GIL を保持したまま `worker_thread_.join()` を待つことで発生する相互待ちデッドロックを解消する。

## 優先度根拠

High。発火すると Python プロセス全体が応答しなくなり、外部からの SIGTERM 以外で復帰できない。`close()` は実利用シナリオで必ず通過する経路で、利用側 (Python アプリケーション側) では回避不可能。ライブラリ側で塞ぐ以外の手段がない。

## 現状

### 該当バインディング (GIL release 未付与)

| ファイル | 行 | バインディング |
| ---- | ---- | ---- |
| `src/bindings/video_encoder.cpp` | 940 | `.def("reset", &VideoEncoder::reset, ...)` |
| `src/bindings/video_encoder.cpp` | 941 | `.def("close", &VideoEncoder::close, ...)` |
| `src/bindings/audio_encoder.cpp` | 460 | `.def("reset", &AudioEncoder::reset, ...)` |
| `src/bindings/audio_encoder.cpp` | 461 | `.def("close", &AudioEncoder::close, ...)` |
| `src/bindings/video_decoder.cpp` | 806 | `.def("reset", &VideoDecoder::reset, ...)` |
| `src/bindings/video_decoder.cpp` | 807 | `.def("close", &VideoDecoder::close, ...)` |
| `src/bindings/audio_decoder.cpp` | 383 | `.def("reset", &AudioDecoder::reset, ...)` |
| `src/bindings/audio_decoder.cpp` | 384 | `.def("close", &AudioDecoder::close, ...)` |

`flush` / `encode` / `decode` には GIL release が付与済みなので、同経路でのデッドロックは発生しない。

### C++ 実装側の経路

#### 共通経路

各コーデックとも以下の経路を持つ:

- `close()` → `stop_worker()` → `worker_thread_.join()`
- `reset()` → `stop_worker()` → `worker_thread_.join()`

ワーカースレッドは `worker_loop()` 内で `nb::gil_scoped_acquire gil` を取得して Python 側コールバック (`output_callback_` / `error_callback_` / `dequeue_callback_`) を呼び出す。

#### `reset()` 内部で `close()` を呼ぶコーデック

`VideoEncoder` / `AudioEncoder` / `AudioDecoder` の `reset()` は内部で `close()` を呼んだ後 `start_worker()` で再開する経路 (`video_encoder.cpp:495-524`, `audio_encoder.cpp:234-263`, `audio_decoder.cpp:159-188`)。`VideoDecoder` のみ `cleanup_decoder()` → `init_decoder()` の経路で再開する (`video_decoder.cpp:270-305`)。いずれも先頭で `stop_worker()` を呼んでおり、最初の `join()` でデッドロックする可能性がある点は共通。

#### `AudioDecoder::close()` のみが `flush()` を内部で呼ぶ

`audio_decoder.cpp:190-217` の `AudioDecoder::close()` は他のコーデックと異なり、`stop_worker()` の前に `flush()` を呼ぶ。`AudioDecoder::flush()` は `queue_cv_.wait(lock, [this]() { return decode_queue_.empty() && pending_tasks_ == 0; });` でワーカースレッドの進捗を待つ (`audio_decoder.cpp:153-157`)。

そのため `AudioDecoder::close()` でのデッドロックは 2 段階で発火し得る:

1. メインスレッドが GIL を保持したまま `close()` に入る。
2. 内部 `flush()` の `queue_cv_.wait()` で待機する。ワーカースレッドがコールバックの `nb::gil_scoped_acquire` 待ちなら `pending_tasks_` が減らず flush は永久に抜けられない。

バインディングに GIL release を付与すれば、ワーカースレッドが GIL を取得してコールバックを完了でき、`flush()` の wait と続く `stop_worker()` の `join()` 双方が正常に完了する。

### デッドロックの成立条件

メインスレッドが GIL を保持したまま `close()` / `reset()` を呼んだ瞬間に、ワーカースレッドがコールバックロジックに到達し `nb::gil_scoped_acquire` で GIL の取得を試みている (ブロック中の) タイミングだと、

1. メインスレッド: GIL 保持で `worker_thread_.join()` (または `AudioDecoder` では先に `queue_cv_.wait()`) を待つ
2. ワーカースレッド: `nb::gil_scoped_acquire` で GIL 取得を待つ

の相互待ちが成立して永久にデッドロックする。

### 影響範囲

#### 修正対象 (本 issue のスコープ)

| クラス | バインディング経由のデッドロック |
| ---- | ---- |
| `VideoEncoder` | `close()` / `reset()` でデッドロックを実観測 (AV1 / libaom 経路)。観測時の状況: 別スレッドから `close()` を呼んでも進まず、`worker_thread_.join()` 直前でメインスレッドが停止 |
| `AudioEncoder` | バインディング漏れと同じ C++ 経路を持つため、コールバックタイミング次第で同様のデッドロックが発火し得る |
| `VideoDecoder` | 同上 |
| `AudioDecoder` | 同上。さらに `close()` 内 `flush()` 経由の別形デッドロック経路も存在する |

ワーカースレッドが起動しないのは `VideoEncoder` / `VideoDecoder` の `VideoToolbox` 経路のみ (`video_encoder.cpp:151-155` で `uses_videotoolbox()`、`video_decoder.cpp:96-104` で `uses_apple_video_toolbox()` を判定)。NVIDIA Video Codec と Intel VPL の経路は Linux 側で無条件にワーカースレッドを起動するため (`video_decoder.cpp:106-108` の `#else` 分岐) 本デッドロックの対象。AudioEncoder / AudioDecoder はすべてのコーデックでワーカースレッドを起動する (`audio_encoder.cpp:132-135`, `audio_decoder.cpp:94-97`)。

#### スコープ外 (別 issue で対応)

各コーデックのデストラクタも `stop_worker()` → `worker_thread_.join()` を呼ぶため、Python GC で破棄される際に同型のデッドロックが発生し得る:

- `video_encoder.cpp:30-33` `~VideoEncoder()`
- `audio_encoder.cpp:43-46` `~AudioEncoder()`
- `video_decoder.cpp:41-44` `~VideoDecoder()`
- `audio_decoder.cpp:43-46` `~AudioDecoder()`

加えて `VideoDecoder::configure()` (`video_decoder.cpp:60-110`) は既存デコーダーがあれば `cleanup_decoder()` を呼び、その中で `stop_worker()` を呼ぶ (`video_decoder.cpp:471-473`)。再 configure 時に同型のデッドロックが発火し得る。

デストラクタはバインディング関数ではないため `nb::call_guard` では対処できず、C++ 実装側で `nb::gil_scoped_release` を取る変更が必要になる。本 issue では扱わず、本 issue クローズと同じコミットで作業者がデストラクタ経路と `VideoDecoder::configure` 経路を扱う後続 issue (`issues/0004-bug-fix-destructor-gil-deadlock.md`) を新規発行する (`issues/SEQUENCE` を 5 に更新)。

## 設計方針

### 修正方針

該当 8 箇所のバインディングに `nb::call_guard<nb::gil_scoped_release>()` を付与する。例として `video_encoder.cpp:940-941` は以下に変更する:

```cpp
.def("reset", &VideoEncoder::reset,
     nb::call_guard<nb::gil_scoped_release>(),
     nb::sig("def reset(self, /) -> None"))
.def("close", &VideoEncoder::close,
     nb::call_guard<nb::gil_scoped_release>(),
     nb::sig("def close(self, /) -> None"))
```

書式は既存の `flush` のバインディング (例: `audio_encoder.cpp:457-459`) に合わせる。

修正は 4 ファイルそれぞれに同形パターンを適用するだけで、C++ 実装側 (`VideoEncoder::close` 本体等) には手を入れない。バインディング層で完結させる理由: `call_guard` を 1 行追加するだけで済み、 C++ 実装側の他経路 (通常呼び出し) への影響がない。デストラクタ経路はバインディング関数を経由しないため別 issue で C++ 側に `nb::gil_scoped_release` を入れて対処する。

`close()` / `reset()` の本体は Python オブジェクトに直接アクセスしないため、メソッド全体を GIL 解放区間にしても安全。`VideoDecoder::reset()` は `state_ == CLOSED` で `std::runtime_error` を投げる経路があるが (`video_decoder.cpp:271-273`)、`nb::call_guard<nb::gil_scoped_release>` は `gil_scoped_release` のデストラクタで GIL を取り戻すため、例外伝播経路でも GIL は正しく復元される。

### テスト追加

`tests/` 直下に 4 ファイル / 8 ケースを追加する:

- `tests/test_video_encoder_close_reset_deadlock.py` (close 用 / reset 用 各 1 ケース)
- `tests/test_audio_encoder_close_reset_deadlock.py` (同上)
- `tests/test_video_decoder_close_reset_deadlock.py` (同上)
- `tests/test_audio_decoder_close_reset_deadlock.py` (同上)

ファイル分割の理由: 既存 `tests/test_parallel_video.py` / `tests/test_parallel_audio.py` のように対象クラス単位で分かれている命名規則に合わせる。pytest の filter (`pytest tests/test_video_encoder_*` 等) でクラスごとに走らせやすくする。

#### 修正前にテストが決定的に失敗する根拠

各コーデックのワーカーループは「コールバックを呼んでから `pending_tasks_--` を実行する」順序になっている (例: `video_encoder.cpp:705-714`)。コールバック内で `threading.Event.wait()` を呼んで待機させると、ワーカーはコールバックから戻らないため `pending_tasks_` が減らない。この状態でメインスレッド (修正前: GIL 保持) が `close()` を呼ぶと:

- `worker_thread_.join()` (または `AudioDecoder::close()` では先に `queue_cv_.wait()`) でブロック
- ワーカーは `Event.wait` の timeout 後に Python レベルへ戻ろうとして GIL を再取得する必要があるが、メインスレッドが GIL を保持しているため取得できない

両者の相互待ちは確実に成立する。修正前のテストは確率ではなく決定的に失敗する。

修正後はバインディング進入時に GIL が解放され、ワーカーは GIL を取得できるためコールバックを完了でき、`pending_tasks_--` まで進んで `close()` が正常に完了する。

#### テスト概形 (VideoEncoder の場合)

```python
import threading

import numpy as np

from webcodecs import (
    LatencyMode,
    VideoEncoder,
    VideoEncoderConfig,
    VideoFrame,
    VideoFrameBufferInit,
    VideoPixelFormat,
)


def create_test_frame(width: int, height: int, timestamp: int) -> VideoFrame:
    y_plane = np.full((height, width), 128, dtype=np.uint8)
    u_plane = np.full((height // 2, width // 2), 128, dtype=np.uint8)
    v_plane = np.full((height // 2, width // 2), 128, dtype=np.uint8)
    data = np.concatenate([y_plane.flatten(), u_plane.flatten(), v_plane.flatten()])
    init: VideoFrameBufferInit = {
        "format": VideoPixelFormat.I420,
        "coded_width": width,
        "coded_height": height,
        "timestamp": timestamp,
    }
    return VideoFrame(data, init)


def test_close_does_not_deadlock_when_callback_in_flight():
    started = threading.Event()
    release = threading.Event()

    # VideoEncoder の出力コールバックは (chunk, metadata) の 2 引数で呼ばれる
    def on_output(chunk, metadata=None):
        started.set()
        # 短時間待機して closer の close() がデッドロックする race window を作る。
        # 修正後はワーカーがこの wait から戻る際に GIL を取れて callback を完了でき、
        # close() が正常終了する。 wait の timeout は closer.join の timeout より短く取る。
        release.wait(timeout=1)

    def on_error(error):
        pass

    encoder = VideoEncoder(on_output, on_error)
    # AV1 はソフトウェアコーデック経路 (libaom) を確実に通る。
    # AVC / HEVC は macOS で VideoToolbox に流れてワーカースレッドが起動しないため再現しない。
    # REALTIME はテスト所要時間を短縮するため。
    config: VideoEncoderConfig = {
        "codec": "av01.0.04M.08",
        "width": 320,
        "height": 240,
        "bitrate": 200_000,
        "framerate": 30.0,
        "latency_mode": LatencyMode.REALTIME,
    }
    encoder.configure(config)
    encoder.encode(create_test_frame(320, 240, 0), {"key_frame": True})

    assert started.wait(timeout=3), "エンコーダーの出力コールバックが呼ばれなかった"

    closer = threading.Thread(target=encoder.close)
    closer.start()
    try:
        closer.join(timeout=3)
        assert not closer.is_alive(), "close() がデッドロックした"
    finally:
        release.set()
        closer.join(timeout=3)
```

`reset` 用は `closer = threading.Thread(target=encoder.reset)` に差し替えて同型に書く。 reset 後に worker が再起動する挙動を確認するため、 `closer.join` 後に `encoder.state` をアサート (`VideoEncoder` / `AudioEncoder` / `AudioDecoder` は `CodecState.UNCONFIGURED`、 `VideoDecoder` は `CodecState.CONFIGURED` のまま) する 1 行を追加する。

#### コーデックごとの差分

| クラス | コーデック設定 | コールバック | 補足 |
| ---- | ---- | ---- | ---- |
| `VideoEncoder` | `av01.0.04M.08`, 320x240 | `on_output(chunk, metadata=None)` | 概形のとおり |
| `AudioEncoder` | `opus`, 48000 Hz, 2 ch | `on_output(chunk, metadata=None)` | `encode()` は `(audio_data)` のみで options 引数なし。`AudioData` を `tests/test_opus_last_frame.py` を参考に生成 |
| `VideoDecoder` | `av01.0.04M.08` | `on_output(frame)` | 事前に `VideoEncoder` で 1 チャンクを生成 (下記) |
| `AudioDecoder` | `opus` | `on_output(audio_data)` | 事前に `AudioEncoder` で 1 チャンクを生成 |

Decoder 用テストの事前準備は以下の順:

1. Encoder を組み立て、1 フレーム / 1 サンプルを encode する。
2. `encoder.flush()` で出力コールバックが呼ばれるまで待つ (collect 用コールバックでチャンクを `list` に蓄積し、`threading.Event` でシグナルする)。
3. `encoder.close()` で encoder を片付ける。
4. Decoder を組み立て、上記 chunk を `decoder.decode(chunk)` に渡す。
5. Decoder 側の `on_output` で `release.wait(timeout=1)` を入れて待機させ、別スレッドから `decoder.close()` を呼ぶ。

`AudioDecoder` も同型。AAC (`mp4a.*`) は macOS の AudioConverter 経由でワーカースレッドが起動するためテスト対象にできるが、本テストでは全プラットフォーム (Linux / macOS / Windows) で動く `opus` を採用する。

#### クリーンアップの限界

`try/finally` で `release.set()` を必ず呼ぶが、修正前のデッドロック発生時は closer が GIL を保持したまま `worker_thread_.join()` でブロックしているため、 `release.set()` を呼んでもワーカーは GIL を取り直せず `Event.wait` から戻れない。 結果としてワーカーはプロセス終了まで残り、 さらに `finally` 内の `release.set()` を実行するメインスレッドも GIL を取得できないため、 pytest プロセス全体が応答不能になる。 `pytest --timeout-method=signal` (POSIX のみ) でも、 SIGALRM の Python ハンドラ実行に GIL が必要なため、 closer が GIL を保持している間は救えない。 修正前のテスト実行で hang した場合は手動で SIGKILL するしかない (実装時に VideoEncoder で実際に確認した挙動)。 修正後は GIL 解放によりワーカーが復帰できるため、 `finally` のクリーンアップが意図通りに機能する。

## 完了条件

- 該当 8 箇所のバインディングに `nb::call_guard<nb::gil_scoped_release>()` が付与されている (確認コマンド例: `grep -nE '\.def\("(close|reset)"' src/bindings/{video,audio}_{encoder,decoder}.cpp` で出る全行に直後の行 `nb::call_guard<nb::gil_scoped_release>()` が並ぶこと)
- 上記 4 ファイル / 8 ケースの regression テストが追加され、`make develop` 後に `NO_UV_SYNC=1 uv run pytest tests/test_video_encoder_close_reset_deadlock.py tests/test_audio_encoder_close_reset_deadlock.py tests/test_video_decoder_close_reset_deadlock.py tests/test_audio_decoder_close_reset_deadlock.py --timeout=10` で PASS する
- `make format` 適用後、差分が出ない (C/C++ と Python テストの双方)
- `uv run ty check` が PASS する
- `docs/PYTHON_INTERFACE.md` は更新不要 (公開 API の引数・戻り値・例外仕様は変わらず、内部実装変更のみ)
- `CHANGES.md` の `## develop` セクションに、種別順 (CHANGE → ADD → UPDATE → FIX) で `[UPDATE]` の後に以下の `[FIX]` エントリを追加する。担当者は実装者の GitHub username を `@` 行で書く (CHANGES.md の既存エントリと同形式、本文行から 2 文字インデント):

  ```
  - [FIX] VideoEncoder / AudioEncoder / VideoDecoder / AudioDecoder の close() / reset() で GIL 保持時にデッドロックする不具合を修正する
    - @<担当者>
  ```

- 本 issue クローズと同じコミットで、デストラクタ経路と `VideoDecoder::configure` 経路を扱う後続 issue (`issues/0004-bug-fix-destructor-gil-deadlock.md`) を新規発行し、`issues/SEQUENCE` を 5 に更新する (内容: 「### スコープ外 (別 issue で対応)」 で挙げた 4 デストラクタ + `VideoDecoder::configure` の `cleanup_decoder()` 経由経路に C++ 側で `nb::gil_scoped_release` を取る修正)
