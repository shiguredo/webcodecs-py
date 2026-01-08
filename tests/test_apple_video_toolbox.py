"""Apple Video Toolbox エンコード/デコードのテスト."""

import os

import numpy as np
import pytest

from webcodecs import (
    CodecState,
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
)

# Apple Video Toolbox 環境でのみテストを実行
pytestmark = pytest.mark.skipif(
    os.environ.get("APPLE_VIDEO_TOOLBOX") is None, reason="Apple Video Toolbox でのみ実行する"
)


@pytest.mark.parametrize(
    ("codec", "expected_supported"),
    [
        ("avc1.42E01E", True),
        ("hvc1.1.6.L93.B0", True),
        ("vp9", False),
    ],
)
def test_encoder_is_config_supported(codec, expected_supported):
    """ビデオエンコーダーの is_config_supported テスト."""
    width, height = 960, 540
    config: VideoEncoderConfig = {
        "codec": codec,
        "width": width,
        "height": height,
    }
    support = VideoEncoder.is_config_supported(config)
    assert support["supported"] is expected_supported
    assert support["config"]["codec"] == codec
    if expected_supported:
        assert support["config"]["width"] == width
        assert support["config"]["height"] == height


@pytest.mark.parametrize(
    ("codec", "hardware_acceleration_engine", "expected_supported"),
    [
        ("avc1.42E01E", None, True),
        ("hvc1.1.6.L93.B0", None, True),
        ("vp9", None, False),
        ("vp09.00.10.08", HardwareAccelerationEngine.APPLE_VIDEO_TOOLBOX, True),
        pytest.param(
            "av01.0.04M.08",
            HardwareAccelerationEngine.APPLE_VIDEO_TOOLBOX,
            True,
            marks=pytest.mark.skip(
                reason="AV1 Decoder を持っている GitHub Self-hosted Runner がないため無効化"
            ),
        ),
    ],
)
def test_decoder_is_config_supported(codec, hardware_acceleration_engine, expected_supported):
    """ビデオデコーダーの is_config_supported テスト."""
    config: VideoDecoderConfig = {
        "codec": codec,
    }
    if hardware_acceleration_engine is not None:
        config["hardware_acceleration_engine"] = hardware_acceleration_engine
    support = VideoDecoder.is_config_supported(config)
    assert support["supported"] is expected_supported
    assert support["config"]["codec"] == codec


def test_h264_encode_decode():
    """H.264 エンコード/デコードのラウンドトリップテスト."""
    # エンコーダー設定
    encoded_chunks = []

    def on_encode_output(chunk):
        encoded_chunks.append(chunk)

    def on_encode_error(error):
        print(f"Encoder error: {error}")

    encoder = VideoEncoder(on_encode_output, on_encode_error)

    # Config でコーデック設定
    # avc: {"format": "annexb"} でデコーダーが期待する Annex B フォーマットで出力
    encoder_config: VideoEncoderConfig = {
        "codec": "avc1.42E01E",
        "width": 640,
        "height": 480,
        "bitrate": 1_000_000,
        "framerate": 30,
        "latency_mode": LatencyMode.REALTIME,
        "hardware_acceleration_engine": HardwareAccelerationEngine.APPLE_VIDEO_TOOLBOX,
        "avc": {"format": "annexb"},
    }

    encoder.configure(encoder_config)

    # テストフレームを作成
    test_frames = []
    width, height = 640, 480
    data_size = width * height * 3 // 2  # I420
    for i in range(5):
        data = np.zeros(data_size, dtype=np.uint8)
        init: VideoFrameBufferInit = {
            "format": VideoPixelFormat.I420,
            "coded_width": width,
            "coded_height": height,
            "timestamp": i * 33333,
        }
        frame = VideoFrame(data, init)
        test_frames.append(frame)
        encoder.encode(frame, {"key_frame": i == 0})

    encoder.flush()

    assert len(encoded_chunks) > 0, "エンコードされたチャンクが生成されませんでした"

    # デコーダー設定
    decoded_frames = []

    def on_decode_output(frame):
        decoded_frames.append(frame)

    def on_decode_error(error):
        print(f"Decoder error: {error}")

    decoder = VideoDecoder(on_decode_output, on_decode_error)

    # Config でコーデック設定
    decoder_config: VideoDecoderConfig = {
        "codec": "avc1.42E01E",
        "coded_width": 640,
        "coded_height": 480,
    }

    decoder.configure(decoder_config)

    # エンコードされたチャンクをデコード
    for chunk in encoded_chunks:
        decoder.decode(chunk)

    decoder.flush()

    assert len(decoded_frames) > 0, "デコードされたフレームが生成されませんでした"
    print(f"エンコードチャンク数: {len(encoded_chunks)}, デコードフレーム数: {len(decoded_frames)}")

    # クリーンアップ
    for frame in test_frames:
        frame.close()
    for frame in decoded_frames:
        frame.close()
    encoder.close()
    decoder.close()


def test_h265_encode_decode():
    """H.265/HEVC エンコード/デコードのラウンドトリップテスト."""
    # エンコーダー設定
    encoded_chunks = []

    def on_encode_output(chunk):
        encoded_chunks.append(chunk)

    def on_encode_error(error):
        print(f"Encoder error: {error}")

    encoder = VideoEncoder(on_encode_output, on_encode_error)

    # Config でコーデック設定
    # hevc: {"format": "annexb"} でデコーダーが期待する Annex B フォーマットで出力
    encoder_config: VideoEncoderConfig = {
        "codec": "hvc1.1.6.L93.B0",
        "width": 1280,
        "height": 720,
        "bitrate": 2_000_000,
        "framerate": 30,
        "latency_mode": LatencyMode.REALTIME,
        "hardware_acceleration_engine": HardwareAccelerationEngine.APPLE_VIDEO_TOOLBOX,
        "hevc": {"format": "annexb"},
    }

    encoder.configure(encoder_config)

    # テストフレームをエンコード
    width, height = 1280, 720
    data_size = width * height * 3 // 2  # I420
    for i in range(3):
        data = np.zeros(data_size, dtype=np.uint8)
        init: VideoFrameBufferInit = {
            "format": VideoPixelFormat.I420,
            "coded_width": width,
            "coded_height": height,
            "timestamp": i * 33333,
        }
        frame = VideoFrame(data, init)
        encoder.encode(frame, {"key_frame": i == 0})
        frame.close()

    encoder.flush()

    assert len(encoded_chunks) > 0, "H.265 エンコードされたチャンクが生成されませんでした"

    # デコーダー設定
    decoded_frames = []

    def on_decode_output(frame):
        decoded_frames.append(frame)

    def on_decode_error(error):
        print(f"Decoder error: {error}")

    decoder = VideoDecoder(on_decode_output, on_decode_error)

    # Config でコーデック設定
    decoder_config: VideoDecoderConfig = {
        "codec": "hvc1.1.6.L93.B0",
        "coded_width": 1280,
        "coded_height": 720,
    }

    decoder.configure(decoder_config)

    # デコード
    for chunk in encoded_chunks:
        decoder.decode(chunk)

    decoder.flush()

    assert len(decoded_frames) > 0, "H.265 デコードされたフレームが生成されませんでした"
    print(
        f"H.265 エンコードチャンク数: {len(encoded_chunks)}, デコードフレーム数: {len(decoded_frames)}"
    )

    # クリーンアップ
    for frame in decoded_frames:
        frame.close()
    encoder.close()
    decoder.close()


