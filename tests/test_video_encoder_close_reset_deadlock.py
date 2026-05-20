"""VideoEncoder の close() / reset() で GIL 保持デッドロックが発生しないことを検証する regression テスト"""

import threading

import numpy as np

from webcodecs import (
    CodecState,
    LatencyMode,
    VideoEncoder,
    VideoEncoderConfig,
    VideoFrame,
    VideoFrameBufferInit,
    VideoPixelFormat,
)


def create_test_frame(width: int, height: int, timestamp: int) -> VideoFrame:
    y_plane = np.full((height, width), 128, dtype=np.uint8)
    u_plane = np.full((height // 2, width // 2), 128, dtype=np.uint8)
    v_plane = np.full((height // 2, width // 2), 128, dtype=np.uint8)
    data = np.concatenate([y_plane.flatten(), u_plane.flatten(), v_plane.flatten()])
    init: VideoFrameBufferInit = {
        "format": VideoPixelFormat.I420,
        "coded_width": width,
        "coded_height": height,
        "timestamp": timestamp,
    }
    return VideoFrame(data, init)


# AV1 はソフトウェアコーデック経路 (libaom) を確実に通る。
# AVC / HEVC は macOS で VideoToolbox に流れてワーカースレッドが起動しないため再現しない。
# REALTIME はテスト所要時間を短縮するため。
_ENCODER_CONFIG: VideoEncoderConfig = {
    "codec": "av01.0.04M.08",
    "width": 320,
    "height": 240,
    "bitrate": 200_000,
    "framerate": 30.0,
    "latency_mode": LatencyMode.REALTIME,
}


def test_close_does_not_deadlock_when_callback_in_flight():
    started = threading.Event()
    release = threading.Event()

    # VideoEncoder の出力コールバックは (chunk, metadata) の 2 引数で呼ばれる
    def on_output(chunk, metadata=None):
        started.set()
        # 短時間待機して closer の close() がデッドロックする race window を作る。
        # 修正後はワーカーがこの wait から戻る際に GIL を取れて callback を完了でき、
        # close() が正常終了する。 修正前は GIL が取れずデッドロックが顕現する。
        release.wait(timeout=1)

    def on_error(error):
        pass

    encoder = VideoEncoder(on_output, on_error)
    encoder.configure(_ENCODER_CONFIG)
    encoder.encode(create_test_frame(320, 240, 0), {"key_frame": True})

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

    encoder = VideoEncoder(on_output, on_error)
    encoder.configure(_ENCODER_CONFIG)
    encoder.encode(create_test_frame(320, 240, 0), {"key_frame": True})

    assert started.wait(timeout=3), "エンコーダーの出力コールバックが呼ばれなかった"

    resetter = threading.Thread(target=encoder.reset)
    resetter.start()
    try:
        resetter.join(timeout=3)
        assert not resetter.is_alive(), "reset() がデッドロックした"
        # reset 後はワーカーが再起動し、 state は UNCONFIGURED に戻る
        assert encoder.state == CodecState.UNCONFIGURED, (
            f"reset 後の state が UNCONFIGURED でない: {encoder.state}"
        )
    finally:
        release.set()
        resetter.join(timeout=3)
        encoder.close()
