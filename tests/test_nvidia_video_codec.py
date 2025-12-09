"""NVIDIA Video Codec SDK (NVENC/NVDEC) のテスト

NVIDIA GPU が利用可能な環境でのみ実行される
"""

import os
import pytest
import numpy as np

from webcodecs import (
    CodecState,
    EncodedVideoChunk,
    EncodedVideoChunkType,
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


@pytest.mark.parametrize(
    "codec",
    [
        "avc1.42001f",
        "hvc1.1.6.L93.B0",
    ],
)
def test_description(codec):
    """description 生成とデコードテスト"""
    encoded_chunks = []
    metadatas = []
    decoded_frames = []
    encode_errors = []
    decode_errors = []

    def on_encode_output(chunk, metadata=None):
        encoded_chunks.append(chunk)
        metadatas.append(metadata)

    def on_encode_error(error):
        encode_errors.append(error)

    def on_decode_output(frame):
        decoded_frames.append(frame)

    def on_decode_error(error):
        decode_errors.append(error)

    # エンコード
    encoder = VideoEncoder(on_encode_output, on_encode_error)
    config = {
        "codec": codec,
        "width": 320,
        "height": 240,
        "bitrate": 500_000,
        "hardware_acceleration_engine": HardwareAccelerationEngine.NVIDIA_VIDEO_CODEC,
    }
    encoder.configure(config)
    assert encoder.state == CodecState.CONFIGURED

    for i in range(5):
        frame = create_test_frame(320, 240, i * 33333)
        encoder.encode(frame, {"key_frame": i == 0})

    encoder.flush()
    encoder.close()

    assert len(encode_errors) == 0
    assert len(encoded_chunks) > 0

    # キーフレームの metadata から description を取得
    key_frame_metadata = None
    for i, chunk in enumerate(encoded_chunks):
        if chunk.type == EncodedVideoChunkType.KEY:
            key_frame_metadata = metadatas[i]
            break

    # キーフレームの metadata に description が含まれていることを確認
    assert key_frame_metadata is not None
    assert "decoder_config" in key_frame_metadata
    decoder_config = key_frame_metadata["decoder_config"]
    assert "description" in decoder_config
    description = decoder_config["description"]
    # avcC/hvcC 形式: 最初のバイトは configurationVersion = 1
    assert len(description) > 0
    assert description[0] == 1

    # デコード (description を使用)
    decoder = VideoDecoder(on_decode_output, on_decode_error)
    decoder.configure(
        {
            "codec": codec,
            "description": description,
            "hardware_acceleration_engine": HardwareAccelerationEngine.NVIDIA_VIDEO_CODEC,
        }
    )

    for chunk in encoded_chunks:
        decoder.decode(chunk)

    decoder.flush()
    decoder.close()

    assert len(decode_errors) == 0
    assert len(decoded_frames) > 0
    # デコードされたフレーム数がエンコードしたフレーム数と一致することを確認
    assert len(decoded_frames) == 5


@pytest.mark.parametrize(
    "codec",
    [
        "avc1.42001f",
        "hvc1.1.6.L93.B0",
        "av01.0.04M.08",
    ],
)
def test_encode_decode(codec):
    """エンコード + デコードのテスト"""
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
            "codec": codec,
            "width": 320,
            "height": 240,
            "bitrate": 500_000,
            "hardware_acceleration_engine": HardwareAccelerationEngine.NVIDIA_VIDEO_CODEC,
        }
    )

    for i in range(5):
        frame = create_test_frame(320, 240, i * 33333)
        encoder.encode(frame, {"key_frame": i == 0})

    encoder.flush()
    encoder.close()

    assert len(encode_errors) == 0
    assert len(encoded_chunks) > 0

    # 最初のチャンクがキーフレームであることを確認
    assert encoded_chunks[0].type == EncodedVideoChunkType.KEY

    # デコード
    decoder = VideoDecoder(on_decode_output, on_decode_error)
    decoder.configure(
        {
            "codec": codec,
            "hardware_acceleration_engine": HardwareAccelerationEngine.NVIDIA_VIDEO_CODEC,
        }
    )

    for chunk in encoded_chunks:
        decoder.decode(chunk)

    decoder.flush()
    decoder.close()

    assert len(decode_errors) == 0
    assert len(decoded_frames) > 0


@pytest.mark.parametrize(
    "codec",
    [
        "avc1.42001f",
        "hvc1.1.6.L93.B0",
        "av01.0.04M.08",
    ],
)
def test_encoder_is_config_supported(codec):
    """エンコーダーのサポートチェック"""
    support = VideoEncoder.is_config_supported(
        {
            "codec": codec,
            "width": 1920,
            "height": 1080,
            "hardware_acceleration_engine": HardwareAccelerationEngine.NVIDIA_VIDEO_CODEC,
        }
    )
    assert support["supported"] is True


@pytest.mark.parametrize(
    "codec",
    [
        "avc1.42001f",
        "hvc1.1.6.L93.B0",
        "av01.0.04M.08",
    ],
)
def test_decoder_is_config_supported(codec):
    """デコーダーのサポートチェック"""
    support = VideoDecoder.is_config_supported(
        {
            "codec": codec,
            "hardware_acceleration_engine": HardwareAccelerationEngine.NVIDIA_VIDEO_CODEC,
        }
    )
    assert support["supported"] is True