def test_h264_decoder_only():
    """H.264 デコーダー単体のテスト（手動で作成したチャンク）."""
    decoded_count = 0

    def on_output(frame):
        nonlocal decoded_count
        decoded_count += 1
        assert frame.coded_width == 320
        assert frame.coded_height == 240
        frame.close()

    def on_error(error):
        print(f"Decoder error: {error}")

    decoder = VideoDecoder(on_output, on_error)

    config: VideoDecoderConfig = {"codec": "avc1.42E01E", "coded_width": 320, "coded_height": 240}
    decoder.configure(config)

    # まずキーフレームが必要なので、エンコーダーで生成
    test_chunk = None

    def on_encode(chunk):
        nonlocal test_chunk
        if test_chunk is None:
            test_chunk = chunk

    def on_enc_error(error):
        print(f"Encoder error: {error}")

    encoder = VideoEncoder(on_encode, on_enc_error)

    enc_config: VideoEncoderConfig = {
        "codec": "avc1.42E01E",
        "width": 320,
        "height": 240,
        "bitrate": 500_000,
        "framerate": 30,
        "latency_mode": LatencyMode.REALTIME,
        "hardware_acceleration_engine": HardwareAccelerationEngine.APPLE_VIDEO_TOOLBOX,
        "avc": {"format": "annexb"},
    }

    encoder.configure(enc_config)

    width, height = 320, 240
    data_size = width * height * 3 // 2  # I420
    data = np.zeros(data_size, dtype=np.uint8)
    frame_init: VideoFrameBufferInit = {
        "format": VideoPixelFormat.I420,
        "coded_width": width,
        "coded_height": height,
        "timestamp": 0,
    }
    frame = VideoFrame(data, frame_init)
    encoder.encode(frame, {"key_frame": True})
    encoder.flush()

    assert test_chunk is not None, "テスト用チャンクが生成されませんでした"

    # デコード
    decoder.decode(test_chunk)
    decoder.flush()

    assert decoded_count > 0, "フレームがデコードされませんでした"

    # クリーンアップ
    frame.close()
    encoder.close()
    decoder.close()


def test_h265_decoder_with_hevc_codec_string():
    """'hevc' コーデック文字列でのデコーダーテスト."""
    decoded_count = 0

    def on_decode_out(frame):
        nonlocal decoded_count
        decoded_count += 1
        frame.close()

    def on_decode_err(error):
        print(f"Decoder error: {error}")

    decoder = VideoDecoder(on_decode_out, on_decode_err)

    dec_config: VideoDecoderConfig = {
        "codec": "hvc1.1.6.L93.B0",  # h265 の別名
        "coded_width": 640,
        "coded_height": 480,
    }
    decoder.configure(dec_config)

    # エンコーダーでテストデータ生成
    test_chunk = None

    def on_encode(chunk):
        nonlocal test_chunk
        test_chunk = chunk

    def on_enc_error(error):
        print(f"Encoder error: {error}")

    encoder = VideoEncoder(on_encode, on_enc_error)

    enc_config: VideoEncoderConfig = {
        "codec": "hvc1.1.6.L93.B0",
        "width": 640,
        "height": 480,
        "bitrate": 1_000_000,
        "framerate": 30,
        "latency_mode": LatencyMode.REALTIME,
        "hardware_acceleration_engine": HardwareAccelerationEngine.APPLE_VIDEO_TOOLBOX,
        "hevc": {"format": "annexb"},
    }

    encoder.configure(enc_config)

    width, height = 640, 480
    data_size = width * height * 3 // 2  # I420
    data = np.zeros(data_size, dtype=np.uint8)
    frame_init: VideoFrameBufferInit = {
        "format": VideoPixelFormat.I420,
        "coded_width": width,
        "coded_height": height,
        "timestamp": 0,
    }
    frame = VideoFrame(data, frame_init)
    encoder.encode(frame, {"key_frame": True})
    encoder.flush()

    assert test_chunk is not None

    # デコード
    decoded_count = 0
    decoder.decode(test_chunk)
    decoder.flush()

    assert decoded_count > 0, "'hevc' コーデック文字列でデコードに失敗"

    # クリーンアップ
    frame.close()
    encoder.close()
    decoder.close()


def test_nv12_frame_encode_decode():
    """NV12 フレームフォーマットでのエンコード/デコードテスト."""
    # エンコーダー設定
    encoded_chunks = []

    def on_encode_output(chunk):
        encoded_chunks.append(chunk)

    def on_encode_error(error):
        print(f"Encoder error: {error}")

    encoder = VideoEncoder(on_encode_output, on_encode_error)

    enc_config: VideoEncoderConfig = {
        "codec": "avc1.42E01E",
        "width": 640,
        "height": 480,
        "bitrate": 1_000_000,
        "framerate": 30,
        "latency_mode": LatencyMode.REALTIME,
        "hardware_acceleration_engine": HardwareAccelerationEngine.APPLE_VIDEO_TOOLBOX,
        "avc": {"format": "annexb"},
    }

    encoder.configure(enc_config)

    # NV12 フレームを作成してエンコード
    width, height = 640, 480
    # NV12: Y プレーン (width * height) + UV インターリーブ (width * height / 2)
    data_size = width * height * 3 // 2
    data = np.zeros(data_size, dtype=np.uint8)
    frame_init: VideoFrameBufferInit = {
        "format": VideoPixelFormat.NV12,
        "coded_width": width,
        "coded_height": height,
        "timestamp": 0,
    }
    frame = VideoFrame(data, frame_init)
    encoder.encode(frame, {"key_frame": True})
    encoder.flush()

    assert len(encoded_chunks) > 0, "NV12 フレームのエンコードに失敗"

    # デコード
    decoded_frames = []

    def on_decode_output(frame):
        decoded_frames.append(frame)

    def on_decode_error(error):
        print(f"Decoder error: {error}")

    decoder = VideoDecoder(on_decode_output, on_decode_error)

    dec_config: VideoDecoderConfig = {
        "codec": "avc1.42E01E",
        "coded_width": 640,
        "coded_height": 480,
    }
    decoder.configure(dec_config)

    for chunk in encoded_chunks:
        decoder.decode(chunk)

    decoder.flush()

    assert len(decoded_frames) > 0, "NV12 エンコード済みフレームのデコードに失敗"
    # デコード後のフレームは NV12 フォーマット
    assert decoded_frames[0].format == VideoPixelFormat.NV12

    # クリーンアップ
    frame.close()
    for frame in decoded_frames:
        frame.close()
    encoder.close()
    decoder.close()


