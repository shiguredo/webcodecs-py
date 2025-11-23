"""ビデオテスト用ユーティリティ関数"""

import numpy as np
from typing import Tuple
from webcodecs import VideoFrame, VideoFrameBufferInit, VideoPixelFormat


# ============================================
# VideoFrame 作成関数
# ============================================


def create_video_frame_from_size(
    width: int, height: int, format: VideoPixelFormat, timestamp: int = 0
) -> VideoFrame:
    """指定サイズの VideoFrame を作成するヘルパー関数

    WebCodecs API 形式で VideoFrame を作成します。

    Args:
        width: フレーム幅
        height: フレーム高さ
        format: ピクセルフォーマット
        timestamp: タイムスタンプ（マイクロ秒）

    Returns:
        VideoFrame: 作成された VideoFrame
    """
    # フォーマットに応じたデータサイズを計算
    if format == VideoPixelFormat.I420:
        data_size = width * height * 3 // 2
    elif format == VideoPixelFormat.I422:
        data_size = width * height * 2
    elif format == VideoPixelFormat.I444:
        data_size = width * height * 3
    elif format == VideoPixelFormat.NV12:
        data_size = width * height * 3 // 2
    elif format in (VideoPixelFormat.RGBA, VideoPixelFormat.BGRA):
        data_size = width * height * 4
    elif format in (VideoPixelFormat.RGB, VideoPixelFormat.BGR):
        data_size = width * height * 3
    else:
        raise ValueError(f"Unsupported format: {format}")

    # データバッファを作成
    data = np.zeros(data_size, dtype=np.uint8)

    # VideoFrameBufferInit を作成
    init: VideoFrameBufferInit = {
        "format": format,
        "coded_width": width,
        "coded_height": height,
        "timestamp": timestamp,
    }

    return VideoFrame(data, init)


def create_video_frame_from_yuv(
    y_plane: np.ndarray,
    u_plane: np.ndarray,
    v_plane: np.ndarray,
    width: int,
    height: int,
    format: VideoPixelFormat = VideoPixelFormat.I420,
    timestamp: int = 0,
) -> VideoFrame:
    """YUV プレーンから VideoFrame を作成

    Args:
        y_plane: Y プレーンのデータ
        u_plane: U プレーンのデータ
        v_plane: V プレーンのデータ
        width: フレーム幅
        height: フレーム高さ
        format: ピクセルフォーマット（デフォルト: I420）
        timestamp: タイムスタンプ（マイクロ秒）

    Returns:
        VideoFrame: 作成された VideoFrame
    """
    # プレーンを結合して連続したバッファを作成
    if format == VideoPixelFormat.I420:
        data = np.concatenate([y_plane.flatten(), u_plane.flatten(), v_plane.flatten()])
    else:
        raise ValueError(f"Unsupported format for YUV: {format}")

    # VideoFrameBufferInit を作成
    init: VideoFrameBufferInit = {
        "format": format,
        "coded_width": width,
        "coded_height": height,
        "timestamp": timestamp,
    }

    return VideoFrame(data, init)


# ============================================
# YUV パターン生成関数
# ============================================


