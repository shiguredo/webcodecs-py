"""ビデオエンコード・デコード品質テスト

このテストは AV1 ビデオコーデックのエンコード・デコード品質を検証します。
PSNR (Peak Signal-to-Noise Ratio) と SSIM (Structural Similarity Index) を使用して品質を評価します。
"""

import numpy as np
import pytest
from typing import Callable
from webcodecs import (
    VideoDecoder,
    VideoDecoderConfig,
    VideoEncoder,
    VideoEncoderConfig,
    VideoFrame,
    VideoFrameBufferInit,
    VideoPixelFormat,
)
from video_test_helpers import (
    generate_checkerboard_pattern,
    generate_gradient_pattern,
    generate_edge_pattern,
    generate_solid_color_pattern,
)


def calculate_psnr(original: np.ndarray, compressed: np.ndarray) -> float:
    """PSNR (Peak Signal-to-Noise Ratio) を計算する

    Args:
        original: 元の画像データ
        compressed: 圧縮後の画像データ

    Returns:
        PSNR 値 (dB)。MSE が 0 の場合は inf を返す
    """
    mse = np.mean((original.astype(float) - compressed.astype(float)) ** 2)
    if mse == 0:
        return float("inf")
    max_pixel = 255.0
    psnr = 20 * np.log10(max_pixel / np.sqrt(mse))
    return psnr


def calculate_ssim(original: np.ndarray, compressed: np.ndarray, window_size: int = 7) -> float:
    """簡易版 SSIM (Structural Similarity Index) を計算する

    Args:
        original: 元の画像データ
        compressed: 圧縮後の画像データ
        window_size: 未使用（互換性のため）

    Returns:
        SSIM 値 (0.0〜1.0)
    """
    # 簡易実装：平均、分散、共分散を使用
    k1, k2 = 0.01, 0.03
    L = 255  # ピクセル値の最大値
    c1 = (k1 * L) ** 2
    c2 = (k2 * L) ** 2

    # 平均
    mu1 = np.mean(original)
    mu2 = np.mean(compressed)

    # 分散と共分散
    sigma1_sq = np.var(original)
    sigma2_sq = np.var(compressed)
    sigma12 = np.mean((original - mu1) * (compressed - mu2))

    # SSIM 計算
    numerator = (2 * mu1 * mu2 + c1) * (2 * sigma12 + c2)
    denominator = (mu1**2 + mu2**2 + c1) * (sigma1_sq + sigma2_sq + c2)
    ssim = numerator / denominator

    return float(ssim)


