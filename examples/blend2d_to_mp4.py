#!/usr/bin/env python3
"""
blend2d-py でダミー映像を生成し、webcodecs-py でエンコードして mp4-py で MP4 ファイルに出力するサンプル

必要な依存関係:
    uv add blend2d-py mp4-py

使い方:
    uv run python examples/blend2d_to_mp4.py
    uv run python examples/blend2d_to_mp4.py --codec h264 --output output.mp4
    uv run python examples/blend2d_to_mp4.py --codec h265 --output output.mp4 --width 1920 --height 1080
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


class AnimatedCircle:
    """アニメーションする円クラス"""

    def __init__(self, x, y, radius, vx, vy, r, g, b, alpha):
        self.x = x
        self.y = y
        self.radius = radius
        self.vx = vx
        self.vy = vy
        self.r = r
        self.g = g
        self.b = b
        self.alpha = alpha

    def update(self, screen_width, screen_height):
        """位置を更新し、画面端で跳ね返る"""
        self.x += self.vx
        self.y += self.vy

        # 左右の壁で跳ね返る
        if self.x - self.radius <= 0 or self.x + self.radius >= screen_width:
            self.vx = -self.vx
            self.x = max(self.radius, min(self.x, screen_width - self.radius))

        # 上下の壁で跳ね返る
        if self.y - self.radius <= 0 or self.y + self.radius >= screen_height:
            self.vy = -self.vy
            self.y = max(self.radius, min(self.y, screen_height - self.radius))

    def draw(self, ctx):
        """円を描画"""
        ctx.set_fill_style_rgba(self.r, self.g, self.b, self.alpha)
        ctx.fill_circle(self.x, self.y, self.radius)


def parse_avcc(
    avcc_data: bytes,
) -> tuple[int, int, int, list[bytes], list[bytes], int | None, int | None, int | None]:
    """avcC box をパースして profile, compatibility, level, SPS リスト, PPS リスト,
    chroma_format, bit_depth_luma_minus8, bit_depth_chroma_minus8 を返す

    High Profile では追加の拡張フィールドが含まれる場合がある
    """
    if len(avcc_data) < 7:
        raise ValueError("avcC data is too short")

    # configurationVersion = avcc_data[0] (通常 1)
    profile_idc = avcc_data[1]
    profile_compatibility = avcc_data[2]
    level_idc = avcc_data[3]
    # length_size_minus_one = avcc_data[4] & 0x03
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

    # High Profile (100), High 10 (110), High 4:2:2 (122), High 4:4:4 (244) では
    # 追加の拡張フィールドがある
    chroma_format = None
    bit_depth_luma_minus8 = None
    bit_depth_chroma_minus8 = None

    if profile_idc in (100, 110, 122, 244, 44, 83, 86, 118, 128, 138, 139, 134):
        if pos + 4 <= len(avcc_data):
            # chroma_format_idc (2 bits) at bits [6:7] of byte
            chroma_format = avcc_data[pos] & 0x03
            pos += 1
            # bit_depth_luma_minus8 (3 bits)
            bit_depth_luma_minus8 = avcc_data[pos] & 0x07
            pos += 1
            # bit_depth_chroma_minus8 (3 bits)
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
    """hvcC box をパースして profile_idc, level_idc, NAL unit types, NAL unit data を返す

    hvcC (HEVCDecoderConfigurationRecord) の構造は複雑なため、
    主要なフィールドを抽出します。
    """
    if len(hvcc_data) < 23:
        raise ValueError("hvcC data is too short")

    # configurationVersion = hvcc_data[0] (通常 1)
    # general_profile_space (2 bits) + general_tier_flag (1 bit) + general_profile_idc (5 bits)
    general_profile_idc = hvcc_data[1] & 0x1F
    # general_profile_compatibility_flags (4 bytes)
    # general_constraint_indicator_flags (6 bytes)
    # general_level_idc (1 byte) at offset 12
    general_level_idc = hvcc_data[12]

    # numOfArrays at offset 22
    num_arrays = hvcc_data[22]
    pos = 23

    nalu_types = []
    nalu_data = []

    for _ in range(num_arrays):
        if pos + 3 > len(hvcc_data):
            break
        # array_completeness (1 bit) + reserved (1 bit) + NAL_unit_type (6 bits)
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
        self.timescale = 30000  # MP4 タイムスケール
        self.frame_duration = self.timescale // fps  # 各フレームの duration
        self.muxer: Mp4FileMuxer | None = None
        self.sample_entry: (
            Union[Mp4SampleEntryAv01, Mp4SampleEntryAvc1, Mp4SampleEntryHev1] | None
        ) = None
        self.frame_count = 0
        # metadata から取得した description (avcC/hvcC)
        self.description: bytes | None = None

    def start(self):
        """Muxer を開始"""
        self.muxer = Mp4FileMuxer(self.filename)
        self.muxer.__enter__()

    def set_description(self, description: bytes):
        """metadata.decoderConfig.description を設定する (avcC/hvcC)"""
        self.description = description

    def write(self, chunk_data: bytes, is_keyframe: bool):
        """フレームを書き込み"""
        if self.muxer is None:
            raise RuntimeError("Muxer が開始されていません")

        # 最初のキーフレームから設定情報を抽出して sample_entry を作成
        if self.sample_entry is None:
            if not is_keyframe:
                raise RuntimeError("最初のフレームはキーフレームである必要があります")

            if self.codec == "av1":
                # AV1 の config_obus を抽出（最初の OBU シーケンス）
                config_obus = self._extract_av1_config_obus(chunk_data)
                self.sample_entry = Mp4SampleEntryAv01(
                    width=self.width,
                    height=self.height,
                    config_obus=config_obus,
                    seq_profile=0,  # Main Profile
                    seq_level_idx_0=8,  # Level 4.0
                    seq_tier_0=0,  # Main tier
                )
            elif self.codec == "h264":
                # H.264: metadata.decoderConfig.description (avcC) を使用
                if self.description is None:
                    raise RuntimeError(
                        "H.264: metadata.decoderConfig.description が設定されていません"
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
                # H.265: metadata.decoderConfig.description (hvcC) を使用
                if self.description is None:
                    raise RuntimeError(
                        "H.265: metadata.decoderConfig.description が設定されていません"
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

        # MP4 サンプルを作成
        # 注: H.264/H.265 は avc/hevc フォーマット (length-prefixed) で出力されるため変換不要
        sample = Mp4MuxSample(
            track_kind="video",
            sample_entry=self.sample_entry,
            keyframe=is_keyframe,
            timescale=self.timescale,
            duration=self.frame_duration,
            data=chunk_data,
        )

        # サンプルを追加
        self.muxer.append_sample(sample)
        self.frame_count += 1

    def _extract_av1_config_obus(self, chunk_data: bytes) -> bytes:
        """AV1 chunk から config OBUs を抽出する

        最初のキーフレームから Sequence Header OBU を抽出します。
        簡易実装として、chunk_data 全体を config_obus として使用します。
        """
        # 簡易実装: 最初のキーフレームのデータをそのまま config_obus として使用
        # 本来は OBU をパースして Sequence Header のみを抽出すべきですが、
        # 多くの場合、最初のキーフレームに必要な情報が含まれています
        return chunk_data

    def stop(self):
        """Muxer を停止"""
        if self.muxer is not None:
            self.muxer.finalize()
            self.muxer.__exit__(None, None, None)
            self.muxer = None


def main():
    parser = argparse.ArgumentParser(
        description="blend2d-py でダミー映像を生成し、エンコードして MP4 ファイルに出力"
    )
    parser.add_argument("--width", type=int, default=640, help="映像の幅（デフォルト: 640）")
    parser.add_argument("--height", type=int, default=480, help="映像の高さ（デフォルト: 480）")
    parser.add_argument("--fps", type=int, default=30, help="フレームレート（デフォルト: 30）")
    parser.add_argument(
        "--duration", type=int, default=15, help="映像の長さ（秒）（デフォルト: 15）"
    )
    parser.add_argument(
        "--bitrate", type=int, default=500000, help="ビットレート（デフォルト: 500000）"
    )
    parser.add_argument(
        "--codec",
        type=str,
        choices=["av1", "h264", "h265"],
        default="av1",
        help="コーデック（デフォルト: av1）",
    )
    parser.add_argument(
        "--output", type=str, default="output.mp4", help="出力ファイル名（デフォルト: output.mp4）"
    )

    args = parser.parse_args()

    width = args.width
    height = args.height
    fps = args.fps
    total_frames = fps * args.duration
    codec = args.codec

    print("=== blend2d-py → webcodecs-py → mp4-py パイプライン ===")
    print(f"コーデック: {codec.upper()}")
    print(f"解像度: {width}x{height}")
    print(f"フレームレート: {fps} fps")
    print(f"映像の長さ: {args.duration} 秒")
    print(f"総フレーム数: {total_frames}")
    print(f"ビットレート: {args.bitrate} bps")
    print(f"出力ファイル: {args.output}")
    print()

    # アニメーションする円を作成
    colors = [
        (255, 0, 0),  # 赤
        (0, 255, 0),  # 緑
        (0, 0, 255),  # 青
        (255, 255, 0),  # 黄色
        (255, 0, 255),  # マゼンタ
        (0, 255, 255),  # シアン
        (255, 128, 0),  # オレンジ
        (128, 0, 255),  # 紫
    ]

    circles = []
    for i, color in enumerate(colors):
        x = random.randint(50, width - 50)
        y = random.randint(50, height - 50)
        radius = random.randint(20, 40)
        vx = random.uniform(-4.0, 4.0)
        vy = random.uniform(-4.0, 4.0)
        alpha = random.randint(150, 220)

        circles.append(AnimatedCircle(x, y, radius, vx, vy, *color, alpha))

    # MP4 ライターを初期化
    mp4_writer = MP4Writer(args.output, width, height, fps, codec)
    mp4_writer.start()

    # エンコーダーを初期化
    encoded_frame_count = 0

    def on_output(chunk, metadata=None):
        nonlocal encoded_frame_count
        # metadata から description を取得 (キーフレーム時のみ提供される)
        if metadata is not None:
            decoder_config = metadata.get("decoderConfig")
            if decoder_config is not None:
                description = decoder_config.get("description")
                if description is not None:
                    # bytes 型に変換して保存
                    mp4_writer.set_description(bytes(description))

        # MP4 ファイルに書き込み
        destination = np.zeros(chunk.byte_length, dtype=np.uint8)
        chunk.copy_to(destination)
        frame_data = bytes(destination)
        is_keyframe = chunk.type == EncodedVideoChunkType.KEY
        mp4_writer.write(frame_data, is_keyframe)
        encoded_frame_count += 1

        # エンコードされたフレームのサイズを表示
        chunk_type = "Key" if is_keyframe else "Delta"
        print(
            f"  フレーム {encoded_frame_count:4d}/{total_frames}: {chunk_type:5s} "
            f"{chunk.byte_length:6d} bytes, timestamp={chunk.timestamp}"
        )

    def on_error(error):
        print(f"エンコーダーエラー: {error}", file=sys.stderr)

    encoder = VideoEncoder(on_output, on_error)

    # コーデックに応じた設定
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
        # High Profile, Level 5.1 (1080p 60fps 以上に対応)
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

    # フレームを生成してエンコード
    frame_count = 0
    timestamp = 0
    frame_duration = 1_000_000 // fps  # マイクロ秒単位 (WebCodecs API 準拠)

    print("フレームの生成とエンコードを開始します...")
    print()

    try:
        for frame_count in range(total_frames):
            # blend2d-py でフレームを生成
            img = Image(width, height)
            ctx = Context(img)

            # 背景を黒で塗りつぶし
            ctx.set_fill_style_rgba(0, 0, 0, 255)
            ctx.fill_all()

            # アルファブレンディングを有効化
            ctx.set_comp_op(CompOp.SRC_OVER)

            # 各円を更新して描画
            for circle in circles:
                circle.update(width, height)
                circle.draw(ctx)

            # フレーム番号を表示（オプション）
            # テキスト描画は未実装なので、簡単な図形で代用
            # 右上に小さな四角形を描画してフレームカウンターとする
            indicator_size = 5 + (frame_count % 20)
            ctx.set_fill_style_rgba(255, 255, 255, 200)
            ctx.fill_circle(width - 30, 30, indicator_size)

            ctx.end()

            # NumPy 配列として取得
            bgra = img.asarray()

            # VideoFrame を作成（BGRA フォーマット）
            init = VideoFrameBufferInit(
                format=VideoPixelFormat.BGRA,
                coded_width=width,
                coded_height=height,
                timestamp=timestamp,
            )
            bgra_frame = VideoFrame(bgra, init)

            # BGRA → I420 変換（AV1 エンコーダーは I420 が必要）
            i420_size = bgra_frame.allocation_size({"format": VideoPixelFormat.I420})
            i420_buffer = np.zeros(i420_size, dtype=np.uint8)
            bgra_frame.copy_to(i420_buffer, {"format": VideoPixelFormat.I420})
            bgra_frame.close()

            # I420 VideoFrame を作成
            i420_init = VideoFrameBufferInit(
                format=VideoPixelFormat.I420,
                coded_width=width,
                coded_height=height,
                timestamp=timestamp,
            )
            i420_frame = VideoFrame(i420_buffer, i420_init)

            # エンコード（最初のフレームと 3 秒ごとにキーフレームを強制）
            keyframe = frame_count == 0 or frame_count % (fps * 3) == 0
            encoder.encode(i420_frame, {"keyFrame": keyframe})
            i420_frame.close()

            timestamp += frame_duration

    except Exception as e:
        print(f"\nエラーが発生しました: {e}", file=sys.stderr)
        return 1

    print(f"\n合計 {frame_count + 1} フレームを生成しました")

    # エンコーダーをフラッシュ
    print("エンコーダーをフラッシュしています...")
    encoder.flush()
    encoder.close()

    print(f"エンコードされたチャンク数: {encoded_frame_count}")
    print()

    # MP4 ライターを停止
    print(f"MP4 ファイルを完了しています: {args.output}")
    mp4_writer.stop()

    print(f"ファイルを保存しました: {args.output}")
    print()

    print("=== 完了 ===")
    return 0


if __name__ == "__main__":
    sys.exit(main())
