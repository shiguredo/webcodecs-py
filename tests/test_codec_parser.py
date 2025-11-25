"""
コーデック文字列パーサーのテスト

WebCodecs API に準拠したコーデック文字列が正しくパースされ、
エンコーダー/デコーダーの設定に反映されることを確認する。
"""

import numpy as np
import pytest

from webcodecs import (
    VideoEncoder,
    VideoFrame,
    VideoFrameBufferInit,
    VideoPixelFormat,
)


def test_av1_codec_string_main_profile_8bit():
    """AV1 Main Profile, 8-bit のコーデック文字列が正しく動作することを確認"""
    width = 320
    height = 240
    data = np.zeros(width * height * 3 // 2, dtype=np.uint8)

    init: VideoFrameBufferInit = {
        "format": VideoPixelFormat.I420,
        "coded_width": width,
        "coded_height": height,
        "timestamp": 0,
    }
    frame = VideoFrame(data, init)

    encoded_chunks = []

    def on_output(chunk):
        encoded_chunks.append(chunk)

    def on_error(error):
        pytest.fail(f"Encoder error: {error}")

    encoder = VideoEncoder(on_output, on_error)
    encoder.configure(
        {
            "codec": "av01.0.04M.08",  # Profile 0 (Main), Level 3.0, Main tier, 8-bit
            "width": width,
            "height": height,
            "bitrate": 400000,
        }
    )

    encoder.encode(frame)
    encoder.flush()
    encoder.close()

    assert len(encoded_chunks) > 0


def test_av1_codec_string_different_levels():
    """AV1 の異なるレベルのコーデック文字列が正しく動作することを確認"""
    width = 320
    height = 240
    data = np.zeros(width * height * 3 // 2, dtype=np.uint8)

    init: VideoFrameBufferInit = {
        "format": VideoPixelFormat.I420,
        "coded_width": width,
        "coded_height": height,
        "timestamp": 0,
    }
    frame = VideoFrame(data, init)

    encoded_chunks = []

    def on_output(chunk):
        encoded_chunks.append(chunk)

    def on_error(error):
        pytest.fail(f"Encoder error: {error}")

    encoder = VideoEncoder(on_output, on_error)
    encoder.configure(
        {
            "codec": "av01.0.05M.08",  # Profile 0 (Main), Level 3.1, Main tier, 8-bit
            "width": width,
            "height": height,
            "bitrate": 400000,
        }
    )

    encoder.encode(frame)
    encoder.flush()
    encoder.close()

    assert len(encoded_chunks) > 0


def test_av1_codec_string_with_optional_parameters():
    """AV1 コーデック文字列のオプションパラメータが正しくパースされることを確認"""
    width = 320
    height = 240
    data = np.zeros(width * height * 3 // 2, dtype=np.uint8)

    init: VideoFrameBufferInit = {
        "format": VideoPixelFormat.I420,
        "coded_width": width,
        "coded_height": height,
        "timestamp": 0,
    }
    frame = VideoFrame(data, init)

    encoded_chunks = []

    def on_output(chunk):
        encoded_chunks.append(chunk)

    def on_error(error):
        pytest.fail(f"Encoder error: {error}")

    encoder = VideoEncoder(on_output, on_error)
    encoder.configure(
        {
            "codec": "av01.0.04M.08.0.112.09.16.09.0",  # オプションパラメータ付き
            "width": width,
            "height": height,
            "bitrate": 400000,
        }
    )

    encoder.encode(frame)
    encoder.flush()
    encoder.close()

    assert len(encoded_chunks) > 0


def test_invalid_av1_codec_string():
    """無効な AV1 コーデック文字列がエラーになることを確認"""
    encoder = VideoEncoder(lambda x: None, lambda x: None)

    with pytest.raises(ValueError, match="Invalid codec string"):
        encoder.configure(
            {
                "codec": "av01.9.04M.08",  # 無効な profile (9)
                "width": 320,
                "height": 240,
            }
        )


def test_invalid_avc_codec_string():
    """無効な AVC コーデック文字列がエラーになることを確認"""
    encoder = VideoEncoder(lambda x: None, lambda x: None)

    with pytest.raises(ValueError, match="Invalid codec string"):
        encoder.configure(
            {
                "codec": "avc1.42",  # 短すぎる
                "width": 320,
                "height": 240,
            }
        )


def test_unsupported_codec_string():
    """サポートされていないコーデック文字列がエラーになることを確認"""
    encoder = VideoEncoder(lambda x: None, lambda x: None)

    with pytest.raises(ValueError, match="Invalid codec string"):
        encoder.configure(
            {
                "codec": "vp9.0",  # サポートされていないコーデック
                "width": 320,
                "height": 240,
            }
        )
