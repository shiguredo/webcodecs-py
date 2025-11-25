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


def test_av1_decoder_creation():
    """AV1 デコーダ作成の基本確認"""

    def on_output(frame):
        pass

    def on_error(error):
        pass

    dec = VideoDecoder(on_output, on_error)
    config: VideoDecoderConfig = {"codec": "av01.0.04M.08"}
    dec.configure(config)
    assert dec.state == CodecState.CONFIGURED


def test_av1_decoder_full_codec_string():
    """AV1 の正式なコーデック文字列 av01.x.xxM.xx の受理確認"""

    def on_output(frame):
        pass

    def on_error(error):
        pass

    dec = VideoDecoder(on_output, on_error)
    config: VideoDecoderConfig = {"codec": "av01.0.04M.08"}  # 8‑bit, Main, Level 4
    dec.configure(config)
    assert dec.state == CodecState.CONFIGURED


def test_av1_is_config_supported():
    """AV1 の is_config_supported が VideoDecoderSupport を返すこと"""
    cfg: VideoDecoderConfig = {"codec": "av01.0.04M.08"}
    support = VideoDecoder.is_config_supported(cfg)
    # 属性アクセスと辞書アクセスの両方をテスト
    assert support["supported"] is True
    assert support["config"]["codec"] == "av01.0.04M.08"

    cfg2: VideoDecoderConfig = {"codec": "av01.0.08M.08"}
    support2 = VideoDecoder.is_config_supported(cfg2)
    assert support2["supported"] is True
    assert support2["config"]["codec"] == "av01.0.08M.08"

    # 未サポートのコーデックをテスト
    cfg3: VideoDecoderConfig = {"codec": "vp9"}
    support3 = VideoDecoder.is_config_supported(cfg3)
    assert support3["supported"] is False


def test_av1_encoder_creation_basic():
    """AV1 エンコーダ初期化の基本確認（将来の実装に備えた存在確認）"""

    def on_output(chunk):
        pass

    def on_error(error):
        pass

    enc = VideoEncoder(on_output, on_error)
    config: VideoEncoderConfig = {
        "codec": "av01.0.04M.08",
        "width": 320,
        "height": 240,
        "bitrate": 500_000,
        "framerate": 30.0,
    }
    enc.configure(config)
    # 現状は内部実体を持たないが、構成状態になることのみ確認
    assert enc.state == CodecState.CONFIGURED
    enc.close()


def test_av1_frame_placeholder():
    """AV1 用フレーム生成のプレースホルダ（エンコードは未実装）"""
    width, height = 320, 240
    data_size = width * height * 3 // 2  # I420
    data = np.zeros(data_size, dtype=np.uint8)
    init: VideoFrameBufferInit = {
        "format": VideoPixelFormat.I420,
        "coded_width": width,
        "coded_height": height,
        "timestamp": 0,
    }
    frame = VideoFrame(data, init)
    assert frame.coded_width == 320 and frame.coded_height == 240
    frame.close()


def _make_test_frame(width: int, height: int, frame_num: int = 0) -> VideoFrame:
    """テスト用の VideoFrame を作成する"""
    data_size = width * height * 3 // 2  # I420
    data = np.zeros(data_size, dtype=np.uint8)
    init: VideoFrameBufferInit = {
        "format": VideoPixelFormat.I420,
        "coded_width": width,
        "coded_height": height,
        "timestamp": frame_num * 1000,  # フレーム番号に基づくタイムスタンプ
    }
    frame = VideoFrame(data, init)
    return frame


def test_av1_encode_decode_roundtrip_small_frame():
    """小さいフレームサイズでの AV1 エンコード・デコードのラウンドトリップテスト"""
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
        "codec": "av01.0.04M.08",
        "width": width,
        "height": height,
        "bitrate": 300_000,
        "framerate": 30.0,
        "latency_mode": LatencyMode.REALTIME,
    }
    encoder.configure(enc_config)

    encoder.encode(frame, {"keyFrame": True})  # キーフレームを強制
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

    def _on_output(f: VideoFrame):
        decoded_frames.append(f)

    def _on_error(err: str):
        pytest.fail(f"デコーダーエラー: {err}")

    decoder = VideoDecoder(_on_output, _on_error)
    dec_config: VideoDecoderConfig = {"codec": "av01.0.04M.08"}
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


def test_av1_encode_decode_multiple_frames():
    """複数フレームでの AV1 エンコード・デコードテスト"""
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
        "codec": "av01.0.04M.08",
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
    decoder = VideoDecoder(lambda fr: decoded.append(fr), lambda err: pytest.fail(err))
    dec_config: VideoDecoderConfig = {"codec": "av01.0.04M.08"}
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


