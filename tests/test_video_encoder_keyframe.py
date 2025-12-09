"""VideoEncoderEncodeOptions のテスト"""

import numpy as np
import pytest
from webcodecs import (
    EncodedVideoChunkType,
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

    # YUV データを連結
    data = np.concatenate([y_data.flatten(), u_data.flatten(), v_data.flatten()])
    init: VideoFrameBufferInit = {
        "format": VideoPixelFormat.I420,
        "coded_width": w,
        "coded_height": h,
        "timestamp": ts,
    }
    frame = VideoFrame(data, init)
    return frame


def test_encode_with_options_key_frame_true():
    outputs = []

    def on_output(chunk):
        outputs.append((chunk.timestamp, chunk.type))

    def on_error(error):
        pytest.fail(f"エンコーダエラー: {error}")

    enc = VideoEncoder(on_output, on_error)

    config: VideoEncoderConfig = {
        "codec": "av01.0.04M.08",
        "width": 160,
        "height": 120,
        "bitrate": 300_000,
        "latency_mode": LatencyMode.REALTIME,  # realtime モードで g_lag_in_frames = 0
    }
    enc.configure(config)

    f0 = create_frame(160, 120, 0)
    enc.encode(f0, {"key_frame": True})

    enc.flush()

    assert len(outputs) >= 1
    # 最初のフレームはキーフレーム強制
    assert outputs[0][1] == EncodedVideoChunkType.KEY

    f0.close()
    enc.close()


def test_encode_with_options_key_frame_false_after_key_frame():
    outputs = []

    def on_output(chunk):
        outputs.append((chunk.timestamp, chunk.type))

    def on_error(error):
        pytest.fail(f"エンコーダエラー: {error}")

    enc = VideoEncoder(on_output, on_error)

    config: VideoEncoderConfig = {
        "codec": "av01.0.04M.08",
        "width": 160,
        "height": 120,
        "bitrate": 300_000,
        "latency_mode": LatencyMode.REALTIME,  # realtime モードで g_lag_in_frames = 0
    }
    enc.configure(config)

    # 先頭はキーフレームを強制
    f0 = create_frame(160, 120, 0)
    enc.encode(f0, {"key_frame": True})

    # 次はデルタフレーム意図
    f1 = create_frame(160, 120, 1000, y=90)
    enc.encode(f1, {})  # 既定 False

    enc.flush()

    # timestamp=0 が KEY、 timestamp=1000 が DELTA を期待
    kinds = {ts: typ for ts, typ in outputs}
    assert kinds.get(0) == EncodedVideoChunkType.KEY
    # 自動キーフレームが入らない想定で DELTA を期待
    assert kinds.get(1000) == EncodedVideoChunkType.DELTA

    f0.close()
    f1.close()
    enc.close()
