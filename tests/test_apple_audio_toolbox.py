"""
Apple Audio Toolbox AAC エンコーダー/デコーダーのテスト
"""

import os
import platform

import numpy as np
import pytest

from webcodecs import (
    AudioData,
    AudioDecoder,
    AudioDecoderConfig,
    AudioEncoder,
    AudioEncoderConfig,
    AudioSampleFormat,
    CodecState,
    EncodedAudioChunk,
)

# macOS 限定かつ APPLE_VIDEO_TOOLBOX 環境変数が設定されている場合のみ実行
pytestmark = pytest.mark.skipif(
    os.environ.get("APPLE_VIDEO_TOOLBOX") is None or platform.system() != "Darwin",
    reason="AAC は macOS のみでサポート、APPLE_VIDEO_TOOLBOX 環境変数が必要",
)


def test_aac_encoder_basic_config_supported() -> None:
    """基本的な AAC 設定がサポートされることを確認"""
    config: AudioEncoderConfig = {
        "codec": "mp4a.40.2",
        "sample_rate": 48000,
        "number_of_channels": 2,
    }
    result = AudioEncoder.is_config_supported(config)
    assert result["supported"] is True


def test_aac_encoder_alias_codec_supported() -> None:
    """aac コーデック名がサポートされることを確認"""
    config: AudioEncoderConfig = {
        "codec": "aac",
        "sample_rate": 44100,
        "number_of_channels": 2,
    }
    result = AudioEncoder.is_config_supported(config)
    assert result["supported"] is True


def test_aac_encoder_mono_supported() -> None:
    """モノラル AAC がサポートされることを確認"""
    config: AudioEncoderConfig = {
        "codec": "mp4a.40.2",
        "sample_rate": 48000,
        "number_of_channels": 1,
    }
    result = AudioEncoder.is_config_supported(config)
    assert result["supported"] is True


def test_aac_encoder_various_sample_rates() -> None:
    """様々なサンプルレートがサポートされることを確認"""
    sample_rates = [8000, 16000, 22050, 32000, 44100, 48000, 96000]
    for rate in sample_rates:
        config: AudioEncoderConfig = {
            "codec": "mp4a.40.2",
            "sample_rate": rate,
            "number_of_channels": 2,
        }
        result = AudioEncoder.is_config_supported(config)
        assert result["supported"] is True, f"Sample rate {rate} should be supported"


def test_aac_encoder_codec_strings() -> None:
    """様々な AAC コーデック文字列がサポートされることを確認"""
    codec_strings = [
        "mp4a.40.2",  # MPEG-4 AAC LC
        "mp4a.40.02",  # MPEG-4 AAC LC (leading 0)
        "mp4a.67",  # MPEG-2 AAC LC
        "aac",  # 簡略表記
    ]
    for codec in codec_strings:
        config: AudioEncoderConfig = {
            "codec": codec,
            "sample_rate": 48000,
            "number_of_channels": 2,
        }
        result = AudioEncoder.is_config_supported(config)
        assert result["supported"] is True, f"Codec {codec} should be supported"


def test_aac_decoder_codec_strings() -> None:
    """様々な AAC コーデック文字列がサポートされることを確認"""
    codec_strings = [
        "mp4a.40.2",  # MPEG-4 AAC LC
        "mp4a.40.02",  # MPEG-4 AAC LC (leading 0)
        "mp4a.67",  # MPEG-2 AAC LC
        "aac",  # 簡略表記
    ]
    for codec in codec_strings:
        config: AudioDecoderConfig = {
            "codec": codec,
            "sample_rate": 48000,
            "number_of_channels": 2,
        }
        result = AudioDecoder.is_config_supported(config)
        assert result["supported"] is True, f"Codec {codec} should be supported"


def test_aac_decoder_basic_config_supported() -> None:
    """基本的な AAC 設定がサポートされることを確認"""
    config: AudioDecoderConfig = {
        "codec": "mp4a.40.2",
        "sample_rate": 48000,
        "number_of_channels": 2,
    }
    result = AudioDecoder.is_config_supported(config)
    assert result["supported"] is True


def test_aac_decoder_alias_codec_supported() -> None:
    """aac コーデック名がサポートされることを確認"""
    config: AudioDecoderConfig = {
        "codec": "aac",
        "sample_rate": 44100,
        "number_of_channels": 2,
    }
    result = AudioDecoder.is_config_supported(config)
    assert result["supported"] is True


def test_aac_encoder_configure() -> None:
    """エンコーダーの設定が正しく行えることを確認"""
    encoded_chunks: list[EncodedAudioChunk] = []

    def output_callback(chunk: EncodedAudioChunk) -> None:
        encoded_chunks.append(chunk)

    def error_callback(error: str) -> None:
        pytest.fail(f"Encoder error: {error}")

    encoder = AudioEncoder(output_callback, error_callback)
    assert encoder.state == CodecState.UNCONFIGURED

    encoder.configure(
        {
            "codec": "mp4a.40.2",
            "sample_rate": 48000,
            "number_of_channels": 2,
            "bitrate": 128000,
        }
    )
    assert encoder.state == CodecState.CONFIGURED

    encoder.close()
    assert encoder.state == CodecState.CLOSED


