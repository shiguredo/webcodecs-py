"""VP9 SVC (Scalable Video Coding) のテスト"""

import sys

import numpy as np
import pytest

from webcodecs import (
    LatencyMode,
    VideoEncoder,
    VideoEncoderConfig,
    VideoFrame,
    VideoFrameBufferInit,
    VideoPixelFormat,
)


def create_frame(w: int, h: int, ts: int, y: int = 80) -> VideoFrame:
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


@pytest.mark.skipif(sys.platform == "win32", reason="VP9 は Windows では未サポート")
def test_vp9_l1t2_encode():
    """VP9 L1T2 (2 temporal layers) エンコードテスト"""
    outputs = []

    def on_output(chunk, metadata=None):
        outputs.append(
            {
                "timestamp": chunk.timestamp,
                "type": chunk.type,
                "metadata": metadata,
            }
        )

    def on_error(error):
        pytest.fail(f"エンコーダエラー: {error}")

    enc = VideoEncoder(on_output, on_error)

    config: VideoEncoderConfig = {
        "codec": "vp09.00.10.08",
        "width": 160,
        "height": 120,
        "bitrate": 300_000,
        "latency_mode": LatencyMode.REALTIME,
        "scalability_mode": "L1T2",
    }
    enc.configure(config)

    frames = []
    for i in range(8):
        f = create_frame(160, 120, i * 33333, y=80 + i * 10)
        frames.append(f)
        enc.encode(f, {"key_frame": i == 0})

    enc.flush()

    assert len(outputs) >= 8

    for f in frames:
        f.close()
    enc.close()


@pytest.mark.skipif(sys.platform == "win32", reason="VP9 は Windows では未サポート")
def test_vp9_l1t2_svc_metadata():
    """VP9 L1T2 で SvcOutputMetadata が正しく出力されることを確認"""
    outputs = []

    def on_output(chunk, metadata=None):
        outputs.append(
            {
                "timestamp": chunk.timestamp,
                "metadata": metadata,
            }
        )

    def on_error(error):
        pytest.fail(f"エンコーダエラー: {error}")

    enc = VideoEncoder(on_output, on_error)

    config: VideoEncoderConfig = {
        "codec": "vp09.00.10.08",
        "width": 160,
        "height": 120,
        "bitrate": 300_000,
        "latency_mode": LatencyMode.REALTIME,
        "scalability_mode": "L1T2",
    }
    enc.configure(config)

    frames = []
    for i in range(4):
        f = create_frame(160, 120, i * 33333, y=80 + i * 10)
        frames.append(f)
        enc.encode(f, {"key_frame": i == 0})

    enc.flush()

    assert len(outputs) >= 4

    # 全フレームに svc メタデータが含まれることを確認
    for output in outputs:
        assert output["metadata"] is not None
        assert "svc" in output["metadata"]
        assert "temporal_layer_id" in output["metadata"]["svc"]

    for f in frames:
        f.close()
    enc.close()


@pytest.mark.skipif(sys.platform == "win32", reason="VP9 は Windows では未サポート")
def test_vp9_l1t2_temporal_layer_sequence():
    """VP9 L1T2 の temporal layer ID が正しいパターンで出力されることを確認

    L1T2 パターン: 0, 1, 0, 1, ...
    """
    outputs = []

    def on_output(chunk, metadata=None):
        outputs.append(
            {
                "timestamp": chunk.timestamp,
                "metadata": metadata,
            }
        )

    def on_error(error):
        pytest.fail(f"エンコーダエラー: {error}")

    enc = VideoEncoder(on_output, on_error)

    config: VideoEncoderConfig = {
        "codec": "vp09.00.10.08",
        "width": 160,
        "height": 120,
        "bitrate": 300_000,
        "latency_mode": LatencyMode.REALTIME,
        "scalability_mode": "L1T2",
    }
    enc.configure(config)

    frames = []
    for i in range(8):
        f = create_frame(160, 120, i * 33333, y=80 + i * 10)
        frames.append(f)
        enc.encode(f, {"key_frame": i == 0})

    enc.flush()

    assert len(outputs) >= 8

    # temporal layer ID のパターンを確認: 0, 1, 0, 1, ...
    expected_pattern = [0, 1, 0, 1, 0, 1, 0, 1]
    for i, output in enumerate(outputs[:8]):
        actual_tid = output["metadata"]["svc"]["temporal_layer_id"]
        expected_tid = expected_pattern[i]
        assert actual_tid == expected_tid, f"Frame {i}: expected {expected_tid}, got {actual_tid}"

    for f in frames:
        f.close()
    enc.close()


