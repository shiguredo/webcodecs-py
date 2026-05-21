"""AudioDecoder の close() / reset() で GIL 保持デッドロックが発生しないことを検証する regression テスト

出力コールバック内で release.wait() を呼んで短時間待機する間に別スレッドから close() / reset() を呼び、
修正前は GIL が取れずデッドロックすること、 修正後はワーカーが GIL を取り戻して正常完了することを確認する。
"""

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
# 48 kHz で 20 ms 相当
_FRAMES_PER_BUFFER = 960

_DECODER_CONFIG: AudioDecoderConfig = {
    "codec": "opus",
    "sample_rate": _SAMPLE_RATE,
    "number_of_channels": _CHANNELS,
}


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


def encode_one_chunk():
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


def test_close_does_not_deadlock_when_callback_in_flight():
    chunk = encode_one_chunk()

    started = threading.Event()
    release = threading.Event()

    def on_output(audio_data):
        started.set()
        release.wait(timeout=1)
        audio_data.close()

    def on_error(error):
        pass

    decoder = AudioDecoder(on_output, on_error)
    decoder.configure(_DECODER_CONFIG)
    decoder.decode(chunk)

    assert started.wait(timeout=3), "デコーダーの出力コールバックが呼ばれなかった"

    closer = threading.Thread(target=decoder.close)
    closer.start()
    try:
        closer.join(timeout=3)
        assert not closer.is_alive(), "close() がデッドロックした"
        assert decoder.state == CodecState.CLOSED, (
            f"close 後の state が CLOSED でない: {decoder.state}"
        )
    finally:
        release.set()
        closer.join(timeout=3)


def test_reset_does_not_deadlock_when_callback_in_flight():
    chunk = encode_one_chunk()

    started = threading.Event()
    release = threading.Event()

    def on_output(audio_data):
        started.set()
        release.wait(timeout=1)
        audio_data.close()

    def on_error(error):
        pass

    decoder = AudioDecoder(on_output, on_error)
    decoder.configure(_DECODER_CONFIG)
    decoder.decode(chunk)

    assert started.wait(timeout=3), "デコーダーの出力コールバックが呼ばれなかった"

    resetter = threading.Thread(target=decoder.reset)
    resetter.start()
    try:
        resetter.join(timeout=3)
        assert not resetter.is_alive(), "reset() がデッドロックした"
        assert decoder.state == CodecState.UNCONFIGURED, (
            f"reset 後の state が UNCONFIGURED でない: {decoder.state}"
        )
    finally:
        release.set()
        resetter.join(timeout=3)
        decoder.close()
