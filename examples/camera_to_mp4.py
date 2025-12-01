#!/usr/bin/env python3
"""
OpenCV カメラキャプチャからエンコードして MP4 ファイルに保存するサンプル

必要な依存関係:
    uv sync --group example

使い方:
    uv run python examples/camera_to_mp4.py
    uv run python examples/camera_to_mp4.py --width 1920 --height 1080 --fps 30 --bitrate 2000000
    uv run python examples/camera_to_mp4.py --codec h264 --output output.mp4
    uv run python examples/camera_to_mp4.py --codec h265 --output output.mp4 --width 1920 --height 1080
"""

import argparse
import queue
import sys
import threading
import time

import cv2
import numpy as np

from mp4 import (
    Mp4FileMuxer,
    Mp4FileMuxerOptions,
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


def bgr_to_i420(bgr_frame: np.ndarray) -> np.ndarray:
    """BGR フレームを I420 (YUV420p) フォーマットに変換する

    Args:
        bgr_frame: BGR フォーマットの numpy 配列 (height, width, 3)

    Returns:
        I420 フォーマットの numpy 配列 (height * 3 // 2, width)
    """
    # BGR → YUV 変換（OpenCV を使用）
    yuv_frame = cv2.cvtColor(bgr_frame, cv2.COLOR_BGR2YUV_I420)

    return yuv_frame


class MP4Writer:
    """MP4 ファイルへの非同期書き込みを行うクラス"""

    def __init__(self, filename: str, width: int, height: int, fps: int, codec: str):
        self.filename = filename
        self.width = width
        self.height = height
        self.fps = fps
        self.codec = codec
        self.timescale = 1_000_000  # マイクロ秒単位
        self.frame_duration = self.timescale // fps
        self.sample_queue: queue.Queue = queue.Queue()
        self.frame_count = 0
        self.muxer: Mp4FileMuxer | None = None
        self.thread: threading.Thread | None = None
        self.running = False
        self.sample_entry: Mp4SampleEntryAv01 | Mp4SampleEntryAvc1 | Mp4SampleEntryHev1 | None = (
            None
        )

    def start(self):
        """ライタースレッドを開始"""
        # moov ボックスのサイズを見積もる（10 分間の録画を想定）
        estimated_frames = self.fps * 60 * 10
        reserved_size = Mp4FileMuxerOptions.estimate_maximum_moov_box_size(0, estimated_frames)
        options = Mp4FileMuxerOptions(reserved_moov_box_size=reserved_size)

        self.muxer = Mp4FileMuxer(self.filename, options)
        self.running = True
        self.thread = threading.Thread(target=self._writer_loop, daemon=True)
        self.thread.start()

    def _create_sample_entry(
        self,
    ) -> Mp4SampleEntryAv01 | Mp4SampleEntryAvc1 | Mp4SampleEntryHev1:
        """コーデックに応じたサンプルエントリーを作成"""
        if self.codec == "av1":
            return Mp4SampleEntryAv01(
                width=self.width,
                height=self.height,
                config_obus=b"",
                seq_profile=0,
                seq_level_idx_0=8,
                seq_tier_0=0,
                high_bitdepth=0,
                twelve_bit=0,
                monochrome=0,
                chroma_subsampling_x=1,
                chroma_subsampling_y=1,
                chroma_sample_position=0,
            )
        elif self.codec == "h264":
            return Mp4SampleEntryAvc1(
                width=self.width,
                height=self.height,
                avc_profile_indication=77,
                profile_compatibility=0,
                avc_level_indication=40,
                sps_data=[],
                pps_data=[],
            )
        elif self.codec == "h265":
            return Mp4SampleEntryHev1(
                width=self.width,
                height=self.height,
                general_profile_idc=1,
                general_level_idc=120,
                nalu_types=[],
                nalu_data=[],
            )
        else:
            raise RuntimeError(f"サポートされていないコーデック: {self.codec}")

    def _convert_annex_b_to_length_prefixed(self, chunk_data: bytes) -> bytes:
        """Annex-B フォーマットを length-prefixed フォーマットに変換する"""
        result = bytearray()
        pos = 0

        while pos < len(chunk_data):
            start_code_len = 0
            if pos + 4 <= len(chunk_data) and chunk_data[pos : pos + 4] == b"\x00\x00\x00\x01":
                start_code_len = 4
            elif pos + 3 <= len(chunk_data) and chunk_data[pos : pos + 3] == b"\x00\x00\x01":
                start_code_len = 3

            if start_code_len == 0:
                pos += 1
                continue

            nalu_start = pos + start_code_len
            next_pos = nalu_start
            while next_pos < len(chunk_data):
                if (
                    next_pos + 4 <= len(chunk_data)
                    and chunk_data[next_pos : next_pos + 4] == b"\x00\x00\x00\x01"
                ):
                    break
                if (
                    next_pos + 3 <= len(chunk_data)
                    and chunk_data[next_pos : next_pos + 3] == b"\x00\x00\x01"
                ):
                    break
                next_pos += 1

            nalu = chunk_data[nalu_start:next_pos]
            if len(nalu) > 0:
                result.extend(len(nalu).to_bytes(4, byteorder="big"))
                result.extend(nalu)

            pos = next_pos if next_pos < len(chunk_data) else len(chunk_data)

        return bytes(result)

    def _writer_loop(self):
        """ライタースレッドのメインループ"""
        while self.running or not self.sample_queue.empty():
            try:
                item = self.sample_queue.get(timeout=0.1)
                if item is None:
                    break
                frame_data, keyframe = item

                # 最初のフレームでサンプルエントリーを作成
                sample_entry = None
                if self.sample_entry is None:
                    self.sample_entry = self._create_sample_entry()
                    sample_entry = self.sample_entry

                # H.264/H.265 の場合は Annex-B から length-prefixed に変換
                if self.codec in ("h264", "h265"):
                    frame_data = self._convert_annex_b_to_length_prefixed(frame_data)

                sample = Mp4MuxSample(
                    track_kind="video",
                    sample_entry=sample_entry,
                    keyframe=keyframe,
                    timescale=self.timescale,
                    duration=self.frame_duration,
                    data=frame_data,
                )
                self.muxer.append_sample(sample)
                self.frame_count += 1
                self.sample_queue.task_done()
            except queue.Empty:
                continue

    def write(self, frame_data: bytes, keyframe: bool):
        """フレームをキューに追加"""
        self.sample_queue.put((frame_data, keyframe))

    def stop(self):
        """ライタースレッドを停止して、ファイルを完了"""
        self.sample_queue.put(None)
        self.running = False
        if self.thread:
            self.thread.join()

        # マルチプレックサーを完了
        if self.muxer:
            self.muxer.finalize()
            self.muxer.close()


def main():
    parser = argparse.ArgumentParser(
        description="OpenCV カメラキャプチャからエンコードして MP4 ファイルに保存"
    )
    parser.add_argument("--width", type=int, default=640, help="映像の幅（デフォルト: 640）")
    parser.add_argument("--height", type=int, default=480, help="映像の高さ（デフォルト: 480）")
    parser.add_argument("--fps", type=int, default=30, help="フレームレート（デフォルト: 30）")
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
        "--frames",
        type=int,
        default=None,
        help="キャプチャするフレーム数（デフォルト: 無制限、Ctrl+C で停止）",
    )
    parser.add_argument(
        "--output", type=str, default="output.mp4", help="出力ファイル名（デフォルト: output.mp4）"
    )
    parser.add_argument(
        "--raw-output",
        type=str,
        default=None,
        help="エンコード前の生 I420 データを保存する Y4M ファイル名（オプション）",
    )
    parser.add_argument("--camera", type=int, default=0, help="カメラデバイス番号（デフォルト: 0）")

    args = parser.parse_args()

    codec = args.codec

    print("=== OpenCV カメラキャプチャ → エンコード ===")
    print(f"コーデック: {codec.upper()}")
    print(f"解像度: {args.width}x{args.height}")
    print(f"フレームレート: {args.fps} fps")
    print(f"ビットレート: {args.bitrate} bps")
    print(f"フレーム数: {args.frames if args.frames is not None else '無制限 (Ctrl+C で停止)'}")
    print(f"出力ファイル: {args.output}")
    print()

    # カメラを開く（macOS では AVFoundation バックエンドを使用）
    camera = cv2.VideoCapture(args.camera, cv2.CAP_AVFOUNDATION)
    if not camera.isOpened():
        print(f"エラー: カメラ {args.camera} を開けませんでした", file=sys.stderr)
        return 1

    # カメラの解像度と FPS を設定
    camera.set(cv2.CAP_PROP_FRAME_WIDTH, args.width)
    camera.set(cv2.CAP_PROP_FRAME_HEIGHT, args.height)
    camera.set(cv2.CAP_PROP_FPS, args.fps)

    # FourCC フォーマットを明示的に設定（高品質な非圧縮フォーマットを優先）
    # まず UYVY (YUV 4:2:2 非圧縮) を試す
    camera.set(cv2.CAP_PROP_FOURCC, cv2.VideoWriter_fourcc(*"UYVY"))
    fourcc = camera.get(cv2.CAP_PROP_FOURCC)
    fourcc_str = "".join([chr((int(fourcc) >> 8 * i) & 0xFF) for i in range(4)])
    print(f"設定された FourCC: {fourcc_str}")

    # UYVY が使えない場合は YUYV を試す
    if fourcc == 0 or fourcc_str == "\x00\x00\x00\x00":
        print("UYVY が使えないため YUYV を試します")
        camera.set(cv2.CAP_PROP_FOURCC, cv2.VideoWriter_fourcc(*"YUYV"))
        fourcc = camera.get(cv2.CAP_PROP_FOURCC)
        fourcc_str = "".join([chr((int(fourcc) >> 8 * i) & 0xFF) for i in range(4)])
        print(f"設定された FourCC: {fourcc_str}")

    # カメラの画質設定を最適化
    # 自動露出を有効化（明るさ調整）
    camera.set(cv2.CAP_PROP_AUTO_EXPOSURE, 3)  # 3 = Auto mode
    # 自動ホワイトバランスを有効化
    camera.set(cv2.CAP_PROP_AUTO_WB, 1)
    # オートフォーカスを有効化（対応カメラのみ）
    camera.set(cv2.CAP_PROP_AUTOFOCUS, 1)

    # 実際の解像度と fps を取得
    actual_width = int(camera.get(cv2.CAP_PROP_FRAME_WIDTH))
    actual_height = int(camera.get(cv2.CAP_PROP_FRAME_HEIGHT))
    actual_fps = camera.get(cv2.CAP_PROP_FPS)
    print(f"カメラの実際の解像度: {actual_width}x{actual_height}")
    print(f"カメラの FPS 設定: {actual_fps}")

    # カメラ設定の確認
    auto_exposure = camera.get(cv2.CAP_PROP_AUTO_EXPOSURE)
    auto_wb = camera.get(cv2.CAP_PROP_AUTO_WB)
    autofocus = camera.get(cv2.CAP_PROP_AUTOFOCUS)
    print(f"カメラ設定: 自動露出={auto_exposure}, 自動WB={auto_wb}, オートフォーカス={autofocus}")
    print("注意: FPS 設定値は参考値です。実際のキャプチャレートは録画後に表示されます。")
    print()

    # MP4 ライターを初期化
    mp4_writer = MP4Writer(args.output, actual_width, actual_height, args.fps, codec)
    mp4_writer.start()

    # Y4M ライター（オプション）
    raw_file = None
    if args.raw_output:
        raw_file = open(args.raw_output, "wb")
        # Y4M ヘッダーを書き込み
        y4m_header = f"YUV4MPEG2 W{actual_width} H{actual_height} F{args.fps}:1 Ip A0:0 C420jpeg\n"
        raw_file.write(y4m_header.encode("ascii"))
        print(f"生データ出力: {args.raw_output}")
        print()

    # エンコーダーを初期化
    encoded_frame_count = 0

    def on_output(chunk):
        nonlocal encoded_frame_count
        # MP4 ファイルに書き込み
        destination = np.zeros(chunk.byte_length, dtype=np.uint8)
        chunk.copy_to(destination)
        frame_data = bytes(destination)
        keyframe = chunk.type == EncodedVideoChunkType.KEY
        mp4_writer.write(frame_data, keyframe)
        encoded_frame_count += 1

        # エンコードされたフレームのサイズを表示
        chunk_type = "Key" if keyframe else "Delta"
        print(
            f"  フレーム {encoded_frame_count:4d}: {chunk_type:5s} {chunk.byte_length:6d} bytes, "
            f"timestamp={chunk.timestamp}"
        )

    def on_error(error):
        print(f"エンコーダーエラー: {error}", file=sys.stderr)

    encoder = VideoEncoder(on_output, on_error)

    # コーデックに応じた設定
    if codec == "av1":
        codec_string = "av01.0.04M.08"
        encoder_config: VideoEncoderConfig = {
            "codec": codec_string,
            "width": actual_width,
            "height": actual_height,
            "bitrate": args.bitrate,
            "framerate": float(args.fps),
            "bitrate_mode": VideoEncoderBitrateMode.CONSTANT,
            "latency_mode": LatencyMode.REALTIME,
        }
    elif codec == "h264":
        codec_string = "avc1.4D0028"
        encoder_config: VideoEncoderConfig = {
            "codec": codec_string,
            "width": actual_width,
            "height": actual_height,
            "bitrate": args.bitrate,
            "framerate": float(args.fps),
            "bitrate_mode": VideoEncoderBitrateMode.CONSTANT,
            "latency_mode": LatencyMode.REALTIME,
            "hardware_acceleration_engine": HardwareAccelerationEngine.APPLE_VIDEO_TOOLBOX,
        }
    elif codec == "h265":
        codec_string = "hvc1.1.6.L120.B0"
        encoder_config: VideoEncoderConfig = {
            "codec": codec_string,
            "width": actual_width,
            "height": actual_height,
            "bitrate": args.bitrate,
            "framerate": float(args.fps),
            "bitrate_mode": VideoEncoderBitrateMode.CONSTANT,
            "latency_mode": LatencyMode.REALTIME,
            "hardware_acceleration_engine": HardwareAccelerationEngine.APPLE_VIDEO_TOOLBOX,
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

    # フレームをキャプチャしてエンコード
    frame_count = 0
    timestamp = 0
    frame_duration = 1_000_000 // args.fps  # マイクロ秒単位 (WebCodecs API 準拠)

    print("フレームのキャプチャとエンコードを開始します...")
    print("Ctrl+C で中断できます")
    print()

    start_time = time.time()
    last_frame_time = start_time
    try:
        while args.frames is None or frame_count < args.frames:
            ret, bgr_frame = camera.read()
            current_time = time.time()
            if not ret:
                print("エラー: フレームを読み込めませんでした", file=sys.stderr)
                break

            # フレーム間隔をログ出力（最初の10フレームのみ）
            if frame_count < 10:
                interval = (current_time - last_frame_time) * 1000  # ミリ秒
                print(f"フレーム {frame_count}: 間隔 {interval:.1f} ms")
            last_frame_time = current_time

            # BGR → I420 変換
            i420_data = bgr_to_i420(bgr_frame)

            # 生データを Y4M ファイルに保存（オプション）
            if raw_file:
                raw_file.write(b"FRAME\n")
                raw_file.write(i420_data.tobytes())

            # with 文で VideoFrame を使用（自動的に close される）
            init: VideoFrameBufferInit = {
                "format": VideoPixelFormat.I420,
                "coded_width": actual_width,
                "coded_height": actual_height,
                "timestamp": timestamp,
            }
            with VideoFrame(i420_data, init) as video_frame:
                # エンコード（最初のフレームと 20 秒ごとにキーフレームを強制）
                # WebCodecs API ではアプリケーション側で明示的にキーフレームを制御する
                keyframe = frame_count == 0 or frame_count % (args.fps * 20) == 0
                encoder.encode(video_frame, {"keyFrame": keyframe})

            frame_count += 1
            timestamp += frame_duration

    except KeyboardInterrupt:
        print("\nキャプチャを中断しました")
    finally:
        # カメラと Y4M ファイルを必ず閉じる
        camera.release()
        if raw_file:
            raw_file.close()

    elapsed_time = time.time() - start_time
    actual_capture_fps = frame_count / elapsed_time if elapsed_time > 0 else 0
    print(f"\n経過時間: {elapsed_time:.2f} 秒")
    print(f"実際のキャプチャレート: {actual_capture_fps:.2f} fps")

    # 実際のキャプチャレートが設定値と大きく異なる場合は警告
    if abs(actual_capture_fps - args.fps) > 5:
        print(
            f"警告: 実際のキャプチャレート ({actual_capture_fps:.1f} fps) が "
            f"設定値 ({args.fps} fps) と異なります。",
            file=sys.stderr,
        )
        print(
            "  カメラがこの解像度で指定された fps をサポートしていない可能性があります。",
            file=sys.stderr,
        )

    # エンコーダーをフラッシュ
    print("エンコーダーをフラッシュしています...")
    encoder.flush()
    encoder.close()

    print(f"合計 {frame_count} フレームをキャプチャしました")
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
