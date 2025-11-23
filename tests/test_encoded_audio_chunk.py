import numpy as np

from webcodecs import EncodedAudioChunk, EncodedAudioChunkType


def test_encoded_audio_chunk_creation():
    """EncodedAudioChunk の作成をテスト"""
    data = b"audio_data_12345"
    chunk = EncodedAudioChunk(data, EncodedAudioChunkType.KEY, timestamp=1000)

    assert chunk.type == EncodedAudioChunkType.KEY
    assert chunk.timestamp == 1000
    assert chunk.duration == 0
    assert chunk.byte_length == len(data)


def test_encoded_audio_chunk_with_duration():
    """duration を指定した EncodedAudioChunk の作成をテスト"""
    data = b"audio_data_with_duration"
    chunk = EncodedAudioChunk(data, EncodedAudioChunkType.KEY, timestamp=2000, duration=20000)

    assert chunk.type == EncodedAudioChunkType.KEY
    assert chunk.timestamp == 2000
    assert chunk.duration == 20000
    assert chunk.byte_length == len(data)


def test_encoded_audio_chunk_copy_to():
    """copy_to() メソッドをテスト (WebCodecs API 準拠)"""
    data = b"original_audio_data"
    chunk = EncodedAudioChunk(data, EncodedAudioChunkType.KEY, timestamp=0)

    # destination バッファを確保
    destination = np.zeros(chunk.byte_length, dtype=np.uint8)
    chunk.copy_to(destination)

    assert bytes(destination) == data
    assert len(destination) == chunk.byte_length


def test_encoded_audio_chunk_get_data():
    """get_data() メソッドをテスト"""
    data = b"test_audio_chunk_data"
    chunk = EncodedAudioChunk(data, EncodedAudioChunkType.KEY, timestamp=5000)

    # copy_to() を使用してデータを取得
    destination = np.zeros(chunk.byte_length, dtype=np.uint8)
    chunk.copy_to(destination)

    assert bytes(destination) == data
    assert len(destination) == chunk.byte_length


def test_encoded_audio_chunk_empty_data():
    """空のデータで EncodedAudioChunk を作成"""
    data = b""
    chunk = EncodedAudioChunk(data, EncodedAudioChunkType.KEY, timestamp=0)

    assert chunk.byte_length == 0
    # 空のデータの場合、サイズ 0 のバッファを渡して copy_to() を呼び出す
    # これにより copy_to() が空のデータでも正しく動作することを確認
    destination = np.zeros(0, dtype=np.uint8)
    chunk.copy_to(destination)
    assert bytes(destination) == data


def test_encoded_audio_chunk_large_data():
    """大きなデータで EncodedAudioChunk を作成"""
    data = b"x" * 100000  # 100KB
    chunk = EncodedAudioChunk(data, EncodedAudioChunkType.KEY, timestamp=0)

    assert chunk.byte_length == 100000
    # copy_to() を使用してデータを取得
    destination = np.zeros(chunk.byte_length, dtype=np.uint8)
    chunk.copy_to(destination)
    assert len(destination) == 100000
    assert bytes(destination) == data


def test_encoded_audio_chunk_type_key():
    """KEY タイプの EncodedAudioChunk をテスト"""
    data = b"key_frame_data"
    chunk = EncodedAudioChunk(data, EncodedAudioChunkType.KEY, timestamp=0)

    assert chunk.type == EncodedAudioChunkType.KEY


def test_encoded_audio_chunk_type_delta():
    """DELTA タイプの EncodedAudioChunk をテスト"""
    data = b"delta_frame_data"
    chunk = EncodedAudioChunk(data, EncodedAudioChunkType.DELTA, timestamp=0)

    assert chunk.type == EncodedAudioChunkType.DELTA


def test_encoded_audio_chunk_opus_data():
    """Opus データで EncodedAudioChunk を作成"""
    opus_data = b"OpusHead" + b"\x00" * 50
    chunk = EncodedAudioChunk(opus_data, EncodedAudioChunkType.KEY, timestamp=0)

    assert chunk.type == EncodedAudioChunkType.KEY
    assert chunk.timestamp == 0
    assert chunk.byte_length == len(opus_data)
    # copy_to() を使用してデータを取得
    destination = np.zeros(chunk.byte_length, dtype=np.uint8)
    chunk.copy_to(destination)
    assert bytes(destination) == opus_data


def test_encoded_audio_chunk_timestamp_sequence():
    """タイムスタンプのシーケンスをテスト"""
    timestamps = [0, 20000, 40000, 60000, 80000, 100000]

    chunks = []
    for ts in timestamps:
        data = b"audio_frame" + bytes([ts // 1000])
        chunk = EncodedAudioChunk(data, EncodedAudioChunkType.KEY, timestamp=ts)
        chunks.append(chunk)
        assert chunk.timestamp == ts

    assert len(chunks) == len(timestamps)
