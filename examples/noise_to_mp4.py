#!/usr/bin/env python3
"""
コーデックテスト用のノイズ付きアニメーションを生成して MP4 ファイルに出力するサンプル

ビットレート負荷が高い映像を生成します:
- 黒背景に軽いノイズ
- 複数のカラフルな図形（四角・円）が異なる速度・方向で動く
- 動き予測が難しく、空間周波数が揺らぐ

必要な依存関係:
    uv add blend2d-py mp4-py

使い方:
    uv run python examples/noise_to_mp4.py
    uv run python examples/noise_to_mp4.py --codec h264 --output output.mp4
    uv run python examples/noise_to_mp4.py --codec h265 --width 1920 --height 1080 --bitrate 5000000
    uv run python examples/noise_to_mp4.py --codec av1 --width 1280 --height 720 --shapes 30
"""

import argparse
import random
import sys
from typing import Union

import numpy as np
from blend2d import CompOp, Context, Image
from mp4 import (
    Mp4FileMuxer,
    Mp4MuxSample,
    Mp4SampleEntryAv01,
    Mp4SampleEntryAvc1,
    Mp4SampleEntryHev1,
)

from webcodecs import (
    EncodedVideoChunkType,
    HardwareAccelerationEngine,
    LatencyMode,
    VideoEncoder,
    VideoEncoderBitrateMode,
    VideoEncoderConfig,
    VideoFrame,
    VideoFrameBufferInit,
    VideoPixelFormat,
)


class MovingShape:
    """アニメーションする図形の基底クラス"""

    def __init__(
        self, x: float, y: float, vx: float, vy: float, r: int, g: int, b: int, alpha: int
    ):
        self.x = x
        self.y = y
        self.vx = vx
        self.vy = vy
        self.r = r
        self.g = g
        self.b = b
        self.alpha = alpha
        self.vx_noise = random.uniform(-0.2, 0.2)
        self.vy_noise = random.uniform(-0.2, 0.2)

    def update(self, screen_width: int, screen_height: int, frame: int):
        """位置を更新し、画面端で跳ね返る。速度も微妙に変化させる"""
        noise_factor = 0.1 * np.sin(frame * 0.05)
        self.vx += self.vx_noise * noise_factor
        self.vy += self.vy_noise * noise_factor

        max_speed = 10.0
        self.vx = max(-max_speed, min(max_speed, self.vx))
        self.vy = max(-max_speed, min(max_speed, self.vy))

        self.x += self.vx
        self.y += self.vy

    def check_bounds(self, screen_width: int, screen_height: int):
        """サブクラスで実装"""
        pass

    def draw(self, ctx: Context):
        """サブクラスで実装"""
        pass


class MovingRect(MovingShape):
    """アニメーションする四角形"""

    def __init__(
        self,
        x: float,
        y: float,
        width: int,
        height: int,
        vx: float,
        vy: float,
        r: int,
        g: int,
        b: int,
        alpha: int,
    ):
        super().__init__(x, y, vx, vy, r, g, b, alpha)
        self.width = width
        self.height = height

    def check_bounds(self, screen_width: int, screen_height: int):
        """画面端で跳ね返る"""
        if self.x <= 0 or self.x + self.width >= screen_width:
            self.vx = -self.vx
            self.x = max(0.0, min(self.x, screen_width - self.width))

        if self.y <= 0 or self.y + self.height >= screen_height:
            self.vy = -self.vy
            self.y = max(0.0, min(self.y, screen_height - self.height))

    def draw(self, ctx: Context):
        """四角形を描画"""
        ctx.set_fill_style_rgba(self.r, self.g, self.b, self.alpha)
        ctx.fill_rect(self.x, self.y, self.width, self.height)


class MovingCircle(MovingShape):
    """アニメーションする円"""

    def __init__(
        self,
        x: float,
        y: float,
        radius: int,
        vx: float,
        vy: float,
        r: int,
        g: int,
        b: int,
        alpha: int,
    ):
        super().__init__(x, y, vx, vy, r, g, b, alpha)
        self.radius = radius

    def check_bounds(self, screen_width: int, screen_height: int):
        """画面端で跳ね返る"""
        if self.x - self.radius <= 0 or self.x + self.radius >= screen_width:
            self.vx = -self.vx
            self.x = max(self.radius, min(self.x, screen_width - self.radius))

        if self.y - self.radius <= 0 or self.y + self.radius >= screen_height:
            self.vy = -self.vy
            self.y = max(self.radius, min(self.y, screen_height - self.radius))

    def draw(self, ctx: Context):
        """円を描画"""
        ctx.set_fill_style_rgba(self.r, self.g, self.b, self.alpha)
        ctx.fill_circle(self.x, self.y, self.radius)


