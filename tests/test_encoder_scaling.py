"""エンコーダーのスケーリング機能テスト.

WebCodecs API 仕様: 「The encoder MUST scale any VideoFrame whose
visible width differs from the configured width value」

- ソフトウェアエンコーダー (AV1, VP8, VP9): libyuv を使用
- ハードウェアエンコーダー (NVENC, Intel VPL): libyuv を使用
- Apple Video Toolbox: VTPixelTransferSession を使用 (test_apple_video_toolbox.py)

テストデータについて:
    このテストでは全てのピクセルフォーマット (I420, I422, I444, NV12, RGBA, BGRA, RGB, BGR)
    に対してスケーリング機能をテストする。テストフレームのデータは全てゼロ (黒) だが、
    各フォーマットに応じた正しいサイズで生成される。VideoFrame はサイズと format 指定に
    基づいてデータを解釈するため、スケーリング機能のテストとしてはサイズが正しければ十分。
"""

import platform

import numpy as np
import pytest

from webcodecs import (
    EncodedVideoChunkType,
    LatencyMode,
    VideoDecoder,
    VideoDecoderConfig,
    VideoEncoder,
    VideoEncoderConfig,
    VideoFrame,
    VideoFrameBufferInit,
    VideoPixelFormat,
)


def _calculate_frame_data_size(width: int, height: int, pixel_format: VideoPixelFormat) -> int:
    """ピクセルフォーマットに応じたデータサイズを計算する."""
    match pixel_format:
        case VideoPixelFormat.I420 | VideoPixelFormat.NV12:
            return width * height * 3 // 2
        case VideoPixelFormat.I422:
            return width * height * 2
        case VideoPixelFormat.I444 | VideoPixelFormat.RGB | VideoPixelFormat.BGR:
            return width * height * 3
        case VideoPixelFormat.RGBA | VideoPixelFormat.BGRA:
            return width * height * 4
        case _:
            raise ValueError(f"Unsupported pixel format: {pixel_format}")


def _make_test_frame(
    width: int,
    height: int,
    frame_num: int = 0,
    pixel_format: VideoPixelFormat = VideoPixelFormat.I420,
) -> VideoFrame:
    """テスト用の VideoFrame を作成する."""
    data_size = _calculate_frame_data_size(width, height, pixel_format)
    data = np.zeros(data_size, dtype=np.uint8)
    init: VideoFrameBufferInit = {
        "format": pixel_format,
        "coded_width": width,
        "coded_height": height,
        "timestamp": frame_num * 1000,
    }
    frame = VideoFrame(data, init)
    return frame


# =============================================================================
# スケーリングテスト (全コーデック共通)
# =============================================================================


@pytest.mark.parametrize(
    "codec",
    [
        pytest.param("av01.0.04M.08", id="AV1"),
        pytest.param(
            "vp8",
            marks=pytest.mark.skipif(
                platform.system() not in ("Darwin", "Linux"),
                reason="VP8 は macOS / Linux のみサポート",
            ),
            id="VP8",
        ),
        pytest.param(
            "vp09.00.10.08",
            marks=pytest.mark.skipif(
                platform.system() not in ("Darwin", "Linux"),
                reason="VP9 は macOS / Linux のみサポート",
            ),
            id="VP9",
        ),
    ],
)
@pytest.mark.parametrize("pixel_format", VideoPixelFormat)
def test_encode_with_scaling(codec: str, pixel_format: VideoPixelFormat):
    """エンコーダのスケーリング機能テスト (各コーデック・各ピクセルフォーマット)."""
    # configure: 320x240 (出力解像度)
    output_width, output_height = 320, 240
    # encode: 640x480 のフレーム (入力解像度)
    input_width, input_height = 640, 480

    encoded_chunks = []

    def on_output(chunk):
        encoded_chunks.append(chunk)

    def on_error(error):
        pytest.fail(f"Encoder error: {error}")

    encoder = VideoEncoder(on_output, on_error)

    config: VideoEncoderConfig = {
        "codec": codec,
        "width": output_width,
        "height": output_height,
        "bitrate": 500_000,
        "framerate": 30.0,
        "latency_mode": LatencyMode.REALTIME,
    }
    encoder.configure(config)

    # 入力解像度のフレームを作成
    frame = _make_test_frame(input_width, input_height, 0, pixel_format)
    encoder.encode(frame, {"key_frame": True})
    encoder.flush()
    frame.close()

    # エンコードが成功していることを確認
    assert len(encoded_chunks) >= 1
    assert encoded_chunks[0].byte_length > 0
    assert encoded_chunks[0].type == EncodedVideoChunkType.KEY

    # デコードして出力解像度を確認
    decoded_frames = []

    def on_decode_output(frame):
        decoded_frames.append(frame)

    def on_decode_error(error):
        pytest.fail(f"Decoder error: {error}")

    decoder = VideoDecoder(on_decode_output, on_decode_error)

    decoder_config: VideoDecoderConfig = {"codec": codec}
    decoder.configure(decoder_config)

    for chunk in encoded_chunks:
        decoder.decode(chunk)
    decoder.flush()

    # デコードされたフレームが出力解像度になっていることを確認
    assert len(decoded_frames) >= 1
    for frame in decoded_frames:
        assert frame.coded_width == output_width
        assert frame.coded_height == output_height
        frame.close()

    encoder.close()
    decoder.close()


