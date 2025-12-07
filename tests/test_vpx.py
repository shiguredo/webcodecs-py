import platform
import numpy as np
import pytest

from webcodecs import (
    VideoEncoderBitrateMode,
    CodecState,
    EncodedVideoChunkType,
    LatencyMode,
    VideoDecoder,
    VideoDecoderConfig,
    VideoEncoder,
    VideoEncoderConfig,
    VideoEncoderEncodeOptions,
    VideoFrame,
    VideoFrameBufferInit,
    VideoPixelFormat,
)


# macOS のみ VP8/VP9 をサポート
pytestmark = pytest.mark.skipif(
    platform.system() != "Darwin",
    reason="VP8/VP9 は macOS のみサポート",
)


def _make_test_frame(width: int, height: int, frame_num: int = 0) -> VideoFrame:
    """テスト用の VideoFrame を作成する"""
    data_size = width * height * 3 // 2  # I420
    data = np.zeros(data_size, dtype=np.uint8)
    init: VideoFrameBufferInit = {
        "format": VideoPixelFormat.I420,
        "coded_width": width,
        "coded_height": height,
        "timestamp": frame_num * 1000,
    }
    frame = VideoFrame(data, init)
    return frame


# =============================================================================
# VP8 テスト
# =============================================================================


def test_vp8_encoder_creation():
    """VP8 エンコーダ作成の基本確認"""

    def on_output(chunk):
        pass

    def on_error(error):
        pass

    enc = VideoEncoder(on_output, on_error)
    config: VideoEncoderConfig = {
        "codec": "vp8",
        "width": 320,
        "height": 240,
        "bitrate": 500_000,
        "framerate": 30.0,
    }
    enc.configure(config)
    assert enc.state == CodecState.CONFIGURED
    enc.close()


def test_vp8_decoder_creation():
    """VP8 デコーダ作成の基本確認"""

    def on_output(frame):
        pass

    def on_error(error):
        pass

    dec = VideoDecoder(on_output, on_error)
    config: VideoDecoderConfig = {"codec": "vp8"}
    dec.configure(config)
    assert dec.state == CodecState.CONFIGURED
    dec.close()


def test_vp8_is_config_supported():
    """VP8 の is_config_supported が VideoDecoderSupport を返すこと"""
    cfg: VideoDecoderConfig = {"codec": "vp8"}
    support = VideoDecoder.is_config_supported(cfg)
    assert support["supported"] is True
    assert support["config"]["codec"] == "vp8"


def test_vp8_encode_decode_roundtrip():
    """VP8 エンコード・デコードのラウンドトリップテスト"""
    width, height = 160, 120
    frame = _make_test_frame(width, height, 0)

    # エンコーダー
    encoded_chunks = []

    def on_encode_output(chunk):
        encoded_chunks.append(chunk)

    def on_encode_error(error):
        pytest.fail(f"エンコーダーエラー: {error}")

    encoder = VideoEncoder(on_encode_output, on_encode_error)

    enc_config: VideoEncoderConfig = {
        "codec": "vp8",
        "width": width,
        "height": height,
        "bitrate": 300_000,
        "framerate": 30.0,
        "latency_mode": LatencyMode.REALTIME,
    }
    encoder.configure(enc_config)

    encoder.encode(frame, {"keyFrame": True})
    encoder.flush()
    frame.close()

    assert len(encoded_chunks) >= 1
    assert encoded_chunks[0].byte_length > 0
    assert encoded_chunks[0].type in (
        EncodedVideoChunkType.KEY,
        EncodedVideoChunkType.DELTA,
    )

    # デコーダー
    decoded_frames = []

    def on_decode_output(f: VideoFrame):
        decoded_frames.append(f)

    def on_decode_error(err: str):
        pytest.fail(f"デコーダーエラー: {err}")

    decoder = VideoDecoder(on_decode_output, on_decode_error)
    dec_config: VideoDecoderConfig = {"codec": "vp8"}
    decoder.configure(dec_config)

    for c in encoded_chunks:
        decoder.decode(c)
    decoder.flush()

    assert len(decoded_frames) >= 1
    out = decoded_frames[0]
    assert out.coded_width == width and out.coded_height == height
    out.close()
    decoder.close()
    encoder.close()


