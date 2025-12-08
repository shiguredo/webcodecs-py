"""NVIDIA Video Codec SDK (NVENC/NVDEC) のテスト

NVIDIA GPU が利用可能な環境でのみ実行される
"""

import os
import pytest
import numpy as np

from webcodecs import (
    CodecState,
    EncodedVideoChunk,
    HardwareAccelerationEngine,
    VideoDecoder,
    VideoEncoder,
    VideoFrame,
    VideoPixelFormat,
)

# NVIDIA Video Codec SDK 環境でのみテストを実行
pytestmark = pytest.mark.skipif(
    os.environ.get("NVIDIA_VIDEO_CODEC") is None, reason="NVIDIA Video Codec SDK でのみ実行する"
)


def create_test_frame(width: int, height: int, timestamp: int) -> VideoFrame:
    """テスト用のフレームを作成"""
    # NV12: Y プレーン (width * height) + UV インターリーブプレーン (width * height // 2)
    y_size = width * height
    uv_size = width * height // 2
    data = np.full(y_size + uv_size, 128, dtype=np.uint8)
    init = {
        "format": VideoPixelFormat.NV12,
        "coded_width": width,
        "coded_height": height,
        "timestamp": timestamp,
    }
    return VideoFrame(data, init)


def test_nvenc_h264_encode():
    """H.264 エンコードのテスト"""
    chunks = []
    errors = []

    def on_output(chunk, metadata=None):
        chunks.append(chunk)

    def on_error(error):
        errors.append(error)

    encoder = VideoEncoder(on_output, on_error)
    config = {
        "codec": "avc1.42001f",
        "width": 320,
        "height": 240,
        "bitrate": 500_000,
        "hardware_acceleration_engine": HardwareAccelerationEngine.NVIDIA_VIDEO_CODEC,
    }
    encoder.configure(config)
    assert encoder.state == CodecState.CONFIGURED

    # フレームをエンコード
    for i in range(10):
        frame = create_test_frame(320, 240, i * 33333)
        encoder.encode(frame, {"keyFrame": i == 0})

    encoder.flush()
    encoder.close()

    assert len(errors) == 0
    assert len(chunks) > 0


def test_nvenc_hevc_encode():
    """HEVC エンコードのテスト"""
    chunks = []
    errors = []

    def on_output(chunk, metadata=None):
        chunks.append(chunk)

    def on_error(error):
        errors.append(error)

    encoder = VideoEncoder(on_output, on_error)
    config = {
        "codec": "hvc1.1.6.L93.B0",
        "width": 320,
        "height": 240,
        "bitrate": 500_000,
        "hardware_acceleration_engine": HardwareAccelerationEngine.NVIDIA_VIDEO_CODEC,
    }
    encoder.configure(config)
    assert encoder.state == CodecState.CONFIGURED

    # フレームをエンコード
    for i in range(10):
        frame = create_test_frame(320, 240, i * 33333)
        encoder.encode(frame, {"keyFrame": i == 0})

    encoder.flush()
    encoder.close()

    assert len(errors) == 0
    assert len(chunks) > 0


def test_nvenc_av1_encode():
    """AV1 エンコードのテスト"""
    chunks = []
    errors = []

    def on_output(chunk, metadata=None):
        chunks.append(chunk)

    def on_error(error):
        errors.append(error)

    encoder = VideoEncoder(on_output, on_error)
    config = {
        "codec": "av01.0.04M.08",
        "width": 320,
        "height": 240,
        "bitrate": 500_000,
        "hardware_acceleration_engine": HardwareAccelerationEngine.NVIDIA_VIDEO_CODEC,
    }
    encoder.configure(config)
    assert encoder.state == CodecState.CONFIGURED

    # フレームをエンコード
    for i in range(10):
        frame = create_test_frame(320, 240, i * 33333)
        encoder.encode(frame, {"keyFrame": i == 0})

    encoder.flush()
    encoder.close()

    assert len(errors) == 0
    assert len(chunks) > 0