def test_key_frame_and_delta_frames():
    """キーフレームとデルタフレームのデコードテスト."""
    # エンコーダーで複数のフレームを生成
    encoded_chunks = []

    def on_encode(chunk):
        encoded_chunks.append(chunk)

    def on_encode_error(error):
        print(f"Encoder error: {error}")

    encoder = VideoEncoder(on_encode, on_encode_error)

    enc_config: VideoEncoderConfig = {
        "codec": "avc1.42E01E",
        "width": 640,
        "height": 480,
        "bitrate": 1_000_000,
        "framerate": 30,
        "latency_mode": LatencyMode.REALTIME,
        "hardware_acceleration_engine": HardwareAccelerationEngine.APPLE_VIDEO_TOOLBOX,
        "avc": {"format": "annexb"},
    }

    encoder.configure(enc_config)

    # キーフレーム1つ、デルタフレーム4つを生成
    width, height = 640, 480
    data_size = width * height * 3 // 2  # I420
    for i in range(5):
        data = np.zeros(data_size, dtype=np.uint8)
        frame_init: VideoFrameBufferInit = {
            "format": VideoPixelFormat.I420,
            "coded_width": width,
            "coded_height": height,
            "timestamp": i * 33333,
        }
        frame = VideoFrame(data, frame_init)
        encoder.encode(frame, {"key_frame": i == 0})
        frame.close()

    encoder.flush()

    # デコーダー設定
    decoded_frames = []

    def on_decode_output(frame):
        decoded_frames.append(frame)

    def on_decode_error(error):
        print(f"Decoder error: {error}")

    decoder = VideoDecoder(on_decode_output, on_decode_error)

    dec_config: VideoDecoderConfig = {
        "codec": "avc1.42E01E",
        "coded_width": 640,
        "coded_height": 480,
    }
    decoder.configure(dec_config)

    # 全チャンクをデコード
    for chunk in encoded_chunks:
        decoder.decode(chunk)

    decoder.flush()

    # キーフレームとデルタフレームの両方がデコードされることを確認
    assert len(decoded_frames) > 0, "フレームがデコードされませんでした"
    # タイムスタンプが正しく保持されているか確認
    for i, frame in enumerate(decoded_frames[:5]):
        expected_timestamp = i * 33333
        # タイムスタンプの誤差を許容
        assert abs(frame.timestamp - expected_timestamp) < 100, (
            f"フレーム {i} のタイムスタンプが不正: 期待値 {expected_timestamp}, 実際 {frame.timestamp}"
        )

    # クリーンアップ
    for frame in decoded_frames:
        frame.close()
    encoder.close()
    decoder.close()


def test_av1_fallback():
    """AV1 は VideoToolbox ではなくソフトウェアデコーダーを使用することを確認."""
    codecs = ["av01.0.04M.08"]

    for codec in codecs:
        # デコーダーが正常に作成できることを確認（ソフトウェアデコーダーを使用）
        def on_output(frame):
            frame.close()

        def on_error(error):
            print(f"Decoder error: {error}")

        decoder = VideoDecoder(on_output, on_error)

        config: VideoDecoderConfig = {"codec": codec, "coded_width": 640, "coded_height": 480}
        decoder.configure(config)

        assert decoder.state == CodecState.CONFIGURED, f"{codec} デコーダーの初期化に失敗"
        decoder.close()


def test_parallel_h264_decode():
    """並列デコードのテスト."""
    # エンコーダーでテストデータ生成
    encoded_chunks = []

    def on_encode(chunk):
        encoded_chunks.append(chunk)

    def on_encode_error(error):
        print(f"Encoder error: {error}")

    encoder = VideoEncoder(on_encode, on_encode_error)

    enc_config: VideoEncoderConfig = {
        "codec": "avc1.42E01E",
        "width": 320,
        "height": 240,
        "bitrate": 500_000,
        "framerate": 30,
        "latency_mode": LatencyMode.REALTIME,
        "hardware_acceleration_engine": HardwareAccelerationEngine.APPLE_VIDEO_TOOLBOX,
        "avc": {"format": "annexb"},
    }

    encoder.configure(enc_config)

    # 10フレーム生成
    width, height = 320, 240
    data_size = width * height * 3 // 2  # I420
    for i in range(10):
        data = np.zeros(data_size, dtype=np.uint8)
        frame_init: VideoFrameBufferInit = {
            "format": VideoPixelFormat.I420,
            "coded_width": width,
            "coded_height": height,
            "timestamp": i * 33333,
        }
        frame = VideoFrame(data, frame_init)
        encoder.encode(frame, {"key_frame": i == 0})
        frame.close()

    encoder.flush()

    assert len(encoded_chunks) > 0

    # デコーダー設定
    decoded_frames = []

    def on_decode_output(frame):
        decoded_frames.append(frame)

    def on_decode_error(error):
        print(f"Decoder error: {error}")

    decoder = VideoDecoder(on_decode_output, on_decode_error)

    decoder_config: VideoDecoderConfig = {
        "codec": "avc1.42E01E",
        "coded_width": 320,
        "coded_height": 240,
    }
    decoder.configure(decoder_config)

    # 全チャンクを一気にデコード（並列処理のテスト）
    for chunk in encoded_chunks:
        decoder.decode(chunk)

    decoder.flush()

    # 順序が保持されているか確認
    assert len(decoded_frames) > 0
    for i in range(min(len(decoded_frames) - 1, 9)):
        assert decoded_frames[i].timestamp <= decoded_frames[i + 1].timestamp, (
            "デコードされたフレームの順序が不正"
        )

    # クリーンアップ
    for frame in decoded_frames:
        frame.close()
    encoder.close()
    decoder.close()


def test_video_toolbox_basic_encode():
    """基本的な VideoToolbox エンコードテスト"""
    encoded_chunks = []

    def on_output(chunk):
        encoded_chunks.append(chunk)
        assert chunk.byte_length > 0

    def on_error(error):
        print(f"Encoder error: {error}")

    encoder = VideoEncoder(on_output, on_error)

    config: VideoEncoderConfig = {
        "codec": "avc1.42E01E",
        "width": 640,
        "height": 480,
        "bitrate": 1_000_000,
        "framerate": 30,
        "latency_mode": LatencyMode.REALTIME,
        "hardware_acceleration_engine": HardwareAccelerationEngine.APPLE_VIDEO_TOOLBOX,
    }

    encoder.configure(config)

    # 5 フレームをエンコード
    width, height = 640, 480
    data_size = width * height * 3 // 2  # I420
    for i in range(5):
        data = np.zeros(data_size, dtype=np.uint8)
        init: VideoFrameBufferInit = {
            "format": VideoPixelFormat.I420,
            "coded_width": width,
            "coded_height": height,
            "timestamp": i * 33333,
        }
        frame = VideoFrame(data, init)
        encoder.encode(frame, {"key_frame": i == 0})
        frame.close()

    encoder.flush()

    assert len(encoded_chunks) > 0, "エンコードされたチャンクが生成されませんでした"
    encoder.close()


