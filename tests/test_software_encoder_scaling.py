"""ソフトウェアエンコーダー (AV1, VP8, VP9) のスケーリング機能テスト.

WebCodecs API 仕様: 「The encoder MUST scale any VideoFrame whose
visible width differs from the configured width value」

libyuv を使用してスケーリングを実装。
"""

import platform

import numpy as np
import pytest

from webcodecs import (
    CodecState,
    EncodedVideoChunkType,
    LatencyMode,
    VideoDecoder,
    VideoDecoderConfig,
    VideoEncoder,
    VideoEncoderConfig,
    VideoFrame,
    VideoFrameBufferInit,
    VideoPixelFormat,
)


def _make_test_frame(width: int, height: int, frame_num: int = 0) -> VideoFrame:
    """テスト用の VideoFrame を作成する."""
    data_size = width * height * 3 // 2  # I420
    data = np.zeros(data_size, dtype=np.uint8)
    init: VideoFrameBufferInit = {
        "format": VideoPixelFormat.I420,
        "coded_width": width,
        "coded_height": height,
        "timestamp": frame_num * 1000,
    }
    frame = VideoFrame(data, init)
    return frame


# =============================================================================
# AV1 スケーリングテスト
# =============================================================================


def test_av1_encode_with_scaling():
    """AV1 エンコーダのスケーリング機能テスト."""
    # configure: 320x240 (出力解像度)
    output_width, output_height = 320, 240
    # encode: 640x480 のフレーム (入力解像度)
    input_width, input_height = 640, 480

    encoded_chunks = []

    def on_output(chunk):
        encoded_chunks.append(chunk)

    def on_error(error):
        pytest.fail(f"Encoder error: {error}")

    encoder = VideoEncoder(on_output, on_error)

    config: VideoEncoderConfig = {
        "codec": "av01.0.04M.08",
        "width": output_width,
        "height": output_height,
        "bitrate": 500_000,
        "framerate": 30.0,
        "latency_mode": LatencyMode.REALTIME,
    }
    encoder.configure(config)

    # 入力解像度のフレームを作成
    frame = _make_test_frame(input_width, input_height, 0)
    encoder.encode(frame, {"key_frame": True})
    encoder.flush()
    frame.close()

    # エンコードが成功していることを確認
    assert len(encoded_chunks) >= 1
    assert encoded_chunks[0].byte_length > 0
    assert encoded_chunks[0].type == EncodedVideoChunkType.KEY

    # デコードして出力解像度を確認
    decoded_frames = []

    def on_decode_output(frame):
        decoded_frames.append(frame)

    def on_decode_error(error):
        pytest.fail(f"Decoder error: {error}")

    decoder = VideoDecoder(on_decode_output, on_decode_error)

    decoder_config: VideoDecoderConfig = {"codec": "av01.0.04M.08"}
    decoder.configure(decoder_config)

    for chunk in encoded_chunks:
        decoder.decode(chunk)
    decoder.flush()

    # デコードされたフレームが出力解像度になっていることを確認
    assert len(decoded_frames) >= 1
    for frame in decoded_frames:
        assert frame.coded_width == output_width, (
            f"出力幅が期待値と異なる: 期待値 {output_width}, 実際 {frame.coded_width}"
        )
        assert frame.coded_height == output_height, (
            f"出力高さが期待値と異なる: 期待値 {output_height}, 実際 {frame.coded_height}"
        )
        frame.close()

    encoder.close()
    decoder.close()


def test_av1_encode_scaling_same_resolution():
    """AV1 configure と同じ解像度のフレームはスケーリングなしでエンコードされることを確認."""
    width, height = 320, 240

    encoded_chunks = []

    def on_output(chunk):
        encoded_chunks.append(chunk)

    def on_error(error):
        pytest.fail(f"Encoder error: {error}")

    encoder = VideoEncoder(on_output, on_error)

    config: VideoEncoderConfig = {
        "codec": "av01.0.04M.08",
        "width": width,
        "height": height,
        "bitrate": 500_000,
        "framerate": 30.0,
        "latency_mode": LatencyMode.REALTIME,
    }
    encoder.configure(config)

    # 同じ解像度のフレーム
    frame = _make_test_frame(width, height, 0)
    encoder.encode(frame, {"key_frame": True})
    encoder.flush()
    frame.close()

    # エンコードが成功していることを確認
    assert len(encoded_chunks) >= 1
    assert encoded_chunks[0].byte_length > 0

    encoder.close()


