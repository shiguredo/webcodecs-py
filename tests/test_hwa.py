"""ハードウェアアクセラレーション (HWA) エンコーダーの統合テスト"""

import os
import sys

import numpy as np
import pytest

from webcodecs import (
    EncodedVideoChunkType,
    HardwareAccelerationEngine,
    LatencyMode,
    VideoEncoder,
    VideoEncoderConfig,
    VideoFrame,
    VideoFrameBufferInit,
    VideoPixelFormat,
    get_video_codec_capabilities,
    parse_avc_annexb,
    parse_avc_description,
    parse_hevc_annexb,
    parse_hevc_description,
)


# =============================================================================
# 環境検出
# =============================================================================


def is_apple_video_toolbox_available() -> bool:
    return os.environ.get("APPLE_VIDEO_TOOLBOX") is not None


def is_nvidia_video_codec_available() -> bool:
    return os.environ.get("NVIDIA_VIDEO_CODEC") is not None


def is_intel_vpl_available() -> bool:
    if sys.platform != "linux":
        return False
    if os.environ.get("INTEL_VPL") != "true":
        return False
    caps = get_video_codec_capabilities()
    return HardwareAccelerationEngine.INTEL_VPL in caps


requires_apple = pytest.mark.skipif(
    not is_apple_video_toolbox_available(),
    reason="Apple Video Toolbox が利用できない環境",
)

requires_nvidia = pytest.mark.skipif(
    not is_nvidia_video_codec_available(),
    reason="NVIDIA Video Codec SDK が利用できない環境",
)

requires_intel = pytest.mark.skipif(
    not is_intel_vpl_available(),
    reason="Intel VPL が利用できない環境",
)


# =============================================================================
# ヘルパー関数
# =============================================================================


def encode_key_frame(
    codec: str,
    engine: HardwareAccelerationEngine,
    codec_config: dict | None = None,
) -> tuple[bytes, bytes | None]:
    """キーフレームをエンコードして (chunk_data, description) を返す"""
    encoded_chunks = []
    metadatas = []

    def on_output(chunk, metadata=None):
        encoded_chunks.append(chunk)
        metadatas.append(metadata)

    def on_error(error):
        raise RuntimeError(f"エンコーダーエラー: {error}")

    encoder = VideoEncoder(on_output, on_error)

    width, height = 640, 480

    config: VideoEncoderConfig = {
        "codec": codec,
        "width": width,
        "height": height,
        "bitrate": 1_000_000,
        "framerate": 30,
        "latency_mode": LatencyMode.REALTIME,
        "hardware_acceleration_engine": engine,
    }
    if codec_config:
        config.update(codec_config)

    encoder.configure(config)

    # 全 HWA エンコーダーは NV12 を使用
    data_size = width * height + width * height // 2
    data = np.zeros(data_size, dtype=np.uint8)
    init: VideoFrameBufferInit = {
        "format": VideoPixelFormat.NV12,
        "coded_width": width,
        "coded_height": height,
        "timestamp": 0,
    }
    frame = VideoFrame(data, init)
    encoder.encode(frame, {"key_frame": True})
    frame.close()

    encoder.flush()
    encoder.close()

    assert len(encoded_chunks) > 0

    key_frame_chunk = None
    key_frame_metadata = None
    for i, chunk in enumerate(encoded_chunks):
        if chunk.type == EncodedVideoChunkType.KEY:
            key_frame_chunk = chunk
            key_frame_metadata = metadatas[i]
            break

    assert key_frame_chunk is not None

    chunk_data = np.zeros(key_frame_chunk.byte_length, dtype=np.uint8)
    key_frame_chunk.copy_to(chunk_data)

    description = None
    if key_frame_metadata and "decoder_config" in key_frame_metadata:
        decoder_config = key_frame_metadata["decoder_config"]
        if "description" in decoder_config:
            description = decoder_config["description"]

    return bytes(chunk_data), description


# =============================================================================
# H.264 (AVC) テスト
# =============================================================================


@requires_apple
def test_avc_parse_description_from_encoder():
    """AVC エンコーダー出力の description (avcC) をパースできることを確認"""
    codec = "avc1.42001f"
    engine = HardwareAccelerationEngine.APPLE_VIDEO_TOOLBOX
    _, description = encode_key_frame(codec, engine)

    assert description is not None
    assert len(description) > 0
    assert description[0] == 1

    info = parse_avc_description(description)

    assert info.sps is not None
    assert info.pps is not None
    assert info.sps.width == 640
    assert info.sps.height == 480


@pytest.mark.parametrize(
    ("engine", "codec_config"),
    [
        pytest.param(
            HardwareAccelerationEngine.APPLE_VIDEO_TOOLBOX,
            {"avc": {"format": "annexb"}},
            marks=requires_apple,
            id="apple",
        ),
        pytest.param(
            HardwareAccelerationEngine.NVIDIA_VIDEO_CODEC,
            None,
            marks=requires_nvidia,
            id="nvidia",
        ),
        pytest.param(
            HardwareAccelerationEngine.INTEL_VPL,
            None,
            marks=requires_intel,
            id="intel",
        ),
    ],
)
def test_avc_parse_annexb_chunk_from_encoder(engine, codec_config):
    """AVC エンコーダー出力の Annex B チャンクをパースできることを確認"""
    codec = "avc1.42001f"
    chunk_data, _ = encode_key_frame(codec, engine, codec_config)

    assert len(chunk_data) >= 4
    has_start_code = chunk_data[0:4] == b"\x00\x00\x00\x01" or chunk_data[0:3] == b"\x00\x00\x01"
    assert has_start_code

    info = parse_avc_annexb(chunk_data)

    assert len(info.nal_units) > 0


# =============================================================================
# H.265 (HEVC) テスト
# =============================================================================


@requires_apple
def test_hevc_parse_description_from_encoder():
    """HEVC エンコーダー出力の description (hvcC) をパースできることを確認"""
    codec = "hvc1.1.6.L93.B0"
    engine = HardwareAccelerationEngine.APPLE_VIDEO_TOOLBOX
    _, description = encode_key_frame(codec, engine)

    assert description is not None
    assert len(description) > 0
    assert description[0] == 1

    info = parse_hevc_description(description)

    assert info.vps is not None
    assert info.sps is not None
    assert info.pps is not None
    assert info.sps.width == 640
    assert info.sps.height == 480


@pytest.mark.parametrize(
    ("engine", "codec_config"),
    [
        pytest.param(
            HardwareAccelerationEngine.APPLE_VIDEO_TOOLBOX,
            {"hevc": {"format": "annexb"}},
            marks=requires_apple,
            id="apple",
        ),
        pytest.param(
            HardwareAccelerationEngine.NVIDIA_VIDEO_CODEC,
            None,
            marks=requires_nvidia,
            id="nvidia",
        ),
        pytest.param(
            HardwareAccelerationEngine.INTEL_VPL,
            None,
            marks=requires_intel,
            id="intel",
        ),
    ],
)
def test_hevc_parse_annexb_chunk_from_encoder(engine, codec_config):
    """HEVC エンコーダー出力の Annex B チャンクをパースできることを確認"""
    codec = "hvc1.1.6.L93.B0"
    chunk_data, _ = encode_key_frame(codec, engine, codec_config)

    assert len(chunk_data) >= 4
    has_start_code = chunk_data[0:4] == b"\x00\x00\x00\x01" or chunk_data[0:3] == b"\x00\x00\x01"
    assert has_start_code

    info = parse_hevc_annexb(chunk_data)

    assert len(info.nal_units) > 0
