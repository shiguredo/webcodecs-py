"""Intel VPL (Video Processing Library) H.264/H.265 エンコード/デコードのテスト"""

import os
import sys

import numpy as np
import pytest
from webcodecs import (
    EncodedVideoChunkType,
    HardwareAccelerationEngine,
    LatencyMode,
    VideoDecoder,
    VideoDecoderConfig,
    VideoEncoder,
    VideoEncoderConfig,
    VideoFrame,
    VideoFrameBufferInit,
    VideoPixelFormat,
    get_video_codec_capabilities,
)


def is_intel_vpl_available() -> bool:
    """Intel VPL が利用可能かどうかを確認"""
    # Linux 以外では利用不可
    if sys.platform != "linux":
        return False
    # 環境変数 INTEL_VPL が設定されていない場合はスキップ
    # (Intel VPL ハードウェアがある環境でのみテストを実行)
    if os.environ.get("INTEL_VPL") != "true":
        return False
    # get_video_codec_capabilities で確認（libvpl が dlopen できるかどうか）
    caps = get_video_codec_capabilities()
    return HardwareAccelerationEngine.INTEL_VPL in caps


# Intel VPL が利用できない場合はテストをスキップ
pytestmark = pytest.mark.skipif(
    not is_intel_vpl_available(),
    reason="Intel VPL is not available",
)