@pytest.mark.parametrize("codec", ["av01.0.04M.08"])
@pytest.mark.parametrize(
    "pattern_generator,pattern_name,min_psnr",
    [
        (generate_checkerboard_pattern, "checkerboard", 30),
        (generate_gradient_pattern, "gradient", 25),
        (generate_edge_pattern, "edge", 20),
        (generate_solid_color_pattern, "solid", 40),
    ],
)
def test_encode_decode_quality_patterns(
    codec: str, pattern_generator: Callable, pattern_name: str, min_psnr: float
):
    """ビデオコーデックで異なるパターンの品質を検証する

    チェッカーボード、グラデーション、エッジ、単色の 4 つのテストパターンを使用して、
    ビデオエンコード・デコードの品質を PSNR と SSIM で評価します。
    """
    width, height = 320, 240

    # テストパターンを生成
    if pattern_generator == generate_solid_color_pattern:
        y_orig, u_orig, v_orig = pattern_generator(width, height, 128)
    else:
        y_orig, u_orig, v_orig = pattern_generator(width, height)

    # エンコーダ設定
    encoded_chunks = []

    def on_output(chunk):
        encoded_chunks.append(chunk)

    def on_error(error):
        pytest.fail(f"{codec} エンコーダエラー: {error}")

    encoder = VideoEncoder(on_output, on_error)
    encoder_config: VideoEncoderConfig = {
        "codec": codec,
        "width": width,
        "height": height,
        "bitrate": 1_500_000,  # 統一ビットレート
        "framerate": 30.0,
    }
    encoder.configure(encoder_config)

    # フレームを作成してデータをコピー
    # YUV データを連結
    data = np.concatenate([y_orig.flatten(), u_orig.flatten(), v_orig.flatten()])
    init: VideoFrameBufferInit = {
        "format": VideoPixelFormat.I420,
        "coded_width": width,
        "coded_height": height,
        "timestamp": 0,
    }
    frame = VideoFrame(data, init)

    # エンコード
    encoder.encode(frame, {"keyFrame": True})
    encoder.flush()

    assert len(encoded_chunks) > 0, f"{codec} でエンコードされたチャンクが生成されませんでした"

    # デコーダ設定
    decoded_frames = []
    decoder = VideoDecoder(
        lambda f: decoded_frames.append(f),
        lambda err: pytest.fail(f"{codec} デコーダエラー: {err}"),
    )
    decoder_config: VideoDecoderConfig = {"codec": codec}
    decoder.configure(decoder_config)

    # デコード
    for chunk in encoded_chunks:
        decoder.decode(chunk)
    decoder.flush()

    assert len(decoded_frames) > 0, f"{codec} でデコードされたフレームが生成されませんでした"

    # デコードされたフレームからデータを取得
    decoded_frame = decoded_frames[0]
    y_dec, u_dec, v_dec = decoded_frame.planes()

    # 品質評価
    psnr_y = calculate_psnr(y_orig, np.array(y_dec))
    psnr_u = calculate_psnr(u_orig, np.array(u_dec))
    psnr_v = calculate_psnr(v_orig, np.array(v_dec))
    ssim_y = calculate_ssim(y_orig, np.array(y_dec))

    # 圧縮サイズの計算
    total_size = sum(chunk.byte_length for chunk in encoded_chunks)
    raw_size = y_orig.nbytes + u_orig.nbytes + v_orig.nbytes
    compression_ratio = raw_size / total_size

    print(f"\n{codec.upper()} - {pattern_name}:")
    print(
        f"  圧縮率: {compression_ratio:.2f}x (生: {raw_size:,} bytes → 圧縮: {total_size:,} bytes)"
    )
    print(f"  PSNR - Y: {psnr_y:.2f} dB, U: {psnr_u:.2f} dB, V: {psnr_v:.2f} dB")
    print(f"  SSIM - Y: {ssim_y:.4f}")

    # パターンごとの最小 PSNR を確認
    assert psnr_y > min_psnr, (
        f"{codec} の {pattern_name} パターンで Y プレーンの PSNR が低すぎます: {psnr_y:.2f} dB (最小: {min_psnr} dB)"
    )

    # クリーンアップ
    frame.close()
    decoded_frame.close()
    encoder.close()
    decoder.close()


@pytest.mark.parametrize("codec", ["av01.0.04M.08"])
def test_encode_decode_bitrate_comparison(codec: str):
    """ビデオコーデックでビットレートが品質に与える影響を比較する

    100kbps、500kbps、1Mbps、2Mbps の 4 つのビットレートで、
    グラデーションパターンをエンコード・デコードし、品質を比較します。
    ビットレートが上がるにつれて品質も向上することを確認します。
    """
    width, height = 320, 240

    # グラデーションパターンを使用（品質差が見やすい）
    y_orig, u_orig, v_orig = generate_gradient_pattern(width, height)

    bitrates = [100_000, 500_000, 1_000_000, 2_000_000]
    results = []

    for bitrate in bitrates:
        # エンコーダ設定
        encoded_chunks = []

        def on_output(chunk):
            encoded_chunks.append(chunk)

        def on_error(error):
            pytest.fail(f"{codec} エンコーダエラー: {error}")

        encoder = VideoEncoder(on_output, on_error)
        encoder_config: VideoEncoderConfig = {
            "codec": codec,
            "width": width,
            "height": height,
            "bitrate": bitrate,
            "framerate": 30.0,
        }
        encoder.configure(encoder_config)

        # フレームを作成
        # YUV データを連結
        data = np.concatenate([y_orig.flatten(), u_orig.flatten(), v_orig.flatten()])
        init: VideoFrameBufferInit = {
            "format": VideoPixelFormat.I420,
            "coded_width": width,
            "coded_height": height,
            "timestamp": 0,
        }
        frame = VideoFrame(data, init)

        # エンコード
        encoder.encode(frame, {"keyFrame": True})
        encoder.flush()

        if encoded_chunks:
            # デコード
            decoded_frames = []
            decoder = VideoDecoder(
                lambda f: decoded_frames.append(f),
                lambda err: pytest.fail(f"{codec} デコーダエラー: {err}"),
            )
            decoder_config: VideoDecoderConfig = {"codec": codec}
            decoder.configure(decoder_config)

            for chunk in encoded_chunks:
                decoder.decode(chunk)
            decoder.flush()

            if decoded_frames:
                y_dec, _, _ = decoded_frames[0].planes()
                psnr = calculate_psnr(y_orig, np.array(y_dec))
                total_size = sum(chunk.byte_length for chunk in encoded_chunks)
                results.append((bitrate, psnr, total_size))
                decoded_frames[0].close()

            decoder.close()

        frame.close()
        encoder.close()

    # 結果の表示
    print(f"\n{codec.upper()} ビットレート比較:")
    for bitrate, psnr, size in results:
        print(f"  {bitrate:8,} bps: PSNR={psnr:6.2f} dB, サイズ={size:6,} bytes")

    # ビットレートが上がるにつれて品質も向上することを確認（許容誤差 2dB）
    for i in range(1, len(results)):
        assert results[i][1] >= results[i - 1][1] - 2, (
            f"{codec} でビットレート増加により品質が大幅に低下: {results[i - 1][0]}bps ({results[i - 1][1]:.2f}dB) -> {results[i][0]}bps ({results[i][1]:.2f}dB)"
        )


