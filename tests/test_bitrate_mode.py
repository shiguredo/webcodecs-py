"""
ビットレートモードのテスト

WebCodecs API に準拠した bitrate_mode パラメータの動作を確認する。
"""

import numpy as np

from webcodecs import (
    VideoEncoderBitrateMode,
    LatencyMode,
    VideoEncoder,
    VideoEncoderConfig,
    VideoFrame,
    VideoFrameBufferInit,
    VideoPixelFormat,
)


def test_bitrate_mode_constant():
    """constant ビットレートモードのテスト"""
    chunks = []

    def on_output(chunk):
        chunks.append(chunk)

    def on_error(error):
        raise Exception(f"エラー: {error}")

    encoder = VideoEncoder(on_output, on_error)

    config: VideoEncoderConfig = {
        "codec": "av01.0.04M.08",
        "width": 640,
        "height": 480,
        "bitrate": 1000000,
        "framerate": 30.0,
        "bitrate_mode": VideoEncoderBitrateMode.CONSTANT,
        "latency_mode": LatencyMode.REALTIME,
    }

    encoder.configure(config)

    # テストフレームを作成
    data = np.zeros(640 * 480 * 3 // 2, dtype=np.uint8)
    init: VideoFrameBufferInit = {
        "format": VideoPixelFormat.I420,
        "coded_width": 640,
        "coded_height": 480,
        "timestamp": 0,
    }
    frame = VideoFrame(data, init)

    encoder.encode(frame, {"key_frame": True})
    encoder.flush()
    encoder.close()

    assert len(chunks) == 1
    assert chunks[0].byte_length > 0


def test_bitrate_mode_variable():
    """variable ビットレートモードのテスト"""
    chunks = []

    def on_output(chunk):
        chunks.append(chunk)

    def on_error(error):
        raise Exception(f"エラー: {error}")

    encoder = VideoEncoder(on_output, on_error)

    config: VideoEncoderConfig = {
        "codec": "av01.0.04M.08",
        "width": 640,
        "height": 480,
        "bitrate": 1000000,
        "framerate": 30.0,
        "bitrate_mode": VideoEncoderBitrateMode.VARIABLE,
        "latency_mode": LatencyMode.REALTIME,
    }

    encoder.configure(config)

    # テストフレームを作成
    data = np.zeros(640 * 480 * 3 // 2, dtype=np.uint8)
    init: VideoFrameBufferInit = {
        "format": VideoPixelFormat.I420,
        "coded_width": 640,
        "coded_height": 480,
        "timestamp": 0,
    }
    frame = VideoFrame(data, init)

    encoder.encode(frame, {"key_frame": True})
    encoder.flush()
    encoder.close()

    assert len(chunks) == 1
    assert chunks[0].byte_length > 0


def test_bitrate_mode_quantizer():
    """quantizer ビットレートモードのテスト"""
    chunks = []

    def on_output(chunk):
        chunks.append(chunk)

    def on_error(error):
        raise Exception(f"エラー: {error}")

    encoder = VideoEncoder(on_output, on_error)

    config: VideoEncoderConfig = {
        "codec": "av01.0.04M.08",
        "width": 640,
        "height": 480,
        "bitrate": 1000000,
        "framerate": 30.0,
        "bitrate_mode": VideoEncoderBitrateMode.QUANTIZER,
        "latency_mode": LatencyMode.REALTIME,
    }

    encoder.configure(config)

    # テストフレームを作成
    data = np.zeros(640 * 480 * 3 // 2, dtype=np.uint8)
    init: VideoFrameBufferInit = {
        "format": VideoPixelFormat.I420,
        "coded_width": 640,
        "coded_height": 480,
        "timestamp": 0,
    }
    frame = VideoFrame(data, init)

    encoder.encode(frame, {"key_frame": True})
    encoder.flush()
    encoder.close()

    assert len(chunks) == 1
    assert chunks[0].byte_length > 0


def test_bitrate_mode_default():
    """デフォルトのビットレートモード（variable）のテスト"""
    chunks = []

    def on_output(chunk):
        chunks.append(chunk)

    def on_error(error):
        raise Exception(f"エラー: {error}")

    encoder = VideoEncoder(on_output, on_error)

    # bitrate_mode を指定しない場合は variable がデフォルト
    config: VideoEncoderConfig = {
        "codec": "av01.0.04M.08",
        "width": 640,
        "height": 480,
        "bitrate": 1000000,
        "framerate": 30.0,
        "latency_mode": LatencyMode.REALTIME,
    }

    encoder.configure(config)

    # テストフレームを作成
    data = np.zeros(640 * 480 * 3 // 2, dtype=np.uint8)
    init: VideoFrameBufferInit = {
        "format": VideoPixelFormat.I420,
        "coded_width": 640,
        "coded_height": 480,
        "timestamp": 0,
    }
    frame = VideoFrame(data, init)

    encoder.encode(frame, {"key_frame": True})
    encoder.flush()
    encoder.close()

    assert len(chunks) == 1
    assert chunks[0].byte_length > 0