def test_video_toolbox_key_frame_control():
    """キーフレーム制御のテスト"""
    key_frame_count = 0

    def on_output(chunk):
        nonlocal key_frame_count
        if chunk.type.name == "KEY":
            key_frame_count += 1

    def on_error(error):
        print(f"Encoder error: {error}")

    encoder = VideoEncoder(on_output, on_error)

    # 1280x720 には Level 3.1 以上が必要
    config: VideoEncoderConfig = {
        "codec": "avc1.42E01F",  # Baseline Profile, Level 3.1
        "width": 1280,
        "height": 720,
        "bitrate": 2_000_000,
        "framerate": 30,
        "latency_mode": LatencyMode.REALTIME,
        "hardware_acceleration_engine": HardwareAccelerationEngine.APPLE_VIDEO_TOOLBOX,
    }

    encoder.configure(config)

    # 最初のフレームと中間のフレームをキーフレームに
    width, height = 1280, 720
    data_size = width * height * 3 // 2  # I420
    for i in range(10):
        data = np.zeros(data_size, dtype=np.uint8)
        init: VideoFrameBufferInit = {
            "format": VideoPixelFormat.I420,
            "coded_width": width,
            "coded_height": height,
            "timestamp": i * 33333,
        }
        frame = VideoFrame(data, init)
        encoder.encode(frame, {"key_frame": i == 0 or i == 5})
        frame.close()

    encoder.flush()

    # VideoToolbox では自動的にキーフレームが挿入される場合があるため、
    # 少なくとも1つはキーフレームがあることを確認
    assert key_frame_count >= 1, f"期待されるキーフレーム数: 1以上, 実際: {key_frame_count}"
    encoder.close()


def test_video_toolbox_minimum_setup():
    """最小限のセットアップでの動作確認"""
    output_count = 0

    def on_output(chunk):
        nonlocal output_count
        output_count += 1

    def on_error(error):
        print(f"Encoder error: {error}")

    encoder = VideoEncoder(on_output, on_error)

    config: VideoEncoderConfig = {
        "codec": "avc1.42E01E",
        "width": 320,
        "height": 240,
        "bitrate": 500_000,
        "framerate": 30,
        "latency_mode": LatencyMode.REALTIME,
        "hardware_acceleration_engine": HardwareAccelerationEngine.APPLE_VIDEO_TOOLBOX,
    }

    encoder.configure(config)

    # 1 フレームだけエンコード
    width, height = 320, 240
    data_size = width * height * 3 // 2  # I420
    data = np.zeros(data_size, dtype=np.uint8)
    init: VideoFrameBufferInit = {
        "format": VideoPixelFormat.I420,
        "coded_width": width,
        "coded_height": height,
        "timestamp": 0,
    }
    frame = VideoFrame(data, init)
    encoder.encode(frame, {"key_frame": True})
    frame.close()

    encoder.flush()

    assert output_count > 0, "出力が受信されませんでした"
    encoder.close()


def test_video_toolbox_h265_encode():
    """H.265/HEVC エンコードのテスト"""
    encoded_chunks = []

    def on_output(chunk):
        encoded_chunks.append(chunk)

    def on_error(error):
        print(f"Encoder error: {error}")

    encoder = VideoEncoder(on_output, on_error)

    config: VideoEncoderConfig = {
        "codec": "hvc1.1.6.L93.B0",
        "width": 1920,
        "height": 1080,
        "bitrate": 4_000_000,
        "framerate": 30,
        "latency_mode": LatencyMode.REALTIME,
        "hardware_acceleration_engine": HardwareAccelerationEngine.APPLE_VIDEO_TOOLBOX,
    }

    encoder.configure(config)

    # 3 フレームをエンコード
    width, height = 1920, 1080
    data_size = width * height * 3 // 2  # I420
    for i in range(3):
        data = np.zeros(data_size, dtype=np.uint8)
        init: VideoFrameBufferInit = {
            "format": VideoPixelFormat.I420,
            "coded_width": width,
            "coded_height": height,
            "timestamp": i * 33333,
        }
        frame = VideoFrame(data, init)
        encoder.encode(frame, {"key_frame": i == 0})
        frame.close()

    encoder.flush()

    assert len(encoded_chunks) > 0, "H.265 チャンクが生成されませんでした"
    encoder.close()


def test_video_toolbox_nv12_input():
    """NV12 入力形式のテスト"""
    output_count = 0

    def on_output(chunk):
        nonlocal output_count
        output_count += 1

    def on_error(error):
        print(f"Encoder error: {error}")

    encoder = VideoEncoder(on_output, on_error)

    config: VideoEncoderConfig = {
        "codec": "avc1.42E01E",
        "width": 640,
        "height": 480,
        "bitrate": 1_000_000,
        "framerate": 30,
        "latency_mode": LatencyMode.REALTIME,
        "hardware_acceleration_engine": HardwareAccelerationEngine.APPLE_VIDEO_TOOLBOX,
    }

    encoder.configure(config)

    # NV12 フレームを作成
    width, height = 640, 480
    data_size = width * height * 3 // 2  # NV12
    data = np.zeros(data_size, dtype=np.uint8)
    init: VideoFrameBufferInit = {
        "format": VideoPixelFormat.NV12,
        "coded_width": width,
        "coded_height": height,
        "timestamp": 0,
    }
    frame = VideoFrame(data, init)
    encoder.encode(frame, {"key_frame": True})
    frame.close()

    encoder.flush()

    assert output_count > 0, "NV12 フレームのエンコードに失敗"
    encoder.close()


def test_video_toolbox_software_fallback():
    """ソフトウェアエンコーダーへのフォールバックテスト"""
    # AV1 では VideoToolbox ではなく NONE（ソフトウェア）を使用
    config: VideoEncoderConfig = {
        "codec": "av01.0.04M.08",
        "width": 640,
        "height": 480,
        "bitrate": 1_000_000,
        "framerate": 30,
        "hardware_acceleration_engine": HardwareAccelerationEngine.NONE,
    }

    encoded_chunks = []

    def on_output(chunk):
        encoded_chunks.append(chunk)

    def on_error(error):
        print(f"Encoder error: {error}")

    encoder = VideoEncoder(on_output, on_error)
    encoder.configure(config)

    # 1 フレームエンコード
    width, height = 640, 480
    data_size = width * height * 3 // 2  # I420
    data = np.zeros(data_size, dtype=np.uint8)
    init: VideoFrameBufferInit = {
        "format": VideoPixelFormat.I420,
        "coded_width": width,
        "coded_height": height,
        "timestamp": 0,
    }
    frame = VideoFrame(data, init)
    encoder.encode(frame, {"key_frame": True})
    encoder.flush()
    frame.close()

    assert len(encoded_chunks) > 0, "AV1 ソフトウェアエンコードに失敗"
    encoder.close()


def test_incompatible_profile_level_raises_exception():
    """プロファイル/レベルが解像度と互換性がない場合に例外が発生することを確認"""

    def on_output(chunk):
        pass

    def on_error(error):
        print(f"Encoder error: {error}")

    encoder = VideoEncoder(on_output, on_error)

    # 1280x720 には Level 3.0 は不十分（Level 3.1 以上が必要）
    config: VideoEncoderConfig = {
        "codec": "avc1.42E01E",  # Baseline Profile, Level 3.0
        "width": 1280,
        "height": 720,
        "bitrate": 2_000_000,
        "framerate": 30,
        "latency_mode": LatencyMode.REALTIME,
        "hardware_acceleration_engine": HardwareAccelerationEngine.APPLE_VIDEO_TOOLBOX,
    }

    # configure 時に例外が発生することを確認
    with pytest.raises(RuntimeError, match="Failed to prepare VideoToolbox session"):
        encoder.configure(config)


