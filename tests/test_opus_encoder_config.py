"""OpusEncoderConfig のテスト"""

import numpy as np
import pytest

from webcodecs import (
    AudioData,
    AudioDataInit,
    AudioEncoder,
    AudioEncoderConfig,
    AudioSampleFormat,
    CodecState,
    OpusEncoderConfig,
)


def test_opus_encoder_config_application_voip():
    """Test Opus encoder with application mode 'voip'."""
    outputs: list = []

    def on_output(chunk):
        outputs.append(chunk)

    def on_error(error):
        pytest.fail(f"Encoder error: {error}")

    encoder = AudioEncoder(on_output, on_error)

    opus_config: OpusEncoderConfig = {
        "application": "voip",
    }
    encoder_config: AudioEncoderConfig = {
        "codec": "opus",
        "sample_rate": 48000,
        "number_of_channels": 1,
        "bitrate": 32000,
        "opus": opus_config,
    }
    encoder.configure(encoder_config)
    assert encoder.state == CodecState.CONFIGURED

    # エンコードを実行
    data = np.zeros((960, 1), dtype=np.int16)
    init: AudioDataInit = {
        "format": AudioSampleFormat.S16,
        "sample_rate": 48000,
        "number_of_frames": 960,
        "number_of_channels": 1,
        "timestamp": 0,
        "data": data,
    }
    audio_data = AudioData(init)
    encoder.encode(audio_data)
    encoder.flush()
    audio_data.close()

    assert len(outputs) > 0
    encoder.close()


def test_opus_encoder_config_application_lowdelay():
    """Test Opus encoder with application mode 'lowdelay'."""
    outputs: list = []

    def on_output(chunk):
        outputs.append(chunk)

    def on_error(error):
        pytest.fail(f"Encoder error: {error}")

    encoder = AudioEncoder(on_output, on_error)

    opus_config: OpusEncoderConfig = {
        "application": "lowdelay",
    }
    encoder_config: AudioEncoderConfig = {
        "codec": "opus",
        "sample_rate": 48000,
        "number_of_channels": 2,
        "bitrate": 64000,
        "opus": opus_config,
    }
    encoder.configure(encoder_config)
    assert encoder.state == CodecState.CONFIGURED

    # エンコードを実行
    data = np.zeros((960, 2), dtype=np.int16)
    init: AudioDataInit = {
        "format": AudioSampleFormat.S16,
        "sample_rate": 48000,
        "number_of_frames": 960,
        "number_of_channels": 2,
        "timestamp": 0,
        "data": data,
    }
    audio_data = AudioData(init)
    encoder.encode(audio_data)
    encoder.flush()
    audio_data.close()

    assert len(outputs) > 0
    encoder.close()


def test_opus_encoder_config_signal_music():
    """Test Opus encoder with signal type 'music'."""
    outputs: list = []

    def on_output(chunk):
        outputs.append(chunk)

    def on_error(error):
        pytest.fail(f"Encoder error: {error}")

    encoder = AudioEncoder(on_output, on_error)

    opus_config: OpusEncoderConfig = {
        "signal": "music",
    }
    encoder_config: AudioEncoderConfig = {
        "codec": "opus",
        "sample_rate": 48000,
        "number_of_channels": 2,
        "bitrate": 128000,
        "opus": opus_config,
    }
    encoder.configure(encoder_config)
    assert encoder.state == CodecState.CONFIGURED

    # エンコードを実行
    data = np.zeros((960, 2), dtype=np.int16)
    init: AudioDataInit = {
        "format": AudioSampleFormat.S16,
        "sample_rate": 48000,
        "number_of_frames": 960,
        "number_of_channels": 2,
        "timestamp": 0,
        "data": data,
    }
    audio_data = AudioData(init)
    encoder.encode(audio_data)
    encoder.flush()
    audio_data.close()

    assert len(outputs) > 0
    encoder.close()


