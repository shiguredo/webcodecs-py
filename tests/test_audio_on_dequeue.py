"""オーディオエンコーダー・デコーダーの on_dequeue コールバックのテスト

AudioEncoder、AudioDecoder の on_dequeue コールバックが適切に呼ばれることを検証します。
"""

import numpy as np
from webcodecs import (
    AudioEncoder,
    AudioEncoderConfig,
    AudioDecoder,
    AudioDecoderConfig,
    AudioData,
    AudioDataInit,
    AudioSampleFormat,
)


def test_audio_encoder_on_dequeue():
    """AudioEncoder の on_dequeue コールバックが呼ばれることを確認"""
    dequeue_count = 0

    def on_dequeue():
        nonlocal dequeue_count
        dequeue_count += 1

    def on_output(chunk):
        pass

    def on_error(error):
        pass

    encoder = AudioEncoder(on_output, on_error)
    encoder.on_dequeue(on_dequeue)

    config: AudioEncoderConfig = {
        "codec": "opus",
        "sample_rate": 48000,
        "number_of_channels": 2,
        "bitrate": 128000,
    }
    encoder.configure(config)

    # テスト用のオーディオデータを作成 (960 サンプル、2 チャンネル)
    samples = np.zeros((960, 2), dtype=np.float32)
    init: AudioDataInit = {
        "format": AudioSampleFormat.F32,
        "sample_rate": 48000,
        "number_of_frames": 960,
        "number_of_channels": 2,
        "timestamp": 0,
        "data": samples,
    }
    audio_data = AudioData(init)

    encoder.encode(audio_data)
    encoder.flush()
    audio_data.close()

    # encode と flush で on_dequeue が呼ばれるはず
    assert dequeue_count > 0
    encoder.close()


def test_audio_decoder_on_dequeue():
    """AudioDecoder の on_dequeue コールバックが呼ばれることを確認"""
    dequeue_count = 0

    def on_dequeue():
        nonlocal dequeue_count
        dequeue_count += 1

    # まずエンコードしてチャンクを作る
    chunks = []

    def on_output(chunk):
        chunks.append(chunk)

    def on_error(error):
        pass

    encoder = AudioEncoder(on_output, on_error)

    config: AudioEncoderConfig = {
        "codec": "opus",
        "sample_rate": 48000,
        "number_of_channels": 2,
        "bitrate": 128000,
    }
    encoder.configure(config)

    samples = np.zeros((960, 2), dtype=np.float32)
    init: AudioDataInit = {
        "format": AudioSampleFormat.F32,
        "sample_rate": 48000,
        "number_of_frames": 960,
        "number_of_channels": 2,
        "timestamp": 0,
        "data": samples,
    }
    audio_data = AudioData(init)
    encoder.encode(audio_data)
    encoder.flush()
    audio_data.close()
    encoder.close()

    # デコーダーでデコード
    decoder = AudioDecoder(lambda data: data.close(), lambda err: None)
    decoder.on_dequeue(on_dequeue)

    decoder_config: AudioDecoderConfig = {
        "codec": "opus",
        "sample_rate": 48000,
        "number_of_channels": 2,
    }
    decoder.configure(decoder_config)

    for chunk in chunks:
        decoder.decode(chunk)
    decoder.flush()

    # デコード処理で on_dequeue が呼ばれるはず
    assert dequeue_count > 0
    decoder.close()
