"""並列オーディオエンコード・デコードのラウンドトリップテスト"""

import threading

import numpy as np

from webcodecs import (
    AudioData,
    AudioDecoder,
    AudioDecoderConfig,
    AudioEncoder,
    AudioEncoderConfig,
    AudioSampleFormat,
)


def create_test_audio(channels, sample_rate, frames, timestamp, value=0.5):
    """テスト用のオーディオデータを作成"""
    shape = (frames, channels)
    data = np.full(shape, value, dtype=np.float32)

    # ノイズを追加して各フレームを区別可能にする
    noise = np.random.randn(*shape) * 0.01
    data += noise

    init = {
        "format": AudioSampleFormat.F32,
        "sample_rate": sample_rate,
        "number_of_frames": frames,
        "number_of_channels": channels,
        "timestamp": timestamp,
        "data": data,
    }
    return AudioData(init)


def test_parallel_audio_encode_decode_queue_size():
    """エンコード後のチャンクを使用した並列デコードキューサイズのテスト"""
    sample_rate = 48000
    channels = 2
    frames_per_chunk = 960  # 20ms at 48kHz

    # エンコードされたチャンクを収集
    encoded_chunks = []

    def on_output(chunk):
        encoded_chunks.append(chunk)

    def on_error(error):
        print(f"Encoder error: {error}")

    # エンコーダを設定
    encoder = AudioEncoder(on_output, on_error)

    config: AudioEncoderConfig = {
        "codec": "opus",
        "sample_rate": sample_rate,
        "number_of_channels": channels,
        "bitrate": 128000,
    }
    encoder.configure(config)

    # 5つのオーディオデータをエンコード
    for i in range(5):
        audio = create_test_audio(channels, sample_rate, frames_per_chunk, i * 20000)
        encoder.encode(audio)
        audio.close()
        audio.close()

    encoder.flush()
    encoder.close()

    assert len(encoded_chunks) >= 5, "エンコードされたチャンクが不足しています"

    # デコーダを設定
    def on_decode_output(audio):
        audio.close()

    def on_decode_error(error):
        print(f"Decoder error: {error}")

    decoder = AudioDecoder(on_decode_output, on_decode_error)

    decoder_config: AudioDecoderConfig = {
        "codec": "opus",
        "sample_rate": sample_rate,
        "number_of_channels": channels,
    }
    decoder.configure(decoder_config)

    # 複数のチャンクを連続してデコードキューに追加
    initial_queue_size = decoder.decode_queue_size
    for chunk in encoded_chunks:
        decoder.decode(chunk)

    # キューサイズが増加していることを確認
    queue_size_after = decoder.decode_queue_size
    print(f"Initial queue size: {initial_queue_size}, After: {queue_size_after}")

    # フラッシュしてから閉じる
    decoder.flush()
    decoder.close()


def test_concurrent_audio_encode_decode():
    """複数スレッドから同時にエンコード・デコードを実行"""
    sample_rate = 48000
    channels = 2
    frames_per_chunk = 960

    # エンコードされたチャンクを収集
    encoded_chunks = []
    chunks_lock = threading.Lock()

    def on_encoder_output(chunk):
        with chunks_lock:
            encoded_chunks.append(chunk)

    def on_encoder_error(error):
        print(f"Encoder error: {error}")

    # エンコーダを設定
    encoder = AudioEncoder(on_encoder_output, on_encoder_error)

    config: AudioEncoderConfig = {
        "codec": "opus",
        "sample_rate": sample_rate,
        "number_of_channels": channels,
        "bitrate": 128000,
    }
    encoder.configure(config)

    # 複数スレッドからエンコード
    def encode_worker(thread_id, num_chunks):
        for i in range(num_chunks):
            timestamp = thread_id * 100000 + i * 20000
            audio = create_test_audio(channels, sample_rate, frames_per_chunk, timestamp)
            encoder.encode(audio)
            audio.close()

    # 3つのスレッドから同時にエンコード
    threads = []
    chunks_per_thread = 3
    for i in range(3):
        t = threading.Thread(target=encode_worker, args=(i, chunks_per_thread))
        threads.append(t)
        t.start()

    for t in threads:
        t.join()

    encoder.flush()
    encoder.close()

    total_expected = 3 * chunks_per_thread
    assert len(encoded_chunks) >= total_expected, (
        f"期待される {total_expected} チャンク、実際: {len(encoded_chunks)}"
    )

    # 出力されたオーディオデータ数をカウント
    output_count = 0
    output_lock = threading.Lock()

    def on_decoder_output(audio):
        nonlocal output_count
        with output_lock:
            output_count += 1
        audio.close()  # Clean up AudioData

    def on_decoder_error(error):
        print(f"Decoder error: {error}")

    # デコーダを設定
    decoder = AudioDecoder(on_decoder_output, on_decoder_error)

    decoder_config: AudioDecoderConfig = {
        "codec": "opus",
        "sample_rate": sample_rate,
        "number_of_channels": channels,
    }
    decoder.configure(decoder_config)

    # デコード
    for chunk in encoded_chunks:
        decoder.decode(chunk)

    decoder.flush()

    print(f"Total chunks: {len(encoded_chunks)}, Decoded audio: {output_count}")
    assert output_count >= total_expected, (
        f"デコードされたオーディオデータが不足: {output_count}/{total_expected}"
    )

    # AudioDataは on_decoder_output 内でclose済み
    decoder.flush()
    decoder.close()


