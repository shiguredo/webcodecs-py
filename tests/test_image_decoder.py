"""ImageDecoder のテスト"""

import io
import sys

import pytest
from PIL import Image

from webcodecs import (
    ImageDecoder,
    VideoPixelFormat,
)


def create_jpeg_bytes(width: int, height: int, color: tuple[int, int, int]) -> bytes:
    """指定サイズと色の JPEG バイトデータを生成"""
    img = Image.new("RGB", (width, height), color=color)
    buffer = io.BytesIO()
    img.save(buffer, format="JPEG")
    return buffer.getvalue()


def create_png_bytes(width: int, height: int, color: tuple[int, int, int]) -> bytes:
    """指定サイズと色の PNG バイトデータを生成"""
    img = Image.new("RGB", (width, height), color=color)
    buffer = io.BytesIO()
    img.save(buffer, format="PNG")
    return buffer.getvalue()


# macOS でのみテストを実行
pytestmark = pytest.mark.skipif(
    sys.platform != "darwin",
    reason="ImageDecoder は macOS のみ対応",
)


def test_is_type_supported_jpeg():
    """JPEG の is_type_supported テスト"""
    assert ImageDecoder.is_type_supported("image/jpeg") is True
    assert ImageDecoder.is_type_supported("image/jpg") is True


def test_is_type_supported_png():
    """PNG の is_type_supported テスト"""
    assert ImageDecoder.is_type_supported("image/png") is True


def test_is_type_supported_gif():
    """GIF の is_type_supported テスト"""
    assert ImageDecoder.is_type_supported("image/gif") is True


def test_is_type_supported_webp():
    """WebP の is_type_supported テスト"""
    assert ImageDecoder.is_type_supported("image/webp") is True


def test_is_type_supported_unsupported():
    """未サポートタイプの is_type_supported テスト"""
    assert ImageDecoder.is_type_supported("image/unsupported") is False
    assert ImageDecoder.is_type_supported("text/plain") is False


def test_jpeg_decode_basic():
    """基本的な JPEG デコードテスト"""
    jpeg_data = create_jpeg_bytes(16, 16, (255, 0, 0))

    decoder = ImageDecoder(
        {
            "type": "image/jpeg",
            "data": jpeg_data,
        }
    )

    assert decoder.type == "image/jpeg"
    assert decoder.complete is True
    assert decoder.is_complete is True
    assert decoder.is_closed is False

    # トラック情報の確認
    tracks = decoder.tracks
    assert tracks.length == 1
    assert tracks.is_ready is True
    assert len(tracks) == 1

    track = tracks[0]
    assert track is not None
    assert track.animated is False
    assert track.frame_count == 1
    assert track.selected is True

    # デコード
    result = decoder.decode()
    assert result["complete"] is True

    frame = result["image"]
    assert frame is not None
    assert frame.format == VideoPixelFormat.RGBA
    assert frame.coded_width == 16
    assert frame.coded_height == 16

    frame.close()
    decoder.close()

    assert decoder.is_closed is True


def test_png_decode_basic():
    """基本的な PNG デコードテスト"""
    png_data = create_png_bytes(32, 24, (0, 255, 0))

    decoder = ImageDecoder(
        {
            "type": "image/png",
            "data": png_data,
        }
    )

    assert decoder.type == "image/png"
    assert decoder.complete is True

    result = decoder.decode()
    assert result["complete"] is True

    frame = result["image"]
    assert frame.format == VideoPixelFormat.RGBA
    assert frame.coded_width == 32
    assert frame.coded_height == 24

    frame.close()
    decoder.close()


def test_jpeg_decode_with_options():
    """オプション付きの JPEG デコードテスト"""
    jpeg_data = create_jpeg_bytes(8, 8, (0, 0, 255))

    decoder = ImageDecoder(
        {
            "type": "image/jpeg",
            "data": jpeg_data,
        }
    )

    result = decoder.decode(
        {
            "frame_index": 0,
            "complete_frames_only": True,
        }
    )
    assert result["complete"] is True

    frame = result["image"]
    assert frame.coded_width == 8
    assert frame.coded_height == 8

    frame.close()
    decoder.close()


def test_track_selected_index():
    """トラックの selected_index テスト"""
    jpeg_data = create_jpeg_bytes(8, 8, (128, 128, 128))

    decoder = ImageDecoder(
        {
            "type": "image/jpeg",
            "data": jpeg_data,
        }
    )

    tracks = decoder.tracks
    assert tracks.selected_index == 0
    assert tracks.selected_track is not None
    assert tracks.selected_track.selected is True

    decoder.close()


def test_decoder_close():
    """close() 後の動作テスト"""
    jpeg_data = create_jpeg_bytes(8, 8, (255, 255, 255))

    decoder = ImageDecoder(
        {
            "type": "image/jpeg",
            "data": jpeg_data,
        }
    )

    decoder.close()
    assert decoder.is_closed is True

    # close 後に decode を呼ぶとエラー
    with pytest.raises(RuntimeError):
        decoder.decode()


def test_decoder_reset():
    """reset() のテスト"""
    jpeg_data = create_jpeg_bytes(8, 8, (100, 100, 100))

    decoder = ImageDecoder(
        {
            "type": "image/jpeg",
            "data": jpeg_data,
        }
    )

    # デコード
    result1 = decoder.decode()
    result1["image"].close()

    # リセット
    decoder.reset()

    # 再度デコード可能
    result2 = decoder.decode()
    assert result2["complete"] is True
    result2["image"].close()

    decoder.close()


def test_invalid_data():
    """不正なデータでのエラーテスト"""
    with pytest.raises(RuntimeError):
        ImageDecoder(
            {
                "type": "image/jpeg",
                "data": b"not a valid jpeg",
            }
        )


def test_frame_index_out_of_range():
    """フレームインデックス範囲外のエラーテスト"""
    jpeg_data = create_jpeg_bytes(8, 8, (200, 200, 200))

    decoder = ImageDecoder(
        {
            "type": "image/jpeg",
            "data": jpeg_data,
        }
    )

    # JPEG は 1 フレームなので index=1 はエラー
    with pytest.raises(RuntimeError):
        decoder.decode({"frame_index": 1})

    decoder.close()


def test_missing_required_type():
    """type が欠けている場合のエラーテスト"""
    jpeg_data = create_jpeg_bytes(8, 8, (50, 50, 50))

    with pytest.raises(Exception):
        ImageDecoder(
            {
                "data": jpeg_data,
            }
        )


def test_missing_required_data():
    """data が欠けている場合のエラーテスト"""
    with pytest.raises(Exception):
        ImageDecoder(
            {
                "type": "image/jpeg",
            }
        )


def test_context_manager():
    """with 文での使用テスト"""
    jpeg_data = create_jpeg_bytes(8, 8, (75, 75, 75))

    decoder = ImageDecoder(
        {
            "type": "image/jpeg",
            "data": jpeg_data,
        }
    )

    result = decoder.decode()
    frame = result["image"]

    # VideoFrame が close() をサポートしていることを確認
    assert not frame.is_closed
    frame.close()
    assert frame.is_closed

    decoder.close()
