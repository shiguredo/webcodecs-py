"""AudioDecoder の close() / reset() で GIL 保持デッドロックが発生しないことを検証する regression テスト"""

import threading

import numpy as np

from webcodecs import (
    AudioData,
    AudioDataInit,
    AudioDecoder,
    AudioDecoderConfig,
    AudioEncoder,
    AudioEncoderConfig,
    AudioSampleFormat,
    CodecState,
)


_SAMPLE_RATE = 48000
_CHANNELS = 2
_FRAMES_PER_BUFFER = 960  # 20 ms at 48 kHz


def create_test_audio_data(timestamp: int) -> AudioData:
    data = np.zeros((_FRAMES_PER_BUFFER, _CHANNELS), dtype=np.float32)
    init: AudioDataInit = {
        "format": AudioSampleFormat.F32,
        "sample_rate": _SAMPLE_RATE,
        "number_of_frames": _FRAMES_PER_BUFFER,
        "number_of_channels": _CHANNELS,
        "timestamp": timestamp,
        "data": data,
    }
    return AudioData(init)


def _encode_one_chunk():
    """事前準備として AudioEncoder で 1 つの EncodedAudioChunk を生成する"""
    encoded_chunks = []

    def on_output(chunk, metadata=None):
        encoded_chunks.append(chunk)

    def on_error(error):
        pass

    encoder = AudioEncoder(on_output, on_error)
    enc_config: AudioEncoderConfig = {
        "codec": "opus",
        "sample_rate": _SAMPLE_RATE,
        "number_of_channels": _CHANNELS,
        "bitrate": 128000,
    }
    encoder.configure(enc_config)
    audio_data = create_test_audio_data(0)
    encoder.encode(audio_data)
    audio_data.close()
    encoder.flush()
    encoder.close()
    assert encoded_chunks, "エンコーダーがチャンクを出力しなかった"
    return encoded_chunks[0]


def _run_close_or_reset(action: str):
    chunk = _encode_one_chunk()

    started = threading.Event()
    release = threading.Event()

    def on_output(audio_data):
        started.set()
        release.wait(timeout=1)
        audio_data.close()

    def on_error(error):
        pass

    decoder = AudioDecoder(on_output, on_error)
    dec_config: AudioDecoderConfig = {
        "codec": "opus",
        "sample_rate": _SAMPLE_RATE,
        "number_of_channels": _CHANNELS,
    }
    decoder.configure(dec_config)
    decoder.decode(chunk)

    assert started.wait(timeout=3), "デコーダーの出力コールバックが呼ばれなかった"

    target = decoder.close if action == "close" else decoder.reset
    closer = threading.Thread(target=target)
    closer.start()
    try:
        closer.join(timeout=3)
        assert not closer.is_alive(), f"{action}() がデッドロックした"
        if action == "reset":
            # AudioDecoder の reset 後は state が UNCONFIGURED に戻る
            assert decoder.state == CodecState.UNCONFIGURED, (
                f"reset 後の state が UNCONFIGURED でない: {decoder.state}"
            )
    finally:
        release.set()
        closer.join(timeout=3)
        if action == "reset":
            decoder.close()


def test_close_does_not_deadlock_when_callback_in_flight():
    _run_close_or_reset("close")


def test_reset_does_not_deadlock_when_callback_in_flight():
    _run_close_or_reset("reset")