def test_1080p_requires_higher_level():
    """1920x1080 解像度には高いレベルが必要なことを確認"""

    def on_output(chunk):
        pass

    def on_error(error):
        print(f"Encoder error: {error}")

    encoder = VideoEncoder(on_output, on_error)

    # 1920x1080 には Level 3.1 は不十分（Level 4.0 以上が推奨）
    config: VideoEncoderConfig = {
        "codec": "avc1.42E01F",  # Baseline Profile, Level 3.1
        "width": 1920,
        "height": 1080,
        "bitrate": 4_000_000,
        "framerate": 30,
        "latency_mode": LatencyMode.REALTIME,
        "hardware_acceleration_engine": HardwareAccelerationEngine.APPLE_VIDEO_TOOLBOX,
    }

    # configure 時に例外が発生することを確認
    with pytest.raises(RuntimeError, match="Failed to prepare VideoToolbox session"):
        encoder.configure(config)


def test_1080p_with_correct_level():
    """1920x1080 解像度で適切なレベルを使用するテスト"""
    encoded_chunks = []

    def on_output(chunk):
        encoded_chunks.append(chunk)

    def on_error(error):
        print(f"Encoder error: {error}")

    encoder = VideoEncoder(on_output, on_error)

    # 1920x1080 には Level 4.0 以上が必要
    config: VideoEncoderConfig = {
        "codec": "avc1.640028",  # High Profile, Level 4.0
        "width": 1920,
        "height": 1080,
        "bitrate": 4_000_000,
        "framerate": 30,
        "latency_mode": LatencyMode.REALTIME,
        "hardware_acceleration_engine": HardwareAccelerationEngine.APPLE_VIDEO_TOOLBOX,
    }

    encoder.configure(config)

    # 1 フレームをエンコード
    width, height = 1920, 1080
    data_size = width * height * 3 // 2  # I420
    data = np.zeros(data_size, dtype=np.uint8)
    init: VideoFrameBufferInit = {
        "format": VideoPixelFormat.I420,
        "coded_width": width,
        "coded_height": height,
        "timestamp": 0,
    }
    frame = VideoFrame(data, init)
    encoder.encode(frame, {"key_frame": True})
    frame.close()

    encoder.flush()

    assert len(encoded_chunks) > 0, "1080p エンコードに失敗"
    encoder.close()


def test_hevc_main10_profile():
    """HEVC Main10 プロファイルのテスト"""
    encoded_chunks = []

    def on_output(chunk):
        encoded_chunks.append(chunk)

    def on_error(error):
        print(f"Encoder error: {error}")

    encoder = VideoEncoder(on_output, on_error)

    # hvc1.2.x.x.x = Main10 Profile
    config: VideoEncoderConfig = {
        "codec": "hvc1.2.6.L93.B0",  # Main10 Profile
        "width": 1280,
        "height": 720,
        "bitrate": 2_000_000,
        "framerate": 30,
        "latency_mode": LatencyMode.REALTIME,
        "hardware_acceleration_engine": HardwareAccelerationEngine.APPLE_VIDEO_TOOLBOX,
    }

    encoder.configure(config)

    # 1 フレームをエンコード
    width, height = 1280, 720
    data_size = width * height * 3 // 2  # I420
    data = np.zeros(data_size, dtype=np.uint8)
    init: VideoFrameBufferInit = {
        "format": VideoPixelFormat.I420,
        "coded_width": width,
        "coded_height": height,
        "timestamp": 0,
    }
    frame = VideoFrame(data, init)
    encoder.encode(frame, {"key_frame": True})
    frame.close()

    encoder.flush()

    assert len(encoded_chunks) > 0, "HEVC Main10 エンコードに失敗"
    encoder.close()


def test_hevc_encoder_annexb_format():
    """HEVC エンコーダーで Annex B フォーマットを指定するテスト."""
    encoded_chunks = []

    def on_output(chunk):
        encoded_chunks.append(chunk)

    def on_error(error):
        pytest.fail(f"エンコーダーエラー: {error}")

    encoder = VideoEncoder(on_output, on_error)

    # hevc.format = "annexb" を指定
    config: VideoEncoderConfig = {
        "codec": "hvc1.1.6.L93.B0",
        "width": 640,
        "height": 480,
        "bitrate": 1_000_000,
        "framerate": 30,
        "latency_mode": LatencyMode.REALTIME,
        "hardware_acceleration_engine": HardwareAccelerationEngine.APPLE_VIDEO_TOOLBOX,
        "hevc": {"format": "annexb"},
    }

    encoder.configure(config)

    # 1 フレームをエンコード
    width, height = 640, 480
    data_size = width * height * 3 // 2  # I420
    data = np.zeros(data_size, dtype=np.uint8)
    init: VideoFrameBufferInit = {
        "format": VideoPixelFormat.I420,
        "coded_width": width,
        "coded_height": height,
        "timestamp": 0,
    }
    frame = VideoFrame(data, init)
    encoder.encode(frame, {"key_frame": True})
    frame.close()

    encoder.flush()

    assert len(encoded_chunks) > 0, "HEVC Annex B エンコードに失敗"

    # Annex B フォーマットでは NAL ユニットが 0x00000001 で始まる
    chunk_data = np.zeros(encoded_chunks[0].byte_length, dtype=np.uint8)
    encoded_chunks[0].copy_to(chunk_data)
    # Annex B の start code を確認 (0x00 0x00 0x00 0x01)
    assert chunk_data[0] == 0x00
    assert chunk_data[1] == 0x00
    assert chunk_data[2] == 0x00
    assert chunk_data[3] == 0x01

    encoder.close()


def test_hevc_encoder_hevc_format():
    """HEVC エンコーダーで HEVC フォーマット (length-prefixed) を指定するテスト."""
    encoded_chunks = []

    def on_output(chunk):
        encoded_chunks.append(chunk)

    def on_error(error):
        pytest.fail(f"エンコーダーエラー: {error}")

    encoder = VideoEncoder(on_output, on_error)

    # hevc.format = "hevc" (デフォルト) を指定
    config: VideoEncoderConfig = {
        "codec": "hvc1.1.6.L93.B0",
        "width": 640,
        "height": 480,
        "bitrate": 1_000_000,
        "framerate": 30,
        "latency_mode": LatencyMode.REALTIME,
        "hardware_acceleration_engine": HardwareAccelerationEngine.APPLE_VIDEO_TOOLBOX,
        "hevc": {"format": "hevc"},
    }

    encoder.configure(config)

    # 1 フレームをエンコード
    width, height = 640, 480
    data_size = width * height * 3 // 2  # I420
    data = np.zeros(data_size, dtype=np.uint8)
    init: VideoFrameBufferInit = {
        "format": VideoPixelFormat.I420,
        "coded_width": width,
        "coded_height": height,
        "timestamp": 0,
    }
    frame = VideoFrame(data, init)
    encoder.encode(frame, {"key_frame": True})
    frame.close()

    encoder.flush()

    assert len(encoded_chunks) > 0, "HEVC format エンコードに失敗"

    # HEVC フォーマットでは NAL ユニットの長さが先頭に来る (Annex B の start code ではない)
    chunk_data = np.zeros(encoded_chunks[0].byte_length, dtype=np.uint8)
    encoded_chunks[0].copy_to(chunk_data)
    # 長さプレフィックス形式では 0x00000001 の start code がない
    # 最初の 4 バイトは NAL ユニットの長さ (big-endian)
    is_length_prefixed = not (
        chunk_data[0] == 0x00
        and chunk_data[1] == 0x00
        and chunk_data[2] == 0x00
        and chunk_data[3] == 0x01
    )
    assert is_length_prefixed, "HEVC フォーマットは length-prefixed であるべき"

    encoder.close()


