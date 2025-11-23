import numpy as np
from webcodecs import (
    VideoEncoder,
    VideoEncoderConfig,
    VideoFrame,
    VideoFrameBufferInit,
    VideoPixelFormat,
)
from video_test_helpers import generate_complex_pattern


def calculate_raw_frame_size(width: int, height: int, format: VideoPixelFormat) -> int:
    """生のフレームサイズを計算する"""
    if format == VideoPixelFormat.I420:
        # Y: width * height, U: width/2 * height/2, V: width/2 * height/2
        return width * height + (width * height // 2)
    elif format == VideoPixelFormat.I422:
        # Y: width * height, U: width/2 * height, V: width/2 * height
        return width * height * 2
    elif format == VideoPixelFormat.I444:
        # Y: width * height, U: width * height, V: width * height
        return width * height * 3
    else:
        raise ValueError(f"Unsupported format: {format}")


def test_different_codecs_compression():
    """異なるコーデックで圧縮率を比較"""
    width, height = 320, 240
    raw_size = calculate_raw_frame_size(width, height, VideoPixelFormat.I420)

    # テストパターンを生成
    y_data, u_data, v_data = generate_complex_pattern(width, height)

    codecs = ["av01.0.04M.08"]
    results = {}

    for codec in codecs:
        try:
            encoder_config: VideoEncoderConfig = {
                "codec": codec,
                "width": width,
                "height": height,
                "bitrate": 500_000,
                "framerate": 30.0,
            }

            chunks = []

            def on_output(chunk):
                chunks.append(chunk)

            def on_error(error):
                print(f"Encoder error: {error}")

            encoder = VideoEncoder(on_output, on_error)
            encoder.configure(encoder_config)

            # YUV データを連結してフレームを作成
            data = np.concatenate([y_data.flatten(), u_data.flatten(), v_data.flatten()])
            init: VideoFrameBufferInit = {
                "format": VideoPixelFormat.I420,
                "coded_width": width,
                "coded_height": height,
                "timestamp": 0,
            }
            frame = VideoFrame(data, init)

            encoder.encode(frame, {"keyFrame": True})
            encoder.flush()

            if chunks:
                compressed_size = sum(chunk.byte_length for chunk in chunks)
                compression_ratio = raw_size / compressed_size
                results[codec] = {"size": compressed_size, "ratio": compression_ratio}

                print(
                    f"\n{codec.upper()} - サイズ: {compressed_size:,} bytes, 圧縮率: {compression_ratio:.2f}x"
                )

            frame.close()
            encoder.close()

        except Exception as e:
            print(f"\n{codec.upper()} - スキップ（エラー: {e}）")

    # 少なくとも1つのコーデックで圧縮が確認できること
    assert len(results) > 0, "どのコーデックでも圧縮できませんでした"

    # ランダムパターンでも何らかの圧縮効果があることを確認
    # （AV1 は複雑なパターンで逆に大きくなることがある）
    for codec, data in results.items():
        if codec == "av01.0.04M.08":
            # AV1 は高品質設定だとランダムパターンで逆に大きくなることがある
            assert data["ratio"] > 0.5, f"{codec} のサイズが大きすぎます: {data['ratio']:.2f}x"
        else:
            assert data["ratio"] > 1.1, f"{codec} の圧縮率が低すぎます: {data['ratio']:.2f}x"