def test_aac_encoder_encode_single_frame() -> None:
    """単一フレームのエンコードが正しく行えることを確認"""
    encoded_chunks: list[EncodedAudioChunk] = []

    def output_callback(chunk: EncodedAudioChunk) -> None:
        encoded_chunks.append(chunk)

    def error_callback(error: str) -> None:
        pytest.fail(f"Encoder error: {error}")

    encoder = AudioEncoder(output_callback, error_callback)
    encoder.configure(
        {
            "codec": "mp4a.40.2",
            "sample_rate": 48000,
            "number_of_channels": 2,
            "bitrate": 128000,
        }
    )

    # テスト用のオーディオデータを作成
    # AAC は 1024 サンプル/フレームなので、十分なサンプルを提供
    sample_rate = 48000
    channels = 2
    duration_seconds = 0.1
    num_samples = int(sample_rate * duration_seconds)

    # サイン波を生成
    t = np.linspace(0, duration_seconds, num_samples, dtype=np.float32)
    frequency = 440.0
    audio_data = np.sin(2 * np.pi * frequency * t).astype(np.float32)

    # ステレオに変換 (frames, channels)
    stereo_data = np.column_stack([audio_data, audio_data])

    audio = AudioData(
        {
            "format": AudioSampleFormat.F32,
            "sample_rate": sample_rate,
            "number_of_channels": channels,
            "number_of_frames": num_samples,
            "timestamp": 0,
            "data": stereo_data,
        }
    )

    encoder.encode(audio)
    encoder.flush()

    # エンコードされたチャンクが生成されることを確認
    assert len(encoded_chunks) > 0
    for chunk in encoded_chunks:
        assert chunk.byte_length > 0

    encoder.close()


def test_aac_encoder_encode_multiple_frames() -> None:
    """複数フレームのエンコードが正しく行えることを確認"""
    encoded_chunks: list[EncodedAudioChunk] = []

    def output_callback(chunk: EncodedAudioChunk) -> None:
        encoded_chunks.append(chunk)

    def error_callback(error: str) -> None:
        pytest.fail(f"Encoder error: {error}")

    encoder = AudioEncoder(output_callback, error_callback)
    encoder.configure(
        {
            "codec": "mp4a.40.2",
            "sample_rate": 48000,
            "number_of_channels": 2,
            "bitrate": 128000,
        }
    )

    sample_rate = 48000
    channels = 2

    for i in range(5):
        duration_seconds = 0.05
        num_samples = int(sample_rate * duration_seconds)

        t = np.linspace(0, duration_seconds, num_samples, dtype=np.float32)
        frequency = 440.0 + i * 100
        audio_data = np.sin(2 * np.pi * frequency * t).astype(np.float32)

        stereo_data = np.column_stack([audio_data, audio_data])

        audio = AudioData(
            {
                "format": AudioSampleFormat.F32,
                "sample_rate": sample_rate,
                "number_of_channels": channels,
                "number_of_frames": num_samples,
                "timestamp": int(i * duration_seconds * 1_000_000),
                "data": stereo_data,
            }
        )

        encoder.encode(audio)

    encoder.flush()

    # 複数のエンコードされたチャンクが生成されることを確認
    assert len(encoded_chunks) > 0

    encoder.close()


def test_aac_encode_decode_roundtrip() -> None:
    """エンコード -> デコードのラウンドトリップテスト"""
    encoded_chunks: list[EncodedAudioChunk] = []
    decoded_frames: list[AudioData] = []

    def encoder_output(chunk: EncodedAudioChunk) -> None:
        encoded_chunks.append(chunk)

    def decoder_output(audio: AudioData) -> None:
        decoded_frames.append(audio)

    def error_callback(error: str) -> None:
        pytest.fail(f"Codec error: {error}")

    sample_rate = 48000
    channels = 2

    # エンコーダーを設定
    encoder = AudioEncoder(encoder_output, error_callback)
    encoder.configure(
        {
            "codec": "mp4a.40.2",
            "sample_rate": sample_rate,
            "number_of_channels": channels,
            "bitrate": 128000,
        }
    )

    # テスト用のオーディオデータを作成
    duration_seconds = 0.2
    num_samples = int(sample_rate * duration_seconds)

    t = np.linspace(0, duration_seconds, num_samples, dtype=np.float32)
    frequency = 440.0
    audio_data = np.sin(2 * np.pi * frequency * t).astype(np.float32)
    stereo_data = np.column_stack([audio_data, audio_data])

    audio = AudioData(
        {
            "format": AudioSampleFormat.F32,
            "sample_rate": sample_rate,
            "number_of_channels": channels,
            "number_of_frames": num_samples,
            "timestamp": 0,
            "data": stereo_data,
        }
    )

    encoder.encode(audio)
    encoder.flush()

    assert len(encoded_chunks) > 0

    # デコーダーを設定
    decoder = AudioDecoder(decoder_output, error_callback)
    decoder.configure(
        {
            "codec": "mp4a.40.2",
            "sample_rate": sample_rate,
            "number_of_channels": channels,
        }
    )

    # エンコードされたチャンクをデコード
    for chunk in encoded_chunks:
        decoder.decode(chunk)

    decoder.flush()

    # デコードされたフレームが生成されることを確認
    assert len(decoded_frames) > 0

    # デコードされたフレームの基本的な検証
    for frame in decoded_frames:
        assert frame.sample_rate == sample_rate
        assert frame.number_of_channels == channels
        assert frame.number_of_frames > 0

    encoder.close()
    decoder.close()
