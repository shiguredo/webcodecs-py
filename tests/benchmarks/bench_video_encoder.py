"""
映像エンコード性能ベンチマーク

実行方法:
    # benchmark グループをインストール
    uv sync --group benchmark

    # VideoToolbox を有効にして実行
    APPLE_VIDEO_TOOLBOX=1 uv run pytest tests/benchmarks/bench_video_encoder.py -v

    # 詳細な統計を表示
    APPLE_VIDEO_TOOLBOX=1 uv run pytest tests/benchmarks/bench_video_encoder.py -v --benchmark-verbose

    # 結果を JSON に保存
    APPLE_VIDEO_TOOLBOX=1 uv run pytest tests/benchmarks/bench_video_encoder.py -v --benchmark-json=benchmark.json
"""

import os
import sys

import numpy as np
import pytest

from webcodecs import (
    HardwareAccelerationEngine,
    VideoEncoder,
    VideoEncoderConfig,
    VideoFrame,
    VideoFrameBufferInit,
    VideoPixelFormat,
)

from blend2d import Context, Image


# 解像度の定義
RESOLUTIONS = {
    "720p": (1280, 720),
    "1080p": (1920, 1080),
}


def skip_if_no_video_toolbox():
    """VideoToolbox が有効でない場合はスキップ"""
    if sys.platform != "darwin":
        pytest.skip("VideoToolbox is only available on macOS")
    if not os.environ.get("APPLE_VIDEO_TOOLBOX"):
        pytest.skip("Set APPLE_VIDEO_TOOLBOX=1 to run VideoToolbox benchmarks")


def create_gradient_frame_blend2d(width: int, height: int, frame_number: int) -> np.ndarray:
    """
    blend2d を使ってグラデーションフレームを生成する
    動きのあるパターンを生成して、エンコーダーの性能を測定する
    """
    image = Image(width, height)
    ctx = Context(image)

    # 背景を塗りつぶし（フレームごとに色を変化）
    r = int(128 + 127 * np.sin(frame_number * 0.1))
    g = int(128 + 127 * np.sin(frame_number * 0.1 + 2))
    b = int(128 + 127 * np.sin(frame_number * 0.1 + 4))
    ctx.set_fill_style_rgba(r, g, b, 255)
    ctx.fill_all()

    # 動く円を描画
    num_circles = 5
    for i in range(num_circles):
        angle = frame_number * 0.05 + i * 1.26
        cx = width / 2 + (width / 4) * np.cos(angle)
        cy = height / 2 + (height / 4) * np.sin(angle)
        radius = 30 + 20 * np.sin(frame_number * 0.1 + i * 0.5)

        ctx.set_fill_style_rgba(
            (i * 50 + frame_number * 3) % 256,
            (i * 80 + frame_number * 5) % 256,
            (i * 110 + frame_number * 7) % 256,
            200,
        )
        ctx.fill_circle(cx, cy, radius)

    ctx.end()

    # BGRA 形式で取得
    return image.asarray()


def create_video_frame(
    data: np.ndarray,
    width: int,
    height: int,
    pixel_format: VideoPixelFormat,
    timestamp: int,
) -> VideoFrame:
    """VideoFrame を作成するヘルパー関数"""
    init = VideoFrameBufferInit(
        format=pixel_format,
        coded_width=width,
        coded_height=height,
        timestamp=timestamp,
    )
    return VideoFrame(data, init)


class TestFrameGeneration:
    """フレーム生成のベンチマーク"""

    def test_blend2d_frame_generation_720p(self, benchmark):
        """720p フレーム生成（blend2d）"""
        width, height = RESOLUTIONS["720p"]

        def generate():
            return create_gradient_frame_blend2d(width, height, 0)

        result = benchmark(generate)
        assert result.shape == (height, width, 4)

    def test_blend2d_frame_generation_1080p(self, benchmark):
        """1080p フレーム生成（blend2d）"""
        width, height = RESOLUTIONS["1080p"]

        def generate():
            return create_gradient_frame_blend2d(width, height, 0)

        result = benchmark(generate)
        assert result.shape == (height, width, 4)