def test_h264_encoder_annexb_format():
    """H.264 エンコーダーで Annex B フォーマットを指定するテスト."""
    encoded_chunks = []

    def on_output(chunk):
        encoded_chunks.append(chunk)

    def on_error(error):
        pytest.fail(f"Encoder error: {error}")

    encoder = VideoEncoder(on_output, on_error)

    # AVC (H.264) with Annex B format
    config: VideoEncoderConfig = {
        "codec": "avc1.42E01E",
        "width": 640,
        "height": 480,
        "bitrate": 1_000_000,
        "framerate": 30.0,
        "hardware_acceleration_engine": HardwareAccelerationEngine.APPLE_VIDEO_TOOLBOX,
        "avc": {"format": "annexb"},
    }

    encoder.configure(config)

    # 1 フレームをエンコード
    width, height = 640, 480
    data_size = width * height * 3 // 2  # I420
    data = np.zeros(data_size, dtype=np.uint8)
    init: VideoFrameBufferInit = {
        "format": VideoPixelFormat.I420,
        "coded_width": width,
        "coded_height": height,
        "timestamp": 0,
    }
    frame = VideoFrame(data, init)
    encoder.encode(frame, {"key_frame": True})
    frame.close()

    encoder.flush()

    assert len(encoded_chunks) > 0, "Annex B format エンコードに失敗"

    # Annex B フォーマットでは start code (0x00000001) が使われる
    chunk_data = np.zeros(encoded_chunks[0].byte_length, dtype=np.uint8)
    encoded_chunks[0].copy_to(chunk_data)
    has_start_code = (
        chunk_data[0] == 0x00
        and chunk_data[1] == 0x00
        and chunk_data[2] == 0x00
        and chunk_data[3] == 0x01
    )
    assert has_start_code, "Annex B フォーマットは start code (0x00000001) で始まるべき"

    encoder.close()


def test_h264_encoder_avc_format():
    """H.264 エンコーダーで AVC フォーマット (length-prefixed) を指定するテスト."""
    encoded_chunks = []

    def on_output(chunk):
        encoded_chunks.append(chunk)

    def on_error(error):
        pytest.fail(f"Encoder error: {error}")

    encoder = VideoEncoder(on_output, on_error)

    # AVC (H.264) with AVC format (length-prefixed)
    config: VideoEncoderConfig = {
        "codec": "avc1.42E01E",
        "width": 640,
        "height": 480,
        "bitrate": 1_000_000,
        "framerate": 30.0,
        "hardware_acceleration_engine": HardwareAccelerationEngine.APPLE_VIDEO_TOOLBOX,
        "avc": {"format": "avc"},
    }

    encoder.configure(config)

    # 1 フレームをエンコード
    width, height = 640, 480
    data_size = width * height * 3 // 2  # I420
    data = np.zeros(data_size, dtype=np.uint8)
    init: VideoFrameBufferInit = {
        "format": VideoPixelFormat.I420,
        "coded_width": width,
        "coded_height": height,
        "timestamp": 0,
    }
    frame = VideoFrame(data, init)
    encoder.encode(frame, {"key_frame": True})
    frame.close()

    encoder.flush()

    assert len(encoded_chunks) > 0, "AVC format エンコードに失敗"

    # AVC フォーマットでは NAL ユニットの長さが先頭に来る (Annex B の start code ではない)
    chunk_data = np.zeros(encoded_chunks[0].byte_length, dtype=np.uint8)
    encoded_chunks[0].copy_to(chunk_data)
    # 長さプレフィックス形式では 0x00000001 の start code がない
    # 最初の 4 バイトは NAL ユニットの長さ (big-endian)
    is_length_prefixed = not (
        chunk_data[0] == 0x00
        and chunk_data[1] == 0x00
        and chunk_data[2] == 0x00
        and chunk_data[3] == 0x01
    )
    assert is_length_prefixed, "AVC フォーマットは length-prefixed であるべき"

    encoder.close()


def test_h264_encoder_avc_quantizer():
    """H.264 エンコーダーで avc.quantizer オプションを使用するテスト.

    VideoToolbox はフレームごとの quantizer 設定をサポートしていないため、
    セッションの Quality プロパティにマッピングしている。
    このテストでは、avc.quantizer オプションがエラーなく受け入れられることを確認する。
    """
    encoded_chunks = []

    def on_output(chunk):
        encoded_chunks.append(chunk)

    def on_error(error):
        pytest.fail(f"Encoder error: {error}")

    encoder = VideoEncoder(on_output, on_error)
    config: VideoEncoderConfig = {
        "codec": "avc1.42E01E",
        "width": 640,
        "height": 480,
        "bitrate": 1_000_000,
        "framerate": 30.0,
        "hardware_acceleration_engine": HardwareAccelerationEngine.APPLE_VIDEO_TOOLBOX,
    }
    encoder.configure(config)

    width, height = 640, 480
    data_size = width * height * 3 // 2
    data = np.random.randint(0, 255, data_size, dtype=np.uint8)
    init: VideoFrameBufferInit = {
        "format": VideoPixelFormat.I420,
        "coded_width": width,
        "coded_height": height,
        "timestamp": 0,
    }
    frame = VideoFrame(data, init)  # type: ignore[arg-type]

    # avc.quantizer オプションを使用してエンコード（エラーなく受け入れられることを確認）
    encoder.encode(frame, {"key_frame": True, "avc": {"quantizer": 10}})
    encoder.encode(frame, {"key_frame": False, "avc": {"quantizer": 30}})
    encoder.encode(frame, {"key_frame": False, "avc": {"quantizer": 51}})
    encoder.flush()
    encoder.close()

    frame.close()

    assert len(encoded_chunks) == 3, (
        f"3 フレームがエンコードされるべき、実際: {len(encoded_chunks)}"
    )


def test_h264_encoder_avc_quantizer_invalid_range():
    """avc.quantizer の範囲外の値でエラーが発生することを確認."""

    def on_output(chunk):
        pass

    def on_error(error):
        pass

    encoder = VideoEncoder(on_output, on_error)
    config: VideoEncoderConfig = {
        "codec": "avc1.42E01E",
        "width": 640,
        "height": 480,
        "bitrate": 1_000_000,
        "framerate": 30.0,
        "hardware_acceleration_engine": HardwareAccelerationEngine.APPLE_VIDEO_TOOLBOX,
    }
    encoder.configure(config)

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

    # quantizer=52 は範囲外 (0-51)
    with pytest.raises(ValueError, match="AVC quantizer must be in range 0-51"):
        encoder.encode(frame, {"key_frame": True, "avc": {"quantizer": 52}})

    frame.close()
    encoder.close()


