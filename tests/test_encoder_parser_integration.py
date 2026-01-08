"""ハードウェアエンコーダー出力に対するヘッダーパーサー統合テスト.

各ハードウェアエンコーダー (Apple Video Toolbox, NVIDIA Video Codec, Intel VPL) の
出力がパーサーで正しく解析できることを確認する。
"""

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
    """Apple Video Toolbox が利用可能かどうかを確認"""
    return os.environ.get("APPLE_VIDEO_TOOLBOX") is not None


def is_nvidia_video_codec_available() -> bool:
    """NVIDIA Video Codec SDK が利用可能かどうかを確認"""
    return os.environ.get("NVIDIA_VIDEO_CODEC") is not None


def is_intel_vpl_available() -> bool:
    """Intel VPL が利用可能かどうかを確認"""
    if sys.platform != "linux":
        return False
    if os.environ.get("INTEL_VPL") != "true":
        return False
    caps = get_video_codec_capabilities()
    return HardwareAccelerationEngine.INTEL_VPL in caps


# =============================================================================
# ヘルパー関数
# =============================================================================


def create_test_frame(
    width: int,
    height: int,
    timestamp: int,
    pixel_format: VideoPixelFormat = VideoPixelFormat.I420,
) -> VideoFrame:
    """テスト用の VideoFrame を作成"""
    if pixel_format == VideoPixelFormat.NV12:
        data_size = width * height + width * height // 2
    else:
        data_size = width * height * 3 // 2
    data = np.zeros(data_size, dtype=np.uint8)
    init: VideoFrameBufferInit = {
        "format": pixel_format,
        "coded_width": width,
        "coded_height": height,
        "timestamp": timestamp,
    }
    return VideoFrame(data, init)


def encode_key_frame(
    codec: str,
    width: int,
    height: int,
    hardware_acceleration_engine: HardwareAccelerationEngine,
    codec_config: dict | None = None,
    pixel_format: VideoPixelFormat = VideoPixelFormat.I420,
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

    config: VideoEncoderConfig = {
        "codec": codec,
        "width": width,
        "height": height,
        "bitrate": 1_000_000,
        "framerate": 30,
        "latency_mode": LatencyMode.REALTIME,
        "hardware_acceleration_engine": hardware_acceleration_engine,
    }
    if codec_config:
        config.update(codec_config)

    encoder.configure(config)

    frame = create_test_frame(width, height, 0, pixel_format)
    encoder.encode(frame, {"key_frame": True})
    frame.close()

    encoder.flush()
    encoder.close()

    assert len(encoded_chunks) > 0, "エンコードされたチャンクが生成されませんでした"

    # キーフレームを探す
    key_frame_chunk = None
    key_frame_metadata = None
    for i, chunk in enumerate(encoded_chunks):
        if chunk.type == EncodedVideoChunkType.KEY:
            key_frame_chunk = chunk
            key_frame_metadata = metadatas[i]
            break

    assert key_frame_chunk is not None, "キーフレームが見つかりませんでした"

    # チャンクデータを取得
    chunk_data = np.zeros(key_frame_chunk.byte_length, dtype=np.uint8)
    key_frame_chunk.copy_to(chunk_data)

    # description を取得
    description = None
    if key_frame_metadata and "decoder_config" in key_frame_metadata:
        decoder_config = key_frame_metadata["decoder_config"]
        if "description" in decoder_config:
            description = decoder_config["description"]

    return bytes(chunk_data), description


# =============================================================================
# スキップマーカー
# =============================================================================

skip_apple = pytest.mark.skipif(
    not is_apple_video_toolbox_available(),
    reason="Apple Video Toolbox が利用できない環境",
)

skip_nvidia = pytest.mark.skipif(
    not is_nvidia_video_codec_available(),
    reason="NVIDIA Video Codec SDK が利用できない環境",
)

skip_intel = pytest.mark.skipif(
    not is_intel_vpl_available(),
    reason="Intel VPL が利用できない環境",
)


# =============================================================================
# description パーステスト
# =============================================================================


@pytest.mark.parametrize(
    ("codec", "width", "height", "engine", "pixel_format"),
    [
        # Apple Video Toolbox
        pytest.param(
            "avc1.42E01E",
            640,
            480,
            HardwareAccelerationEngine.APPLE_VIDEO_TOOLBOX,
            VideoPixelFormat.I420,
            marks=skip_apple,
            id="apple-h264",
        ),
        pytest.param(
            "hvc1.1.6.L93.B0",
            1280,
            720,
            HardwareAccelerationEngine.APPLE_VIDEO_TOOLBOX,
            VideoPixelFormat.I420,
            marks=skip_apple,
            id="apple-h265",
        ),
        # NVIDIA Video Codec
        pytest.param(
            "avc1.42001f",
            640,
            480,
            HardwareAccelerationEngine.NVIDIA_VIDEO_CODEC,
            VideoPixelFormat.NV12,
            marks=skip_nvidia,
            id="nvidia-h264",
        ),
        pytest.param(
            "hvc1.1.6.L93.B0",
            1280,
            720,
            HardwareAccelerationEngine.NVIDIA_VIDEO_CODEC,
            VideoPixelFormat.NV12,
            marks=skip_nvidia,
            id="nvidia-h265",
        ),
        # Intel VPL
        pytest.param(
            "avc1.42001e",
            640,
            480,
            HardwareAccelerationEngine.INTEL_VPL,
            VideoPixelFormat.I420,
            marks=skip_intel,
            id="intel-h264",
        ),
        pytest.param(
            "hvc1.1.6.L93.B0",
            1280,
            720,
            HardwareAccelerationEngine.INTEL_VPL,
            VideoPixelFormat.I420,
            marks=skip_intel,
            id="intel-h265",
        ),
    ],
)
def test_description_parse(codec, width, height, engine, pixel_format):
    """description (avcC/hvcC) のパーステスト"""
    is_hevc = codec.startswith("hvc") or codec.startswith("hev")

    _, description = encode_key_frame(
        codec,
        width,
        height,
        engine,
        pixel_format=pixel_format,
    )

    assert description is not None
    assert len(description) > 0
    assert description[0] == 1

    if is_hevc:
        info = parse_hevc_description(description)
        assert info.vps is not None
    else:
        info = parse_avc_description(description)

    assert info.sps is not None
    assert info.pps is not None
    assert info.sps.width == width
    assert info.sps.height == height


# =============================================================================
# Annex B チャンクパーステスト (Apple Video Toolbox のみ)
# =============================================================================


@pytest.mark.parametrize(
    ("codec", "codec_config"),
    [
        pytest.param("avc1.42E01E", {"avc": {"format": "annexb"}}, id="h264"),
        pytest.param("hvc1.1.6.L93.B0", {"hevc": {"format": "annexb"}}, id="h265"),
    ],
)
@skip_apple
def test_annexb_chunk_parse(codec, codec_config):
    """Annex B チャンクのパーステスト (Apple Video Toolbox)"""
    is_hevc = codec.startswith("hvc") or codec.startswith("hev")

    chunk_data, _ = encode_key_frame(
        codec,
        640,
        480,
        HardwareAccelerationEngine.APPLE_VIDEO_TOOLBOX,
        codec_config,
    )

    assert len(chunk_data) >= 4
    has_start_code = chunk_data[0:4] == b"\x00\x00\x00\x01" or chunk_data[0:3] == b"\x00\x00\x01"
    assert has_start_code

    if is_hevc:
        info = parse_hevc_annexb(chunk_data)
    else:
        info = parse_avc_annexb(chunk_data)

    assert len(info.nal_units) > 0