def test_opus_encoder_config_signal_voice():
    """Test Opus encoder with signal type 'voice'."""
    outputs: list = []

    def on_output(chunk):
        outputs.append(chunk)

    def on_error(error):
        pytest.fail(f"Encoder error: {error}")

    encoder = AudioEncoder(on_output, on_error)

    opus_config: OpusEncoderConfig = {
        "signal": "voice",
    }
    encoder_config: AudioEncoderConfig = {
        "codec": "opus",
        "sample_rate": 48000,
        "number_of_channels": 1,
        "bitrate": 32000,
        "opus": opus_config,
    }
    encoder.configure(encoder_config)
    assert encoder.state == CodecState.CONFIGURED

    # エンコードを実行
    data = np.zeros((960, 1), dtype=np.int16)
    init: AudioDataInit = {
        "format": AudioSampleFormat.S16,
        "sample_rate": 48000,
        "number_of_frames": 960,
        "number_of_channels": 1,
        "timestamp": 0,
        "data": data,
    }
    audio_data = AudioData(init)
    encoder.encode(audio_data)
    encoder.flush()
    audio_data.close()

    assert len(outputs) > 0
    encoder.close()


def test_opus_encoder_config_complexity():
    """Test Opus encoder with various complexity values."""
    # 複雑度 0 (最速) から 10 (最高品質) までテスト
    complexities = [0, 5, 10]

    for complexity in complexities:
        outputs: list = []

        def on_output(chunk):
            outputs.append(chunk)

        def on_error(error):
            pytest.fail(f"Encoder error: {error}")

        encoder = AudioEncoder(on_output, on_error)

        opus_config: OpusEncoderConfig = {
            "complexity": complexity,
        }
        encoder_config: AudioEncoderConfig = {
            "codec": "opus",
            "sample_rate": 48000,
            "number_of_channels": 2,
            "bitrate": 64000,
            "opus": opus_config,
        }
        encoder.configure(encoder_config)
        assert encoder.state == CodecState.CONFIGURED

        # エンコードを実行
        data = np.zeros((960, 2), dtype=np.int16)
        init: AudioDataInit = {
            "format": AudioSampleFormat.S16,
            "sample_rate": 48000,
            "number_of_frames": 960,
            "number_of_channels": 2,
            "timestamp": 0,
            "data": data,
        }
        audio_data = AudioData(init)
        encoder.encode(audio_data)
        encoder.flush()
        audio_data.close()

        assert len(outputs) > 0
        encoder.close()


def test_opus_encoder_config_packetlossperc():
    """Test Opus encoder with packet loss percentage."""
    outputs: list = []

    def on_output(chunk):
        outputs.append(chunk)

    def on_error(error):
        pytest.fail(f"Encoder error: {error}")

    encoder = AudioEncoder(on_output, on_error)

    opus_config: OpusEncoderConfig = {
        "packetlossperc": 10,  # 10% パケットロスを想定
    }
    encoder_config: AudioEncoderConfig = {
        "codec": "opus",
        "sample_rate": 48000,
        "number_of_channels": 2,
        "bitrate": 64000,
        "opus": opus_config,
    }
    encoder.configure(encoder_config)
    assert encoder.state == CodecState.CONFIGURED

    # エンコードを実行
    data = np.zeros((960, 2), dtype=np.int16)
    init: AudioDataInit = {
        "format": AudioSampleFormat.S16,
        "sample_rate": 48000,
        "number_of_frames": 960,
        "number_of_channels": 2,
        "timestamp": 0,
        "data": data,
    }
    audio_data = AudioData(init)
    encoder.encode(audio_data)
    encoder.flush()
    audio_data.close()

    assert len(outputs) > 0
    encoder.close()


def test_opus_encoder_config_useinbandfec():
    """Test Opus encoder with in-band FEC enabled."""
    outputs: list = []

    def on_output(chunk):
        outputs.append(chunk)

    def on_error(error):
        pytest.fail(f"Encoder error: {error}")

    encoder = AudioEncoder(on_output, on_error)

    opus_config: OpusEncoderConfig = {
        "useinbandfec": True,
        "packetlossperc": 5,  # FEC は packetlossperc と組み合わせて使用
    }
    encoder_config: AudioEncoderConfig = {
        "codec": "opus",
        "sample_rate": 48000,
        "number_of_channels": 2,
        "bitrate": 64000,
        "opus": opus_config,
    }
    encoder.configure(encoder_config)
    assert encoder.state == CodecState.CONFIGURED

    # エンコードを実行
    data = np.zeros((960, 2), dtype=np.int16)
    init: AudioDataInit = {
        "format": AudioSampleFormat.S16,
        "sample_rate": 48000,
        "number_of_frames": 960,
        "number_of_channels": 2,
        "timestamp": 0,
        "data": data,
    }
    audio_data = AudioData(init)
    encoder.encode(audio_data)
    encoder.flush()
    audio_data.close()

    assert len(outputs) > 0
    encoder.close()


