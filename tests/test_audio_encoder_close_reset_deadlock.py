"""AudioEncoder の close() / reset() で GIL 保持デッドロックが発生しないことを検証する regression テスト"""

import threading

import numpy as np

from webcodecs import (
    AudioData,
    AudioDataInit,
    AudioEncoder,
    AudioEncoderConfig,
    AudioSampleFormat,
    CodecState,
)


def create_test_audio_data(timestamp: int) -> AudioData:
    sample_rate = 48000
    number_of_channels = 2
    number_of_frames = 960  # 48 kHz で 20 ms
    data = np.zeros((number_of_frames, number_of_channels), dtype=np.float32)
    init: AudioDataInit = {
        "format": AudioSampleFormat.F32,
        "sample_rate": sample_rate,
        "number_of_frames": number_of_frames,
        "number_of_channels": number_of_channels,
        "timestamp": timestamp,
        "data": data,
    }
    return AudioData(init)


# Opus は全プラットフォーム (Linux / macOS / Windows) でワーカースレッドを起動する
_ENCODER_CONFIG: AudioEncoderConfig = {
    "codec": "opus",
    "sample_rate": 48000,
    "number_of_channels": 2,
    "bitrate": 128000,
}


def test_close_does_not_deadlock_when_callback_in_flight():
    started = threading.Event()
    release = threading.Event()

    # AudioEncoder の出力コールバックは (chunk, metadata) の 2 引数で呼ばれる
    def on_output(chunk, metadata=None):
        started.set()
        release.wait(timeout=1)

    def on_error(error):
        pass

    encoder = AudioEncoder(on_output, on_error)
    encoder.configure(_ENCODER_CONFIG)
    audio_data = create_test_audio_data(0)
    encoder.encode(audio_data)
    audio_data.close()

    assert started.wait(timeout=3), "エンコーダーの出力コールバックが呼ばれなかった"

    closer = threading.Thread(target=encoder.close)
    closer.start()
    try:
        closer.join(timeout=3)
        assert not closer.is_alive(), "close() がデッドロックした"
    finally:
        release.set()
        closer.join(timeout=3)


def test_reset_does_not_deadlock_when_callback_in_flight():
    started = threading.Event()
    release = threading.Event()

    def on_output(chunk, metadata=None):
        started.set()
        release.wait(timeout=1)

    def on_error(error):
        pass

    encoder = AudioEncoder(on_output, on_error)
    encoder.configure(_ENCODER_CONFIG)
    audio_data = create_test_audio_data(0)
    encoder.encode(audio_data)
    audio_data.close()

    assert started.wait(timeout=3), "エンコーダーの出力コールバックが呼ばれなかった"

    resetter = threading.Thread(target=encoder.reset)
    resetter.start()
    try:
        resetter.join(timeout=3)
        assert not resetter.is_alive(), "reset() がデッドロックした"
        assert encoder.state == CodecState.UNCONFIGURED, (
            f"reset 後の state が UNCONFIGURED でない: {encoder.state}"
        )
    finally:
        release.set()
        resetter.join(timeout=3)
        encoder.close()