def test_av1_unsupported_bit_depth():
    """AV1 でサポートされていないビット深度の場合に例外が発生することを確認"""

    def on_output(chunk):
        pass

    def on_error(error):
        print(f"Encoder error: {error}")

    encoder = VideoEncoder(on_output, on_error)

    # av01.0.04M.16 = ビット深度 16（サポートされていない）
    config: VideoEncoderConfig = {
        "codec": "av01.0.04M.16",
        "width": 640,
        "height": 480,
        "bitrate": 1_000_000,
        "framerate": 30,
    }

    # configure 時に例外が発生することを確認
    # コーデック文字列のパース時に ValueError が発生
    with pytest.raises(ValueError, match="Invalid AV1 bit depth"):
        encoder.configure(config)


def test_av1_encode_with_quantizer():
    """AV1 エンコード時に quantizer オプションを指定するテスト"""
    width, height = 160, 120
    frame = _make_test_frame(width, height, 0)

    encoded_chunks = []

    def on_encode_output(chunk):
        encoded_chunks.append(chunk)

    def on_encode_error(error):
        pytest.fail(f"エンコーダーエラー: {error}")

    encoder = VideoEncoder(on_encode_output, on_encode_error)

    # bitrate_mode を quantizer に設定
    enc_config: VideoEncoderConfig = {
        "codec": "av01.0.04M.08",
        "width": width,
        "height": height,
        "framerate": 30.0,
        "latency_mode": LatencyMode.REALTIME,
        "bitrate_mode": VideoEncoderBitrateMode.QUANTIZER,
    }
    encoder.configure(enc_config)

    # av1.quantizer オプションを指定してエンコード
    options: VideoEncoderEncodeOptions = {"keyFrame": True, "av1": {"quantizer": 30}}
    encoder.encode(frame, options)
    encoder.flush()
    frame.close()

    assert len(encoded_chunks) >= 1
    assert encoded_chunks[0].byte_length > 0
    assert encoded_chunks[0].type == EncodedVideoChunkType.KEY

    encoder.close()


def test_av1_encode_quantizer_range():
    """AV1 quantizer の範囲 (0-63) が正しく適用されるテスト"""
    width, height = 128, 96

    # 低い quantizer (高品質) と高い quantizer (低品質) でサイズを比較
    def encode_with_quantizer(quantizer_value):
        chunks = []

        def on_output(chunk):
            chunks.append(chunk)

        def on_error(error):
            pytest.fail(f"エンコーダーエラー: {error}")

        encoder = VideoEncoder(on_output, on_error)
        config: VideoEncoderConfig = {
            "codec": "av01.0.04M.08",
            "width": width,
            "height": height,
            "framerate": 30.0,
            "latency_mode": LatencyMode.REALTIME,
            "bitrate_mode": VideoEncoderBitrateMode.QUANTIZER,
        }
        encoder.configure(config)

        frame = _make_test_frame(width, height, 0)
        encoder.encode(frame, {"keyFrame": True, "av1": {"quantizer": quantizer_value}})
        encoder.flush()
        frame.close()
        encoder.close()

        return sum(c.byte_length for c in chunks)

    # quantizer が低い (高品質) 方がサイズが大きい
    size_low_q = encode_with_quantizer(10)
    size_high_q = encode_with_quantizer(50)

    # 高品質 (低 quantizer) の方がサイズが大きいはず
    assert size_low_q > size_high_q, (
        f"低 quantizer={size_low_q} > 高 quantizer={size_high_q} であるべき"
    )


def test_av1_encode_quantizer_invalid_range():
    """AV1 quantizer が範囲外 (0-63) の場合にエラーになるテスト"""
    width, height = 128, 96

    def on_output(chunk):
        pass

    def on_error(error):
        pass

    encoder = VideoEncoder(on_output, on_error)
    config: VideoEncoderConfig = {
        "codec": "av01.0.04M.08",
        "width": width,
        "height": height,
        "framerate": 30.0,
        "latency_mode": LatencyMode.REALTIME,
        "bitrate_mode": VideoEncoderBitrateMode.QUANTIZER,
    }
    encoder.configure(config)

    frame = _make_test_frame(width, height, 0)

    # 範囲外の quantizer (64 以上) でエラー
    with pytest.raises(ValueError, match="quantizer must be in range 0-63"):
        encoder.encode(frame, {"keyFrame": True, "av1": {"quantizer": 64}})

    frame.close()
    encoder.close()