def generate_test_frame_yuv420(
    width: int, height: int, frame_num: int = 0
) -> Tuple[np.ndarray, np.ndarray, np.ndarray]:
    """numpy で テスト用 YUV420 フレームを生成する

    フレーム番号によって変化するシンプルなパターンを作成する。

    Args:
        width: フレーム幅
        height: フレーム高さ
        frame_num: フレーム番号（パターン変化用）

    Returns:
        Y, U, V プレーンのタプル
    """
    # Y プレーン（輝度）
    y_plane = np.zeros((height, width), dtype=np.uint8)
    # グラデーションパターンを作成
    for y in range(height):
        for x in range(width):
            # フレーム番号でシフトする対角グラデーション
            value = (x + y + frame_num * 10) % 256
            y_plane[y, x] = value

    # U と V プレーン（色差） - 半分の解像度
    uv_height = height // 2
    uv_width = width // 2

    # U プレーン - 青黄グラデーション
    u_plane = np.zeros((uv_height, uv_width), dtype=np.uint8)
    for y in range(uv_height):
        for x in range(uv_width):
            u_plane[y, x] = (128 + (x - uv_width // 2) * 2) % 256

    # V プレーン - 赤緑グラデーション
    v_plane = np.zeros((uv_height, uv_width), dtype=np.uint8)
    for y in range(uv_height):
        for x in range(uv_width):
            v_plane[y, x] = (128 + (y - uv_height // 2) * 2) % 256

    return y_plane, u_plane, v_plane


def generate_test_pattern_yuv420(
    width: int, height: int, pattern_type: str = "gradient", frame_num: int = 0
) -> Tuple[np.ndarray, np.ndarray, np.ndarray]:
    """様々なテストパターンを YUV420 フォーマットで生成する

    Args:
        width: フレーム幅
        height: フレーム高さ
        pattern_type: パターンタイプ ("gradient", "checkerboard", "solid", "edge", "color_bars")
        frame_num: フレーム番号（アニメーション用）

    Returns:
        Y, U, V プレーンのタプル
    """
    # Y プレーン
    y_plane = np.zeros((height, width), dtype=np.uint8)

    if pattern_type == "gradient":
        # 対角グラデーションパターン（フレーム番号でアニメーション）
        for y in range(height):
            for x in range(width):
                y_plane[y, x] = (x + y + frame_num * 5) % 256

    elif pattern_type == "checkerboard":
        # チェッカーボードパターン
        block_size = 32
        for y in range(height):
            for x in range(width):
                if ((x // block_size) + (y // block_size)) % 2 == 0:
                    y_plane[y, x] = 255
                else:
                    y_plane[y, x] = 0

    elif pattern_type == "solid":
        # 単色パターン（グレー）
        y_plane[:] = 128

    elif pattern_type == "edge":
        # エッジパターン（縦・横のライン）
        # 背景を中間グレーに
        y_plane[:] = 128
        # 縦ライン
        for x in range(0, width, 40):
            y_plane[:, x : min(x + 2, width)] = 255
        # 横ライン
        for y in range(0, height, 40):
            y_plane[y : min(y + 2, height), :] = 0

    elif pattern_type == "color_bars":
        # SMPTE カラーバーパターン
        bar_width = width // 8
        colors_y = [235, 210, 170, 145, 106, 81, 41, 16]  # Y値
        for y in range(height):
            for x in range(width):
                bar_index = min(x // bar_width, 7)
                y_plane[y, x] = colors_y[bar_index]

    # U と V プレーン - 半分の解像度
    uv_height = height // 2
    uv_width = width // 2

    # パターンに応じて色差を調整
    u_plane = np.full((uv_height, uv_width), 128, dtype=np.uint8)
    v_plane = np.full((uv_height, uv_width), 128, dtype=np.uint8)

    if pattern_type == "gradient":
        # グラデーションパターンに色を追加
        for y in range(uv_height):
            for x in range(uv_width):
                u_plane[y, x] = (128 + x * 256 // uv_width - 128) % 256
                v_plane[y, x] = (128 + y * 256 // uv_height - 128) % 256

    elif pattern_type == "color_bars":
        # カラーバーに色を追加
        colors_u = [128, 16, 166, 54, 202, 90, 240, 128]
        colors_v = [128, 138, 142, 152, 156, 166, 170, 128]
        bar_width_uv = uv_width // 8
        for y in range(uv_height):
            for x in range(uv_width):
                bar_index = min(x // bar_width_uv, 7)
                u_plane[y, x] = colors_u[bar_index]
                v_plane[y, x] = colors_v[bar_index]

    return y_plane, u_plane, v_plane


def generate_checkerboard_pattern(
    width: int, height: int, block_size: int = 32
) -> Tuple[np.ndarray, np.ndarray, np.ndarray]:
    """チェッカーボードパターンを YUV420 で生成する

    Args:
        width: フレーム幅
        height: フレーム高さ
        block_size: チェッカーボードのブロックサイズ

    Returns:
        Y, U, V プレーンのタプル
    """
    y_plane = np.zeros((height, width), dtype=np.uint8)

    for y in range(height):
        for x in range(width):
            if ((x // block_size) + (y // block_size)) % 2 == 0:
                y_plane[y, x] = 255
            else:
                y_plane[y, x] = 0

    # 色差プレーンは中間値（グレー）
    uv_height = height // 2
    uv_width = width // 2
    u_plane = np.full((uv_height, uv_width), 128, dtype=np.uint8)
    v_plane = np.full((uv_height, uv_width), 128, dtype=np.uint8)

    return y_plane, u_plane, v_plane


def generate_gradient_pattern(width: int, height: int) -> Tuple[np.ndarray, np.ndarray, np.ndarray]:
    """グラデーションパターンを YUV420 で生成する

    Args:
        width: フレーム幅
        height: フレーム高さ

    Returns:
        Y, U, V プレーンのタプル
    """
    y_plane = np.zeros((height, width), dtype=np.uint8)

    # 対角グラデーション
    for y in range(height):
        for x in range(width):
            value = int((x / width + y / height) * 128)
            y_plane[y, x] = value

    # 色差プレーン
    uv_height = height // 2
    uv_width = width // 2

    u_plane = np.zeros((uv_height, uv_width), dtype=np.uint8)
    v_plane = np.zeros((uv_height, uv_width), dtype=np.uint8)

    for y in range(uv_height):
        for x in range(uv_width):
            u_plane[y, x] = int((x / uv_width) * 255)
            v_plane[y, x] = int((y / uv_height) * 255)

    return y_plane, u_plane, v_plane


def generate_complex_pattern(width: int, height: int) -> Tuple[np.ndarray, np.ndarray, np.ndarray]:
    """複雑なパターン（高圧縮率を必要とする）を生成する

    Args:
        width: フレーム幅
        height: フレーム高さ

    Returns:
        Y, U, V プレーンのタプル
    """
    # ノイズの多い複雑なパターン
    np.random.seed(42)  # 再現性のためシード固定
    y_plane = np.random.randint(0, 256, (height, width), dtype=np.uint8)

    uv_height = height // 2
    uv_width = width // 2
    u_plane = np.random.randint(64, 192, (uv_height, uv_width), dtype=np.uint8)
    v_plane = np.random.randint(64, 192, (uv_height, uv_width), dtype=np.uint8)

    return y_plane, u_plane, v_plane


def generate_simple_pattern(width: int, height: int) -> Tuple[np.ndarray, np.ndarray, np.ndarray]:
    """単純なパターン（高圧縮率が期待できる）を生成する

    Args:
        width: フレーム幅
        height: フレーム高さ

    Returns:
        Y, U, V プレーンのタプル
    """
    # 単色のシンプルなパターン
    y_plane = np.full((height, width), 128, dtype=np.uint8)

    uv_height = height // 2
    uv_width = width // 2
    u_plane = np.full((uv_height, uv_width), 128, dtype=np.uint8)
    v_plane = np.full((uv_height, uv_width), 128, dtype=np.uint8)

    return y_plane, u_plane, v_plane


def generate_solid_color_pattern(
    width: int, height: int, y_val: int = 128
) -> Tuple[np.ndarray, np.ndarray, np.ndarray]:
    """単色パターンを生成する（ロスレス圧縮のテスト）

    Args:
        width: フレーム幅
        height: フレーム高さ
        y_val: Y プレーンの値 (0-255)

    Returns:
        Y, U, V プレーンのタプル
    """
    y_plane = np.full((height, width), y_val, dtype=np.uint8)

    uv_height = height // 2
    uv_width = width // 2
    u_plane = np.full((uv_height, uv_width), 128, dtype=np.uint8)
    v_plane = np.full((uv_height, uv_width), 128, dtype=np.uint8)

    return y_plane, u_plane, v_plane


def generate_edge_pattern(width: int, height: int) -> Tuple[np.ndarray, np.ndarray, np.ndarray]:
    """エッジパターンを生成する（高周波成分のテスト）

    Args:
        width: フレーム幅
        height: フレーム高さ

    Returns:
        Y, U, V プレーンのタプル
    """
    y_plane = np.zeros((height, width), dtype=np.uint8)

    # 垂直・水平のストライプパターン
    stripe_width = 4
    for y in range(height):
        for x in range(width):
            if (x // stripe_width) % 2 == 0:
                y_plane[y, x] = 235
            else:
                y_plane[y, x] = 16

            # 中央に水平ストライプを追加
            if height // 3 < y < 2 * height // 3:
                if (y // stripe_width) % 2 == 0:
                    y_plane[y, x] = 235 - y_plane[y, x] + 16

    uv_height = height // 2
    uv_width = width // 2
    u_plane = np.full((uv_height, uv_width), 128, dtype=np.uint8)
    v_plane = np.full((uv_height, uv_width), 128, dtype=np.uint8)

    return y_plane, u_plane, v_plane