def create_frame(w: int, h: int, ts: int, y: int = 80) -> VideoFrame:
    """テスト用の VideoFrame を作成"""
    y_size = w * h
    uv_size = (w // 2) * (h // 2)
    y_data = np.full(y_size, y, dtype=np.uint8)
    u_data = np.full(uv_size, 128, dtype=np.uint8)
    v_data = np.full(uv_size, 128, dtype=np.uint8)

    data = np.concatenate([y_data.flatten(), u_data.flatten(), v_data.flatten()])
    init: VideoFrameBufferInit = {
        "format": VideoPixelFormat.I420,
        "coded_width": w,
        "coded_height": h,
        "timestamp": ts,
    }
    frame = VideoFrame(data, init)
    return frame


def test_intel_vpl_h264_encode():
    """Intel VPL H.264 エンコードのテスト"""
    outputs = []

    def on_output(chunk):
        outputs.append(chunk)

    def on_error(error):
        pytest.fail(f"エンコーダエラー: {error}")

    enc = VideoEncoder(on_output, on_error)

    config: VideoEncoderConfig = {
        "codec": "avc1.42001e",
        "width": 320,
        "height": 240,
        "bitrate": 500_000,
        "latency_mode": LatencyMode.REALTIME,
        "hardware_acceleration_engine": HardwareAccelerationEngine.INTEL_VPL,
    }
    enc.configure(config)

    frames = []
    for i in range(5):
        frame = create_frame(320, 240, i * 33333, y=80 + i * 10)
        frames.append(frame)
        enc.encode(frame, {"key_frame": i == 0})

    enc.flush()

    assert len(outputs) >= 1
    # 最初のフレームはキーフレーム
    assert outputs[0].type == EncodedVideoChunkType.KEY

    for frame in frames:
        frame.close()
    enc.close()


def test_intel_vpl_hevc_encode():
    """Intel VPL HEVC (H.265) エンコードのテスト"""
    outputs = []

    def on_output(chunk):
        outputs.append(chunk)

    def on_error(error):
        pytest.fail(f"エンコーダエラー: {error}")

    enc = VideoEncoder(on_output, on_error)

    config: VideoEncoderConfig = {
        "codec": "hvc1.1.6.L93.B0",
        "width": 320,
        "height": 240,
        "bitrate": 500_000,
        "latency_mode": LatencyMode.REALTIME,
        "hardware_acceleration_engine": HardwareAccelerationEngine.INTEL_VPL,
    }
    enc.configure(config)

    frames = []
    for i in range(5):
        frame = create_frame(320, 240, i * 33333, y=80 + i * 10)
        frames.append(frame)
        enc.encode(frame, {"key_frame": i == 0})

    enc.flush()

    assert len(outputs) >= 1
    # 最初のフレームはキーフレーム
    assert outputs[0].type == EncodedVideoChunkType.KEY

    for frame in frames:
        frame.close()
    enc.close()


def test_intel_vpl_h264_encode_decode_roundtrip():
    """Intel VPL H.264 エンコード/デコード往復のテスト"""
    encoded_chunks = []
    decoded_frames = []

    def on_encode_output(chunk):
        encoded_chunks.append(chunk)

    def on_encode_error(error):
        pytest.fail(f"エンコーダエラー: {error}")

    def on_decode_output(frame):
        decoded_frames.append(frame)

    def on_decode_error(error):
        pytest.fail(f"デコーダエラー: {error}")

    # エンコーダを設定
    enc = VideoEncoder(on_encode_output, on_encode_error)
    enc_config: VideoEncoderConfig = {
        "codec": "avc1.42001e",
        "width": 320,
        "height": 240,
        "bitrate": 500_000,
        "latency_mode": LatencyMode.REALTIME,
        "hardware_acceleration_engine": HardwareAccelerationEngine.INTEL_VPL,
    }
    enc.configure(enc_config)

    # フレームをエンコード
    frames = []
    for i in range(3):
        frame = create_frame(320, 240, i * 33333, y=80 + i * 20)
        frames.append(frame)
        enc.encode(frame, {"key_frame": i == 0})

    enc.flush()

    assert len(encoded_chunks) >= 1

    # デコーダを設定
    dec = VideoDecoder(on_decode_output, on_decode_error)
    dec_config: VideoDecoderConfig = {
        "codec": "avc1.42001e",
        "coded_width": 320,
        "coded_height": 240,
        "hardware_acceleration_engine": HardwareAccelerationEngine.INTEL_VPL,
    }
    dec.configure(dec_config)

    # エンコードされたチャンクをデコード
    for chunk in encoded_chunks:
        dec.decode(chunk)

    dec.flush()

    assert len(decoded_frames) >= 1
    # デコードされたフレームの解像度を確認
    for frame in decoded_frames:
        assert frame.coded_width == 320
        assert frame.coded_height == 240

    # クリーンアップ
    for frame in frames:
        frame.close()
    for frame in decoded_frames:
        frame.close()
    enc.close()
    dec.close()


def test_intel_vpl_encoder_is_config_supported():
    """Intel VPL エンコーダの is_config_supported のテスト"""
    # H.264 設定
    h264_config: VideoEncoderConfig = {
        "codec": "avc1.42001e",
        "width": 1920,
        "height": 1080,
        "bitrate": 5_000_000,
        "hardware_acceleration_engine": HardwareAccelerationEngine.INTEL_VPL,
    }
    h264_result = VideoEncoder.is_config_supported(h264_config)
    assert h264_result["supported"] is True

    # HEVC 設定
    hevc_config: VideoEncoderConfig = {
        "codec": "hvc1.1.6.L93.B0",
        "width": 1920,
        "height": 1080,
        "bitrate": 5_000_000,
        "hardware_acceleration_engine": HardwareAccelerationEngine.INTEL_VPL,
    }
    hevc_result = VideoEncoder.is_config_supported(hevc_config)
    assert hevc_result["supported"] is True

    # AV1 設定
    av1_config: VideoEncoderConfig = {
        "codec": "av01.0.04M.08",
        "width": 1920,
        "height": 1080,
        "bitrate": 5_000_000,
        "hardware_acceleration_engine": HardwareAccelerationEngine.INTEL_VPL,
    }
    av1_result = VideoEncoder.is_config_supported(av1_config)
    assert av1_result["supported"] is True


def test_intel_vpl_decoder_is_config_supported():
    """Intel VPL デコーダの is_config_supported のテスト"""
    # H.264 設定
    h264_config: VideoDecoderConfig = {
        "codec": "avc1.42001e",
        "coded_width": 1920,
        "coded_height": 1080,
        "hardware_acceleration_engine": HardwareAccelerationEngine.INTEL_VPL,
    }
    h264_result = VideoDecoder.is_config_supported(h264_config)
    assert h264_result["supported"] is True

    # HEVC 設定
    hevc_config: VideoDecoderConfig = {
        "codec": "hvc1.1.6.L93.B0",
        "coded_width": 1920,
        "coded_height": 1080,
        "hardware_acceleration_engine": HardwareAccelerationEngine.INTEL_VPL,
    }
    hevc_result = VideoDecoder.is_config_supported(hevc_config)
    assert hevc_result["supported"] is True

    # AV1 設定
    av1_config: VideoDecoderConfig = {
        "codec": "av01.0.04M.08",
        "coded_width": 1920,
        "coded_height": 1080,
        "hardware_acceleration_engine": HardwareAccelerationEngine.INTEL_VPL,
    }
    av1_result = VideoDecoder.is_config_supported(av1_config)
    assert av1_result["supported"] is True


def test_intel_vpl_av1_encode_decode_roundtrip():
    """Intel VPL AV1 エンコード/デコードのラウンドトリップテスト"""
    encoded_chunks = []
    decoded_frames = []

    def on_encode_output(chunk):
        encoded_chunks.append(chunk)

    def on_encode_error(error):
        raise RuntimeError(f"Encoder error: {error}")

    def on_decode_output(frame):
        decoded_frames.append(frame)

    def on_decode_error(error):
        raise RuntimeError(f"Decoder error: {error}")

    # エンコーダを作成
    enc = VideoEncoder(on_encode_output, on_encode_error)
    enc_config: VideoEncoderConfig = {
        "codec": "av01.0.04M.08",
        "width": 320,
        "height": 240,
        "bitrate": 500_000,
        "latency_mode": LatencyMode.REALTIME,
        "hardware_acceleration_engine": HardwareAccelerationEngine.INTEL_VPL,
    }
    enc.configure(enc_config)

    # フレームをエンコード
    frames = []
    for i in range(10):
        frame = create_frame(320, 240, i * 33333, y=80 + i * 10)
        frames.append(frame)
        enc.encode(frame, {"key_frame": i == 0})

    enc.flush()

    # エンコード結果を確認
    assert len(encoded_chunks) >= 1, f"Expected at least 1 encoded chunk, got {len(encoded_chunks)}"

    # デコーダを作成
    dec = VideoDecoder(on_decode_output, on_decode_error)
    dec_config: VideoDecoderConfig = {
        "codec": "av01.0.04M.08",
        "coded_width": 320,
        "coded_height": 240,
        "hardware_acceleration_engine": HardwareAccelerationEngine.INTEL_VPL,
    }
    dec.configure(dec_config)

    # デコード
    for chunk in encoded_chunks:
        dec.decode(chunk)

    dec.flush()

    # デコード結果を確認
    assert len(decoded_frames) >= 1, f"Expected at least 1 decoded frame, got {len(decoded_frames)}"

    # クリーンアップ
    for frame in frames:
        frame.close()
    for frame in decoded_frames:
        frame.close()
    enc.close()
    dec.close()
