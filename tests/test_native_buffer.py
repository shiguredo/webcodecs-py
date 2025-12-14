"""native_buffer (CVPixelBufferRef) サポートのテスト."""

import ctypes
import os
import platform

import numpy as np
import pytest

from webcodecs import (
    VideoEncoder,
    VideoEncoderConfig,
    VideoFrame,
    VideoPixelFormat,
)

# Apple Video Toolbox 環境でのみテストを実行
pytestmark = pytest.mark.skipif(
    os.environ.get("APPLE_VIDEO_TOOLBOX") is None,
    reason="Apple Video Toolbox でのみ実行する",
)


def test_video_frame_with_native_buffer():
    """VideoFrame が native_buffer を保持できることを確認する."""
    width, height = 640, 480

    # NV12 データを作成
    y_size = width * height
    uv_size = width * height // 2
    data = np.zeros(y_size + uv_size, dtype=np.uint8)

    # VideoFrame を作成
    frame = VideoFrame(
        data,
        {
            "format": VideoPixelFormat.NV12,
            "coded_width": width,
            "coded_height": height,
            "timestamp": 0,
        },
    )

    # native_buffer プロパティが存在することを確認
    assert hasattr(frame, "native_buffer")
    # デフォルトは None
    assert frame.native_buffer is None


@pytest.mark.skipif(
    platform.system() != "Darwin",
    reason="macOS でのみ実行する",
)
def test_video_frame_set_native_buffer_with_capsule():
    """VideoFrame に PyCapsule (CVPixelBufferRef) を設定できることを確認する."""
    width, height = 640, 480

    # NV12 データを作成
    y_size = width * height
    uv_size = width * height // 2
    data = np.zeros(y_size + uv_size, dtype=np.uint8)

    # VideoFrame を作成
    frame = VideoFrame(
        data,
        {
            "format": VideoPixelFormat.NV12,
            "coded_width": width,
            "coded_height": height,
            "timestamp": 0,
        },
    )

    # ダミーの capsule を作成（実際の CVPixelBuffer は使えないのでテスト用）
    # 注意: これは単なる API テストで、実際のエンコードには使用しない
    pythonapi = ctypes.pythonapi
    pythonapi.PyCapsule_New.restype = ctypes.py_object
    pythonapi.PyCapsule_New.argtypes = [
        ctypes.c_void_p,
        ctypes.c_char_p,
        ctypes.c_void_p,
    ]

    # ダミーポインタで capsule を作成
    dummy_ptr = ctypes.c_void_p(0x12345678)
    capsule = pythonapi.PyCapsule_New(dummy_ptr, b"CVPixelBufferRef", None)

    # native_buffer を設定
    frame.native_buffer = capsule

    # 設定されたことを確認
    assert frame.native_buffer is not None

    # capsule 名を確認
    pythonapi.PyCapsule_GetName.restype = ctypes.c_char_p
    pythonapi.PyCapsule_GetName.argtypes = [ctypes.py_object]
    name = pythonapi.PyCapsule_GetName(frame.native_buffer)
    assert name == b"CVPixelBufferRef"