@pytest.mark.skipif(sys.platform == "win32", reason="VP9 は Windows では未サポート")
def test_vp9_l1t3_temporal_layer_sequence():
    """VP9 L1T3 の temporal layer ID が正しいパターンで出力されることを確認

    L1T3 パターン: 0, 2, 1, 2, 0, 2, 1, 2, ...
    """
    outputs = []

    def on_output(chunk, metadata=None):
        outputs.append(
            {
                "timestamp": chunk.timestamp,
                "metadata": metadata,
            }
        )

    def on_error(error):
        pytest.fail(f"エンコーダエラー: {error}")

    enc = VideoEncoder(on_output, on_error)

    config: VideoEncoderConfig = {
        "codec": "vp09.00.10.08",
        "width": 160,
        "height": 120,
        "bitrate": 300_000,
        "latency_mode": LatencyMode.REALTIME,
        "scalability_mode": "L1T3",
    }
    enc.configure(config)

    frames = []
    for i in range(8):
        f = create_frame(160, 120, i * 33333, y=80 + i * 10)
        frames.append(f)
        enc.encode(f, {"key_frame": i == 0})

    enc.flush()

    assert len(outputs) >= 8

    # temporal layer ID のパターンを確認: 0, 2, 1, 2, 0, 2, 1, 2, ...
    expected_pattern = [0, 2, 1, 2, 0, 2, 1, 2]
    for i, output in enumerate(outputs[:8]):
        actual_tid = output["metadata"]["svc"]["temporal_layer_id"]
        expected_tid = expected_pattern[i]
        assert actual_tid == expected_tid, f"Frame {i}: expected {expected_tid}, got {actual_tid}"

    for f in frames:
        f.close()
    enc.close()


@pytest.mark.skipif(sys.platform == "win32", reason="VP9 は Windows では未サポート")
def test_vp9_invalid_scalability_mode():
    """サポートされない scalabilityMode でエラーが発生することを確認"""

    def on_output(chunk, metadata=None):
        pass

    def on_error(error):
        pass

    enc = VideoEncoder(on_output, on_error)

    config: VideoEncoderConfig = {
        "codec": "vp09.00.10.08",
        "width": 160,
        "height": 120,
        "bitrate": 300_000,
        "latency_mode": LatencyMode.REALTIME,
        "scalability_mode": "INVALID",
    }

    with pytest.raises(RuntimeError, match="Unsupported scalability mode"):
        enc.configure(config)

    enc.close()


@pytest.mark.skipif(sys.platform == "win32", reason="VP9 は Windows では未サポート")
def test_vp9_spatial_svc_not_supported():
    """L2T1 など spatial SVC が拒否されることを確認"""

    def on_output(chunk, metadata=None):
        pass

    def on_error(error):
        pass

    enc = VideoEncoder(on_output, on_error)

    config: VideoEncoderConfig = {
        "codec": "vp09.00.10.08",
        "width": 160,
        "height": 120,
        "bitrate": 300_000,
        "latency_mode": LatencyMode.REALTIME,
        "scalability_mode": "L2T1",
    }

    with pytest.raises(RuntimeError, match="Spatial SVC is not supported"):
        enc.configure(config)

    enc.close()


@pytest.mark.skipif(sys.platform == "win32", reason="VP9 は Windows では未サポート")
def test_vp9_no_svc_metadata_without_scalability_mode():
    """scalabilityMode が指定されていない場合、svc メタデータが含まれないことを確認"""
    outputs = []

    def on_output(chunk, metadata=None):
        outputs.append(
            {
                "timestamp": chunk.timestamp,
                "metadata": metadata,
            }
        )

    def on_error(error):
        pytest.fail(f"エンコーダエラー: {error}")

    enc = VideoEncoder(on_output, on_error)

    config: VideoEncoderConfig = {
        "codec": "vp09.00.10.08",
        "width": 160,
        "height": 120,
        "bitrate": 300_000,
        "latency_mode": LatencyMode.REALTIME,
    }
    enc.configure(config)

    frames = []
    for i in range(4):
        f = create_frame(160, 120, i * 33333, y=80 + i * 10)
        frames.append(f)
        enc.encode(f, {"key_frame": i == 0})

    enc.flush()

    assert len(outputs) >= 4

    # svc メタデータが含まれないことを確認
    for output in outputs:
        if output["metadata"]:
            assert "svc" not in output["metadata"]

    for f in frames:
        f.close()
    enc.close()
