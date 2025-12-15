"""Free-Threading 環境での並列アクセステスト

このテストは Python 3.13t/3.14t の Free-Threading ビルドでのみ実行される。
GIL ビルドではスキップされる。
"""

import sys
import threading
from concurrent.futures import ThreadPoolExecutor

import numpy as np
import pytest

from webcodecs import (
    AudioData,
    AudioDecoder,
    AudioDecoderConfig,
    AudioEncoder,
    AudioEncoderConfig,
    AudioSampleFormat,
    EncodedAudioChunk,
    EncodedVideoChunk,
    LatencyMode,
    VideoDecoder,
    VideoDecoderConfig,
    VideoEncoder,
    VideoEncoderConfig,
    VideoFrame,
    VideoPixelFormat,
)

# Free-Threading ビルドかどうかを確認
is_free_threading = hasattr(sys, "_is_gil_enabled") and not sys._is_gil_enabled()


def create_test_video_frame(width: int = 320, height: int = 240) -> VideoFrame:
    """テスト用の I420 VideoFrame を作成"""
    y_size = width * height
    uv_size = (width // 2) * (height // 2)
    data_size = y_size + uv_size * 2
    data = np.random.randint(0, 256, data_size, dtype=np.uint8)
    return VideoFrame(data, {
        "format": VideoPixelFormat.I420,
        "coded_width": width,
        "coded_height": height,
        "timestamp": 0,
    })


def create_test_audio_data(
    sample_rate: int = 48000, channels: int = 2, samples: int = 960
) -> AudioData:
    """テスト用の AudioData を作成"""
    data = np.random.uniform(-1.0, 1.0, (samples, channels)).astype(np.float32)
    return AudioData({
        "format": AudioSampleFormat.F32,
        "sample_rate": sample_rate,
        "number_of_frames": samples,
        "number_of_channels": channels,
        "timestamp": 0,
        "data": data,
    })


@pytest.mark.skipif(not is_free_threading, reason="Free-Threading build required")
def test_concurrent_video_encoder_callback_modification():
    """複数スレッドから VideoEncoder のコールバックを同時変更"""
    chunks = []
    errors = []
    lock = threading.Lock()

    def on_output(chunk, metadata=None):
        with lock:
            chunks.append(chunk)

    def on_error(err):
        with lock:
            errors.append(err)

    encoder = VideoEncoder(on_output, on_error)
    encoder.configure(
        VideoEncoderConfig(
            codec="av01.0.04M.08",
            width=320,
            height=240,
        )
    )

    barrier = threading.Barrier(4)
    modification_count = [0]
    count_lock = threading.Lock()

    def modify_callbacks(thread_id: int):
        barrier.wait()
        for i in range(100):
            # コールバックを同時に変更
            def new_output(chunk, metadata=None, tid=thread_id, idx=i):
                with lock:
                    chunks.append((tid, idx, chunk))

            encoder.on_output(new_output)
            with count_lock:
                modification_count[0] += 1

    threads = []
    for i in range(4):
        t = threading.Thread(target=modify_callbacks, args=(i,))
        threads.append(t)
        t.start()

    for t in threads:
        t.join()

    # データ競合なしで完了することを確認
    assert modification_count[0] == 400
    encoder.close()


@pytest.mark.skipif(not is_free_threading, reason="Free-Threading build required")
def test_concurrent_video_decode_encode():
    """複数スレッドから同時にエンコード・デコードを実行"""
    encoded_chunks = []
    encode_errors = []
    lock = threading.Lock()

    def on_encode_output(chunk):
        with lock:
            encoded_chunks.append(chunk)

    def on_encode_error(err):
        with lock:
            encode_errors.append(err)

    # エンコーダーを作成してフレームをエンコード
    encoder = VideoEncoder(on_encode_output, on_encode_error)
    config: VideoEncoderConfig = {
        "codec": "av01.0.04M.08",
        "width": 320,
        "height": 240,
        "bitrate": 500000,
        "latency_mode": LatencyMode.REALTIME,
    }
    encoder.configure(config)

    def encode_worker(thread_id: int, num_frames: int):
        for i in range(num_frames):
            frame = create_test_video_frame()
            encoder.encode(frame, {"key_frame": i == 0})
            frame.close()

    threads = []
    for i in range(4):
        t = threading.Thread(target=encode_worker, args=(i, 5))
        threads.append(t)
        t.start()

    for t in threads:
        t.join()

    encoder.flush()
    encoder.close()

    # エラーがないことを確認
    assert len(encode_errors) == 0, f"Encode errors: {encode_errors}"
    # エンコードされたチャンクを確認
    assert len(encoded_chunks) > 0, f"No chunks produced"


@pytest.mark.skipif(not is_free_threading, reason="Free-Threading build required")
def test_concurrent_audio_encode():
    """複数スレッドから同時にオーディオエンコードを実行"""
    encoded_chunks = []
    lock = threading.Lock()

    def on_output(chunk):
        with lock:
            encoded_chunks.append(chunk)

    def on_error(err):
        pass

    encoder = AudioEncoder(on_output, on_error)
    encoder.configure(
        AudioEncoderConfig(
            codec="opus",
            sample_rate=48000,
            number_of_channels=2,
        )
    )

    barrier = threading.Barrier(4)

    def encode_worker(thread_id: int, num_chunks: int):
        barrier.wait()
        for i in range(num_chunks):
            audio = create_test_audio_data()
            encoder.encode(audio)

    threads = []
    for i in range(4):
        t = threading.Thread(target=encode_worker, args=(i, 10))
        threads.append(t)
        t.start()

    for t in threads:
        t.join()

    encoder.flush()

    # エンコードされたチャンクを確認
    assert len(encoded_chunks) > 0
    encoder.close()


@pytest.mark.skipif(not is_free_threading, reason="Free-Threading build required")
def test_multiple_encoders_parallel():
    """複数のエンコーダーインスタンスを並列で操作"""
    results = {}
    lock = threading.Lock()

    def create_and_use_encoder(encoder_id: int):
        chunks = []

        def on_output(chunk):
            chunks.append(chunk)

        def on_error(err):
            pass

        encoder = VideoEncoder(on_output, on_error)
        config: VideoEncoderConfig = {
            "codec": "av01.0.04M.08",
            "width": 320,
            "height": 240,
            "latency_mode": LatencyMode.REALTIME,
        }
        encoder.configure(config)

        for i in range(5):
            frame = create_test_video_frame()
            encoder.encode(frame, {"key_frame": i == 0})
            frame.close()

        encoder.flush()
        encoder.close()

        with lock:
            results[encoder_id] = len(chunks)

    with ThreadPoolExecutor(max_workers=4) as executor:
        futures = [executor.submit(create_and_use_encoder, i) for i in range(4)]
        for f in futures:
            f.result()

    # 全エンコーダーが出力を生成したことを確認
    assert len(results) == 4
    for encoder_id, count in results.items():
        assert count > 0, f"Encoder {encoder_id} produced no output"


@pytest.mark.skipif(not is_free_threading, reason="Free-Threading build required")
def test_gil_status():
    """GIL の状態を確認"""
    assert hasattr(sys, "_is_gil_enabled")
    assert not sys._is_gil_enabled(), "GIL should be disabled in Free-Threading build"


# GIL ビルドでも実行されるテスト（Free-Threading 環境の検出テスト）
def test_free_threading_detection():
    """Free-Threading 環境の検出機能をテスト"""
    has_gil_check = hasattr(sys, "_is_gil_enabled")
    if has_gil_check:
        gil_enabled = sys._is_gil_enabled()
        # Python 3.13+ では _is_gil_enabled が存在する
        assert isinstance(gil_enabled, bool)
    # Python 3.12 以下では _is_gil_enabled が存在しない
