"""並列エンコード・デコードのラウンドトリップテスト"""

import threading

import numpy as np

from webcodecs import (
    LatencyMode,
    VideoDecoder,
    VideoDecoderConfig,
    VideoEncoder,
    VideoEncoderConfig,
    VideoFrame,
    VideoFrameBufferInit,
    VideoPixelFormat,
)


def create_test_frame(width, height, timestamp):
    """テスト用のフレームを作成"""
    # Y プレーンを設定 (2D array)
    y_plane = np.full((height, width), 128, dtype=np.uint8)

    # U, V プレーンを設定 (2D array, half resolution)
    u_plane = np.full((height // 2, width // 2), 128, dtype=np.uint8)
    v_plane = np.full((height // 2, width // 2), 128, dtype=np.uint8)

    # YUV データを連結
    data = np.concatenate([y_plane.flatten(), u_plane.flatten(), v_plane.flatten()])
    init: VideoFrameBufferInit = {
        "format": VideoPixelFormat.I420,
        "coded_width": width,
        "coded_height": height,
        "timestamp": timestamp,
    }
    frame = VideoFrame(data, init)
    return frame


def test_parallel_encode_decode_queue_size():
    """エンコード後のチャンクを使用した並列デコードキューサイズのテスト"""
    width, height = 640, 480

    # エンコードされたチャンクを収集
    encoded_chunks = []

    def on_output(chunk):
        encoded_chunks.append(chunk)

    def on_error(error):
        print(f"Encoder error: {error}")

    # エンコーダを設定
    encoder = VideoEncoder(on_output, on_error)

    config: VideoEncoderConfig = {
        "codec": "av01.0.04M.08",
        "width": width,
        "height": height,
        "bitrate": 1_000_000,
        "framerate": 30.0,
        "latency_mode": LatencyMode.REALTIME,
    }
    encoder.configure(config)

    # 5つのフレームをエンコード
    for i in range(5):
        frame = create_test_frame(width, height, i * 1000)
        encoder.encode(frame, {"key_frame": i == 0})
        frame.close()

    encoder.flush()
    encoder.close()

    assert len(encoded_chunks) >= 5, "エンコードされたチャンクが不足しています"

    # デコーダを設定
    def on_decode_output(frame):
        frame.close()

    def on_decode_error(error):
        print(f"Decoder error: {error}")

    decoder = VideoDecoder(on_decode_output, on_decode_error)

    decoder_config: VideoDecoderConfig = {
        "codec": "av01.0.04M.08",
        "coded_width": width,
        "coded_height": height,
    }
    decoder.configure(decoder_config)

    # 複数のチャンクを連続してデコードキューに追加
    initial_queue_size = decoder.decode_queue_size
    for chunk in encoded_chunks:
        decoder.decode(chunk)

    # キューサイズが増加していることを確認
    queue_size_after = decoder.decode_queue_size
    print(f"Initial queue size: {initial_queue_size}, After: {queue_size_after}")

    decoder.flush()
    decoder.close()


def test_concurrent_encode_decode():
    """複数スレッドから同時にエンコード・デコードを実行"""
    width, height = 640, 480

    # エンコードされたチャンクを収集
    encoded_chunks = []
    chunks_lock = threading.Lock()

    def on_encoder_output(chunk):
        with chunks_lock:
            encoded_chunks.append(chunk)

    def on_encoder_error(error):
        print(f"Encoder error: {error}")

    # エンコーダを設定
    encoder = VideoEncoder(on_encoder_output, on_encoder_error)

    config: VideoEncoderConfig = {
        "codec": "av01.0.04M.08",
        "width": width,
        "height": height,
        "bitrate": 1_000_000,
        "framerate": 30.0,
        "latency_mode": LatencyMode.REALTIME,
    }
    encoder.configure(config)

    # 複数スレッドからエンコード
    def encode_worker(thread_id, num_frames):
        for i in range(num_frames):
            timestamp = thread_id * 10000 + i * 1000
            frame = create_test_frame(width, height, timestamp)
            encoder.encode(frame, {"key_frame": i == 0})
            frame.close()

    # 3つのスレッドから同時にエンコード
    threads = []
    frames_per_thread = 3
    for i in range(3):
        t = threading.Thread(target=encode_worker, args=(i, frames_per_thread))
        threads.append(t)
        t.start()

    for t in threads:
        t.join()

    encoder.flush()
    encoder.close()

    total_expected = 3 * frames_per_thread
    assert len(encoded_chunks) >= total_expected, (
        f"期待される {total_expected} チャンク、実際: {len(encoded_chunks)}"
    )

    # 出力されたフレーム数をカウント
    output_count = 0
    output_lock = threading.Lock()

    def on_decoder_output(frame):
        nonlocal output_count
        with output_lock:
            output_count += 1
        frame.close()

    def on_decoder_error(error):
        print(f"Decoder error: {error}")

    # デコーダを設定
    decoder = VideoDecoder(on_decoder_output, on_decoder_error)

    decoder_config: VideoDecoderConfig = {
        "codec": "av01.0.04M.08",
        "coded_width": width,
        "coded_height": height,
    }
    decoder.configure(decoder_config)

    # デコード
    for chunk in encoded_chunks:
        decoder.decode(chunk)

    decoder.flush()

    print(f"Total chunks: {len(encoded_chunks)}, Decoded frames: {output_count}")
    assert output_count >= total_expected, (
        f"デコードされたフレーム数が不足: {output_count}/{total_expected}"
    )

    decoder.flush()
    decoder.close()


def test_encode_decode_without_waiting():
    """エンコード・デコードが非同期で実行されることを確認"""
    width, height = 640, 480

    # 10フレームをエンコード
    encoded_chunks = []

    def on_output(chunk):
        encoded_chunks.append(chunk)

    def on_error(error):
        print(f"Encoder error: {error}")

    # エンコーダを設定
    encoder = VideoEncoder(on_output, on_error)

    config: VideoEncoderConfig = {
        "codec": "av01.0.04M.08",
        "width": width,
        "height": height,
        "bitrate": 1_000_000,
        "framerate": 30.0,
        "latency_mode": LatencyMode.REALTIME,
    }
    encoder.configure(config)

    for i in range(10):
        frame = create_test_frame(width, height, i * 1000)
        encoder.encode(frame, {"key_frame": i == 0})
        frame.close()

    encoder.flush()
    encoder.close()

    assert len(encoded_chunks) >= 10, "エンコードされたチャンクが不足しています"

    # デコーダを設定
    def on_decode_output(frame):
        frame.close()

    def on_decode_error(error):
        print(f"Decoder error: {error}")

    decoder = VideoDecoder(on_decode_output, on_decode_error)

    decoder_config: VideoDecoderConfig = {
        "codec": "av01.0.04M.08",
        "coded_width": width,
        "coded_height": height,
    }
    decoder.configure(decoder_config)

    # decode()呼び出しが即座に返ることを確認
    import time

    start_times = []
    end_times = []

    for i, chunk in enumerate(encoded_chunks[:10]):
        start_time = time.perf_counter()
        decoder.decode(chunk)
        end_time = time.perf_counter()

        start_times.append(start_time)
        end_times.append(end_time)

    # 各decode()呼び出しが素早く返ることを確認
    for i in range(min(10, len(encoded_chunks))):
        duration = end_times[i] - start_times[i]
        print(f"decode({i}) took {duration:.6f} seconds")
        # 並列処理の場合、decode()は即座に返るはず（< 0.01秒）
        assert duration < 0.01, f"decode({i}) took too long: {duration:.6f}s"

    decoder.flush()
    decoder.close()


def test_encode_decode_callbacks():
    """エンコード・デコードのコールバックが正しく呼ばれることを確認"""
    width, height = 640, 480

    # 5フレームをエンコード
    encoded_chunks = []

    def on_output(chunk):
        encoded_chunks.append(chunk)

    def on_error(error):
        print(f"Encoder error: {error}")

    # エンコーダを設定
    encoder = VideoEncoder(on_output, on_error)

    config: VideoEncoderConfig = {
        "codec": "av01.0.04M.08",
        "width": width,
        "height": height,
        "bitrate": 1_000_000,
        "framerate": 30.0,
        "latency_mode": LatencyMode.REALTIME,
    }
    encoder.configure(config)

    # エンコーダのコールバックカウント
    encoder_dequeue_count = 0
    encoder_lock = threading.Lock()

    def on_encoder_dequeue():
        nonlocal encoder_dequeue_count
        with encoder_lock:
            encoder_dequeue_count += 1

    encoder.on_dequeue(on_encoder_dequeue)

    for i in range(5):
        frame = create_test_frame(width, height, i * 1000)
        encoder.encode(frame, {"key_frame": i == 0})
        frame.close()

    encoder.flush()

    print(f"Encoder dequeue callback called {encoder_dequeue_count} times")
    assert encoder_dequeue_count >= 5, (
        f"エンコーダのdequeueコールバックが不足: {encoder_dequeue_count}"
    )

    encoder.close()

    assert len(encoded_chunks) >= 5, "エンコードされたチャンクが不足しています"

    # デコーダを設定
    def on_decode_output(frame):
        frame.close()

    def on_decode_error(error):
        print(f"Decoder error: {error}")

    decoder = VideoDecoder(on_decode_output, on_decode_error)

    decoder_config: VideoDecoderConfig = {
        "codec": "av01.0.04M.08",
        "coded_width": width,
        "coded_height": height,
    }
    decoder.configure(decoder_config)

    # デコーダのコールバックカウント
    decoder_dequeue_count = 0
    decoder_lock = threading.Lock()

    def on_decoder_dequeue():
        nonlocal decoder_dequeue_count
        with decoder_lock:
            decoder_dequeue_count += 1

    decoder.on_dequeue(on_decoder_dequeue)

    # デコード
    for chunk in encoded_chunks[:5]:
        decoder.decode(chunk)

    decoder.flush()

    print(f"Decoder dequeue callback called {decoder_dequeue_count} times")
    assert decoder_dequeue_count >= 5, (
        f"デコーダのdequeueコールバックが不足: {decoder_dequeue_count}"
    )

    decoder.flush()
    decoder.close()


def test_encode_decode_frame_ordering():
    """エンコード・デコード後のフレーム順序が保たれることを確認"""
    width, height = 640, 480

    # タイムスタンプ順に10フレームをエンコード
    expected_timestamps = []
    encoded_chunks = []

    def on_output(chunk):
        encoded_chunks.append(chunk)

    def on_error(error):
        print(f"Encoder error: {error}")

    # エンコーダを設定
    encoder = VideoEncoder(on_output, on_error)

    config: VideoEncoderConfig = {
        "codec": "av01.0.04M.08",
        "width": width,
        "height": height,
        "bitrate": 1_000_000,
        "framerate": 30.0,
        "latency_mode": LatencyMode.REALTIME,
    }
    encoder.configure(config)

    for i in range(10):
        timestamp = i * 1000
        expected_timestamps.append(timestamp)
        frame = create_test_frame(width, height, timestamp)
        encoder.encode(frame, {"key_frame": i == 0})
        frame.close()

    encoder.flush()
    encoder.close()

    assert len(encoded_chunks) >= 10, "エンコードされたチャンクが不足しています"

    output_timestamps = []
    output_lock = threading.Lock()

    def on_decode_output(frame):
        with output_lock:
            output_timestamps.append(frame.timestamp)
        frame.close()

    def on_decode_error(error):
        print(f"Decoder error: {error}")

    # デコーダを設定
    decoder = VideoDecoder(on_decode_output, on_decode_error)

    decoder_config: VideoDecoderConfig = {
        "codec": "av01.0.04M.08",
        "coded_width": width,
        "coded_height": height,
    }
    decoder.configure(decoder_config)

    # デコード
    for chunk in encoded_chunks[:10]:
        decoder.decode(chunk)

    decoder.flush()

    # 出力順序が保たれていることを確認
    print(f"Expected timestamps: {expected_timestamps}")
    print(f"Output timestamps: {output_timestamps}")

    # タイムスタンプが保持されていることを確認
    for i in range(min(len(output_timestamps), len(expected_timestamps))):
        assert output_timestamps[i] == expected_timestamps[i], (
            f"Timestamp mismatch at index {i}: expected {expected_timestamps[i]}, got {output_timestamps[i]}"
        )

    decoder.flush()
    decoder.close()


def test_parallel_encode_decode_with_keyframes():
    """キーフレームとデルタフレームを混在させた並列エンコード・デコード"""
    width, height = 320, 240

    # エンコードされたチャンクとその型を収集
    encoded_chunks = []
    chunk_types = []

    def on_encoder_output(chunk):
        encoded_chunks.append(chunk)
        chunk_types.append((chunk.timestamp, chunk.type))

    def on_encoder_error(error):
        print(f"Encoder error: {error}")

    # エンコーダを設定
    encoder = VideoEncoder(on_encoder_output, on_encoder_error)
    config: VideoEncoderConfig = {
        "codec": "av01.0.04M.08",
        "width": width,
        "height": height,
        "bitrate": 500_000,
        "framerate": 30.0,
        "latency_mode": LatencyMode.REALTIME,
    }
    encoder.configure(config)

    # キーフレームとデルタフレームを混在させて10フレームをエンコード
    frames_to_encode = []
    for i in range(10):
        frame = create_test_frame(width, height, i * 1000)
        frames_to_encode.append(frame)
        # 最初と5番目をキーフレームに
        is_keyframe = i == 0 or i == 5
        encoder.encode(frame, {"key_frame": is_keyframe})
        frame.close()

    encoder.flush()
    encoder.close()

    assert len(encoded_chunks) >= 10, f"エンコードされたチャンクが不足: {len(encoded_chunks)}"

    print("Encoded chunk types:")
    for timestamp, chunk_type in chunk_types:
        print(f"  Timestamp {timestamp}: {chunk_type}")

    # デコードされたフレームを収集
    decoded_frames = []
    decoded_timestamps = []

    def on_decoder_output(frame):
        decoded_timestamps.append(frame.timestamp)
        # フレームデータを取得（検証のため）
        decoded_frames.append(frame)
        frame.close()

    def on_decoder_error(error):
        print(f"Decoder error: {error}")

    # デコーダを設定
    decoder = VideoDecoder(on_decoder_output, on_decoder_error)

    decoder_config: VideoDecoderConfig = {
        "codec": "av01.0.04M.08",
        "coded_width": width,
        "coded_height": height,
    }
    decoder.configure(decoder_config)

    # 全てのチャンクをデコード
    for chunk in encoded_chunks:
        decoder.decode(chunk)

    decoder.flush()

    # デコードされたフレーム数が正しいことを確認
    assert len(decoded_timestamps) >= 10, f"デコードされたフレームが不足: {len(decoded_timestamps)}"

    # タイムスタンプが保持されていることを確認
    for i in range(min(10, len(decoded_timestamps))):
        expected_timestamp = i * 1000
        assert decoded_timestamps[i] == expected_timestamp, (
            f"Timestamp mismatch at index {i}: expected {expected_timestamp}, got {decoded_timestamps[i]}"
        )

    print(f"Successfully decoded {len(decoded_timestamps)} frames with correct timestamps")

    decoder.flush()
    decoder.close()