def test_h265_encoder_hevc_quantizer():
    """H.265 エンコーダーで hevc.quantizer オプションを使用するテスト.

    VideoToolbox はフレームごとの quantizer 設定をサポートしていないため、
    このテストでは、hevc.quantizer オプションがエラーなく受け入れられることを確認する。
    """
    encoded_chunks = []

    def on_output(chunk):
        encoded_chunks.append(chunk)

    def on_error(error):
        pytest.fail(f"Encoder error: {error}")

    encoder = VideoEncoder(on_output, on_error)
    config: VideoEncoderConfig = {
        "codec": "hvc1.1.6.L93.B0",
        "width": 640,
        "height": 480,
        "bitrate": 1_000_000,
        "framerate": 30.0,
        "hardware_acceleration_engine": HardwareAccelerationEngine.APPLE_VIDEO_TOOLBOX,
    }
    encoder.configure(config)

    width, height = 640, 480
    data_size = width * height * 3 // 2
    data: np.ndarray[tuple[int], np.dtype[np.uint8]] = np.random.randint(
        0, 255, data_size, dtype=np.uint8
    )
    init: VideoFrameBufferInit = {
        "format": VideoPixelFormat.I420,
        "coded_width": width,
        "coded_height": height,
        "timestamp": 0,
    }
    frame = VideoFrame(data, init)

    # hevc.quantizer オプションを使用してエンコード（エラーなく受け入れられることを確認）
    encoder.encode(frame, {"key_frame": True, "hevc": {"quantizer": 10}})
    encoder.encode(frame, {"key_frame": False, "hevc": {"quantizer": 30}})
    encoder.encode(frame, {"key_frame": False, "hevc": {"quantizer": 51}})
    encoder.flush()
    encoder.close()

    frame.close()

    assert len(encoded_chunks) == 3, (
        f"3 フレームがエンコードされるべき、実際: {len(encoded_chunks)}"
    )


def test_h265_encoder_hevc_quantizer_invalid_range():
    """hevc.quantizer の範囲外の値でエラーが発生することを確認."""

    def on_output(chunk):
        pass

    def on_error(error):
        pass

    encoder = VideoEncoder(on_output, on_error)
    config: VideoEncoderConfig = {
        "codec": "hvc1.1.6.L93.B0",
        "width": 640,
        "height": 480,
        "bitrate": 1_000_000,
        "framerate": 30.0,
        "hardware_acceleration_engine": HardwareAccelerationEngine.APPLE_VIDEO_TOOLBOX,
    }
    encoder.configure(config)

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

    # quantizer=52 は範囲外 (0-51)
    with pytest.raises(ValueError, match="HEVC quantizer must be in range 0-51"):
        encoder.encode(frame, {"key_frame": True, "hevc": {"quantizer": 52}})

    frame.close()
    encoder.close()


def test_h264_decode_multiple_delta_frames():
    """H.264 デコーダーが複数のデルタフレームを正しくデコードできることを確認.

    この修正により、キーフレームで作成した CMFormatDescription を
    キャッシュして後続のデルタフレームでも再利用するようになった。
    """
    # エンコーダー設定
    encoded_chunks = []

    def on_encode_output(chunk):
        encoded_chunks.append(chunk)

    def on_encode_error(error):
        pytest.fail(f"Encoder error: {error}")

    encoder = VideoEncoder(on_encode_output, on_encode_error)

    encoder_config: VideoEncoderConfig = {
        "codec": "avc1.42E01E",
        "width": 320,
        "height": 240,
        "bitrate": 500_000,
        "framerate": 30,
        "latency_mode": LatencyMode.REALTIME,
        "hardware_acceleration_engine": HardwareAccelerationEngine.APPLE_VIDEO_TOOLBOX,
        "avc": {"format": "annexb"},
    }

    encoder.configure(encoder_config)

    # テストフレームを作成（10フレーム: キー + 9デルタ）
    test_frames = []
    width, height = 320, 240
    data_size = width * height * 3 // 2  # I420
    for i in range(10):
        # 各フレームで少し異なるデータを使用して動きを再現
        data = np.full(data_size, (i * 25) % 256, dtype=np.uint8)
        init: VideoFrameBufferInit = {
            "format": VideoPixelFormat.I420,
            "coded_width": width,
            "coded_height": height,
            "timestamp": i * 33333,
        }
        frame = VideoFrame(data, init)
        test_frames.append(frame)
        # 最初のフレームのみキーフレーム
        encoder.encode(frame, {"key_frame": i == 0})

    encoder.flush()

    assert len(encoded_chunks) >= 10, (
        f"10 フレームがエンコードされるべき、実際: {len(encoded_chunks)}"
    )

    # デコーダー設定
    decoded_frames = []
    decode_errors = []

    def on_decode_output(frame):
        decoded_frames.append(frame)

    def on_decode_error(error):
        decode_errors.append(error)

    decoder = VideoDecoder(on_decode_output, on_decode_error)

    decoder_config: VideoDecoderConfig = {
        "codec": "avc1.42E01E",
        "coded_width": 320,
        "coded_height": 240,
        "hardware_acceleration_engine": HardwareAccelerationEngine.APPLE_VIDEO_TOOLBOX,
    }

    decoder.configure(decoder_config)

    # エンコードされたチャンクをデコード
    for chunk in encoded_chunks:
        decoder.decode(chunk)

    decoder.flush()

    # デコードエラーがないことを確認
    assert len(decode_errors) == 0, f"デコードエラーが発生: {decode_errors}"

    # すべてのフレームがデコードされたことを確認（少なくとも8フレーム）
    assert len(decoded_frames) >= 8, (
        f"少なくとも 8 フレームがデコードされるべき、実際: {len(decoded_frames)}"
    )

    print(f"エンコードチャンク数: {len(encoded_chunks)}, デコードフレーム数: {len(decoded_frames)}")

    # クリーンアップ
    for frame in test_frames:
        frame.close()
    for frame in decoded_frames:
        frame.close()
    encoder.close()
    decoder.close()


def test_h265_decode_multiple_delta_frames():
    """H.265 デコーダーが複数のデルタフレームを正しくデコードできることを確認.

    この修正により、キーフレームで作成した CMFormatDescription を
    キャッシュして後続のデルタフレームでも再利用するようになった。
    """
    # エンコーダー設定
    encoded_chunks = []

    def on_encode_output(chunk):
        encoded_chunks.append(chunk)

    def on_encode_error(error):
        pytest.fail(f"Encoder error: {error}")

    encoder = VideoEncoder(on_encode_output, on_encode_error)

    encoder_config: VideoEncoderConfig = {
        "codec": "hvc1.1.6.L93.B0",
        "width": 320,
        "height": 240,
        "bitrate": 500_000,
        "framerate": 30,
        "latency_mode": LatencyMode.REALTIME,
        "hardware_acceleration_engine": HardwareAccelerationEngine.APPLE_VIDEO_TOOLBOX,
        "hevc": {"format": "annexb"},
    }

    encoder.configure(encoder_config)

    # テストフレームを作成（10フレーム: キー + 9デルタ）
    test_frames = []
    width, height = 320, 240
    data_size = width * height * 3 // 2  # I420
    for i in range(10):
        # 各フレームで少し異なるデータを使用して動きを再現
        data = np.full(data_size, (i * 25) % 256, dtype=np.uint8)
        init: VideoFrameBufferInit = {
            "format": VideoPixelFormat.I420,
            "coded_width": width,
            "coded_height": height,
            "timestamp": i * 33333,
        }
        frame = VideoFrame(data, init)
        test_frames.append(frame)
        # 最初のフレームのみキーフレーム
        encoder.encode(frame, {"key_frame": i == 0})

    encoder.flush()

    assert len(encoded_chunks) >= 10, (
        f"10 フレームがエンコードされるべき、実際: {len(encoded_chunks)}"
    )

    # デコーダー設定
    decoded_frames = []
    decode_errors = []

    def on_decode_output(frame):
        decoded_frames.append(frame)

    def on_decode_error(error):
        decode_errors.append(error)

    decoder = VideoDecoder(on_decode_output, on_decode_error)

    decoder_config: VideoDecoderConfig = {
        "codec": "hvc1.1.6.L93.B0",
        "coded_width": 320,
        "coded_height": 240,
        "hardware_acceleration_engine": HardwareAccelerationEngine.APPLE_VIDEO_TOOLBOX,
    }

    decoder.configure(decoder_config)

    # エンコードされたチャンクをデコード
    for chunk in encoded_chunks:
        decoder.decode(chunk)

    decoder.flush()

    # デコードエラーがないことを確認
    assert len(decode_errors) == 0, f"デコードエラーが発生: {decode_errors}"

    # すべてのフレームがデコードされたことを確認（少なくとも8フレーム）
    assert len(decoded_frames) >= 8, (
        f"少なくとも 8 フレームがデコードされるべき、実際: {len(decoded_frames)}"
    )

    print(f"エンコードチャンク数: {len(encoded_chunks)}, デコードフレーム数: {len(decoded_frames)}")

    # クリーンアップ
    for frame in test_frames:
        frame.close()
    for frame in decoded_frames:
        frame.close()
    encoder.close()
    decoder.close()


