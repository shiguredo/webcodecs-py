"""ビデオエンコーダー・デコーダーの on_dequeue コールバックのテスト

VideoEncoder、VideoDecoder の on_dequeue コールバックが適切に呼ばれることを検証します。
"""

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


def test_video_encoder_on_dequeue():
    """VideoEncoder の on_dequeue コールバックが呼ばれることを確認"""
    dequeue_count = 0

    def on_dequeue():
        nonlocal dequeue_count
        dequeue_count += 1

    def on_output(chunk):
        pass

    def on_error(error):
        pass

    encoder = VideoEncoder(on_output, on_error)
    encoder.on_dequeue(on_dequeue)

    config: VideoEncoderConfig = {
        "codec": "av01.0.04M.08",
        "width": 160,
        "height": 120,
        "bitrate": 100000,
        "latency_mode": LatencyMode.REALTIME,
    }
    encoder.configure(config)

    width, height = 160, 120
    data_size = width * height * 3 // 2  # I420
    data = np.zeros(data_size, dtype=np.uint8)
    init: VideoFrameBufferInit = {
        "format": VideoPixelFormat.I420,
        "coded_width": width,
        "coded_height": height,
        "timestamp": 0,
    }
    frame = VideoFrame(data, init)
    encoder.encode(frame)
    encoder.flush()
    frame.close()

    # encode と flush で少なくとも 1 回は on_dequeue が呼ばれるはず
    assert dequeue_count > 0
    encoder.close()


def test_video_decoder_on_dequeue():
    """VideoDecoder の on_dequeue コールバックが呼ばれることを確認"""
    dequeue_count = 0

    def on_dequeue():
        nonlocal dequeue_count
        dequeue_count += 1

    # まずエンコードしてチャンクを作る
    chunks = []

    def on_output(chunk):
        chunks.append(chunk)

    def on_error(error):
        pass

    encoder = VideoEncoder(on_output, on_error)

    config: VideoEncoderConfig = {
        "codec": "av01.0.04M.08",
        "width": 160,
        "height": 120,
        "bitrate": 100000,
        "latency_mode": LatencyMode.REALTIME,
    }
    encoder.configure(config)

    width, height = 160, 120
    data_size = width * height * 3 // 2  # I420
    data = np.zeros(data_size, dtype=np.uint8)
    init2: VideoFrameBufferInit = {
        "format": VideoPixelFormat.I420,
        "coded_width": width,
        "coded_height": height,
        "timestamp": 0,
    }
    frame = VideoFrame(data, init2)
    encoder.encode(frame, {"key_frame": True})
    encoder.flush()
    frame.close()
    encoder.close()

    # デコーダーでデコード
    decoder = VideoDecoder(lambda frame: frame.close(), lambda err: None)
    decoder.on_dequeue(on_dequeue)

    decoder_config: VideoDecoderConfig = {"codec": "av01.0.04M.08"}
    decoder.configure(decoder_config)

    for chunk in chunks:
        decoder.decode(chunk)
    decoder.flush()

    # デコード処理で on_dequeue が呼ばれるはず
    assert dequeue_count > 0
    decoder.close()


def test_video_multiple_callbacks():
    """複数のコールバックが適切に呼ばれることを確認"""
    output_count = 0
    dequeue_count = 0

    def on_output(chunk):
        nonlocal output_count
        output_count += 1

    def on_dequeue():
        nonlocal dequeue_count
        dequeue_count += 1

    def on_error(error):
        pass

    encoder = VideoEncoder(on_output, on_error)
    encoder.on_dequeue(on_dequeue)

    config: VideoEncoderConfig = {
        "codec": "av01.0.04M.08",
        "width": 160,
        "height": 120,
        "bitrate": 100000,
        "latency_mode": LatencyMode.REALTIME,
    }
    encoder.configure(config)

    width, height = 160, 120
    data_size = width * height * 3 // 2  # I420
    data = np.zeros(data_size, dtype=np.uint8)
    init: VideoFrameBufferInit = {
        "format": VideoPixelFormat.I420,
        "coded_width": width,
        "coded_height": height,
        "timestamp": 0,
    }
    frame = VideoFrame(data, init)
    encoder.encode(frame)
    encoder.flush()
    frame.close()

    # 両方のコールバックが呼ばれるはず
    assert output_count > 0
    assert dequeue_count > 0
    encoder.close()
