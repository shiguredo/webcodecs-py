"""native_buffer (CVPixelBufferRef) サポートのテスト."""

import ctypes
import os
import platform

import numpy as np
import pytest

from webcodecs import (
    VideoFrame,
    VideoPixelFormat,
)

# Apple Video Toolbox 環境でのみテストを実行
pytestmark = pytest.mark.skipif(
    os.environ.get("APPLE_VIDEO_TOOLBOX") is None,
    reason="Apple Video Toolbox でのみ実行する",
)


def create_cv_pixel_buffer_capsule(ptr_value: int = 0x12345678):
    """テスト用のダミー CVPixelBufferRef capsule を作成する."""
    pythonapi = ctypes.pythonapi
    pythonapi.PyCapsule_New.restype = ctypes.py_object
    pythonapi.PyCapsule_New.argtypes = [
        ctypes.c_void_p,
        ctypes.c_char_p,
        ctypes.c_void_p,
    ]
    dummy_ptr = ctypes.c_void_p(ptr_value)
    return pythonapi.PyCapsule_New(dummy_ptr, b"CVPixelBufferRef", None)


def test_video_frame_with_data_has_no_native_buffer():
    """data で作成した VideoFrame の native_buffer は None であることを確認する."""
    width, height = 640, 480

    y_size = width * height
    uv_size = width * height // 2
    data = np.zeros(y_size + uv_size, dtype=np.uint8)

    frame = VideoFrame(
        data,
        {
            "format": VideoPixelFormat.NV12,
            "coded_width": width,
            "coded_height": height,
            "timestamp": 0,
        },
    )

    assert frame.native_buffer is None


@pytest.mark.skipif(
    platform.system() != "Darwin",
    reason="macOS でのみ実行する",
)
def test_video_frame_with_native_buffer():
    """PyCapsule を直接渡して VideoFrame を作成できることを確認する."""
    width, height = 640, 480

    capsule = create_cv_pixel_buffer_capsule()

    frame = VideoFrame(
        capsule,
        {
            "format": VideoPixelFormat.NV12,
            "coded_width": width,
            "coded_height": height,
            "timestamp": 0,
        },
    )

    # native_buffer が設定されていることを確認
    assert frame.native_buffer is not None

    # capsule 名を確認
    pythonapi = ctypes.pythonapi
    pythonapi.PyCapsule_GetName.restype = ctypes.c_char_p
    pythonapi.PyCapsule_GetName.argtypes = [ctypes.py_object]
    name = pythonapi.PyCapsule_GetName(frame.native_buffer)
    assert name == b"CVPixelBufferRef"

    # プロパティが正しく設定されていることを確認
    assert frame.coded_width == width
    assert frame.coded_height == height
    assert frame.format == VideoPixelFormat.NV12
    assert frame.timestamp == 0


@pytest.mark.skipif(
    platform.system() != "Darwin",
    reason="macOS でのみ実行する",
)
def test_video_frame_native_buffer_plane_raises_error():
    """native_buffer のみの VideoFrame で plane() を呼ぶとエラーになることを確認する."""
    width, height = 640, 480

    capsule = create_cv_pixel_buffer_capsule()

    frame = VideoFrame(
        capsule,
        {
            "format": VideoPixelFormat.NV12,
            "coded_width": width,
            "coded_height": height,
            "timestamp": 0,
        },
    )

    with pytest.raises(RuntimeError, match="native_buffer"):
        frame.plane(0)


@pytest.mark.skipif(
    platform.system() != "Darwin",
    reason="macOS でのみ実行する",
)
def test_video_frame_native_buffer_copy_to_raises_error():
    """native_buffer のみの VideoFrame で copy_to() を呼ぶとエラーになることを確認する."""
    width, height = 640, 480

    capsule = create_cv_pixel_buffer_capsule()

    frame = VideoFrame(
        capsule,
        {
            "format": VideoPixelFormat.NV12,
            "coded_width": width,
            "coded_height": height,
            "timestamp": 0,
        },
    )

    y_size = width * height
    uv_size = width * height // 2
    destination = np.zeros(y_size + uv_size, dtype=np.uint8)

    with pytest.raises(RuntimeError, match="native_buffer"):
        frame.copy_to(destination)


@pytest.mark.skipif(
    platform.system() != "Darwin",
    reason="macOS でのみ実行する",
)
def test_video_frame_native_buffer_planes_raises_error():
    """native_buffer のみの VideoFrame で planes() を呼ぶとエラーになることを確認する."""
    width, height = 640, 480

    capsule = create_cv_pixel_buffer_capsule()

    frame = VideoFrame(
        capsule,
        {
            "format": VideoPixelFormat.I420,
            "coded_width": width,
            "coded_height": height,
            "timestamp": 0,
        },
    )

    with pytest.raises(RuntimeError, match="native_buffer"):
        frame.planes()


@pytest.mark.skipif(
    platform.system() != "Darwin",
    reason="macOS でのみ実行する",
)
def test_video_frame_native_buffer_clone_raises_error():
    """native_buffer のみの VideoFrame で clone() を呼ぶとエラーになることを確認する."""
    width, height = 640, 480

    capsule = create_cv_pixel_buffer_capsule()

    frame = VideoFrame(
        capsule,
        {
            "format": VideoPixelFormat.NV12,
            "coded_width": width,
            "coded_height": height,
            "timestamp": 0,
        },
    )

    with pytest.raises(RuntimeError, match="native_buffer"):
        frame.clone()