def generate_noise_cache(
    width: int, height: int, num_patterns: int = 10, noise_intensity: int = 15
) -> list[np.ndarray]:
    """ノイズパターンを事前生成してキャッシュ"""
    cache = []
    for _ in range(num_patterns):
        noise = np.random.normal(0, noise_intensity, (height, width, 4)).astype(np.int16)
        cache.append(noise)
    return cache


def add_noise_cached(
    frame: np.ndarray, noise_cache: list[np.ndarray], frame_num: int
) -> np.ndarray:
    """キャッシュされたノイズを適用"""
    noise = noise_cache[frame_num % len(noise_cache)]
    noisy_frame = np.clip(frame.astype(np.int16) + noise, 0, 255).astype(np.uint8)
    return noisy_frame


def create_shapes(width: int, height: int, num_shapes: int) -> list[MovingShape]:
    """アニメーション用の図形を作成"""
    colors = [
        (255, 0, 0),
        (0, 255, 0),
        (0, 0, 255),
        (255, 255, 0),
        (255, 0, 255),
        (0, 255, 255),
        (255, 128, 0),
        (128, 0, 255),
        (64, 255, 64),
        (255, 192, 203),
        (128, 128, 255),
        (255, 128, 128),
    ]

    shapes: list[MovingShape] = []
    for _ in range(num_shapes):
        x = random.randint(50, width - 150)
        y = random.randint(50, height - 150)
        vx = random.uniform(-8.0, 8.0)
        vy = random.uniform(-8.0, 8.0)
        color = random.choice(colors)
        alpha = random.randint(120, 200)

        if random.random() < 0.5:
            w = random.randint(40, 120)
            h = random.randint(40, 120)
            shapes.append(MovingRect(x, y, w, h, vx, vy, *color, alpha))
        else:
            r = random.randint(20, 60)
            shapes.append(MovingCircle(x, y, r, vx, vy, *color, alpha))

    return shapes


def parse_avcc(
    avcc_data: bytes,
) -> tuple[int, int, int, list[bytes], list[bytes], int | None, int | None, int | None]:
    """avcC box をパース"""
    if len(avcc_data) < 7:
        raise ValueError("avcC data is too short")

    profile_idc = avcc_data[1]
    profile_compatibility = avcc_data[2]
    level_idc = avcc_data[3]
    num_sps = avcc_data[5] & 0x1F

    pos = 6
    sps_list = []
    for _ in range(num_sps):
        if pos + 2 > len(avcc_data):
            raise ValueError("avcC data is truncated (SPS length)")
        sps_length = int.from_bytes(avcc_data[pos : pos + 2], "big")
        pos += 2
        if pos + sps_length > len(avcc_data):
            raise ValueError("avcC data is truncated (SPS data)")
        sps_list.append(avcc_data[pos : pos + sps_length])
        pos += sps_length

    if pos >= len(avcc_data):
        raise ValueError("avcC data is truncated (num PPS)")
    num_pps = avcc_data[pos]
    pos += 1

    pps_list = []
    for _ in range(num_pps):
        if pos + 2 > len(avcc_data):
            raise ValueError("avcC data is truncated (PPS length)")
        pps_length = int.from_bytes(avcc_data[pos : pos + 2], "big")
        pos += 2
        if pos + pps_length > len(avcc_data):
            raise ValueError("avcC data is truncated (PPS data)")
        pps_list.append(avcc_data[pos : pos + pps_length])
        pos += pps_length

    chroma_format = None
    bit_depth_luma_minus8 = None
    bit_depth_chroma_minus8 = None

    if profile_idc in (100, 110, 122, 244, 44, 83, 86, 118, 128, 138, 139, 134):
        if pos + 4 <= len(avcc_data):
            chroma_format = avcc_data[pos] & 0x03
            pos += 1
            bit_depth_luma_minus8 = avcc_data[pos] & 0x07
            pos += 1
            bit_depth_chroma_minus8 = avcc_data[pos] & 0x07
            pos += 1

    return (
        profile_idc,
        profile_compatibility,
        level_idc,
        sps_list,
        pps_list,
        chroma_format,
        bit_depth_luma_minus8,
        bit_depth_chroma_minus8,
    )


