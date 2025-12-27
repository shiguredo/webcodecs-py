"""Property-Based Testing による encode/decode のラウンドトリップテスト"""

import platform

import numpy as np
import pytest
from hypothesis import given, settings, HealthCheck
from hypothesis import strategies as st
from hypothesis.extra.numpy import arrays

from webcodecs import (
    AudioData,
    AudioDataInit,
    AudioDecoder,
    AudioDecoderConfig,
    AudioEncoder,
    AudioEncoderConfig,
    AudioSampleFormat,
    CodecState,
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


# =============================================================================
# Opus Audio PBT
# =============================================================================


# Opus がサポートするサンプルレート
OPUS_SAMPLE_RATES = [8000, 12000, 16000, 24000, 48000]

# Opus がサポートするチャンネル数
OPUS_CHANNELS = [1, 2]

# Opus のビットレート範囲
OPUS_BITRATE_MIN = 6000
OPUS_BITRATE_MAX = 256000


@st.composite
def opus_config_strategy(draw):
    """Opus エンコーダ設定を生成する hypothesis ストラテジ"""
    sample_rate = draw(st.sampled_from(OPUS_SAMPLE_RATES))
    number_of_channels = draw(st.sampled_from(OPUS_CHANNELS))
    bitrate = draw(st.integers(min_value=OPUS_BITRATE_MIN, max_value=OPUS_BITRATE_MAX))
    return {
        "sample_rate": sample_rate,
        "number_of_channels": number_of_channels,
        "bitrate": bitrate,
    }


@st.composite
def audio_samples_strategy(draw, sample_rate: int, number_of_channels: int):
    """オーディオサンプルを生成する hypothesis ストラテジ

    Opus の 20ms フレーム (960 サンプル @ 48kHz) に対応するサンプル数を生成
    """
    # 20ms に相当するサンプル数
    frame_size = sample_rate // 50
    # 1-5 フレーム分のデータを生成
    num_frames = draw(st.integers(min_value=1, max_value=5))
    total_samples = frame_size * num_frames

    # ランダムなオーディオデータを生成 (-0.9 から 0.9)
    samples = draw(
        arrays(
            dtype=np.float32,
            shape=(total_samples, number_of_channels),
            elements=st.floats(min_value=-0.9, max_value=0.9, allow_nan=False, allow_infinity=False),
        )
    )
    return samples


@given(config=opus_config_strategy())
@settings(
    max_examples=20,
    deadline=None,
    suppress_health_check=[HealthCheck.too_slow],
)
def prop_opus_encode_decode_roundtrip(config):
    """Opus エンコード・デコードのラウンドトリップ PBT"""
    sample_rate = config["sample_rate"]
    number_of_channels = config["number_of_channels"]
    bitrate = config["bitrate"]

    # 20ms フレームサイズ
    frame_size = sample_rate // 50

    # 3 フレーム分のサイン波を生成
    duration = 3 * frame_size / sample_rate
    t = np.arange(int(sample_rate * duration)) / sample_rate
    frequency = 440.0

    if number_of_channels == 1:
        samples = (0.5 * np.sin(2 * np.pi * frequency * t)).astype(np.float32)
        samples = samples.reshape(-1, 1)
    else:
        left = (0.5 * np.sin(2 * np.pi * frequency * t)).astype(np.float32)
        right = (0.5 * np.sin(2 * np.pi * frequency * 2 * t)).astype(np.float32)
        samples = np.column_stack((left, right))

    # エンコーダを作成
    encoded_chunks = []
    encoder_error = None

    def on_encoder_output(chunk):
        encoded_chunks.append(chunk)

    def on_encoder_error(error):
        nonlocal encoder_error
        encoder_error = error

    encoder = AudioEncoder(on_encoder_output, on_encoder_error)
    encoder_config: AudioEncoderConfig = {
        "codec": "opus",
        "sample_rate": sample_rate,
        "number_of_channels": number_of_channels,
        "bitrate": bitrate,
    }
    encoder.configure(encoder_config)
    assert encoder.state == CodecState.CONFIGURED

    # フレームごとにエンコード
    num_frames_to_encode = len(samples) // frame_size
    for i in range(num_frames_to_encode):
        start = i * frame_size
        end = start + frame_size
        frame_samples = samples[start:end]

        timestamp = (i * frame_size * 1_000_000) // sample_rate
        init: AudioDataInit = {
            "format": AudioSampleFormat.F32,
            "sample_rate": sample_rate,
            "number_of_frames": frame_size,
            "number_of_channels": number_of_channels,
            "timestamp": timestamp,
            "data": frame_samples,
        }
        audio_data = AudioData(init)
        encoder.encode(audio_data)
        audio_data.close()

    encoder.flush()

    # エンコードが成功したことを確認
    assert encoder_error is None, f"エンコーダエラー: {encoder_error}"
    assert len(encoded_chunks) > 0, "エンコードされたチャンクがありません"
    for chunk in encoded_chunks:
        assert chunk.byte_length > 0

    # デコーダを作成
    decoded_outputs = []
    decoder_error = None

    def on_decoder_output(audio):
        decoded_outputs.append(audio)

    def on_decoder_error(error):
        nonlocal decoder_error
        decoder_error = error

    decoder = AudioDecoder(on_decoder_output, on_decoder_error)
    decoder_config: AudioDecoderConfig = {
        "codec": "opus",
        "sample_rate": sample_rate,
        "number_of_channels": number_of_channels,
    }
    decoder.configure(decoder_config)
    assert decoder.state == CodecState.CONFIGURED

    # デコード
    for chunk in encoded_chunks:
        decoder.decode(chunk)
    decoder.flush()

    # デコードが成功したことを確認
    assert decoder_error is None, f"デコーダエラー: {decoder_error}"
    assert len(decoded_outputs) > 0, "デコードされた出力がありません"

    for audio in decoded_outputs:
        assert audio.number_of_frames > 0
        assert audio.sample_rate == sample_rate
        assert audio.number_of_channels == number_of_channels

    # クリーンアップ
    for audio in decoded_outputs:
        audio.close()
    encoder.close()
    decoder.close()


# =============================================================================
# VP8 Video PBT
# =============================================================================


# VP8 がサポートする解像度範囲
VP8_WIDTH_MIN = 32
VP8_WIDTH_MAX = 640
VP8_HEIGHT_MIN = 32
VP8_HEIGHT_MAX = 480


@st.composite
def vp8_config_strategy(draw):
    """VP8 エンコーダ設定を生成する hypothesis ストラテジ"""
    # 解像度は 2 の倍数に丸める
    width = draw(st.integers(min_value=VP8_WIDTH_MIN // 2, max_value=VP8_WIDTH_MAX // 2)) * 2
    height = draw(st.integers(min_value=VP8_HEIGHT_MIN // 2, max_value=VP8_HEIGHT_MAX // 2)) * 2
    bitrate = draw(st.integers(min_value=100_000, max_value=2_000_000))
    framerate = draw(st.floats(min_value=15.0, max_value=60.0, allow_nan=False, allow_infinity=False))

    return {
        "width": width,
        "height": height,
        "bitrate": bitrate,
        "framerate": framerate,
    }


@st.composite
def video_frame_data_strategy(draw, width: int, height: int):
    """VideoFrame 用のピクセルデータを生成する hypothesis ストラテジ"""
    # I420 フォーマット: Y + U + V
    y_size = width * height
    uv_size = (width // 2) * (height // 2)
    data_size = y_size + 2 * uv_size

    # ランダムなピクセルデータを生成
    data = draw(
        arrays(
            dtype=np.uint8,
            shape=(data_size,),
            elements=st.integers(min_value=0, max_value=255),
        )
    )
    return data


def make_test_video_frame(
    width: int, height: int, frame_num: int = 0, data: np.ndarray | None = None
) -> VideoFrame:
    """テスト用の VideoFrame を作成する"""
    if data is None:
        # I420 フォーマット: Y + U + V
        y_size = width * height
        uv_size = (width // 2) * (height // 2)
        data_size = y_size + 2 * uv_size

        # フレーム番号に基づいてパターンを変える
        data = np.zeros(data_size, dtype=np.uint8)
        # Y プレーン: グラデーションパターン
        # オーバーフローを避けるため int32 で計算してから uint8 に変換
        y_data = (np.arange(y_size, dtype=np.int32) + frame_num * 10) % 256
        data[:y_size] = y_data.astype(np.uint8)
        # U, V プレーン: 中間値
        data[y_size:] = 128

    init: VideoFrameBufferInit = {
        "format": VideoPixelFormat.I420,
        "coded_width": width,
        "coded_height": height,
        "timestamp": frame_num * 33333,  # ~30fps
    }
    return VideoFrame(data, init)


# macOS / Linux のみ VP8 をサポート
@pytest.mark.skipif(
    platform.system() not in ("Darwin", "Linux"),
    reason="VP8 は macOS / Linux のみサポート",
)
@given(config=vp8_config_strategy())
@settings(
    max_examples=10,
    deadline=None,
    suppress_health_check=[HealthCheck.too_slow],
)
def prop_vp8_encode_decode_roundtrip(config):
    """VP8 エンコード・デコードのラウンドトリップ PBT"""
    width = config["width"]
    height = config["height"]
    bitrate = config["bitrate"]
    framerate = config["framerate"]

    # エンコーダを作成
    encoded_chunks = []
    encoder_error = None

    def on_encoder_output(chunk):
        encoded_chunks.append(chunk)

    def on_encoder_error(error):
        nonlocal encoder_error
        encoder_error = error

    encoder = VideoEncoder(on_encoder_output, on_encoder_error)
    encoder_config: VideoEncoderConfig = {
        "codec": "vp8",
        "width": width,
        "height": height,
        "bitrate": bitrate,
        "framerate": framerate,
        "latency_mode": LatencyMode.REALTIME,
    }
    encoder.configure(encoder_config)
    assert encoder.state == CodecState.CONFIGURED

    # 3 フレームをエンコード
    num_frames = 3
    for i in range(num_frames):
        frame = make_test_video_frame(width, height, i)
        encoder.encode(frame, {"key_frame": (i == 0)})
        frame.close()

    encoder.flush()

    # エンコードが成功したことを確認
    assert encoder_error is None, f"エンコーダエラー: {encoder_error}"
    assert len(encoded_chunks) >= 1, "エンコードされたチャンクがありません"

    for chunk in encoded_chunks:
        assert chunk.byte_length > 0
        assert chunk.type in (EncodedVideoChunkType.KEY, EncodedVideoChunkType.DELTA)

    # デコーダを作成
    decoded_frames = []
    decoder_error = None

    def on_decoder_output(frame):
        decoded_frames.append(frame)

    def on_decoder_error(error):
        nonlocal decoder_error
        decoder_error = error

    decoder = VideoDecoder(on_decoder_output, on_decoder_error)
    decoder_config: VideoDecoderConfig = {"codec": "vp8"}
    decoder.configure(decoder_config)
    assert decoder.state == CodecState.CONFIGURED

    # デコード
    for chunk in encoded_chunks:
        decoder.decode(chunk)
    decoder.flush()

    # デコードが成功したことを確認
    assert decoder_error is None, f"デコーダエラー: {decoder_error}"
    assert len(decoded_frames) >= 1, "デコードされたフレームがありません"

    for frame in decoded_frames:
        assert frame.coded_width == width
        assert frame.coded_height == height

    # クリーンアップ
    for frame in decoded_frames:
        frame.close()
    encoder.close()
    decoder.close()
