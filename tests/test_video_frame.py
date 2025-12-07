"""VideoFrame のテスト

VideoFrame の作成、プロパティ、メモリ操作のテスト
"""

import numpy as np
import pytest

from webcodecs import PlaneLayout, VideoFrame, VideoFrameBufferInit, VideoPixelFormat


def test_video_frame_i420():
    """I420 フォーマットの VideoFrame 作成"""
    width, height = 640, 480

    # I420 用のデータを準備（Y + U + V）
    y_size = width * height
    uv_size = width * height // 4
    data_size = y_size + uv_size * 2
    data = np.zeros(data_size, dtype=np.uint8)

    # Y プレーンを 128 で埋める
    data[:y_size] = 128
    # U, V プレーンを 128 で埋める（グレー）
    data[y_size:] = 128

    # VideoFrameBufferInit で初期化パラメータを作成
    init: VideoFrameBufferInit = {
        "format": VideoPixelFormat.I420,
        "coded_width": width,
        "coded_height": height,
        "timestamp": 1000000,  # microseconds
        "duration": 33333,  # microseconds
    }

    # VideoFrame を作成
    frame = VideoFrame(data, init)

    # プロパティの確認
    assert frame.format == VideoPixelFormat.I420
    assert frame.coded_width == width
    assert frame.coded_height == height
    assert frame.timestamp == 1000000
    assert frame.duration == 33333

    frame.close()