def test_nvdec_h264_decode():
    """H.264 デコードのテスト"""
    encoded_chunks = []
    decoded_frames = []
    encode_errors = []
    decode_errors = []

    def on_encode_output(chunk, metadata=None):
        encoded_chunks.append(chunk)

    def on_encode_error(error):
        encode_errors.append(error)

    def on_decode_output(frame):
        decoded_frames.append(frame)

    def on_decode_error(error):
        decode_errors.append(error)

    # エンコード
    encoder = VideoEncoder(on_encode_output, on_encode_error)
    encoder.configure(
        {
            "codec": "avc1.42001f",
            "width": 320,
            "height": 240,
            "bitrate": 500_000,
            "hardware_acceleration_engine": HardwareAccelerationEngine.NVIDIA_VIDEO_CODEC,
        }
    )

    for i in range(5):
        frame = create_test_frame(320, 240, i * 33333)
        encoder.encode(frame, {"keyFrame": i == 0})

    encoder.flush()
    encoder.close()

    assert len(encode_errors) == 0
    assert len(encoded_chunks) > 0

    # デコード
    decoder = VideoDecoder(on_decode_output, on_decode_error)
    decoder.configure(
        {
            "codec": "avc1.42001f",
            "hardware_acceleration_engine": HardwareAccelerationEngine.NVIDIA_VIDEO_CODEC,
        }
    )

    for chunk in encoded_chunks:
        decoder.decode(chunk)

    decoder.flush()
    decoder.close()

    assert len(decode_errors) == 0
    assert len(decoded_frames) > 0


def test_nvidia_encoder_h264_is_config_supported():
    """H.264 エンコーダーのサポートチェック"""
    support = VideoEncoder.is_config_supported(
        {
            "codec": "avc1.42001f",
            "width": 1920,
            "height": 1080,
            "hardware_acceleration_engine": HardwareAccelerationEngine.NVIDIA_VIDEO_CODEC,
        }
    )
    assert support["supported"] is True


def test_nvidia_encoder_hevc_is_config_supported():
    """HEVC エンコーダーのサポートチェック"""
    support = VideoEncoder.is_config_supported(
        {
            "codec": "hvc1.1.6.L93.B0",
            "width": 1920,
            "height": 1080,
            "hardware_acceleration_engine": HardwareAccelerationEngine.NVIDIA_VIDEO_CODEC,
        }
    )
    assert support["supported"] is True


def test_nvidia_encoder_av1_is_config_supported():
    """AV1 エンコーダーのサポートチェック"""
    support = VideoEncoder.is_config_supported(
        {
            "codec": "av01.0.04M.08",
            "width": 1920,
            "height": 1080,
            "hardware_acceleration_engine": HardwareAccelerationEngine.NVIDIA_VIDEO_CODEC,
        }
    )
    assert support["supported"] is True


def test_nvidia_decoder_h264_is_config_supported():
    """H.264 デコーダーのサポートチェック"""
    support = VideoDecoder.is_config_supported(
        {
            "codec": "avc1.42001f",
            "hardware_acceleration_engine": HardwareAccelerationEngine.NVIDIA_VIDEO_CODEC,
        }
    )
    assert support["supported"] is True


def test_nvidia_decoder_hevc_is_config_supported():
    """HEVC デコーダーのサポートチェック"""
    support = VideoDecoder.is_config_supported(
        {
            "codec": "hvc1.1.6.L93.B0",
            "hardware_acceleration_engine": HardwareAccelerationEngine.NVIDIA_VIDEO_CODEC,
        }
    )
    assert support["supported"] is True


def test_nvidia_decoder_av1_is_config_supported():
    """AV1 デコーダーのサポートチェック"""
    support = VideoDecoder.is_config_supported(
        {
            "codec": "av01.0.04M.08",
            "hardware_acceleration_engine": HardwareAccelerationEngine.NVIDIA_VIDEO_CODEC,
        }
    )
    assert support["supported"] is True