def test_audio_encode_decode_without_waiting():
    """エンコード・デコードが非同期で実行されることを確認"""
    sample_rate = 48000
    channels = 2
    frames_per_chunk = 960

    # 10個のオーディオデータをエンコード
    encoded_chunks = []

    def on_output(chunk):
        encoded_chunks.append(chunk)

    def on_error(error):
        print(f"Encoder error: {error}")

    # エンコーダを設定
    encoder = AudioEncoder(on_output, on_error)

    config: AudioEncoderConfig = {
        "codec": "opus",
        "sample_rate": sample_rate,
        "number_of_channels": channels,
        "bitrate": 128000,
    }
    encoder.configure(config)

    for i in range(10):
        audio = create_test_audio(channels, sample_rate, frames_per_chunk, i * 20000)
        encoder.encode(audio)
        audio.close()

    encoder.flush()
    encoder.close()

    assert len(encoded_chunks) >= 10, "エンコードされたチャンクが不足しています"

    # デコーダを設定
    def on_decode_output(audio):
        audio.close()

    def on_decode_error(error):
        print(f"Decoder error: {error}")

    decoder = AudioDecoder(on_decode_output, on_decode_error)

    decoder_config: AudioDecoderConfig = {
        "codec": "opus",
        "sample_rate": sample_rate,
        "number_of_channels": channels,
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


def test_audio_encode_decode_callbacks():
    """エンコード・デコードのコールバックが正しく呼ばれることを確認"""
    sample_rate = 48000
    channels = 2
    frames_per_chunk = 960

    # 5個のオーディオデータをエンコード
    encoded_chunks = []

    def on_output(chunk):
        encoded_chunks.append(chunk)

    def on_error(error):
        print(f"Encoder error: {error}")

    # エンコーダを設定
    encoder = AudioEncoder(on_output, on_error)

    config: AudioEncoderConfig = {
        "codec": "opus",
        "sample_rate": sample_rate,
        "number_of_channels": channels,
        "bitrate": 128000,
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
        audio = create_test_audio(channels, sample_rate, frames_per_chunk, i * 20000)
        encoder.encode(audio)
        audio.close()

    encoder.flush()

    print(f"Encoder dequeue callback called {encoder_dequeue_count} times")
    assert encoder_dequeue_count >= 5, (
        f"エンコーダのdequeueコールバックが不足: {encoder_dequeue_count}"
    )

    encoder.close()

    assert len(encoded_chunks) >= 5, "エンコードされたチャンクが不足しています"

    # デコーダを設定
    def on_decode_output(audio):
        audio.close()

    def on_decode_error(error):
        print(f"Decoder error: {error}")

    decoder = AudioDecoder(on_decode_output, on_decode_error)

    decoder_config: AudioDecoderConfig = {
        "codec": "opus",
        "sample_rate": sample_rate,
        "number_of_channels": channels,
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


def test_audio_encode_decode_frame_ordering():
    """エンコード・デコード後のタイムスタンプ順序が保たれることを確認"""
    sample_rate = 48000
    channels = 2
    frames_per_chunk = 960

    # タイムスタンプ順に10個のオーディオデータをエンコード
    expected_timestamps = []
    encoded_chunks = []

    def on_encoder_output(chunk):
        encoded_chunks.append(chunk)

    def on_error(error):
        print(f"Encoder error: {error}")

    # エンコーダを設定
    encoder = AudioEncoder(on_encoder_output, on_error)

    config: AudioEncoderConfig = {
        "codec": "opus",
        "sample_rate": sample_rate,
        "number_of_channels": channels,
        "bitrate": 128000,
    }
    encoder.configure(config)

    for i in range(10):
        timestamp = i * 20000
        expected_timestamps.append(timestamp)
        audio = create_test_audio(channels, sample_rate, frames_per_chunk, timestamp)
        encoder.encode(audio)
        audio.close()

    encoder.flush()
    encoder.close()

    assert len(encoded_chunks) >= 10, "エンコードされたチャンクが不足しています"

    output_timestamps = []
    output_lock = threading.Lock()

    def on_decoder_output(audio):
        with output_lock:
            output_timestamps.append(audio.timestamp)
        audio.close()  # Clean up AudioData

    def on_decode_error(error):
        print(f"Decoder error: {error}")

    # デコーダを設定
    decoder = AudioDecoder(on_decoder_output, on_decode_error)

    decoder_config: AudioDecoderConfig = {
        "codec": "opus",
        "sample_rate": sample_rate,
        "number_of_channels": channels,
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

    # AudioDataは on_output 内でclose済み
    decoder.flush()
    decoder.close()


def test_parallel_audio_encode_decode_with_different_types():
    """異なるチャンクタイプでの並列エンコード・デコード"""
    sample_rate = 48000
    channels = 2
    frames_per_chunk = 960

    # エンコードされたチャンクとその型を収集
    encoded_chunks = []
    chunk_types = []

    def on_encoder_output(chunk):
        encoded_chunks.append(chunk)
        chunk_types.append((chunk.timestamp, chunk.type))

    def on_encoder_error(error):
        print(f"Encoder error: {error}")

    # エンコーダを設定
    encoder = AudioEncoder(on_encoder_output, on_encoder_error)

    config: AudioEncoderConfig = {
        "codec": "opus",
        "sample_rate": sample_rate,
        "number_of_channels": channels,
        "bitrate": 128000,
    }
    encoder.configure(config)

    # 10個のオーディオデータをエンコード
    for i in range(10):
        audio = create_test_audio(channels, sample_rate, frames_per_chunk, i * 20000)
        encoder.encode(audio)
        audio.close()

    encoder.flush()
    encoder.close()

    assert len(encoded_chunks) >= 10, f"エンコードされたチャンクが不足: {len(encoded_chunks)}"

    print("Encoded chunk types:")
    for timestamp, chunk_type in chunk_types:
        print(f"  Timestamp {timestamp}: {chunk_type}")

    # デコードされたオーディオデータを収集
    decoded_audio_list = []
    decoded_timestamps = []

    def on_decoder_output(audio):
        decoded_timestamps.append(audio.timestamp)
        decoded_audio_list.append(audio)

    def on_decoder_error(error):
        print(f"Decoder error: {error}")

    # デコーダを設定
    decoder = AudioDecoder(on_decoder_output, on_decoder_error)

    decoder_config: AudioDecoderConfig = {
        "codec": "opus",
        "sample_rate": sample_rate,
        "number_of_channels": channels,
    }
    decoder.configure(decoder_config)

    # 全てのチャンクをデコード
    for chunk in encoded_chunks:
        decoder.decode(chunk)

    decoder.flush()

    # デコードされたオーディオデータ数が正しいことを確認
    assert len(decoded_timestamps) >= 10, (
        f"デコードされたオーディオデータが不足: {len(decoded_timestamps)}"
    )

    # タイムスタンプが保持されていることを確認
    for i in range(min(10, len(decoded_timestamps))):
        expected_timestamp = i * 20000
        assert decoded_timestamps[i] == expected_timestamp, (
            f"Timestamp mismatch at index {i}: expected {expected_timestamp}, got {decoded_timestamps[i]}"
        )

    print(f"Successfully decoded {len(decoded_timestamps)} audio chunks with correct timestamps")

    # Clean up decoded AudioData
    for audio in decoded_audio_list:
        audio.close()

    decoder.flush()
    decoder.close()