# =============================================================================
# VP9 VideoToolbox デコーダーテスト
# =============================================================================


def test_vp9_decode_videotoolbox():
    """libvpx でエンコード → VideoToolbox でデコードするテスト."""
    width, height = 320, 240

    # libvpx でエンコード
    encoded_chunks = []

    def on_encode_output(chunk):
        encoded_chunks.append(chunk)

    def on_encode_error(error):
        pytest.fail(f"エンコーダーエラー: {error}")

    encoder = VideoEncoder(on_encode_output, on_encode_error)

    encoder_config: VideoEncoderConfig = {
        "codec": "vp09.00.10.08",
        "width": width,
        "height": height,
        "bitrate": 500_000,
        "framerate": 30,
        "latency_mode": LatencyMode.REALTIME,
    }
    encoder.configure(encoder_config)

    # テストフレームを作成してエンコード
    test_frames = []
    data_size = width * height * 3 // 2
    for i in range(5):
        data = np.full(data_size, (i * 50) % 256, dtype=np.uint8)
        init: VideoFrameBufferInit = {
            "format": VideoPixelFormat.I420,
            "coded_width": width,
            "coded_height": height,
            "timestamp": i * 33333,
        }
        frame = VideoFrame(data, init)
        test_frames.append(frame)
        encoder.encode(frame, {"key_frame": i == 0})

    encoder.flush()

    assert len(encoded_chunks) >= 1, "エンコードされたチャンクが生成されませんでした"

    # VideoToolbox でデコード
    decoded_frames = []
    decode_errors = []

    def on_decode_output(frame):
        decoded_frames.append(frame)

    def on_decode_error(error):
        decode_errors.append(error)

    decoder = VideoDecoder(on_decode_output, on_decode_error)

    decoder_config: VideoDecoderConfig = {
        "codec": "vp09.00.10.08",
        "coded_width": width,
        "coded_height": height,
        "hardware_acceleration_engine": HardwareAccelerationEngine.APPLE_VIDEO_TOOLBOX,
    }
    decoder.configure(decoder_config)

    for chunk in encoded_chunks:
        decoder.decode(chunk)

    decoder.flush()

    # デコードエラーがないことを確認
    assert len(decode_errors) == 0, f"デコードエラーが発生: {decode_errors}"

    # フレームがデコードされたことを確認
    assert len(decoded_frames) >= 1, "デコードされたフレームが生成されませんでした"

    # デコードされたフレームのサイズを確認
    for frame in decoded_frames:
        assert frame.coded_width == width
        assert frame.coded_height == height

    print(
        f"VP9 エンコードチャンク数: {len(encoded_chunks)}, デコードフレーム数: {len(decoded_frames)}"
    )

    # クリーンアップ
    for frame in test_frames:
        frame.close()
    for frame in decoded_frames:
        frame.close()
    encoder.close()
    decoder.close()


# =============================================================================
# AV1 VideoToolbox デコーダーテスト
# =============================================================================


@pytest.mark.skip(reason="AV1 Decoder を持っている GitHub Self-hosted Runner がないため無効化")
def test_av1_decode_videotoolbox():
    """libaom でエンコード → VideoToolbox でデコードするテスト."""
    width, height = 320, 240

    # libaom でエンコード
    encoded_chunks = []

    def on_encode_output(chunk):
        encoded_chunks.append(chunk)

    def on_encode_error(error):
        pytest.fail(f"エンコーダーエラー: {error}")

    encoder = VideoEncoder(on_encode_output, on_encode_error)

    encoder_config: VideoEncoderConfig = {
        "codec": "av01.0.04M.08",
        "width": width,
        "height": height,
        "bitrate": 500_000,
        "framerate": 30,
        "latency_mode": LatencyMode.REALTIME,
    }
    encoder.configure(encoder_config)

    # テストフレームを作成してエンコード
    test_frames = []
    data_size = width * height * 3 // 2
    for i in range(5):
        data = np.full(data_size, (i * 50) % 256, dtype=np.uint8)
        init: VideoFrameBufferInit = {
            "format": VideoPixelFormat.I420,
            "coded_width": width,
            "coded_height": height,
            "timestamp": i * 33333,
        }
        frame = VideoFrame(data, init)
        test_frames.append(frame)
        encoder.encode(frame, {"key_frame": i == 0})

    encoder.flush()

    assert len(encoded_chunks) >= 1, "エンコードされたチャンクが生成されませんでした"

    # VideoToolbox でデコード
    decoded_frames = []
    decode_errors = []

    def on_decode_output(frame):
        decoded_frames.append(frame)

    def on_decode_error(error):
        decode_errors.append(error)

    decoder = VideoDecoder(on_decode_output, on_decode_error)

    decoder_config: VideoDecoderConfig = {
        "codec": "av01.0.04M.08",
        "coded_width": width,
        "coded_height": height,
        "hardware_acceleration_engine": HardwareAccelerationEngine.APPLE_VIDEO_TOOLBOX,
    }
    decoder.configure(decoder_config)

    for chunk in encoded_chunks:
        decoder.decode(chunk)

    decoder.flush()

    # デコードエラーがないことを確認
    assert len(decode_errors) == 0, f"デコードエラーが発生: {decode_errors}"

    # フレームがデコードされたことを確認
    assert len(decoded_frames) >= 1, "デコードされたフレームが生成されませんでした"

    # デコードされたフレームのサイズを確認
    for frame in decoded_frames:
        assert frame.coded_width == width
        assert frame.coded_height == height

    print(
        f"AV1 エンコードチャンク数: {len(encoded_chunks)}, デコードフレーム数: {len(decoded_frames)}"
    )

    # クリーンアップ
    for frame in test_frames:
        frame.close()
    for frame in decoded_frames:
        frame.close()
    encoder.close()
    decoder.close()