def test_av1_encode_scaling_multiple_frames():
    """AV1 複数フレームでのスケーリングテスト."""
    # configure: 320x240 (出力解像度)
    output_width, output_height = 320, 240
    # encode: 640x480 のフレーム (入力解像度)
    input_width, input_height = 640, 480
    num_frames = 3

    encoded_chunks = []

    def on_output(chunk):
        encoded_chunks.append(chunk)

    def on_error(error):
        pytest.fail(f"Encoder error: {error}")

    encoder = VideoEncoder(on_output, on_error)

    config: VideoEncoderConfig = {
        "codec": "av01.0.04M.08",
        "width": output_width,
        "height": output_height,
        "bitrate": 500_000,
        "framerate": 30.0,
        "latency_mode": LatencyMode.REALTIME,
    }
    encoder.configure(config)

    # 入力解像度のフレームを複数作成・エンコード
    for i in range(num_frames):
        frame = _make_test_frame(input_width, input_height, i)
        encoder.encode(frame, {"key_frame": i == 0})
        frame.close()

    encoder.flush()

    # エンコードが成功していることを確認
    assert len(encoded_chunks) >= num_frames

    encoder.close()


# =============================================================================
# VP8 スケーリングテスト
# =============================================================================


@pytest.mark.skipif(
    platform.system() not in ("Darwin", "Linux"),
    reason="VP8 は macOS / Linux のみサポート",
)
def test_vp8_encode_with_scaling():
    """VP8 エンコーダのスケーリング機能テスト."""
    # configure: 320x240 (出力解像度)
    output_width, output_height = 320, 240
    # encode: 640x480 のフレーム (入力解像度)
    input_width, input_height = 640, 480

    encoded_chunks = []

    def on_output(chunk):
        encoded_chunks.append(chunk)

    def on_error(error):
        pytest.fail(f"Encoder error: {error}")

    encoder = VideoEncoder(on_output, on_error)

    config: VideoEncoderConfig = {
        "codec": "vp8",
        "width": output_width,
        "height": output_height,
        "bitrate": 500_000,
        "framerate": 30.0,
        "latency_mode": LatencyMode.REALTIME,
    }
    encoder.configure(config)

    # 入力解像度のフレームを作成
    frame = _make_test_frame(input_width, input_height, 0)
    encoder.encode(frame, {"key_frame": True})
    encoder.flush()
    frame.close()

    # エンコードが成功していることを確認
    assert len(encoded_chunks) >= 1
    assert encoded_chunks[0].byte_length > 0
    assert encoded_chunks[0].type == EncodedVideoChunkType.KEY

    # デコードして出力解像度を確認
    decoded_frames = []

    def on_decode_output(frame):
        decoded_frames.append(frame)

    def on_decode_error(error):
        pytest.fail(f"Decoder error: {error}")

    decoder = VideoDecoder(on_decode_output, on_decode_error)

    decoder_config: VideoDecoderConfig = {"codec": "vp8"}
    decoder.configure(decoder_config)

    for chunk in encoded_chunks:
        decoder.decode(chunk)
    decoder.flush()

    # デコードされたフレームが出力解像度になっていることを確認
    assert len(decoded_frames) >= 1
    for frame in decoded_frames:
        assert frame.coded_width == output_width, (
            f"出力幅が期待値と異なる: 期待値 {output_width}, 実際 {frame.coded_width}"
        )
        assert frame.coded_height == output_height, (
            f"出力高さが期待値と異なる: 期待値 {output_height}, 実際 {frame.coded_height}"
        )
        frame.close()

    encoder.close()
    decoder.close()


@pytest.mark.skipif(
    platform.system() not in ("Darwin", "Linux"),
    reason="VP8 は macOS / Linux のみサポート",
)
def test_vp8_encode_scaling_same_resolution():
    """VP8 configure と同じ解像度のフレームはスケーリングなしでエンコードされることを確認."""
    width, height = 320, 240

    encoded_chunks = []

    def on_output(chunk):
        encoded_chunks.append(chunk)

    def on_error(error):
        pytest.fail(f"Encoder error: {error}")

    encoder = VideoEncoder(on_output, on_error)

    config: VideoEncoderConfig = {
        "codec": "vp8",
        "width": width,
        "height": height,
        "bitrate": 500_000,
        "framerate": 30.0,
        "latency_mode": LatencyMode.REALTIME,
    }
    encoder.configure(config)

    # 同じ解像度のフレーム
    frame = _make_test_frame(width, height, 0)
    encoder.encode(frame, {"key_frame": True})
    encoder.flush()
    frame.close()

    # エンコードが成功していることを確認
    assert len(encoded_chunks) >= 1
    assert encoded_chunks[0].byte_length > 0

    encoder.close()


# =============================================================================
# VP9 スケーリングテスト
# =============================================================================


