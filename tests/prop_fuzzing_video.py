"""Property-Based Testing によるファジングテスト (Video)

ランダムな ndarray を入力してクラッシュやセグフォルトが発生しないことを確認
"""

import numpy as np
from hypothesis import given, settings, HealthCheck
from hypothesis import strategies as st
from hypothesis.extra.numpy import arrays

from webcodecs import (
    VideoFrame,
    VideoFrameBufferInit,
    VideoPixelFormat,
)


def calculate_video_buffer_size(pixel_format: VideoPixelFormat, width: int, height: int) -> int:
    """ピクセルフォーマットに応じたバッファサイズを計算"""
    if pixel_format in (VideoPixelFormat.I420, VideoPixelFormat.NV12):
        return width * height * 3 // 2
    elif pixel_format == VideoPixelFormat.I422:
        return width * height * 2
    elif pixel_format == VideoPixelFormat.I444:
        return width * height * 3
    elif pixel_format in (VideoPixelFormat.RGBA, VideoPixelFormat.BGRA):
        return width * height * 4
    elif pixel_format in (VideoPixelFormat.RGB, VideoPixelFormat.BGR):
        return width * height * 3
    else:
        raise ValueError(f"未知のピクセルフォーマット: {pixel_format}")


# I420 と NV12 は UV サブサンプリングがあるため偶数のみ
EVEN_VIDEO_FORMATS = [
    VideoPixelFormat.I420,
    VideoPixelFormat.I422,
    VideoPixelFormat.I444,
    VideoPixelFormat.NV12,
]

# 任意のサイズが可能なフォーマット
ANY_SIZE_VIDEO_FORMATS = [
    VideoPixelFormat.RGBA,
    VideoPixelFormat.BGRA,
    VideoPixelFormat.RGB,
    VideoPixelFormat.BGR,
]

# planes() がサポートするフォーマット
PLANAR_VIDEO_FORMATS = [VideoPixelFormat.I420, VideoPixelFormat.I422, VideoPixelFormat.I444]


@st.composite
def video_frame_strategy(draw):
    """ランダムな VideoFrame 設定を生成するストラテジ"""
    # フォーマットを選択
    use_even_format = draw(st.booleans())

    if use_even_format:
        pixel_format = draw(st.sampled_from(EVEN_VIDEO_FORMATS))
        # 偶数サイズのみ (I420/NV12/I422 は UV サブサンプリングのため)
        width = draw(st.integers(min_value=2, max_value=128)) * 2
        height = draw(st.integers(min_value=2, max_value=128)) * 2
    else:
        pixel_format = draw(st.sampled_from(ANY_SIZE_VIDEO_FORMATS))
        width = draw(st.integers(min_value=1, max_value=256))
        height = draw(st.integers(min_value=1, max_value=256))

    # バッファサイズを計算
    buffer_size = calculate_video_buffer_size(pixel_format, width, height)

    # ランダムなピクセルデータを生成
    data = draw(
        arrays(
            dtype=np.uint8,
            shape=(buffer_size,),
            elements=st.integers(min_value=0, max_value=255),
        )
    )

    # ランダムな timestamp と duration
    timestamp = draw(st.integers(min_value=0, max_value=2**62))
    duration = draw(st.integers(min_value=0, max_value=1_000_000_000))

    return {
        "format": pixel_format,
        "width": width,
        "height": height,
        "data": data,
        "timestamp": timestamp,
        "duration": duration,
    }


@given(config=video_frame_strategy())
@settings(
    max_examples=100,
    deadline=None,
    suppress_health_check=[HealthCheck.too_slow, HealthCheck.data_too_large],
)
def prop_video_frame_random_pixels(config):
    """ランダムなピクセルデータで VideoFrame を作成してクラッシュしないことを確認"""
    init: VideoFrameBufferInit = {
        "format": config["format"],
        "coded_width": config["width"],
        "coded_height": config["height"],
        "timestamp": config["timestamp"],
        "duration": config["duration"],
    }

    # VideoFrame を作成
    frame = VideoFrame(config["data"], init)

    # 基本的なプロパティにアクセスできることを確認
    assert frame.format == config["format"]
    assert frame.coded_width == config["width"]
    assert frame.coded_height == config["height"]
    assert frame.timestamp == config["timestamp"]
    assert not frame.is_closed

    frame.close()
    assert frame.is_closed


@given(config=video_frame_strategy())
@settings(
    max_examples=50,
    deadline=None,
    suppress_health_check=[HealthCheck.too_slow, HealthCheck.data_too_large],
)
def prop_video_frame_operations_with_random_data(config):
    """ランダムな VideoFrame に対して各種操作を実行してクラッシュしないことを確認"""
    init: VideoFrameBufferInit = {
        "format": config["format"],
        "coded_width": config["width"],
        "coded_height": config["height"],
        "timestamp": config["timestamp"],
    }

    frame = VideoFrame(config["data"], init)

    # planes() を呼び出す (I420/I422/I444 のみ)
    if config["format"] in PLANAR_VIDEO_FORMATS:
        planes = frame.planes()
        assert len(planes) > 0
        for plane in planes:
            assert isinstance(plane, np.ndarray)

    # allocation_size() を呼び出す
    alloc_size = frame.allocation_size()
    assert alloc_size > 0

    # copy_to() を呼び出す
    destination = np.zeros(alloc_size, dtype=np.uint8)
    layouts = frame.copy_to(destination)
    assert len(layouts) > 0

    # clone() を呼び出す
    cloned = frame.clone()
    assert cloned.format == frame.format
    assert cloned.coded_width == frame.coded_width
    assert cloned.coded_height == frame.coded_height
    cloned.close()

    frame.close()


@given(
    width=st.integers(min_value=2, max_value=64).map(lambda x: x * 2),
    height=st.integers(min_value=2, max_value=64).map(lambda x: x * 2),
)
@settings(
    max_examples=50,
    deadline=None,
    suppress_health_check=[HealthCheck.too_slow],
)
def prop_video_frame_extreme_pixel_values(width, height):
    """極端なピクセル値 (0, 255) でクラッシュしないことを確認"""
    buffer_size = width * height * 3 // 2  # I420

    # 全て 0 のデータ
    data_zeros = np.zeros(buffer_size, dtype=np.uint8)
    init: VideoFrameBufferInit = {
        "format": VideoPixelFormat.I420,
        "coded_width": width,
        "coded_height": height,
        "timestamp": 0,
    }
    frame_zeros = VideoFrame(data_zeros, init)
    assert frame_zeros.coded_width == width
    frame_zeros.close()

    # 全て 255 のデータ
    data_max = np.full(buffer_size, 255, dtype=np.uint8)
    frame_max = VideoFrame(data_max, init)
    assert frame_max.coded_width == width
    frame_max.close()

    # ランダムパターン
    data_random = np.random.randint(0, 256, size=buffer_size, dtype=np.uint8)
    frame_random = VideoFrame(data_random, init)
    assert frame_random.coded_width == width
    frame_random.close()