def test_vp8_encode_decode_multiple_frames():
    """複数フレームでの VP8 エンコード・デコードテスト"""
    width, height = 128, 96
    num_frames = 5

    # エンコーダー
    chunks = []

    def on_encode_output(chunk):
        chunks.append(chunk)

    def on_encode_error(error):
        pytest.fail(f"エンコーダーエラー: {error}")

    encoder = VideoEncoder(on_encode_output, on_encode_error)

    enc_config: VideoEncoderConfig = {
        "codec": "vp8",
        "width": width,
        "height": height,
        "bitrate": 250_000,
        "framerate": 24.0,
        "latency_mode": LatencyMode.REALTIME,
    }
    encoder.configure(enc_config)

    for i in range(num_frames):
        f = _make_test_frame(width, height, i)
        encoder.encode(f, {"keyFrame": (i == 0)})
        f.close()

    encoder.flush()
    assert len(chunks) >= 1

    # デコーダー
    decoded = []

    def on_decode_output(fr):
        decoded.append(fr)

    def on_decode_error(err):
        pytest.fail(err)

    decoder = VideoDecoder(on_decode_output, on_decode_error)
    dec_config: VideoDecoderConfig = {"codec": "vp8"}
    decoder.configure(dec_config)

    for c in chunks:
        decoder.decode(c)
    decoder.flush()

    assert len(decoded) >= 1
    assert all(fr.coded_width == width and fr.coded_height == height for fr in decoded)
    for fr in decoded:
        fr.close()
    decoder.close()
    encoder.close()


def test_vp8_encode_with_quantizer():
    """VP8 エンコード時に quantizer オプションを指定するテスト"""
    width, height = 160, 120
    frame = _make_test_frame(width, height, 0)

    encoded_chunks = []

    def on_encode_output(chunk):
        encoded_chunks.append(chunk)

    def on_encode_error(error):
        pytest.fail(f"エンコーダーエラー: {error}")

    encoder = VideoEncoder(on_encode_output, on_encode_error)

    enc_config: VideoEncoderConfig = {
        "codec": "vp8",
        "width": width,
        "height": height,
        "framerate": 30.0,
        "latency_mode": LatencyMode.REALTIME,
        "bitrate_mode": VideoEncoderBitrateMode.QUANTIZER,
    }
    encoder.configure(enc_config)

    options: VideoEncoderEncodeOptions = {"keyFrame": True, "vp8": {"quantizer": 30}}
    encoder.encode(frame, options)
    encoder.flush()
    frame.close()

    assert len(encoded_chunks) >= 1
    assert encoded_chunks[0].byte_length > 0
    assert encoded_chunks[0].type == EncodedVideoChunkType.KEY

    encoder.close()


# =============================================================================
# VP9 テスト
# =============================================================================


def test_vp9_encoder_creation():
    """VP9 エンコーダ作成の基本確認"""

    def on_output(chunk):
        pass

    def on_error(error):
        pass

    enc = VideoEncoder(on_output, on_error)
    config: VideoEncoderConfig = {
        "codec": "vp09.00.10.08",
        "width": 320,
        "height": 240,
        "bitrate": 500_000,
        "framerate": 30.0,
    }
    enc.configure(config)
    assert enc.state == CodecState.CONFIGURED
    enc.close()


def test_vp9_decoder_creation():
    """VP9 デコーダ作成の基本確認"""

    def on_output(frame):
        pass

    def on_error(error):
        pass

    dec = VideoDecoder(on_output, on_error)
    config: VideoDecoderConfig = {"codec": "vp09.00.10.08"}
    dec.configure(config)
    assert dec.state == CodecState.CONFIGURED
    dec.close()


def test_vp9_is_config_supported():
    """VP9 の is_config_supported が VideoDecoderSupport を返すこと"""
    cfg: VideoDecoderConfig = {"codec": "vp09.00.10.08"}
    support = VideoDecoder.is_config_supported(cfg)
    assert support["supported"] is True
    assert support["config"]["codec"] == "vp09.00.10.08"