@pytest.mark.parametrize("codec", ["av01.0.04M.08"])
@pytest.mark.parametrize("width,height", [(160, 120), (320, 240), (640, 480)])
def test_encode_decode_frame_size_comparison(codec: str, width: int, height: int):
    """ビデオコーデックで異なるフレームサイズの品質を比較する

    160x120、320x240、640x480 の 3 つのフレームサイズで、
    チェッカーボードパターンをエンコード・デコードし、品質を評価します。
    フレームサイズに関わらず一定の品質を維持することを確認します。
    """
    # チェッカーボードパターンを生成（サイズに応じて四角のサイズを調整）
    square_size = max(16, width // 20)
    y_orig, u_orig, v_orig = generate_checkerboard_pattern(width, height, square_size)

    # エンコーダ設定（ビットレートをフレームサイズに比例）
    encoded_chunks = []

    def on_output(chunk):
        encoded_chunks.append(chunk)

    def on_error(error):
        pytest.fail(f"{codec} エンコーダエラー: {error}")

    encoder = VideoEncoder(on_output, on_error)
    encoder_config: VideoEncoderConfig = {
        "codec": codec,
        "width": width,
        "height": height,
        "bitrate": width * height * 10,  # ピクセルあたり10ビット
        "framerate": 30.0,
    }
    encoder.configure(encoder_config)

    # フレームを作成
    # YUV データを連結
    data = np.concatenate([y_orig.flatten(), u_orig.flatten(), v_orig.flatten()])
    init: VideoFrameBufferInit = {
        "format": VideoPixelFormat.I420,
        "coded_width": width,
        "coded_height": height,
        "timestamp": 0,
    }
    frame = VideoFrame(data, init)

    # エンコード
    encoder.encode(frame, {"keyFrame": True})
    encoder.flush()

    assert len(encoded_chunks) > 0, f"{codec} でエンコード失敗: {width}x{height}"

    # デコード
    decoded_frames = []
    decoder = VideoDecoder(
        lambda f: decoded_frames.append(f),
        lambda err: pytest.fail(f"{codec} デコーダエラー: {err}"),
    )
    decoder_config: VideoDecoderConfig = {"codec": codec}
    decoder.configure(decoder_config)

    for chunk in encoded_chunks:
        decoder.decode(chunk)
    decoder.flush()

    assert len(decoded_frames) > 0, f"{codec} でデコード失敗: {width}x{height}"

    # 品質評価
    decoded_frame = decoded_frames[0]
    y_dec, u_dec, v_dec = decoded_frame.planes()
    psnr_y = calculate_psnr(y_orig, np.array(y_dec))

    # 圧縮サイズの計算
    total_size = sum(chunk.byte_length for chunk in encoded_chunks)
    raw_size = y_orig.nbytes + u_orig.nbytes + v_orig.nbytes
    compression_ratio = raw_size / total_size

    print(f"\n{codec.upper()} - {width}x{height}:")
    print(f"  ビットレート: {encoder_config['bitrate']:,} bps")
    print(f"  圧縮率: {compression_ratio:.2f}x")
    print(f"  Y プレーン PSNR: {psnr_y:.2f} dB")

    # サイズに関わらず一定の品質を維持（チェッカーボードは圧縮しやすいので高い値を期待）
    assert psnr_y > 30, f"{codec} の品質が低すぎます ({width}x{height}): {psnr_y:.2f} dB"

    # クリーンアップ
    frame.close()
    decoded_frame.close()
    encoder.close()
    decoder.close()