def parse_hvcc(hvcc_data: bytes) -> tuple[int, int, list[int], list[bytes]]:
    """hvcC box をパース"""
    if len(hvcc_data) < 23:
        raise ValueError("hvcC data is too short")

    general_profile_idc = hvcc_data[1] & 0x1F
    general_level_idc = hvcc_data[12]
    num_arrays = hvcc_data[22]
    pos = 23

    nalu_types = []
    nalu_data = []

    for _ in range(num_arrays):
        if pos + 3 > len(hvcc_data):
            break
        nal_unit_type = hvcc_data[pos] & 0x3F
        pos += 1
        num_nalus = int.from_bytes(hvcc_data[pos : pos + 2], "big")
        pos += 2

        for _ in range(num_nalus):
            if pos + 2 > len(hvcc_data):
                break
            nalu_length = int.from_bytes(hvcc_data[pos : pos + 2], "big")
            pos += 2
            if pos + nalu_length > len(hvcc_data):
                break
            nalu_types.append(nal_unit_type)
            nalu_data.append(hvcc_data[pos : pos + nalu_length])
            pos += nalu_length

    return general_profile_idc, general_level_idc, nalu_types, nalu_data


class MP4Writer:
    """MP4 ファイルへの書き込みを行うクラス"""

    def __init__(self, filename: str, width: int, height: int, fps: int, codec: str):
        self.filename = filename
        self.width = width
        self.height = height
        self.fps = fps
        self.codec = codec
        self.timescale = 30000
        self.frame_duration = self.timescale // fps
        self.muxer: Mp4FileMuxer | None = None
        self.sample_entry: (
            Union[Mp4SampleEntryAv01, Mp4SampleEntryAvc1, Mp4SampleEntryHev1] | None
        ) = None
        self.frame_count = 0
        self.description: bytes | None = None

    def start(self):
        """Muxer を開始"""
        self.muxer = Mp4FileMuxer(self.filename)
        self.muxer.__enter__()

    def set_description(self, description: bytes):
        """metadata.decoder_config.description を設定する"""
        self.description = description

    def write(self, chunk_data: bytes, is_key_frame: bool):
        """フレームを書き込み"""
        if self.muxer is None:
            raise RuntimeError("Muxer が開始されていません")

        if self.sample_entry is None:
            if not is_key_frame:
                raise RuntimeError("最初のフレームはキーフレームである必要があります")

            if self.codec == "av1":
                config_obus = self._extract_av1_config_obus(chunk_data)
                self.sample_entry = Mp4SampleEntryAv01(
                    width=self.width,
                    height=self.height,
                    config_obus=config_obus,
                    seq_profile=0,
                    seq_level_idx_0=8,
                    seq_tier_0=0,
                )
            elif self.codec == "h264":
                if self.description is None:
                    raise RuntimeError(
                        "H.264: metadata.decoder_config.description が設定されていません"
                    )
                (
                    profile_idc,
                    profile_compat,
                    level_idc,
                    sps_list,
                    pps_list,
                    chroma_format,
                    bit_depth_luma_minus8,
                    bit_depth_chroma_minus8,
                ) = parse_avcc(self.description)
                self.sample_entry = Mp4SampleEntryAvc1(
                    width=self.width,
                    height=self.height,
                    avc_profile_indication=profile_idc,
                    profile_compatibility=profile_compat,
                    avc_level_indication=level_idc,
                    sps_data=sps_list,
                    pps_data=pps_list,
                    chroma_format=chroma_format,
                    bit_depth_luma_minus8=bit_depth_luma_minus8,
                    bit_depth_chroma_minus8=bit_depth_chroma_minus8,
                )
            elif self.codec == "h265":
                if self.description is None:
                    raise RuntimeError(
                        "H.265: metadata.decoder_config.description が設定されていません"
                    )
                profile_idc, level_idc, nalu_types, nalu_data = parse_hvcc(self.description)
                self.sample_entry = Mp4SampleEntryHev1(
                    width=self.width,
                    height=self.height,
                    general_profile_idc=profile_idc,
                    general_level_idc=level_idc,
                    nalu_types=nalu_types,
                    nalu_data=nalu_data,
                )
            else:
                raise RuntimeError(f"サポートされていないコーデック: {self.codec}")

        sample = Mp4MuxSample(
            track_kind="video",
            sample_entry=self.sample_entry,
            keyframe=is_key_frame,
            timescale=self.timescale,
            duration=self.frame_duration,
            data=chunk_data,
        )

        self.muxer.append_sample(sample)
        self.frame_count += 1

    def _extract_av1_config_obus(self, chunk_data: bytes) -> bytes:
        """AV1 chunk から config OBUs を抽出する"""
        return chunk_data

    def stop(self):
        """Muxer を停止"""
        if self.muxer is not None:
            self.muxer.finalize()
            self.muxer.__exit__(None, None, None)
            self.muxer = None