def test_vp9_encode_decode_roundtrip():
    """VP9 エンコード・デコードのラウンドトリップテスト"""
    width, height = 160, 120
    frame = _make_test_frame(width, height, 0)

    # エンコーダー
    encoded_chunks = []

    def on_encode_output(chunk):
        encoded_chunks.append(chunk)

    def on_encode_error(error):
        pytest.fail(f"エンコーダーエラー: {error}")

    encoder = VideoEncoder(on_encode_output, on_encode_error)

    enc_config: VideoEncoderConfig = {
        "codec": "vp09.00.10.08",
        "width": width,
        "height": height,
        "bitrate": 300_000,
        "framerate": 30.0,
        "latency_mode": LatencyMode.REALTIME,
    }
    encoder.configure(enc_config)

    encoder.encode(frame, {"keyFrame": True})
    encoder.flush()
    frame.close()

    assert len(encoded_chunks) >= 1
    assert encoded_chunks[0].byte_length > 0
    assert encoded_chunks[0].type in (
        EncodedVideoChunkType.KEY,
        EncodedVideoChunkType.DELTA,
    )

    # デコーダー
    decoded_frames = []

    def on_decode_output(f: VideoFrame):
        decoded_frames.append(f)

    def on_decode_error(err: str):
        pytest.fail(f"デコーダーエラー: {err}")

    decoder = VideoDecoder(on_decode_output, on_decode_error)
    dec_config: VideoDecoderConfig = {"codec": "vp09.00.10.08"}
    decoder.configure(dec_config)

    for c in encoded_chunks:
        decoder.decode(c)
    decoder.flush()

    assert len(decoded_frames) >= 1
    out = decoded_frames[0]
    assert out.coded_width == width and out.coded_height == height
    out.close()
    decoder.close()
    encoder.close()


def test_vp9_encode_decode_multiple_frames():
    """複数フレームでの VP9 エンコード・デコードテスト"""
    width, height = 128, 96
    num_frames = 5

    # エンコーダー
    chunks = []

    def on_encode_output(chunk):
        chunks.append(chunk)

    def on_encode_error(error):
        pytest.fail(f"エンコーダーエラー: {error}")

    encoder = VideoEncoder(on_encode_output, on_encode_error)

    enc_config: VideoEncoderConfig = {
        "codec": "vp09.00.10.08",
        "width": width,
        "height": height,
        "bitrate": 250_000,
        "framerate": 24.0,
        "latency_mode": LatencyMode.REALTIME,
    }
    encoder.configure(enc_config)

    for i in range(num_frames):
        f = _make_test_frame(width, height, i)
        encoder.encode(f, {"keyFrame": (i == 0)})
        f.close()

    encoder.flush()
    assert len(chunks) >= 1

    # デコーダー
    decoded = []

    def on_decode_output(fr):
        decoded.append(fr)

    def on_decode_error(err):
        pytest.fail(err)

    decoder = VideoDecoder(on_decode_output, on_decode_error)
    dec_config: VideoDecoderConfig = {"codec": "vp09.00.10.08"}
    decoder.configure(dec_config)

    for c in chunks:
        decoder.decode(c)
    decoder.flush()

    assert len(decoded) >= 1
    assert all(fr.coded_width == width and fr.coded_height == height for fr in decoded)
    for fr in decoded:
        fr.close()
    decoder.close()
    encoder.close()


def test_vp9_encode_with_quantizer():
    """VP9 エンコード時に quantizer オプションを指定するテスト"""
    width, height = 160, 120
    frame = _make_test_frame(width, height, 0)

    encoded_chunks = []

    def on_encode_output(chunk):
        encoded_chunks.append(chunk)

    def on_encode_error(error):
        pytest.fail(f"エンコーダーエラー: {error}")

    encoder = VideoEncoder(on_encode_output, on_encode_error)

    enc_config: VideoEncoderConfig = {
        "codec": "vp09.00.10.08",
        "width": width,
        "height": height,
        "framerate": 30.0,
        "latency_mode": LatencyMode.REALTIME,
        "bitrate_mode": VideoEncoderBitrateMode.QUANTIZER,
    }
    encoder.configure(enc_config)

    options: VideoEncoderEncodeOptions = {"keyFrame": True, "vp9": {"quantizer": 30}}
    encoder.encode(frame, options)
    encoder.flush()
    frame.close()

    assert len(encoded_chunks) >= 1
    assert encoded_chunks[0].byte_length > 0
    assert encoded_chunks[0].type == EncodedVideoChunkType.KEY

    encoder.close()


