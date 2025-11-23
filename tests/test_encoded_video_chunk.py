from webcodecs import EncodedVideoChunk, EncodedVideoChunkType


def test_encoded_video_chunk_creation():
    """EncodedVideoChunk の作成をテスト"""
    data = b"video_data_12345"
    chunk = EncodedVideoChunk(data, EncodedVideoChunkType.KEY, timestamp=1000)

    assert chunk.type == EncodedVideoChunkType.KEY
    assert chunk.timestamp == 1000
    assert chunk.duration == 0
    assert chunk.byte_length == len(data)


def test_encoded_video_chunk_with_duration():
    """duration を指定した EncodedVideoChunk の作成をテスト"""
    data = b"video_data_with_duration"
    chunk = EncodedVideoChunk(data, EncodedVideoChunkType.DELTA, timestamp=2000, duration=33000)

    assert chunk.type == EncodedVideoChunkType.DELTA
    assert chunk.timestamp == 2000
    assert chunk.duration == 33000
    assert chunk.byte_length == len(data)


def test_encoded_video_chunk_copy_to():
    """copy_to() メソッドをテスト (WebCodecs API 準拠)"""
    import numpy as np

    data = b"original_video_data"
    chunk = EncodedVideoChunk(data, EncodedVideoChunkType.KEY, timestamp=0)

    # destination バッファを確保
    destination = np.zeros(chunk.byte_length, dtype=np.uint8)
    chunk.copy_to(destination)

    assert bytes(destination) == data
    assert len(destination) == chunk.byte_length


def test_encoded_video_chunk_get_data():
    """get_data() メソッドをテスト"""
    import numpy as np

    data = b"test_video_chunk_data"
    chunk = EncodedVideoChunk(data, EncodedVideoChunkType.DELTA, timestamp=5000)

    # copy_to() を使用してデータを取得
    destination = np.zeros(chunk.byte_length, dtype=np.uint8)
    chunk.copy_to(destination)

    assert bytes(destination) == data
    assert len(destination) == chunk.byte_length


def test_encoded_video_chunk_empty_data():
    """空のデータで EncodedVideoChunk を作成"""
    import numpy as np

    data = b""
    chunk = EncodedVideoChunk(data, EncodedVideoChunkType.KEY, timestamp=0)

    assert chunk.byte_length == 0
    # 空のデータの場合、サイズ 0 のバッファを渡して copy_to() を呼び出す
    # これにより copy_to() が空のデータでも正しく動作することを確認
    destination = np.zeros(0, dtype=np.uint8)
    chunk.copy_to(destination)
    assert bytes(destination) == data


def test_encoded_video_chunk_large_data():
    """大きなデータで EncodedVideoChunk を作成"""
    import numpy as np

    data = b"x" * 1000000  # 1MB
    chunk = EncodedVideoChunk(data, EncodedVideoChunkType.KEY, timestamp=0)

    assert chunk.byte_length == 1000000
    # copy_to() を使用してデータを取得
    destination = np.zeros(chunk.byte_length, dtype=np.uint8)
    chunk.copy_to(destination)
    assert len(destination) == 1000000
    assert bytes(destination) == data


def test_encoded_video_chunk_type_key():
    """KEY タイプの EncodedVideoChunk をテスト"""
    data = b"key_frame_data"
    chunk = EncodedVideoChunk(data, EncodedVideoChunkType.KEY, timestamp=0)

    assert chunk.type == EncodedVideoChunkType.KEY


def test_encoded_video_chunk_type_delta():
    """DELTA タイプの EncodedVideoChunk をテスト"""
    data = b"delta_frame_data"
    chunk = EncodedVideoChunk(data, EncodedVideoChunkType.DELTA, timestamp=0)

    assert chunk.type == EncodedVideoChunkType.DELTA
