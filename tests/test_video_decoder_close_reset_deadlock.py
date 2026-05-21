"""VideoDecoder の close() / reset() で GIL 保持デッドロックが発生しないことを検証する regression テスト

出力コールバック内で release.wait() を呼んで短時間待機する間に別スレッドから close() / reset() を呼び、
修正前は GIL が取れずデッドロックすること、 修正後はワーカーが GIL を取り戻して正常完了することを確認する。
"""

import threading

import numpy as np

from webcodecs import (
    CodecState,
    LatencyMode,
    VideoDecoder,
    VideoDecoderConfig,
    VideoEncoder,
    VideoEncoderConfig,
    VideoFrame,
    VideoFrameBufferInit,
    VideoPixelFormat,
)


_WIDTH = 320
_HEIGHT = 240
_CODEC = "av01.0.04M.08"

_DECODER_CONFIG: VideoDecoderConfig = {
    "codec": _CODEC,
    "coded_width": _WIDTH,
    "coded_height": _HEIGHT,
}


def create_test_frame(timestamp: int) -> VideoFrame:
    y_plane = np.full((_HEIGHT, _WIDTH), 128, dtype=np.uint8)
    u_plane = np.full((_HEIGHT // 2, _WIDTH // 2), 128, dtype=np.uint8)
    v_plane = np.full((_HEIGHT // 2, _WIDTH // 2), 128, dtype=np.uint8)
    data = np.concatenate([y_plane.flatten(), u_plane.flatten(), v_plane.flatten()])
    init: VideoFrameBufferInit = {
        "format": VideoPixelFormat.I420,
        "coded_width": _WIDTH,
        "coded_height": _HEIGHT,
        "timestamp": timestamp,
    }
    return VideoFrame(data, init)


def encode_one_chunk():
    encoded_chunks = []

    def on_output(chunk, metadata=None):
        encoded_chunks.append(chunk)

    def on_error(error):
        pass

    encoder = VideoEncoder(on_output, on_error)
    enc_config: VideoEncoderConfig = {
        "codec": _CODEC,
        "width": _WIDTH,
        "height": _HEIGHT,
        "bitrate": 200_000,
        "framerate": 30.0,
        "latency_mode": LatencyMode.REALTIME,
    }
    encoder.configure(enc_config)
    frame = create_test_frame(0)
    encoder.encode(frame, {"key_frame": True})
    frame.close()
    encoder.flush()
    encoder.close()
    assert encoded_chunks, "エンコーダーがチャンクを出力しなかった"
    return encoded_chunks[0]


def test_close_does_not_deadlock_when_callback_in_flight():
    chunk = encode_one_chunk()

    started = threading.Event()
    release = threading.Event()

    def on_output(frame):
        started.set()
        release.wait(timeout=1)
        frame.close()

    def on_error(error):
        pass

    decoder = VideoDecoder(on_output, on_error)
    decoder.configure(_DECODER_CONFIG)
    decoder.decode(chunk)

    assert started.wait(timeout=3), "デコーダーの出力コールバックが呼ばれなかった"

    closer = threading.Thread(target=decoder.close)
    closer.start()
    try:
        closer.join(timeout=3)
        assert not closer.is_alive(), "close() がデッドロックした"
    finally:
        release.set()
        closer.join(timeout=3)


def test_reset_does_not_deadlock_when_callback_in_flight():
    chunk = encode_one_chunk()

    started = threading.Event()
    release = threading.Event()

    def on_output(frame):
        started.set()
        release.wait(timeout=1)
        frame.close()

    def on_error(error):
        pass

    decoder = VideoDecoder(on_output, on_error)
    decoder.configure(_DECODER_CONFIG)
    decoder.decode(chunk)

    assert started.wait(timeout=3), "デコーダーの出力コールバックが呼ばれなかった"

    resetter = threading.Thread(target=decoder.reset)
    resetter.start()
    try:
        resetter.join(timeout=3)
        assert not resetter.is_alive(), "reset() がデッドロックした"
        # 現実装では VideoDecoder の reset は state を変えない (CONFIGURED のまま)。
        # WebCodecs 仕様では UNCONFIGURED に遷移すべきで、 仕様準拠は issue 0005 で対応する。
        assert decoder.state == CodecState.CONFIGURED, (
            f"reset 後の state が CONFIGURED でない: {decoder.state}"
        )
    finally:
        release.set()
        resetter.join(timeout=3)
        decoder.close()