def test_opus_encoder_config_usedtx():
    """Test Opus encoder with DTX (discontinuous transmission) enabled."""
    outputs: list = []

    def on_output(chunk):
        outputs.append(chunk)

    def on_error(error):
        pytest.fail(f"Encoder error: {error}")

    encoder = AudioEncoder(on_output, on_error)

    opus_config: OpusEncoderConfig = {
        "usedtx": True,
    }
    encoder_config: AudioEncoderConfig = {
        "codec": "opus",
        "sample_rate": 48000,
        "number_of_channels": 1,
        "bitrate": 32000,
        "opus": opus_config,
    }
    encoder.configure(encoder_config)
    assert encoder.state == CodecState.CONFIGURED

    # エンコードを実行
    data = np.zeros((960, 1), dtype=np.int16)
    init: AudioDataInit = {
        "format": AudioSampleFormat.S16,
        "sample_rate": 48000,
        "number_of_frames": 960,
        "number_of_channels": 1,
        "timestamp": 0,
        "data": data,
    }
    audio_data = AudioData(init)
    encoder.encode(audio_data)
    encoder.flush()
    audio_data.close()

    assert len(outputs) > 0
    encoder.close()


def test_opus_encoder_config_combined():
    """Test Opus encoder with combined configuration."""
    outputs: list = []

    def on_output(chunk):
        outputs.append(chunk)

    def on_error(error):
        pytest.fail(f"Encoder error: {error}")

    encoder = AudioEncoder(on_output, on_error)

    # 全てのオプションを組み合わせたリアルタイム VoIP 向け設定
    opus_config: OpusEncoderConfig = {
        "application": "voip",
        "signal": "voice",
        "complexity": 5,
        "packetlossperc": 5,
        "useinbandfec": True,
        "usedtx": True,
    }
    encoder_config: AudioEncoderConfig = {
        "codec": "opus",
        "sample_rate": 48000,
        "number_of_channels": 1,
        "bitrate": 32000,
        "opus": opus_config,
    }
    encoder.configure(encoder_config)
    assert encoder.state == CodecState.CONFIGURED

    # エンコードを実行
    data = np.zeros((960, 1), dtype=np.int16)
    init: AudioDataInit = {
        "format": AudioSampleFormat.S16,
        "sample_rate": 48000,
        "number_of_frames": 960,
        "number_of_channels": 1,
        "timestamp": 0,
        "data": data,
    }
    audio_data = AudioData(init)
    encoder.encode(audio_data)
    encoder.flush()
    audio_data.close()

    assert len(outputs) > 0
    encoder.close()


def test_opus_encoder_config_music_high_quality():
    """Test Opus encoder with high quality music configuration."""
    outputs: list = []

    def on_output(chunk):
        outputs.append(chunk)

    def on_error(error):
        pytest.fail(f"Encoder error: {error}")

    encoder = AudioEncoder(on_output, on_error)

    # 高品質音楽向け設定
    opus_config: OpusEncoderConfig = {
        "application": "audio",
        "signal": "music",
        "complexity": 10,
    }
    encoder_config: AudioEncoderConfig = {
        "codec": "opus",
        "sample_rate": 48000,
        "number_of_channels": 2,
        "bitrate": 256000,
        "opus": opus_config,
    }
    encoder.configure(encoder_config)
    assert encoder.state == CodecState.CONFIGURED

    # エンコードを実行
    data = np.zeros((960, 2), dtype=np.int16)
    init: AudioDataInit = {
        "format": AudioSampleFormat.S16,
        "sample_rate": 48000,
        "number_of_frames": 960,
        "number_of_channels": 2,
        "timestamp": 0,
        "data": data,
    }
    audio_data = AudioData(init)
    encoder.encode(audio_data)
    encoder.flush()
    audio_data.close()

    assert len(outputs) > 0
    encoder.close()