@pytest.mark.parametrize(
    "codec",
    [
        pytest.param("av01.0.04M.08", id="AV1"),
        pytest.param(
            "vp8",
            marks=pytest.mark.skipif(
                platform.system() not in ("Darwin", "Linux"),
                reason="VP8 は macOS / Linux のみサポート",
            ),
            id="VP8",
        ),
        pytest.param(
            "vp09.00.10.08",
            marks=pytest.mark.skipif(
                platform.system() not in ("Darwin", "Linux"),
                reason="VP9 は macOS / Linux のみサポート",
            ),
            id="VP9",
        ),
    ],
)
@pytest.mark.parametrize("pixel_format", VideoPixelFormat)
def test_encode_scaling_same_resolution(codec: str, pixel_format: VideoPixelFormat):
    """configure と同じ解像度のフレームはスケーリングなしでエンコード (各コーデック・各ピクセルフォーマット)."""
    width, height = 320, 240

    encoded_chunks = []

    def on_output(chunk):
        encoded_chunks.append(chunk)

    def on_error(error):
        pytest.fail(f"Encoder error: {error}")

    encoder = VideoEncoder(on_output, on_error)

    config: VideoEncoderConfig = {
        "codec": codec,
        "width": width,
        "height": height,
        "bitrate": 500_000,
        "framerate": 30.0,
        "latency_mode": LatencyMode.REALTIME,
    }
    encoder.configure(config)

    # 同じ解像度のフレーム
    frame = _make_test_frame(width, height, 0, pixel_format)
    encoder.encode(frame, {"key_frame": True})
    encoder.flush()
    frame.close()

    # エンコードが成功していることを確認
    assert len(encoded_chunks) >= 1
    assert encoded_chunks[0].byte_length > 0

    encoder.close()


@pytest.mark.parametrize(
    "codec",
    [
        pytest.param("av01.0.04M.08", id="AV1"),
        pytest.param(
            "vp09.00.10.08",
            marks=pytest.mark.skipif(
                platform.system() not in ("Darwin", "Linux"),
                reason="VP9 は macOS / Linux のみサポート",
            ),
            id="VP9",
        ),
    ],
)
@pytest.mark.parametrize("pixel_format", VideoPixelFormat)
def test_encode_scaling_multiple_frames(codec: str, pixel_format: VideoPixelFormat):
    """複数フレームでのスケーリングテスト (各コーデック・各ピクセルフォーマット)."""
    # configure: 320x240 (出力解像度)
    output_width, output_height = 320, 240
    # encode: 640x480 のフレーム (入力解像度)
    input_width, input_height = 640, 480
    num_frames = 3

    encoded_chunks = []

    def on_output(chunk):
        encoded_chunks.append(chunk)

    def on_error(error):
        pytest.fail(f"Encoder error: {error}")

    encoder = VideoEncoder(on_output, on_error)

    config: VideoEncoderConfig = {
        "codec": codec,
        "width": output_width,
        "height": output_height,
        "bitrate": 500_000,
        "framerate": 30.0,
        "latency_mode": LatencyMode.REALTIME,
    }
    encoder.configure(config)

    # 入力解像度のフレームを複数作成・エンコード
    for i in range(num_frames):
        frame = _make_test_frame(input_width, input_height, i, pixel_format)
        encoder.encode(frame, {"key_frame": i == 0})
        frame.close()

    encoder.flush()

    # エンコードが成功していることを確認
    assert len(encoded_chunks) >= num_frames

    encoder.close()