def test_video_frame_layout():
    """PlaneLayout を指定した VideoFrame の作成"""
    width, height = 320, 240

    # カスタムレイアウトでデータを準備
    y_stride = 384  # width より大きい stride
    uv_stride = 192
    y_size = y_stride * height
    uv_size = uv_stride * (height // 2)
    data_size = y_size + uv_size * 2
    data = np.zeros(data_size, dtype=np.uint8)

    # PlaneLayout を定義
    layout = [
        PlaneLayout(offset=0, stride=y_stride),  # Y plane
        PlaneLayout(offset=y_size, stride=uv_stride),  # U plane
        PlaneLayout(offset=y_size + uv_size, stride=uv_stride),  # V plane
    ]

    init: VideoFrameBufferInit = {
        "format": VideoPixelFormat.I420,
        "coded_width": width,
        "coded_height": height,
        "timestamp": 0,
        "layout": layout,
    }

    frame = VideoFrame(data, init)

    assert frame.format == VideoPixelFormat.I420
    assert frame.coded_width == width
    assert frame.coded_height == height

    # copy_to() でレイアウト情報を取得できることを確認 (WebCodecs API 準拠)
    destination = np.zeros(frame.allocation_size(), dtype=np.uint8)
    output_layout = frame.copy_to(destination)
    assert len(output_layout) == 3
    # copy_to() は連続したメモリにコピーするため、出力レイアウトは入力と異なる
    assert output_layout[0].offset == 0
    assert output_layout[0].stride == width  # 出力は width ベースの stride

    frame.close()


def test_video_frame_visible_rect():
    """visible_rect を指定した VideoFrame の作成"""
    width, height = 1920, 1080

    # フル HD のバッファを準備
    data_size = width * height * 3 // 2  # I420
    data = np.zeros(data_size, dtype=np.uint8)

    # 中央の 1280x720 の領域だけを表示
    init: VideoFrameBufferInit = {
        "format": VideoPixelFormat.I420,
        "coded_width": width,
        "coded_height": height,
        "timestamp": 0,
        "visible_rect": {
            "x": 320,
            "y": 180,
            "width": 1280,
            "height": 720,
        },
        "display_width": 1280,
        "display_height": 720,
    }

    frame = VideoFrame(data, init)

    assert frame.coded_width == 1920
    assert frame.coded_height == 1080
    assert frame.visible_rect is not None
    assert frame.visible_rect.x == 320
    assert frame.visible_rect.y == 180
    assert frame.visible_rect.width == 1280
    assert frame.visible_rect.height == 720
    assert frame.display_width == 1280
    assert frame.display_height == 720

    frame.close()


def test_video_frame_color_space():
    """color_space を指定した VideoFrame の作成"""
    width, height = 640, 480
    data_size = width * height * 3 // 2
    data = np.zeros(data_size, dtype=np.uint8)

    init: VideoFrameBufferInit = {
        "format": VideoPixelFormat.I420,
        "coded_width": width,
        "coded_height": height,
        "timestamp": 0,
        "color_space": {
            "primaries": "bt709",
            "transfer": "bt709",
            "matrix": "bt709",
            "full_range": False,
        },
    }

    frame = VideoFrame(data, init)
    assert frame.color_space is not None
    assert frame.color_space.primaries == "bt709"
    assert frame.color_space.transfer == "bt709"
    assert frame.color_space.matrix == "bt709"
    assert frame.color_space.full_range is False

    frame.close()


def test_video_frame_rgba():
    """RGBA フォーマットの VideoFrame 作成"""
    width, height = 320, 240

    # RGBA データを準備
    data = np.zeros((height, width, 4), dtype=np.uint8)
    data[:, :, 0] = 255  # Red channel
    data[:, :, 3] = 255  # Alpha channel

    init: VideoFrameBufferInit = {
        "format": VideoPixelFormat.RGBA,
        "coded_width": width,
        "coded_height": height,
        "timestamp": 42000,
    }

    frame = VideoFrame(data, init)

    assert frame.format == VideoPixelFormat.RGBA
    assert frame.coded_width == width
    assert frame.coded_height == height
    assert frame.timestamp == 42000

    frame.close()


def test_video_frame_nv12():
    """NV12 フォーマットの VideoFrame 作成"""
    width, height = 640, 480

    # NV12 用のデータを準備（Y + UV インターリーブ）
    y_size = width * height
    uv_size = width * height // 2
    data_size = y_size + uv_size
    data = np.zeros(data_size, dtype=np.uint8)

    init: VideoFrameBufferInit = {
        "format": VideoPixelFormat.NV12,
        "coded_width": width,
        "coded_height": height,
        "timestamp": 0,
    }

    frame = VideoFrame(data, init)

    assert frame.format == VideoPixelFormat.NV12
    assert frame.coded_width == width
    assert frame.coded_height == height

    frame.close()


def test_video_frame_rotation_flip():
    """rotation と flip を指定した VideoFrame の作成"""
    width, height = 640, 480
    data_size = width * height * 3 // 2
    data = np.zeros(data_size, dtype=np.uint8)

    init: VideoFrameBufferInit = {
        "format": VideoPixelFormat.I420,
        "coded_width": width,
        "coded_height": height,
        "timestamp": 0,
        "rotation": 90,  # 90度回転
        "flip": True,  # 水平反転
    }

    frame = VideoFrame(data, init)

    assert frame.rotation == 90
    assert frame.flip is True

    # 回転後の display dimensions
    # 90度または270度回転時は width と height が入れ替わる
    assert frame.display_width == height
    assert frame.display_height == width

    frame.close()


def test_video_frame_metadata():
    """metadata を指定した VideoFrame の作成と TypedDict の型チェック"""
    from webcodecs import VideoFrameMetadata

    width, height = 640, 480
    data_size = width * height * 3 // 2
    data = np.zeros(data_size, dtype=np.uint8)

    metadata: VideoFrameMetadata = {
        "capture_time": 1234567890.0,
        "receive_time": 1234567891.0,
        "rtp_timestamp": 12345,
    }

    init: VideoFrameBufferInit = {
        "format": VideoPixelFormat.I420,
        "coded_width": width,
        "coded_height": height,
        "timestamp": 0,
        "metadata": metadata,
    }

    frame = VideoFrame(data, init)

    result = frame.metadata()
    assert result["capture_time"] == 1234567890.0
    assert result["receive_time"] == 1234567891.0
    assert result["rtp_timestamp"] == 12345

    frame.close()


def test_video_frame_clone_with_metadata():
    """clone() で metadata がコピーされることを確認"""
    width, height = 640, 480
    data = np.zeros(width * height * 3 // 2, dtype=np.uint8)

    from webcodecs import VideoFrameMetadata

    metadata: VideoFrameMetadata = {
        "capture_time": 1234567890.0,
        "rtp_timestamp": 12345,
    }

    init: VideoFrameBufferInit = {
        "format": VideoPixelFormat.I420,
        "coded_width": width,
        "coded_height": height,
        "timestamp": 0,
        "metadata": metadata,
    }

    frame = VideoFrame(data, init)
    cloned = frame.clone()

    # metadata がコピーされているか確認
    cloned_metadata = cloned.metadata()
    assert cloned_metadata["capture_time"] == 1234567890.0
    assert cloned_metadata["rtp_timestamp"] == 12345

    frame.close()
    cloned.close()


def test_video_frame_copy_to_with_metadata():
    """copy_to() でフォーマット変換しても metadata が保持されることを確認"""
    width, height = 640, 480
    data = np.zeros(width * height * 3 // 2, dtype=np.uint8)

    from webcodecs import VideoFrameMetadata

    metadata: VideoFrameMetadata = {
        "capture_time": 1234567890.0,
    }

    init: VideoFrameBufferInit = {
        "format": VideoPixelFormat.I420,
        "coded_width": width,
        "coded_height": height,
        "timestamp": 0,
        "metadata": metadata,
    }

    frame = VideoFrame(data, init)

    # RGBA に変換
    rgba_size = frame.allocation_size({"format": VideoPixelFormat.RGBA})
    rgba_buffer = np.zeros(rgba_size, dtype=np.uint8)
    frame.copy_to(rgba_buffer, {"format": VideoPixelFormat.RGBA})

    # 元のフレームの metadata は保持されている
    result = frame.metadata()
    assert result["capture_time"] == 1234567890.0

    frame.close()


def test_video_frame_buffer_init_validation():
    """VideoFrameBufferInit のバリデーションテスト"""
    # テスト用のダミーデータ
    data = np.zeros(640 * 480 * 3 // 2, dtype=np.uint8)

    # 必須パラメータが不足している場合
    with pytest.raises(ValueError, match="format is required"):
        VideoFrame(data, {"coded_width": 640, "coded_height": 480, "timestamp": 0})

    with pytest.raises(ValueError, match="coded_width is required"):
        VideoFrame(data, {"format": VideoPixelFormat.I420, "coded_height": 480, "timestamp": 0})

    with pytest.raises(ValueError, match="coded_height is required"):
        VideoFrame(data, {"format": VideoPixelFormat.I420, "coded_width": 640, "timestamp": 0})

    with pytest.raises(ValueError, match="timestamp is required"):
        VideoFrame(data, {"format": VideoPixelFormat.I420, "coded_width": 640, "coded_height": 480})


def test_video_frame_planes():
    """planes() でプレーンデータを取得"""
    width, height = 320, 240

    # 元データを作成
    y_size = width * height
    uv_size = width * height // 4
    data_size = y_size + uv_size * 2
    original_data = np.arange(data_size, dtype=np.uint8)

    init: VideoFrameBufferInit = {
        "format": VideoPixelFormat.I420,
        "coded_width": width,
        "coded_height": height,
        "timestamp": 0,
    }

    # VideoFrame を作成（初期化時にデータはコピーされる）
    frame = VideoFrame(original_data, init)

    # planes() でプレーンデータを取得
    y_plane, u_plane, v_plane = frame.planes()

    # 初期値を確認
    assert y_plane[0, 0] == 0
    assert y_plane[0, 1] == 1

    # 元データは変更されていない（初期化時にコピーされたため）
    assert original_data[0] == 0

    frame.close()


def test_video_frame_copy_to():
    """copy_to() でコピーを取得 (WebCodecs API 準拠)"""
    from webcodecs import PlaneLayout

    width, height = 320, 240

    # 特定の値でデータを作成
    y_size = width * height
    uv_size = width * height // 4
    data = np.zeros(y_size + uv_size * 2, dtype=np.uint8)
    data[:y_size] = 100  # Y プレーン
    data[y_size : y_size + uv_size] = 110  # U プレーン
    data[y_size + uv_size :] = 120  # V プレーン

    init: VideoFrameBufferInit = {
        "format": VideoPixelFormat.I420,
        "coded_width": width,
        "coded_height": height,
        "timestamp": 0,
    }

    frame = VideoFrame(data, init)

    # destination バッファを確保
    destination = np.zeros(y_size + uv_size * 2, dtype=np.uint8)

    # copy_to() でコピー（PlaneLayout のリストを返す）
    layouts = frame.copy_to(destination)

    # PlaneLayout のリストが返されることを確認
    assert len(layouts) == 3
    assert all(isinstance(layout, PlaneLayout) for layout in layouts)

    # コピーされたデータの値を確認
    assert np.all(destination[:y_size] == 100)  # Y プレーン
    assert np.all(destination[y_size : y_size + uv_size] == 110)  # U プレーン
    assert np.all(destination[y_size + uv_size :] == 120)  # V プレーン

    # destination に書き込んでも元のフレームは影響を受けない
    destination[:y_size] = 200

    # planes() でプレーンデータを取得して確認
    y_plane, u_plane, v_plane = frame.planes()
    assert np.all(y_plane == 100)  # 元の値のまま
    assert np.all(u_plane == 110)
    assert np.all(v_plane == 120)

    frame.close()


def test_video_frame_clone():
    """clone() でフレームを複製"""
    width, height = 320, 240

    # 元データを作成
    y_size = width * height
    uv_size = width * height // 4
    data = np.zeros(y_size + uv_size * 2, dtype=np.uint8)
    data[:y_size] = 100  # Y プレーン
    data[y_size : y_size + uv_size] = 110  # U プレーン
    data[y_size + uv_size :] = 120  # V プレーン

    init: VideoFrameBufferInit = {
        "format": VideoPixelFormat.I420,
        "coded_width": width,
        "coded_height": height,
        "timestamp": 1234567,
        "duration": 33333,
    }

    original = VideoFrame(data, init)

    # clone() で複製
    cloned = original.clone()

    # プロパティが複製されていることを確認
    assert cloned.format == original.format
    assert cloned.coded_width == original.coded_width
    assert cloned.coded_height == original.coded_height
    assert cloned.timestamp == original.timestamp
    assert cloned.duration == original.duration

    # データが複製されていることを確認
    y_orig, u_orig, v_orig = original.planes()
    y_clone, u_clone, v_clone = cloned.planes()

    assert np.all(y_clone == 100)
    assert np.all(u_clone == 110)
    assert np.all(v_clone == 120)

    # 複製したフレームを変更しても元のフレームは影響を受けない
    y_clone[:] = 200

    y_orig_after, _, _ = original.planes()
    assert np.all(y_orig_after == 100)  # 元の値のまま

    original.close()
    cloned.close()


def test_video_frame_is_closed():
    """is_closed プロパティのテスト"""
    width, height = 320, 240
    data_size = width * height * 3 // 2
    data = np.zeros(data_size, dtype=np.uint8)

    init: VideoFrameBufferInit = {
        "format": VideoPixelFormat.I420,
        "coded_width": width,
        "coded_height": height,
        "timestamp": 0,
    }

    frame = VideoFrame(data, init)

    # 作成直後は閉じていない
    assert frame.is_closed is False

    # close() 後は閉じている
    frame.close()
    assert frame.is_closed is True


def test_video_frame_plane():
    """plane() で各プレーンを個別に取得"""
    width, height = 320, 240

    # 各プレーンに異なる値を設定
    y_size = width * height
    uv_size = width * height // 4
    data = np.zeros(y_size + uv_size * 2, dtype=np.uint8)
    data[:y_size] = 100  # Y プレーン
    data[y_size : y_size + uv_size] = 110  # U プレーン
    data[y_size + uv_size :] = 120  # V プレーン

    init: VideoFrameBufferInit = {
        "format": VideoPixelFormat.I420,
        "coded_width": width,
        "coded_height": height,
        "timestamp": 0,
    }

    frame = VideoFrame(data, init)

    # 各プレーンを個別に取得
    y_plane = frame.plane(0)
    u_plane = frame.plane(1)
    v_plane = frame.plane(2)

    # サイズと値を確認
    assert y_plane.shape == (height, width)
    assert u_plane.shape == (height // 2, width // 2)
    assert v_plane.shape == (height // 2, width // 2)

    assert np.all(y_plane == 100)
    assert np.all(u_plane == 110)
    assert np.all(v_plane == 120)

    frame.close()


def test_video_frame_copy_to_with_format_conversion():
    """copy_to() で format オプションを使ったフォーマット変換"""
    width, height = 320, 240

    # I420 フレームを作成
    y_size = width * height
    uv_size = width * height // 4
    data = np.zeros(y_size + uv_size * 2, dtype=np.uint8)
    data[:y_size] = 128  # Y プレーン (グレー)
    data[y_size:] = 128  # U, V プレーン

    init: VideoFrameBufferInit = {
        "format": VideoPixelFormat.I420,
        "coded_width": width,
        "coded_height": height,
        "timestamp": 0,
    }

    i420_frame = VideoFrame(data, init)

    # I420 から NV12 に変換して copy_to
    nv12_size = i420_frame.allocation_size({"format": VideoPixelFormat.NV12})
    nv12_buffer = np.zeros(nv12_size, dtype=np.uint8)
    layouts = i420_frame.copy_to(nv12_buffer, {"format": VideoPixelFormat.NV12})

    # NV12 は 2 プレーン（Y と UV）
    assert len(layouts) == 2

    i420_frame.close()


def test_video_frame_copy_to_rgb():
    """copy_to() で format オプションを使って RGB 形式に変換"""
    width, height = 320, 240

    # I420 フレームを作成
    y_size = width * height
    uv_size = width * height // 4
    data = np.zeros(y_size + uv_size * 2, dtype=np.uint8)
    data[:y_size] = 128  # Y プレーン
    data[y_size:] = 128  # U, V プレーン

    init: VideoFrameBufferInit = {
        "format": VideoPixelFormat.I420,
        "coded_width": width,
        "coded_height": height,
        "timestamp": 0,
    }

    frame = VideoFrame(data, init)

    # RGB に変換
    rgb_size = frame.allocation_size({"format": VideoPixelFormat.RGB})
    rgb_buffer = np.zeros(rgb_size, dtype=np.uint8)
    frame.copy_to(rgb_buffer, {"format": VideoPixelFormat.RGB})

    # RGB データのサイズを確認 (width * height * 3)
    expected_size = width * height * 3
    assert rgb_buffer.size == expected_size

    frame.close()


def test_video_frame_copy_to_rgba():
    """copy_to() で format オプションを使って RGBA 形式に変換"""
    width, height = 320, 240

    # I420 フレームを作成
    y_size = width * height
    uv_size = width * height // 4
    data = np.zeros(y_size + uv_size * 2, dtype=np.uint8)
    data[:y_size] = 128  # Y プレーン
    data[y_size:] = 128  # U, V プレーン

    init: VideoFrameBufferInit = {
        "format": VideoPixelFormat.I420,
        "coded_width": width,
        "coded_height": height,
        "timestamp": 0,
    }

    frame = VideoFrame(data, init)

    # RGBA に変換
    rgba_size = frame.allocation_size({"format": VideoPixelFormat.RGBA})
    rgba_buffer = np.zeros(rgba_size, dtype=np.uint8)
    frame.copy_to(rgba_buffer, {"format": VideoPixelFormat.RGBA})

    # RGBA データのサイズを確認 (width * height * 4)
    expected_size = width * height * 4
    assert rgba_buffer.size == expected_size

    frame.close()


def test_video_frame_allocation_size_i420():
    """allocation_size() で I420 フォーマットのバッファサイズを取得"""
    width, height = 640, 480

    # I420 の場合: width * height * 3 / 2
    expected_size = width * height * 3 // 2

    y_size = width * height
    uv_size = width * height // 4
    data = np.zeros(y_size + uv_size * 2, dtype=np.uint8)

    init: VideoFrameBufferInit = {
        "format": VideoPixelFormat.I420,
        "coded_width": width,
        "coded_height": height,
        "timestamp": 0,
    }

    frame = VideoFrame(data, init)

    # allocation_size() が正しいサイズを返すことを確認
    assert frame.allocation_size() == expected_size

    # copy_to() で使用できることを確認
    destination = np.zeros(frame.allocation_size(), dtype=np.uint8)
    layouts = frame.copy_to(destination)
    assert len(layouts) == 3  # I420 は 3 プレーン

    frame.close()


def test_video_frame_allocation_size_i422():
    """allocation_size() で I422 フォーマットのバッファサイズを取得"""
    width, height = 320, 240

    # I422 の場合: width * height * 2
    expected_size = width * height * 2

    data = np.zeros(expected_size, dtype=np.uint8)

    init: VideoFrameBufferInit = {
        "format": VideoPixelFormat.I422,
        "coded_width": width,
        "coded_height": height,
        "timestamp": 0,
    }

    frame = VideoFrame(data, init)

    # allocation_size() が正しいサイズを返すことを確認
    assert frame.allocation_size() == expected_size

    # copy_to() で使用できることを確認
    destination = np.zeros(frame.allocation_size(), dtype=np.uint8)
    layouts = frame.copy_to(destination)
    assert len(layouts) == 3  # I422 は 3 プレーン

    frame.close()


def test_video_frame_allocation_size_all_formats():
    """全フォーマットで allocation_size() が正しいサイズを返すことを確認"""
    width, height = 320, 240

    test_cases = [
        (VideoPixelFormat.I420, width * height * 3 // 2),
        (VideoPixelFormat.I422, width * height * 2),
        (VideoPixelFormat.I444, width * height * 3),
        (VideoPixelFormat.NV12, width * height * 3 // 2),
        (VideoPixelFormat.RGBA, width * height * 4),
        (VideoPixelFormat.BGRA, width * height * 4),
        (VideoPixelFormat.RGB, width * height * 3),
        (VideoPixelFormat.BGR, width * height * 3),
    ]

    for pixel_format, expected_size in test_cases:
        data = np.zeros(expected_size, dtype=np.uint8)

        init: VideoFrameBufferInit = {
            "format": pixel_format,
            "coded_width": width,
            "coded_height": height,
            "timestamp": 0,
        }

        frame = VideoFrame(data, init)
        assert frame.allocation_size() == expected_size, f"{pixel_format} のサイズが不正"
        frame.close()


def test_video_frame_allocation_size_with_coded_dimensions():
    """coded_width/height が width/height と異なる場合の allocation_size()"""
    # visible_rect で表示領域を制限した場合
    coded_width, coded_height = 1920, 1088  # 16 の倍数にアライメント
    visible_width, visible_height = 1920, 1080  # 実際の表示サイズ

    # バッファサイズは coded_width/height に基づく
    expected_size = coded_width * coded_height * 3 // 2  # I420

    data = np.zeros(expected_size, dtype=np.uint8)

    init: VideoFrameBufferInit = {
        "format": VideoPixelFormat.I420,
        "coded_width": coded_width,
        "coded_height": coded_height,
        "timestamp": 0,
        "visible_rect": {"x": 0, "y": 0, "width": visible_width, "height": visible_height},
    }

    frame = VideoFrame(data, init)

    # allocation_size() は coded_width/height を基準にする
    assert frame.allocation_size() == expected_size
    assert frame.coded_width == coded_width
    assert frame.coded_height == coded_height

    frame.close()


def test_video_frame_copy_to_with_rect():
    """copy_to() で rect オプションを使用してサブ領域をコピー"""
    width, height = 320, 240

    # 元データを作成
    y_size = width * height
    uv_size = width * height // 4
    data = np.zeros(y_size + uv_size * 2, dtype=np.uint8)

    # Y プレーンにパターンを設定
    y_data = data[:y_size].reshape((height, width))
    for row in range(height):
        y_data[row, :] = row % 256

    init: VideoFrameBufferInit = {
        "format": VideoPixelFormat.I420,
        "coded_width": width,
        "coded_height": height,
        "timestamp": 0,
    }

    frame = VideoFrame(data, init)

    # rect を指定してサブ領域をコピー
    rect_x, rect_y, rect_w, rect_h = 64, 48, 128, 96
    options = {"rect": {"x": rect_x, "y": rect_y, "width": rect_w, "height": rect_h}}

    # allocation_size で必要なサイズを取得
    required_size = frame.allocation_size(options)
    expected_size = rect_w * rect_h * 3 // 2  # I420
    assert required_size == expected_size

    destination = np.zeros(required_size, dtype=np.uint8)
    layouts = frame.copy_to(destination, options)

    assert len(layouts) == 3

    # Y プレーンの値を確認（行番号のパターンが保持されているか）
    y_out = destination[: rect_w * rect_h].reshape((rect_h, rect_w))
    for row in range(rect_h):
        expected_value = (rect_y + row) % 256
        assert y_out[row, 0] == expected_value

    frame.close()


def test_video_frame_copy_to_with_layout():
    """copy_to() で layout オプションを使用してカスタムレイアウトでコピー"""
    width, height = 320, 240

    y_size = width * height
    uv_size = width * height // 4
    data = np.zeros(y_size + uv_size * 2, dtype=np.uint8)
    data[:y_size] = 100  # Y
    data[y_size : y_size + uv_size] = 110  # U
    data[y_size + uv_size :] = 120  # V

    init: VideoFrameBufferInit = {
        "format": VideoPixelFormat.I420,
        "coded_width": width,
        "coded_height": height,
        "timestamp": 0,
    }

    frame = VideoFrame(data, init)

    # カスタムレイアウトを指定（stride を width より大きくする）
    y_stride = 384
    uv_stride = 192
    y_offset = 0
    u_offset = y_stride * height
    v_offset = u_offset + uv_stride * (height // 2)

    options = {
        "layout": [
            PlaneLayout(offset=y_offset, stride=y_stride),
            PlaneLayout(offset=u_offset, stride=uv_stride),
            PlaneLayout(offset=v_offset, stride=uv_stride),
        ]
    }

    required_size = frame.allocation_size(options)
    destination = np.zeros(required_size, dtype=np.uint8)
    layouts = frame.copy_to(destination, options)

    # 出力レイアウトが指定通りか確認
    assert layouts[0].offset == y_offset
    assert layouts[0].stride == y_stride
    assert layouts[1].offset == u_offset
    assert layouts[1].stride == uv_stride
    assert layouts[2].offset == v_offset
    assert layouts[2].stride == uv_stride

    # Y プレーンの値を確認
    assert destination[y_offset] == 100

    frame.close()


def test_video_frame_allocation_size_with_options():
    """allocation_size() で options を指定した場合のサイズ計算"""
    width, height = 640, 480

    data_size = width * height * 3 // 2
    data = np.zeros(data_size, dtype=np.uint8)

    init: VideoFrameBufferInit = {
        "format": VideoPixelFormat.I420,
        "coded_width": width,
        "coded_height": height,
        "timestamp": 0,
    }

    frame = VideoFrame(data, init)

    # options なしの場合
    assert frame.allocation_size() == data_size

    # rect を指定した場合
    rect_options = {"rect": {"x": 0, "y": 0, "width": 320, "height": 240}}
    expected_rect_size = 320 * 240 * 3 // 2
    assert frame.allocation_size(rect_options) == expected_rect_size

    frame.close()


def test_video_frame_copy_to_nv12():
    """NV12 フォーマットの copy_to() テスト"""
    width, height = 320, 240

    # NV12 用のデータを準備（Y + UV インターリーブ）
    y_size = width * height
    uv_size = width * height // 2
    data = np.zeros(y_size + uv_size, dtype=np.uint8)
    data[:y_size] = 100  # Y プレーン
    data[y_size:] = 128  # UV プレーン

    init: VideoFrameBufferInit = {
        "format": VideoPixelFormat.NV12,
        "coded_width": width,
        "coded_height": height,
        "timestamp": 0,
    }

    frame = VideoFrame(data, init)

    # allocation_size が正しいサイズを返すことを確認
    expected_size = y_size + uv_size
    assert frame.allocation_size() == expected_size

    # copy_to() でコピー
    destination = np.zeros(expected_size, dtype=np.uint8)
    layouts = frame.copy_to(destination)

    # NV12 は 2 プレーン
    assert len(layouts) == 2
    assert layouts[0].offset == 0
    assert layouts[0].stride == width
    assert layouts[1].offset == y_size
    assert layouts[1].stride == width

    # コピーされた値を確認
    assert np.all(destination[:y_size] == 100)
    assert np.all(destination[y_size:] == 128)

    frame.close()


def test_video_frame_copy_to_nv12_with_rect():
    """NV12 フォーマットの copy_to() で rect オプションを使用"""
    width, height = 320, 240

    # NV12 用のデータを準備
    y_size = width * height
    uv_size = width * height // 2
    data = np.zeros(y_size + uv_size, dtype=np.uint8)

    # Y プレーンにパターンを設定
    y_data = data[:y_size].reshape((height, width))
    for row in range(height):
        y_data[row, :] = row % 256

    init: VideoFrameBufferInit = {
        "format": VideoPixelFormat.NV12,
        "coded_width": width,
        "coded_height": height,
        "timestamp": 0,
    }

    frame = VideoFrame(data, init)

    # rect を指定してサブ領域をコピー
    rect_x, rect_y, rect_w, rect_h = 64, 48, 128, 96
    options = {"rect": {"x": rect_x, "y": rect_y, "width": rect_w, "height": rect_h}}

    required_size = frame.allocation_size(options)
    expected_size = rect_w * rect_h * 3 // 2  # NV12
    assert required_size == expected_size

    destination = np.zeros(required_size, dtype=np.uint8)
    layouts = frame.copy_to(destination, options)

    assert len(layouts) == 2

    # Y プレーンの値を確認（行番号のパターンが保持されているか）
    y_out = destination[: rect_w * rect_h].reshape((rect_h, rect_w))
    for row in range(rect_h):
        expected_value = (rect_y + row) % 256
        assert y_out[row, 0] == expected_value

    frame.close()


def test_video_frame_context_manager():
    """VideoFrame の context manager 対応テスト"""
    width, height = 320, 240
    data_size = width * height * 3 // 2
    data = np.zeros(data_size, dtype=np.uint8)

    init: VideoFrameBufferInit = {
        "format": VideoPixelFormat.I420,
        "coded_width": width,
        "coded_height": height,
        "timestamp": 0,
    }

    # with 文で VideoFrame を使用
    with VideoFrame(data, init) as frame:
        assert not frame.is_closed
        assert frame.coded_width == width
        assert frame.coded_height == height

    # with 文を抜けると自動的に close される
    assert frame.is_closed


def test_video_frame_context_manager_exception():
    """VideoFrame の context manager で例外が発生しても close される"""
    width, height = 320, 240
    data_size = width * height * 3 // 2
    data = np.zeros(data_size, dtype=np.uint8)

    init: VideoFrameBufferInit = {
        "format": VideoPixelFormat.I420,
        "coded_width": width,
        "coded_height": height,
        "timestamp": 0,
    }

    frame = None
    with pytest.raises(ValueError):
        with VideoFrame(data, init) as frame:
            assert not frame.is_closed
            raise ValueError("test exception")

    # 例外が発生しても close される
    assert frame is not None
    assert frame.is_closed


def test_video_frame_context_manager_returns_self():
    """VideoFrame の __enter__ は self を返す"""
    width, height = 320, 240
    data_size = width * height * 3 // 2
    data = np.zeros(data_size, dtype=np.uint8)

    init: VideoFrameBufferInit = {
        "format": VideoPixelFormat.I420,
        "coded_width": width,
        "coded_height": height,
        "timestamp": 0,
    }

    frame_outer = VideoFrame(data, init)
    with frame_outer as frame_inner:
        # __enter__ は self を返すので同じオブジェクト
        assert frame_outer is frame_inner

    frame_outer.close()