class TestVideoFrameCreation:
    """VideoFrame 作成のベンチマーク"""

    def test_video_frame_creation_720p(self, benchmark):
        """720p VideoFrame 作成"""
        width, height = RESOLUTIONS["720p"]
        # I420 データを事前に準備
        i420_size = width * height + (width // 2) * (height // 2) * 2
        data = np.random.randint(0, 256, i420_size, dtype=np.uint8)

        def create_frame():
            return create_video_frame(data, width, height, VideoPixelFormat.I420, 0)

        result = benchmark(create_frame)
        assert result.coded_width == width
        assert result.coded_height == height

    def test_video_frame_creation_1080p(self, benchmark):
        """1080p VideoFrame 作成"""
        width, height = RESOLUTIONS["1080p"]
        i420_size = width * height + (width // 2) * (height // 2) * 2
        data = np.random.randint(0, 256, i420_size, dtype=np.uint8)

        def create_frame():
            return create_video_frame(data, width, height, VideoPixelFormat.I420, 0)

        result = benchmark(create_frame)
        assert result.coded_width == width
        assert result.coded_height == height


class TestVideoEncoderH264:
    """H.264 エンコードのベンチマーク"""

    def test_h264_encode_single_frame_720p(self, benchmark):
        """720p H.264 単一フレームエンコード"""
        skip_if_no_video_toolbox()
        width, height = RESOLUTIONS["720p"]

        output_chunks = []

        def on_output(chunk):
            output_chunks.append(chunk)

        def on_error(error):
            raise RuntimeError(f"Encoder error: {error}")

        config: VideoEncoderConfig = {
            "codec": "avc1.42001f",
            "width": width,
            "height": height,
            "bitrate": 5_000_000,
            "framerate": 30,
            "hardware_acceleration_engine": HardwareAccelerationEngine.APPLE_VIDEO_TOOLBOX,
        }

        encoder = VideoEncoder(on_output, on_error)
        encoder.configure(config)

        # I420 データを事前に準備
        i420_size = width * height + (width // 2) * (height // 2) * 2
        data = np.random.randint(0, 256, i420_size, dtype=np.uint8)
        timestamp = [0]

        def encode_one_frame():
            frame = create_video_frame(data, width, height, VideoPixelFormat.I420, timestamp[0])
            encoder.encode(frame, {"keyFrame": True})
            encoder.flush()
            timestamp[0] += 33333

        benchmark(encode_one_frame)

        encoder.close()

    def test_h264_encode_single_frame_1080p(self, benchmark):
        """1080p H.264 単一フレームエンコード"""
        skip_if_no_video_toolbox()
        width, height = RESOLUTIONS["1080p"]

        output_chunks = []

        def on_output(chunk):
            output_chunks.append(chunk)

        def on_error(error):
            raise RuntimeError(f"Encoder error: {error}")

        config: VideoEncoderConfig = {
            "codec": "avc1.640028",
            "width": width,
            "height": height,
            "bitrate": 10_000_000,
            "framerate": 30,
            "hardware_acceleration_engine": HardwareAccelerationEngine.APPLE_VIDEO_TOOLBOX,
        }

        encoder = VideoEncoder(on_output, on_error)
        encoder.configure(config)

        i420_size = width * height + (width // 2) * (height // 2) * 2
        data = np.random.randint(0, 256, i420_size, dtype=np.uint8)
        timestamp = [0]

        def encode_one_frame():
            frame = create_video_frame(data, width, height, VideoPixelFormat.I420, timestamp[0])
            encoder.encode(frame, {"keyFrame": True})
            encoder.flush()
            timestamp[0] += 33333

        benchmark(encode_one_frame)

        encoder.close()


class TestVideoEncoderH265:
    """H.265 エンコードのベンチマーク"""

    def test_h265_encode_single_frame_720p(self, benchmark):
        """720p H.265 単一フレームエンコード"""
        skip_if_no_video_toolbox()
        width, height = RESOLUTIONS["720p"]

        output_chunks = []

        def on_output(chunk):
            output_chunks.append(chunk)

        def on_error(error):
            raise RuntimeError(f"Encoder error: {error}")

        config: VideoEncoderConfig = {
            "codec": "hvc1.1.6.L93.B0",
            "width": width,
            "height": height,
            "bitrate": 3_000_000,
            "framerate": 30,
            "hardware_acceleration_engine": HardwareAccelerationEngine.APPLE_VIDEO_TOOLBOX,
        }

        encoder = VideoEncoder(on_output, on_error)
        encoder.configure(config)

        i420_size = width * height + (width // 2) * (height // 2) * 2
        data = np.random.randint(0, 256, i420_size, dtype=np.uint8)
        timestamp = [0]

        def encode_one_frame():
            frame = create_video_frame(data, width, height, VideoPixelFormat.I420, timestamp[0])
            encoder.encode(frame, {"keyFrame": True})
            encoder.flush()
            timestamp[0] += 33333

        benchmark(encode_one_frame)

        encoder.close()

    def test_h265_encode_single_frame_1080p(self, benchmark):
        """1080p H.265 単一フレームエンコード"""
        skip_if_no_video_toolbox()
        width, height = RESOLUTIONS["1080p"]

        output_chunks = []

        def on_output(chunk):
            output_chunks.append(chunk)

        def on_error(error):
            raise RuntimeError(f"Encoder error: {error}")

        config: VideoEncoderConfig = {
            "codec": "hvc1.1.6.L120.B0",
            "width": width,
            "height": height,
            "bitrate": 6_000_000,
            "framerate": 30,
            "hardware_acceleration_engine": HardwareAccelerationEngine.APPLE_VIDEO_TOOLBOX,
        }

        encoder = VideoEncoder(on_output, on_error)
        encoder.configure(config)

        i420_size = width * height + (width // 2) * (height // 2) * 2
        data = np.random.randint(0, 256, i420_size, dtype=np.uint8)
        timestamp = [0]

        def encode_one_frame():
            frame = create_video_frame(data, width, height, VideoPixelFormat.I420, timestamp[0])
            encoder.encode(frame, {"keyFrame": True})
            encoder.flush()
            timestamp[0] += 33333

        benchmark(encode_one_frame)

        encoder.close()


class TestEndToEndPipeline:
    """フレーム生成からエンコードまでの総合ベンチマーク"""

    def test_blend2d_to_h264_720p(self, benchmark):
        """blend2d フレーム生成 + H.264 エンコード (720p)"""
        skip_if_no_video_toolbox()
        width, height = RESOLUTIONS["720p"]

        output_chunks = []

        def on_output(chunk):
            output_chunks.append(chunk)

        def on_error(error):
            raise RuntimeError(f"Encoder error: {error}")

        config: VideoEncoderConfig = {
            "codec": "avc1.42001f",
            "width": width,
            "height": height,
            "bitrate": 5_000_000,
            "framerate": 30,
            "hardware_acceleration_engine": HardwareAccelerationEngine.APPLE_VIDEO_TOOLBOX,
        }

        encoder = VideoEncoder(on_output, on_error)
        encoder.configure(config)

        frame_number = [0]

        def pipeline():
            # blend2d でフレーム生成（BGRA）
            bgra = create_gradient_frame_blend2d(width, height, frame_number[0])

            # BGRA VideoFrame を作成して I420 に変換
            bgra_frame = create_video_frame(
                bgra, width, height, VideoPixelFormat.BGRA, frame_number[0] * 33333
            )

            # I420 に変換
            i420_size = bgra_frame.allocation_size({"format": VideoPixelFormat.I420})
            i420_buffer = np.zeros(i420_size, dtype=np.uint8)
            bgra_frame.copy_to(i420_buffer, {"format": VideoPixelFormat.I420})
            bgra_frame.close()

            # I420 VideoFrame を作成
            i420_frame = create_video_frame(
                i420_buffer, width, height, VideoPixelFormat.I420, frame_number[0] * 33333
            )

            # エンコード
            encoder.encode(i420_frame, {"keyFrame": frame_number[0] % 30 == 0})
            encoder.flush()

            frame_number[0] += 1

        benchmark(pipeline)

        encoder.close()

    def test_blend2d_to_h264_1080p(self, benchmark):
        """blend2d フレーム生成 + H.264 エンコード (1080p)"""
        skip_if_no_video_toolbox()
        width, height = RESOLUTIONS["1080p"]

        output_chunks = []

        def on_output(chunk):
            output_chunks.append(chunk)

        def on_error(error):
            raise RuntimeError(f"Encoder error: {error}")

        config: VideoEncoderConfig = {
            "codec": "avc1.640028",
            "width": width,
            "height": height,
            "bitrate": 10_000_000,
            "framerate": 30,
            "hardware_acceleration_engine": HardwareAccelerationEngine.APPLE_VIDEO_TOOLBOX,
        }

        encoder = VideoEncoder(on_output, on_error)
        encoder.configure(config)

        frame_number = [0]

        def pipeline():
            bgra = create_gradient_frame_blend2d(width, height, frame_number[0])

            bgra_frame = create_video_frame(
                bgra, width, height, VideoPixelFormat.BGRA, frame_number[0] * 33333
            )

            i420_size = bgra_frame.allocation_size({"format": VideoPixelFormat.I420})
            i420_buffer = np.zeros(i420_size, dtype=np.uint8)
            bgra_frame.copy_to(i420_buffer, {"format": VideoPixelFormat.I420})
            bgra_frame.close()

            i420_frame = create_video_frame(
                i420_buffer, width, height, VideoPixelFormat.I420, frame_number[0] * 33333
            )

            encoder.encode(i420_frame, {"keyFrame": frame_number[0] % 30 == 0})
            encoder.flush()

            frame_number[0] += 1

        benchmark(pipeline)

        encoder.close()


class TestFlushOverhead:
    """flush() のオーバーヘッド測定"""

    def test_h264_encode_without_flush_720p(self, benchmark):
        """720p H.264 エンコードのみ（flush なし）"""
        skip_if_no_video_toolbox()
        width, height = RESOLUTIONS["720p"]

        output_chunks = []

        def on_output(chunk):
            output_chunks.append(chunk)

        def on_error(error):
            raise RuntimeError(f"Encoder error: {error}")

        config: VideoEncoderConfig = {
            "codec": "avc1.42001f",
            "width": width,
            "height": height,
            "bitrate": 5_000_000,
            "framerate": 30,
            "hardware_acceleration_engine": HardwareAccelerationEngine.APPLE_VIDEO_TOOLBOX,
        }

        encoder = VideoEncoder(on_output, on_error)
        encoder.configure(config)

        i420_size = width * height + (width // 2) * (height // 2) * 2
        data = np.random.randint(0, 256, i420_size, dtype=np.uint8)
        timestamp = [0]

        def encode_only():
            frame = create_video_frame(data, width, height, VideoPixelFormat.I420, timestamp[0])
            encoder.encode(frame, {"keyFrame": True})
            timestamp[0] += 33333

        benchmark(encode_only)

        encoder.flush()
        encoder.close()

    def test_h264_encode_without_flush_1080p(self, benchmark):
        """1080p H.264 エンコードのみ（flush なし）"""
        skip_if_no_video_toolbox()
        width, height = RESOLUTIONS["1080p"]

        output_chunks = []

        def on_output(chunk):
            output_chunks.append(chunk)

        def on_error(error):
            raise RuntimeError(f"Encoder error: {error}")

        config: VideoEncoderConfig = {
            "codec": "avc1.640028",
            "width": width,
            "height": height,
            "bitrate": 10_000_000,
            "framerate": 30,
            "hardware_acceleration_engine": HardwareAccelerationEngine.APPLE_VIDEO_TOOLBOX,
        }

        encoder = VideoEncoder(on_output, on_error)
        encoder.configure(config)

        i420_size = width * height + (width // 2) * (height // 2) * 2
        data = np.random.randint(0, 256, i420_size, dtype=np.uint8)
        timestamp = [0]

        def encode_only():
            frame = create_video_frame(data, width, height, VideoPixelFormat.I420, timestamp[0])
            encoder.encode(frame, {"keyFrame": True})
            timestamp[0] += 33333

        benchmark(encode_only)

        encoder.flush()
        encoder.close()


class TestH264ProfileComparison:
    """H.264 プロファイル別のエンコード性能比較"""

    def test_h264_baseline_1080p(self, benchmark):
        """1080p H.264 Baseline プロファイル"""
        skip_if_no_video_toolbox()
        width, height = RESOLUTIONS["1080p"]

        output_chunks = []

        def on_output(chunk):
            output_chunks.append(chunk)

        def on_error(error):
            raise RuntimeError(f"Encoder error: {error}")

        # Baseline Profile Level 4.0
        config: VideoEncoderConfig = {
            "codec": "avc1.42E028",
            "width": width,
            "height": height,
            "bitrate": 10_000_000,
            "framerate": 30,
            "hardware_acceleration_engine": HardwareAccelerationEngine.APPLE_VIDEO_TOOLBOX,
        }

        encoder = VideoEncoder(on_output, on_error)
        encoder.configure(config)

        i420_size = width * height + (width // 2) * (height // 2) * 2
        data = np.random.randint(0, 256, i420_size, dtype=np.uint8)
        timestamp = [0]

        def encode_one_frame():
            frame = create_video_frame(data, width, height, VideoPixelFormat.I420, timestamp[0])
            encoder.encode(frame, {"keyFrame": True})
            encoder.flush()
            timestamp[0] += 33333

        benchmark(encode_one_frame)

        encoder.close()

    def test_h264_main_1080p(self, benchmark):
        """1080p H.264 Main プロファイル"""
        skip_if_no_video_toolbox()
        width, height = RESOLUTIONS["1080p"]

        output_chunks = []

        def on_output(chunk):
            output_chunks.append(chunk)

        def on_error(error):
            raise RuntimeError(f"Encoder error: {error}")

        # Main Profile Level 4.0
        config: VideoEncoderConfig = {
            "codec": "avc1.4D4028",
            "width": width,
            "height": height,
            "bitrate": 10_000_000,
            "framerate": 30,
            "hardware_acceleration_engine": HardwareAccelerationEngine.APPLE_VIDEO_TOOLBOX,
        }

        encoder = VideoEncoder(on_output, on_error)
        encoder.configure(config)

        i420_size = width * height + (width // 2) * (height // 2) * 2
        data = np.random.randint(0, 256, i420_size, dtype=np.uint8)
        timestamp = [0]

        def encode_one_frame():
            frame = create_video_frame(data, width, height, VideoPixelFormat.I420, timestamp[0])
            encoder.encode(frame, {"keyFrame": True})
            encoder.flush()
            timestamp[0] += 33333

        benchmark(encode_one_frame)

        encoder.close()

    def test_h264_high_1080p(self, benchmark):
        """1080p H.264 High プロファイル"""
        skip_if_no_video_toolbox()
        width, height = RESOLUTIONS["1080p"]

        output_chunks = []

        def on_output(chunk):
            output_chunks.append(chunk)

        def on_error(error):
            raise RuntimeError(f"Encoder error: {error}")

        # High Profile Level 4.0
        config: VideoEncoderConfig = {
            "codec": "avc1.640028",
            "width": width,
            "height": height,
            "bitrate": 10_000_000,
            "framerate": 30,
            "hardware_acceleration_engine": HardwareAccelerationEngine.APPLE_VIDEO_TOOLBOX,
        }

        encoder = VideoEncoder(on_output, on_error)
        encoder.configure(config)

        i420_size = width * height + (width // 2) * (height // 2) * 2
        data = np.random.randint(0, 256, i420_size, dtype=np.uint8)
        timestamp = [0]

        def encode_one_frame():
            frame = create_video_frame(data, width, height, VideoPixelFormat.I420, timestamp[0])
            encoder.encode(frame, {"keyFrame": True})
            encoder.flush()
            timestamp[0] += 33333

        benchmark(encode_one_frame)

        encoder.close()


class TestWarmupEffect:
    """ウォームアップ効果の測定（初回 flush vs 2回目以降）"""

    def test_h264_warmup_effect_1080p(self, benchmark):
        """1080p H.264 ウォームアップ後のエンコード"""
        skip_if_no_video_toolbox()
        width, height = RESOLUTIONS["1080p"]

        output_chunks = []

        def on_output(chunk):
            output_chunks.append(chunk)

        def on_error(error):
            raise RuntimeError(f"Encoder error: {error}")

        config: VideoEncoderConfig = {
            "codec": "avc1.640028",
            "width": width,
            "height": height,
            "bitrate": 10_000_000,
            "framerate": 30,
            "hardware_acceleration_engine": HardwareAccelerationEngine.APPLE_VIDEO_TOOLBOX,
        }

        encoder = VideoEncoder(on_output, on_error)
        encoder.configure(config)

        i420_size = width * height + (width // 2) * (height // 2) * 2
        data = np.random.randint(0, 256, i420_size, dtype=np.uint8)

        # ウォームアップ: 最初の 5 フレームをエンコード
        for i in range(5):
            frame = create_video_frame(data, width, height, VideoPixelFormat.I420, i * 33333)
            encoder.encode(frame, {"keyFrame": i == 0})
        encoder.flush()

        timestamp = [5 * 33333]

        def encode_after_warmup():
            frame = create_video_frame(data, width, height, VideoPixelFormat.I420, timestamp[0])
            encoder.encode(frame, {"keyFrame": True})
            encoder.flush()
            timestamp[0] += 33333

        benchmark(encode_after_warmup)

        encoder.close()

    def test_h265_warmup_effect_1080p(self, benchmark):
        """1080p H.265 ウォームアップ後のエンコード"""
        skip_if_no_video_toolbox()
        width, height = RESOLUTIONS["1080p"]

        output_chunks = []

        def on_output(chunk):
            output_chunks.append(chunk)

        def on_error(error):
            raise RuntimeError(f"Encoder error: {error}")

        config: VideoEncoderConfig = {
            "codec": "hvc1.1.6.L120.B0",
            "width": width,
            "height": height,
            "bitrate": 6_000_000,
            "framerate": 30,
            "hardware_acceleration_engine": HardwareAccelerationEngine.APPLE_VIDEO_TOOLBOX,
        }

        encoder = VideoEncoder(on_output, on_error)
        encoder.configure(config)

        i420_size = width * height + (width // 2) * (height // 2) * 2
        data = np.random.randint(0, 256, i420_size, dtype=np.uint8)

        # ウォームアップ: 最初の 5 フレームをエンコード
        for i in range(5):
            frame = create_video_frame(data, width, height, VideoPixelFormat.I420, i * 33333)
            encoder.encode(frame, {"keyFrame": i == 0})
        encoder.flush()

        timestamp = [5 * 33333]

        def encode_after_warmup():
            frame = create_video_frame(data, width, height, VideoPixelFormat.I420, timestamp[0])
            encoder.encode(frame, {"keyFrame": True})
            encoder.flush()
            timestamp[0] += 33333

        benchmark(encode_after_warmup)

        encoder.close()


class TestBatchEncoding:
    """バッチエンコードのベンチマーク（連続フレーム処理）"""

    def test_h264_encode_30_frames_720p(self, benchmark):
        """720p H.264 30フレーム連続エンコード"""
        skip_if_no_video_toolbox()
        width, height = RESOLUTIONS["720p"]

        def run_batch():
            output_chunks = []

            def on_output(chunk):
                output_chunks.append(chunk)

            def on_error(error):
                raise RuntimeError(f"Encoder error: {error}")

            config: VideoEncoderConfig = {
                "codec": "avc1.42001f",
                "width": width,
                "height": height,
                "bitrate": 5_000_000,
                "framerate": 30,
                "hardware_acceleration_engine": HardwareAccelerationEngine.APPLE_VIDEO_TOOLBOX,
            }

            encoder = VideoEncoder(on_output, on_error)
            encoder.configure(config)

            # I420 データを事前に準備
            i420_size = width * height + (width // 2) * (height // 2) * 2
            data = np.random.randint(0, 256, i420_size, dtype=np.uint8)

            # 30 フレームをエンコード
            for i in range(30):
                frame = create_video_frame(data, width, height, VideoPixelFormat.I420, i * 33333)
                encoder.encode(frame, {"keyFrame": i == 0})

            encoder.flush()
            encoder.close()

            return len(output_chunks)

        result = benchmark(run_batch)
        assert result >= 1

    def test_h264_encode_30_frames_1080p(self, benchmark):
        """1080p H.264 30フレーム連続エンコード"""
        skip_if_no_video_toolbox()
        width, height = RESOLUTIONS["1080p"]

        def run_batch():
            output_chunks = []

            def on_output(chunk):
                output_chunks.append(chunk)

            def on_error(error):
                raise RuntimeError(f"Encoder error: {error}")

            config: VideoEncoderConfig = {
                "codec": "avc1.640028",
                "width": width,
                "height": height,
                "bitrate": 10_000_000,
                "framerate": 30,
                "hardware_acceleration_engine": HardwareAccelerationEngine.APPLE_VIDEO_TOOLBOX,
            }

            encoder = VideoEncoder(on_output, on_error)
            encoder.configure(config)

            i420_size = width * height + (width // 2) * (height // 2) * 2
            data = np.random.randint(0, 256, i420_size, dtype=np.uint8)

            for i in range(30):
                frame = create_video_frame(data, width, height, VideoPixelFormat.I420, i * 33333)
                encoder.encode(frame, {"keyFrame": i == 0})

            encoder.flush()
            encoder.close()

            return len(output_chunks)

        result = benchmark(run_batch)
        assert result >= 1