def test_vp9_different_profiles():
    """VP9 の異なるプロファイルでの設定テスト"""

    def on_output(chunk):
        pass

    def on_error(error):
        pass

    # Profile 0 (8-bit, 4:2:0)
    enc0 = VideoEncoder(on_output, on_error)
    config0: VideoEncoderConfig = {
        "codec": "vp09.00.10.08",
        "width": 320,
        "height": 240,
        "bitrate": 500_000,
        "framerate": 30.0,
    }
    enc0.configure(config0)
    assert enc0.state == CodecState.CONFIGURED
    enc0.close()

    # Profile 1 (8-bit, 4:2:2 or 4:4:4)
    enc1 = VideoEncoder(on_output, on_error)
    config1: VideoEncoderConfig = {
        "codec": "vp09.01.10.08",
        "width": 320,
        "height": 240,
        "bitrate": 500_000,
        "framerate": 30.0,
    }
    enc1.configure(config1)
    assert enc1.state == CodecState.CONFIGURED
    enc1.close()

    # Profile 2 (10-bit, 4:2:0)
    enc2 = VideoEncoder(on_output, on_error)
    config2: VideoEncoderConfig = {
        "codec": "vp09.02.10.10",
        "width": 320,
        "height": 240,
        "bitrate": 500_000,
        "framerate": 30.0,
    }
    enc2.configure(config2)
    assert enc2.state == CodecState.CONFIGURED
    enc2.close()

    # Profile 3 (10-bit, 4:2:2 or 4:4:4)
    enc3 = VideoEncoder(on_output, on_error)
    config3: VideoEncoderConfig = {
        "codec": "vp09.03.10.10",
        "width": 320,
        "height": 240,
        "bitrate": 500_000,
        "framerate": 30.0,
    }
    enc3.configure(config3)
    assert enc3.state == CodecState.CONFIGURED
    enc3.close()


def test_vp9_quantizer_values():
    """VP9 quantizer で異なる値を指定できることを確認するテスト"""
    width, height = 128, 96

    def encode_with_quantizer(quantizer_value):
        chunks = []

        def on_output(chunk):
            chunks.append(chunk)

        def on_error(error):
            pytest.fail(f"エンコーダーエラー: {error}")

        encoder = VideoEncoder(on_output, on_error)
        config: VideoEncoderConfig = {
            "codec": "vp09.00.10.08",
            "width": width,
            "height": height,
            "framerate": 30.0,
            "latency_mode": LatencyMode.REALTIME,
            "bitrate_mode": VideoEncoderBitrateMode.QUANTIZER,
        }
        encoder.configure(config)

        frame = _make_test_frame(width, height, 0)
        encoder.encode(frame, {"keyFrame": True, "vp9": {"quantizer": quantizer_value}})
        encoder.flush()
        frame.close()
        encoder.close()

        return sum(c.byte_length for c in chunks)

    # 異なる quantizer 値でエンコードできることを確認
    size_low_q = encode_with_quantizer(10)
    size_high_q = encode_with_quantizer(50)

    # 両方のエンコードが成功することを確認
    assert size_low_q > 0
    assert size_high_q > 0


def test_vp8_cbr_mode():
    """VP8 の CBR (Constant Bitrate) モードテスト"""
    width, height = 160, 120
    frame = _make_test_frame(width, height, 0)

    encoded_chunks = []

    def on_encode_output(chunk):
        encoded_chunks.append(chunk)

    def on_encode_error(error):
        pytest.fail(f"エンコーダーエラー: {error}")

    encoder = VideoEncoder(on_encode_output, on_encode_error)

    enc_config: VideoEncoderConfig = {
        "codec": "vp8",
        "width": width,
        "height": height,
        "bitrate": 300_000,
        "framerate": 30.0,
        "latency_mode": LatencyMode.REALTIME,
        "bitrate_mode": VideoEncoderBitrateMode.CONSTANT,
    }
    encoder.configure(enc_config)

    encoder.encode(frame, {"keyFrame": True})
    encoder.flush()
    frame.close()

    assert len(encoded_chunks) >= 1
    encoder.close()


def test_vp9_cbr_mode():
    """VP9 の CBR (Constant Bitrate) モードテスト"""
    width, height = 160, 120
    frame = _make_test_frame(width, height, 0)

    encoded_chunks = []

    def on_encode_output(chunk):
        encoded_chunks.append(chunk)

    def on_encode_error(error):
        pytest.fail(f"エンコーダーエラー: {error}")

    encoder = VideoEncoder(on_encode_output, on_encode_error)

    enc_config: VideoEncoderConfig = {
        "codec": "vp09.00.10.08",
        "width": width,
        "height": height,
        "bitrate": 300_000,
        "framerate": 30.0,
        "latency_mode": LatencyMode.REALTIME,
        "bitrate_mode": VideoEncoderBitrateMode.CONSTANT,
    }
    encoder.configure(enc_config)

    encoder.encode(frame, {"keyFrame": True})
    encoder.flush()
    frame.close()

    assert len(encoded_chunks) >= 1
    encoder.close()