@pytest.mark.skipif(
    platform.system() not in ("Darwin", "Linux"),
    reason="VP9 は macOS / Linux のみサポート",
)
def test_vp9_encode_with_scaling():
    """VP9 エンコーダのスケーリング機能テスト."""
    # configure: 320x240 (出力解像度)
    output_width, output_height = 320, 240
    # encode: 640x480 のフレーム (入力解像度)
    input_width, input_height = 640, 480

    encoded_chunks = []

    def on_output(chunk):
        encoded_chunks.append(chunk)

    def on_error(error):
        pytest.fail(f"Encoder error: {error}")

    encoder = VideoEncoder(on_output, on_error)

    config: VideoEncoderConfig = {
        "codec": "vp09.00.10.08",
        "width": output_width,
        "height": output_height,
        "bitrate": 500_000,
        "framerate": 30.0,
        "latency_mode": LatencyMode.REALTIME,
    }
    encoder.configure(config)

    # 入力解像度のフレームを作成
    frame = _make_test_frame(input_width, input_height, 0)
    encoder.encode(frame, {"key_frame": True})
    encoder.flush()
    frame.close()

    # エンコードが成功していることを確認
    assert len(encoded_chunks) >= 1
    assert encoded_chunks[0].byte_length > 0
    assert encoded_chunks[0].type == EncodedVideoChunkType.KEY

    # デコードして出力解像度を確認
    decoded_frames = []

    def on_decode_output(frame):
        decoded_frames.append(frame)

    def on_decode_error(error):
        pytest.fail(f"Decoder error: {error}")

    decoder = VideoDecoder(on_decode_output, on_decode_error)

    decoder_config: VideoDecoderConfig = {"codec": "vp09.00.10.08"}
    decoder.configure(decoder_config)

    for chunk in encoded_chunks:
        decoder.decode(chunk)
    decoder.flush()

    # デコードされたフレームが出力解像度になっていることを確認
    assert len(decoded_frames) >= 1
    for frame in decoded_frames:
        assert frame.coded_width == output_width, (
            f"出力幅が期待値と異なる: 期待値 {output_width}, 実際 {frame.coded_width}"
        )
        assert frame.coded_height == output_height, (
            f"出力高さが期待値と異なる: 期待値 {output_height}, 実際 {frame.coded_height}"
        )
        frame.close()

    encoder.close()
    decoder.close()


@pytest.mark.skipif(
    platform.system() not in ("Darwin", "Linux"),
    reason="VP9 は macOS / Linux のみサポート",
)
def test_vp9_encode_scaling_same_resolution():
    """VP9 configure と同じ解像度のフレームはスケーリングなしでエンコードされることを確認."""
    width, height = 320, 240

    encoded_chunks = []

    def on_output(chunk):
        encoded_chunks.append(chunk)

    def on_error(error):
        pytest.fail(f"Encoder error: {error}")

    encoder = VideoEncoder(on_output, on_error)

    config: VideoEncoderConfig = {
        "codec": "vp09.00.10.08",
        "width": width,
        "height": height,
        "bitrate": 500_000,
        "framerate": 30.0,
        "latency_mode": LatencyMode.REALTIME,
    }
    encoder.configure(config)

    # 同じ解像度のフレーム
    frame = _make_test_frame(width, height, 0)
    encoder.encode(frame, {"key_frame": True})
    encoder.flush()
    frame.close()

    # エンコードが成功していることを確認
    assert len(encoded_chunks) >= 1
    assert encoded_chunks[0].byte_length > 0

    encoder.close()


@pytest.mark.skipif(
    platform.system() not in ("Darwin", "Linux"),
    reason="VP9 は macOS / Linux のみサポート",
)
def test_vp9_encode_scaling_multiple_frames():
    """VP9 複数フレームでのスケーリングテスト."""
    # configure: 320x240 (出力解像度)
    output_width, output_height = 320, 240
    # encode: 640x480 のフレーム (入力解像度)
    input_width, input_height = 640, 480
    num_frames = 3

    encoded_chunks = []

    def on_output(chunk):
        encoded_chunks.append(chunk)

    def on_error(error):
        pytest.fail(f"Encoder error: {error}")

    encoder = VideoEncoder(on_output, on_error)

    config: VideoEncoderConfig = {
        "codec": "vp09.00.10.08",
        "width": output_width,
        "height": output_height,
        "bitrate": 500_000,
        "framerate": 30.0,
        "latency_mode": LatencyMode.REALTIME,
    }
    encoder.configure(config)

    # 入力解像度のフレームを複数作成・エンコード
    for i in range(num_frames):
        frame = _make_test_frame(input_width, input_height, i)
        encoder.encode(frame, {"key_frame": i == 0})
        frame.close()

    encoder.flush()

    # エンコードが成功していることを確認
    assert len(encoded_chunks) >= num_frames

    encoder.close()