def main():
    parser = argparse.ArgumentParser(
        description="コーデックテスト用のノイズ付きアニメーションを生成して MP4 ファイルに出力"
    )
    parser.add_argument("--width", type=int, default=1280, help="映像の幅（デフォルト: 1280）")
    parser.add_argument("--height", type=int, default=720, help="映像の高さ（デフォルト: 720）")
    parser.add_argument("--fps", type=int, default=30, help="フレームレート（デフォルト: 30）")
    parser.add_argument(
        "--duration", type=int, default=15, help="映像の長さ（秒）（デフォルト: 15）"
    )
    parser.add_argument(
        "--bitrate", type=int, default=3000000, help="ビットレート（デフォルト: 3000000）"
    )
    parser.add_argument(
        "--codec",
        type=str,
        choices=["av1", "h264", "h265"],
        default="av1",
        help="コーデック（デフォルト: av1）",
    )
    parser.add_argument(
        "--output",
        type=str,
        default="noise_output.mp4",
        help="出力ファイル名（デフォルト: noise_output.mp4）",
    )
    parser.add_argument("--shapes", type=int, default=20, help="図形の数（デフォルト: 20）")
    parser.add_argument(
        "--noise-intensity", type=int, default=40, help="ノイズ強度（デフォルト: 40）"
    )

    args = parser.parse_args()

    width = args.width
    height = args.height
    fps = args.fps
    total_frames = fps * args.duration
    codec = args.codec

    print("=== コーデックテスト用ノイズアニメーション -> MP4 ===")
    print(f"コーデック: {codec.upper()}")
    print(f"解像度: {width}x{height}")
    print(f"フレームレート: {fps} fps")
    print(f"映像の長さ: {args.duration} 秒")
    print(f"総フレーム数: {total_frames}")
    print(f"ビットレート: {args.bitrate} bps ({args.bitrate / 1000:.0f} kbps)")
    print(f"図形の数: {args.shapes}")
    print(f"ノイズ強度: {args.noise_intensity}")
    print(f"出力ファイル: {args.output}")
    print()

    print("図形とノイズキャッシュを生成中...")
    shapes = create_shapes(width, height, args.shapes)
    noise_cache = generate_noise_cache(
        width, height, num_patterns=10, noise_intensity=args.noise_intensity
    )

    mp4_writer = MP4Writer(args.output, width, height, fps, codec)
    mp4_writer.start()

    encoded_frame_count = 0

    def on_output(chunk, metadata=None):
        nonlocal encoded_frame_count
        if metadata is not None:
            decoder_config = metadata.get("decoder_config")
            if decoder_config is not None:
                description = decoder_config.get("description")
                if description is not None:
                    mp4_writer.set_description(bytes(description))

        destination = np.zeros(chunk.byte_length, dtype=np.uint8)
        chunk.copy_to(destination)
        frame_data = bytes(destination)
        is_key_frame = chunk.type == EncodedVideoChunkType.KEY
        mp4_writer.write(frame_data, is_key_frame)
        encoded_frame_count += 1

        chunk_type = "Key" if is_key_frame else "Delta"
        print(
            f"  フレーム {encoded_frame_count:4d}/{total_frames}: {chunk_type:5s} "
            f"{chunk.byte_length:6d} bytes, timestamp={chunk.timestamp}"
        )

    def on_error(error):
        print(f"エンコーダーエラー: {error}", file=sys.stderr)

    encoder = VideoEncoder(on_output, on_error)

    if codec == "av1":
        codec_string = "av01.0.04M.08"
        encoder_config: VideoEncoderConfig = {
            "codec": codec_string,
            "width": width,
            "height": height,
            "bitrate": args.bitrate,
            "framerate": float(fps),
            "bitrate_mode": VideoEncoderBitrateMode.CONSTANT,
            "latency_mode": LatencyMode.REALTIME,
        }
    elif codec == "h264":
        codec_string = "avc1.640033"
        encoder_config: VideoEncoderConfig = {
            "codec": codec_string,
            "width": width,
            "height": height,
            "bitrate": args.bitrate,
            "framerate": float(fps),
            "bitrate_mode": VideoEncoderBitrateMode.CONSTANT,
            "latency_mode": LatencyMode.REALTIME,
            "hardware_acceleration_engine": HardwareAccelerationEngine.APPLE_VIDEO_TOOLBOX,
            "avc": {"format": "avc"},
        }
    elif codec == "h265":
        codec_string = "hvc1.1.6.L120.B0"
        encoder_config: VideoEncoderConfig = {
            "codec": codec_string,
            "width": width,
            "height": height,
            "bitrate": args.bitrate,
            "framerate": float(fps),
            "bitrate_mode": VideoEncoderBitrateMode.CONSTANT,
            "latency_mode": LatencyMode.REALTIME,
            "hardware_acceleration_engine": HardwareAccelerationEngine.APPLE_VIDEO_TOOLBOX,
            "hevc": {"format": "hevc"},
        }
    else:
        raise RuntimeError(f"サポートされていないコーデック: {codec}")

    encoder.configure(encoder_config)
    print("エンコーダーを初期化しました")
    print(f"  コーデック: {encoder_config['codec']}")
    if codec in ("h264", "h265"):
        print("  ハードウェアアクセラレーション: Apple Video Toolbox")
    print(f"  ビットレート: {args.bitrate} bps ({args.bitrate / 1000:.0f} kbps)")
    print()

    timestamp = 0
    frame_duration = 1_000_000 // fps

    print("フレームの生成とエンコードを開始します...")
    print()

    img = Image(width, height)

    try:
        for frame_count in range(total_frames):
            with Context(img) as ctx:
                ctx.set_comp_op(CompOp.SRC_COPY)
                ctx.set_fill_style_rgba(0, 0, 0, 255)
                ctx.fill_all()

                ctx.set_comp_op(CompOp.SRC_OVER)

                for shape in shapes:
                    shape.update(width, height, frame_count)
                    shape.check_bounds(width, height)
                    shape.draw(ctx)

            bgra = img.asarray()
            bgra = add_noise_cached(bgra, noise_cache, frame_count)

            init: VideoFrameBufferInit = {
                "format": VideoPixelFormat.BGRA,
                "coded_width": width,
                "coded_height": height,
                "timestamp": timestamp,
            }
            with VideoFrame(bgra, init) as bgra_frame:
                i420_size = bgra_frame.allocation_size({"format": VideoPixelFormat.I420})
                i420_buffer = np.zeros(i420_size, dtype=np.uint8)
                bgra_frame.copy_to(i420_buffer, {"format": VideoPixelFormat.I420})

            i420_init: VideoFrameBufferInit = {
                "format": VideoPixelFormat.I420,
                "coded_width": width,
                "coded_height": height,
                "timestamp": timestamp,
            }
            with VideoFrame(i420_buffer, i420_init) as i420_frame:
                key_frame = frame_count == 0 or frame_count % (fps * 3) == 0
                encoder.encode(i420_frame, {"key_frame": key_frame})

            timestamp += frame_duration

    except Exception as e:
        print(f"\nエラーが発生しました: {e}", file=sys.stderr)
        return 1

    print(f"\n合計 {total_frames} フレームを生成しました")

    print("エンコーダーをフラッシュしています...")
    encoder.flush()
    encoder.close()

    print(f"エンコードされたチャンク数: {encoded_frame_count}")
    print()

    print(f"MP4 ファイルを完了しています: {args.output}")
    mp4_writer.stop()

    print(f"ファイルを保存しました: {args.output}")
    print()

    print("=== 完了 ===")
    return 0


if __name__ == "__main__":
    sys.exit(main())
