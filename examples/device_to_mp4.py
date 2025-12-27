#!/usr/bin/env python3
"""
uvc-py と portaudio-py を使ってカメラとマイクからキャプチャして MP4 ファイルに保存するサンプル

必要な依存関係:
    uv sync --group example

使い方:
    uv run python examples/device_to_mp4.py --list-devices
    uv run python examples/device_to_mp4.py
    uv run python examples/device_to_mp4.py --width 1920 --height 1080 --fps 30
    uv run python examples/device_to_mp4.py --video-codec h264 --output output.mp4
    uv run python examples/device_to_mp4.py --video-codec h264 --native-buffer  # macOS で native buffer を利用
    uv run python examples/device_to_mp4.py --audio  # 音声も録音（実験的）
"""

import argparse
import platform
import queue
import sys
import threading
import time

import numpy as np
import portaudio
import uvc

from mp4 import (
    Mp4FileMuxer,
    Mp4FileMuxerOptions,
    Mp4MuxSample,
    Mp4SampleEntryAv01,
    Mp4SampleEntryAvc1,
    Mp4SampleEntryHev1,
    Mp4SampleEntryMp4a,
)


def parse_avcc(
    avcc_data: bytes,
) -> tuple[int, int, int, list[bytes], list[bytes], int | None, int | None, int | None]:
    """avcC box をパースして profile, compatibility, level, SPS リスト, PPS リスト,
    chroma_format, bit_depth_luma_minus8, bit_depth_chroma_minus8 を返す
    """
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
    """hvcC box をパースして profile_idc, level_idc, NAL unit types, NAL unit data を返す"""
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

from webcodecs import (
    AudioData,
    AudioEncoder,
    AudioEncoderConfig,
    AudioSampleFormat,
    EncodedAudioChunk,
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


def list_devices():
    """利用可能なデバイス一覧を表示"""
    print("=== 映像デバイス (UVC) ===")
    video_devices = uvc.list_devices()
    if not video_devices:
        print("  映像デバイスが見つかりません")
    else:
        for device_info in video_devices:
            print(f"  [{device_info.index}] {device_info.name}")
    print()

    print("=== 音声入力デバイス (PortAudio) ===")
    audio_devices = portaudio.list_input_devices()
    if not audio_devices:
        print("  音声入力デバイスが見つかりません")
    else:
        for device_info in audio_devices:
            print(
                f"  [{device_info.index}] {device_info.name} "
                f"(チャンネル: {device_info.max_input_channels}, "
                f"サンプルレート: {int(device_info.default_sample_rate)} Hz)"
            )
    print()


def generate_aac_decoder_specific_info(sample_rate: int, channels: int) -> bytes:
    """AAC AudioSpecificConfig を生成する"""
    sample_rate_index_map = {
        96000: 0,
        88200: 1,
        64000: 2,
        48000: 3,
        44100: 4,
        32000: 5,
        24000: 6,
        22050: 7,
        16000: 8,
        12000: 9,
        11025: 10,
        8000: 11,
        7350: 12,
    }

    sample_rate_index = sample_rate_index_map.get(sample_rate, 4)
    audio_object_type = 2
    channel_config = channels

    byte1 = (audio_object_type << 3) | (sample_rate_index >> 1)
    byte2 = ((sample_rate_index & 1) << 7) | (channel_config << 3)

    return bytes([byte1, byte2])


class MP4Writer:
    """MP4 ファイルへの非同期書き込みを行うクラス"""

    def __init__(
        self,
        filename: str,
        width: int,
        height: int,
        fps: int,
        video_codec: str,
        audio_sample_rate: int | None = None,
        audio_channels: int | None = None,
        audio_bitrate: int | None = None,
    ):
        self.filename = filename
        self.width = width
        self.height = height
        self.fps = fps
        self.video_codec = video_codec
        self.audio_sample_rate = audio_sample_rate
        self.audio_channels = audio_channels
        self.audio_bitrate = audio_bitrate
        self.video_timescale = 1_000_000
        self.video_frame_duration = self.video_timescale // fps
        self.sample_queue: queue.Queue = queue.Queue()
        self.video_frame_count = 0
        self.audio_chunk_count = 0
        self.muxer: Mp4FileMuxer | None = None
        self.thread: threading.Thread | None = None
        self.running = False
        self.video_sample_entry: (
            Mp4SampleEntryAv01 | Mp4SampleEntryAvc1 | Mp4SampleEntryHev1 | None
        ) = None
        self.audio_sample_entry: Mp4SampleEntryMp4a | None = None
        self.description: bytes | None = None

    def set_description(self, description: bytes):
        """metadata.decoder_config.description を設定する (avcC/hvcC)"""
        self.description = description

    def start(self):
        """ライタースレッドを開始"""
        estimated_frames = self.fps * 60 * 10
        reserved_size = Mp4FileMuxerOptions.estimate_maximum_moov_box_size(0, estimated_frames)
        options = Mp4FileMuxerOptions(reserved_moov_box_size=reserved_size)

        self.muxer = Mp4FileMuxer(self.filename, options)
        self.running = True
        self.thread = threading.Thread(target=self._writer_loop, daemon=True)
        self.thread.start()

    def _create_video_sample_entry(
        self,
        chunk_data: bytes,
    ) -> Mp4SampleEntryAv01 | Mp4SampleEntryAvc1 | Mp4SampleEntryHev1:
        """コーデックに応じた映像サンプルエントリーを作成"""
        if self.video_codec == "av1":
            return Mp4SampleEntryAv01(
                width=self.width,
                height=self.height,
                config_obus=chunk_data,
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
        elif self.video_codec == "h264":
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
            return Mp4SampleEntryAvc1(
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
        elif self.video_codec == "h265":
            if self.description is None:
                raise RuntimeError(
                    "H.265: metadata.decoder_config.description が設定されていません"
                )
            profile_idc, level_idc, nalu_types, nalu_data = parse_hvcc(self.description)
            return Mp4SampleEntryHev1(
                width=self.width,
                height=self.height,
                general_profile_idc=profile_idc,
                general_level_idc=level_idc,
                nalu_types=nalu_types,
                nalu_data=nalu_data,
            )
        else:
            raise RuntimeError(f"サポートされていない映像コーデック: {self.video_codec}")

    def _create_audio_sample_entry(self) -> Mp4SampleEntryMp4a:
        """音声サンプルエントリーを作成"""
        if self.audio_sample_rate is None or self.audio_channels is None:
            raise RuntimeError("音声パラメータが設定されていません")

        dec_specific_info = generate_aac_decoder_specific_info(
            self.audio_sample_rate, self.audio_channels
        )

        return Mp4SampleEntryMp4a(
            channel_count=self.audio_channels,
            sample_rate=self.audio_sample_rate,
            dec_specific_info=dec_specific_info,
            sample_size=16,
            avg_bitrate=self.audio_bitrate or 128000,
            max_bitrate=self.audio_bitrate or 128000,
        )

    def _writer_loop(self):
        """ライタースレッドのメインループ"""
        while self.running or not self.sample_queue.empty():
            try:
                item = self.sample_queue.get(timeout=0.1)
                if item is None:
                    break

                track_kind, data, key_frame_or_duration = item

                if track_kind == "video":
                    frame_data = data
                    key_frame = key_frame_or_duration

                    if self.video_sample_entry is None:
                        self.video_sample_entry = self._create_video_sample_entry(frame_data)

                    sample = Mp4MuxSample(
                        track_kind="video",
                        sample_entry=self.video_sample_entry,
                        keyframe=key_frame,
                        timescale=self.video_timescale,
                        duration=self.video_frame_duration,
                        data=frame_data,
                    )
                    assert self.muxer is not None
                    self.muxer.append_sample(sample)
                    self.video_frame_count += 1

                elif track_kind == "audio":
                    chunk_data = data
                    duration_samples = key_frame_or_duration

                    if self.audio_sample_entry is None:
                        self.audio_sample_entry = self._create_audio_sample_entry()

                    sample = Mp4MuxSample(
                        track_kind="audio",
                        sample_entry=self.audio_sample_entry,
                        keyframe=True,
                        timescale=self.audio_sample_rate or 48000,
                        duration=duration_samples,
                        data=chunk_data,
                    )
                    assert self.muxer is not None
                    self.muxer.append_sample(sample)
                    self.audio_chunk_count += 1

                self.sample_queue.task_done()
            except queue.Empty:
                continue

    def write_video(self, frame_data: bytes, key_frame: bool):
        """映像フレームをキューに追加"""
        self.sample_queue.put(("video", frame_data, key_frame))

    def write_audio(self, chunk_data: bytes, duration_samples: int):
        """音声チャンクをキューに追加"""
        self.sample_queue.put(("audio", chunk_data, duration_samples))

    def stop(self):
        """ライタースレッドを停止して、ファイルを完了"""
        self.sample_queue.put(None)
        self.running = False
        if self.thread:
            self.thread.join()

        if self.muxer:
            self.muxer.finalize()
            self.muxer.close()


def main():
    parser = argparse.ArgumentParser(
        description="uvc-py と portaudio-py を使ってカメラとマイクからキャプチャして MP4 ファイルに保存"
    )
    parser.add_argument(
        "--list-devices",
        action="store_true",
        help="利用可能なデバイス一覧を表示して終了",
    )
    parser.add_argument("--width", type=int, default=640, help="映像の幅（デフォルト: 640）")
    parser.add_argument("--height", type=int, default=480, help="映像の高さ（デフォルト: 480）")
    parser.add_argument("--fps", type=int, default=30, help="フレームレート（デフォルト: 30）")
    parser.add_argument(
        "--video-bitrate", type=int, default=500000, help="映像ビットレート（デフォルト: 500000）"
    )
    parser.add_argument(
        "--video-codec",
        type=str,
        choices=["av1", "h264", "h265"],
        default="av1",
        help="映像コーデック（デフォルト: av1）",
    )
    parser.add_argument(
        "--audio-codec",
        type=str,
        choices=["aac"],
        default="aac",
        help="音声コーデック（デフォルト: aac）",
    )
    parser.add_argument(
        "--duration",
        type=int,
        default=10,
        help="録画時間（秒）（デフォルト: 10）",
    )
    parser.add_argument(
        "--output", type=str, default="output.mp4", help="出力ファイル名（デフォルト: output.mp4）"
    )
    parser.add_argument(
        "--raw-output",
        type=str,
        default=None,
        help="エンコード前の生 NV12 データを保存する Y4M ファイル名（オプション）",
    )
    parser.add_argument(
        "--video-device", type=int, default=0, help="映像デバイス番号（デフォルト: 0）"
    )
    parser.add_argument(
        "--native-buffer",
        action="store_true",
        help="macOS で native buffer (CVPixelBufferRef) を使用",
    )
    parser.add_argument(
        "--audio",
        action="store_true",
        default=False,
        help="音声を録音する ※現在 mp4-py が映像+音声の同時書き込み未対応のため実験的機能",
    )
    parser.add_argument(
        "--audio-device",
        type=int,
        default=None,
        help="音声入力デバイス番号（デフォルト: システムデフォルト）",
    )
    parser.add_argument(
        "--audio-sample-rate",
        type=int,
        default=48000,
        help="音声サンプルレート（デフォルト: 48000）",
    )
    parser.add_argument(
        "--audio-channels",
        type=int,
        default=1,
        choices=[1, 2],
        help="音声チャンネル数（デフォルト: 1）",
    )
    parser.add_argument(
        "--audio-bitrate",
        type=int,
        default=128000,
        help="音声ビットレート（デフォルト: 128000）",
    )

    args = parser.parse_args()

    if args.list_devices:
        list_devices()
        return 0

    use_native_buffer = args.native_buffer
    if use_native_buffer and platform.system() != "Darwin":
        print("警告: --native-buffer は macOS でのみ使用可能です。無効化します。", file=sys.stderr)
        use_native_buffer = False

    if use_native_buffer and args.raw_output:
        print(
            "エラー: --native-buffer と --raw-output は同時に使用できません",
            file=sys.stderr,
        )
        return 1

    enable_audio = args.audio
    if enable_audio and platform.system() != "Darwin":
        print("警告: 音声エンコード (AAC) は macOS でのみ使用可能です。無効化します。", file=sys.stderr)
        enable_audio = False

    video_codec = args.video_codec
    audio_codec = args.audio_codec

    print("=== デバイスキャプチャ -> エンコード -> MP4 ===")
    print(f"映像コーデック: {video_codec.upper()}")
    print(f"映像解像度: {args.width}x{args.height}")
    print(f"フレームレート: {args.fps} fps")
    print(f"映像ビットレート: {args.video_bitrate} bps")
    if enable_audio:
        print(f"音声コーデック: {audio_codec.upper()}")
        print(f"音声サンプルレート: {args.audio_sample_rate} Hz")
        print(f"音声チャンネル数: {args.audio_channels}")
        print(f"音声ビットレート: {args.audio_bitrate} bps")
    else:
        print("音声: 無効")
    print(f"録画時間: {args.duration} 秒")
    print(f"出力ファイル: {args.output}")
    if use_native_buffer:
        print("native buffer: 有効")
    print()

    video_devices = uvc.list_devices()
    if not video_devices:
        print("エラー: UVC デバイスが見つかりません", file=sys.stderr)
        return 1

    print("利用可能な映像デバイス:")
    for device_info in video_devices:
        print(f"  [{device_info.index}] {device_info.name}")
    print()

    if args.video_device >= len(video_devices):
        print(f"エラー: 映像デバイス {args.video_device} が見つかりません", file=sys.stderr)
        return 1

    video_device = uvc.open(args.video_device)
    print(f"映像デバイスをオープン: {video_device.info.name}")

    formats = video_device.get_supported_formats()
    print("サポートされているフォーマット:")
    for format_info in formats[:10]:
        print(f"  {format_info}")
    if len(formats) > 10:
        print(f"  ... 他 {len(formats) - 10} 件")
    print()

    video_device.start(
        width=args.width,
        height=args.height,
        fps=args.fps,
        capture_format=uvc.Format.NV12,
    )

    actual_width = args.width
    actual_height = args.height
    print(f"映像キャプチャ設定: {actual_width}x{actual_height} @ {args.fps} fps")
    print()

    audio_stream = None
    if enable_audio:
        audio_devices = portaudio.list_input_devices()
        if not audio_devices:
            print("警告: 音声入力デバイスが見つかりません。音声を無効化します。", file=sys.stderr)
            enable_audio = False
        else:
            print("利用可能な音声入力デバイス:")
            for device_info in audio_devices:
                print(f"  [{device_info.index}] {device_info.name}")
            print()

            audio_device_index = args.audio_device
            if audio_device_index is None:
                default_device_index = portaudio.get_default_input_device()
                if default_device_index is not None:
                    audio_device_index = default_device_index
                else:
                    audio_device_index = audio_devices[0].index

            audio_device_info = portaudio.get_device_info(audio_device_index)
            if audio_device_info is None:
                print(
                    f"エラー: 音声デバイス {audio_device_index} が見つかりません",
                    file=sys.stderr,
                )
                return 1

            print(f"音声デバイスをオープン: {audio_device_info.name}")

            input_params = portaudio.StreamParameters(
                device=audio_device_index,
                channel_count=args.audio_channels,
                sample_format=portaudio.FLOAT32,
                suggested_latency=audio_device_info.default_low_input_latency,
            )
            audio_stream = portaudio.Stream(
                input_parameters=input_params,
                sample_rate=float(args.audio_sample_rate),
                frames_per_buffer=1024,
            )
            audio_stream.start()
            print(f"音声キャプチャ設定: {args.audio_sample_rate} Hz, {args.audio_channels} ch")
            print()

    mp4_writer = MP4Writer(
        args.output,
        actual_width,
        actual_height,
        args.fps,
        video_codec,
        audio_sample_rate=args.audio_sample_rate if enable_audio else None,
        audio_channels=args.audio_channels if enable_audio else None,
        audio_bitrate=args.audio_bitrate if enable_audio else None,
    )
    mp4_writer.start()

    raw_file = None
    if args.raw_output:
        raw_file = open(args.raw_output, "wb")
        y4m_header = f"YUV4MPEG2 W{actual_width} H{actual_height} F{args.fps}:1 Ip A0:0 C420jpeg\n"
        raw_file.write(y4m_header.encode("ascii"))
        print(f"生データ出力: {args.raw_output}")
        print()

    encoded_video_frame_count = 0

    def on_video_output(chunk, metadata=None):
        nonlocal encoded_video_frame_count
        if metadata is not None:
            decoder_config = metadata.get("decoder_config")
            if decoder_config is not None:
                description = decoder_config.get("description")
                if description is not None:
                    mp4_writer.set_description(bytes(description))

        destination = np.zeros(chunk.byte_length, dtype=np.uint8)
        chunk.copy_to(destination)
        frame_data = bytes(destination)
        key_frame = chunk.type == EncodedVideoChunkType.KEY
        mp4_writer.write_video(frame_data, key_frame)
        encoded_video_frame_count += 1

        chunk_type = "Key" if key_frame else "Delta"
        print(
            f"  映像 {encoded_video_frame_count:4d}: {chunk_type:5s} {chunk.byte_length:6d} bytes, "
            f"timestamp={chunk.timestamp}"
        )

    def on_video_error(error):
        print(f"映像エンコーダーエラー: {error}", file=sys.stderr)

    video_encoder = VideoEncoder(on_video_output, on_video_error)

    if video_codec == "av1":
        codec_string = "av01.0.04M.08"
        video_encoder_config: VideoEncoderConfig = {
            "codec": codec_string,
            "width": actual_width,
            "height": actual_height,
            "bitrate": args.video_bitrate,
            "framerate": float(args.fps),
            "bitrate_mode": VideoEncoderBitrateMode.CONSTANT,
            "latency_mode": LatencyMode.REALTIME,
        }
    elif video_codec == "h264":
        codec_string = "avc1.4D0028"
        video_encoder_config: VideoEncoderConfig = {
            "codec": codec_string,
            "width": actual_width,
            "height": actual_height,
            "bitrate": args.video_bitrate,
            "framerate": float(args.fps),
            "bitrate_mode": VideoEncoderBitrateMode.CONSTANT,
            "latency_mode": LatencyMode.REALTIME,
            "hardware_acceleration_engine": HardwareAccelerationEngine.APPLE_VIDEO_TOOLBOX,
        }
    elif video_codec == "h265":
        codec_string = "hvc1.1.6.L120.B0"
        video_encoder_config: VideoEncoderConfig = {
            "codec": codec_string,
            "width": actual_width,
            "height": actual_height,
            "bitrate": args.video_bitrate,
            "framerate": float(args.fps),
            "bitrate_mode": VideoEncoderBitrateMode.CONSTANT,
            "latency_mode": LatencyMode.REALTIME,
            "hardware_acceleration_engine": HardwareAccelerationEngine.APPLE_VIDEO_TOOLBOX,
        }
    else:
        raise RuntimeError(f"サポートされていないコーデック: {video_codec}")

    video_encoder.configure(video_encoder_config)
    print("映像エンコーダーを初期化しました")
    print(f"  コーデック: {video_encoder_config['codec']}")
    if video_codec in ("h264", "h265"):
        print("  ハードウェアアクセラレーション: Apple Video Toolbox")
    print(f"  ビットレート: {args.video_bitrate} bps ({args.video_bitrate / 1000:.0f} kbps)")
    print()

    audio_encoder = None
    encoded_audio_chunk_count = 0

    if enable_audio:

        def on_audio_output(chunk: EncodedAudioChunk) -> None:
            nonlocal encoded_audio_chunk_count
            destination = np.zeros(chunk.byte_length, dtype=np.uint8)
            chunk.copy_to(destination)
            chunk_data = bytes(destination)
            duration_samples = 1024
            mp4_writer.write_audio(chunk_data, duration_samples)
            encoded_audio_chunk_count += 1

            print(
                f"  音声 {encoded_audio_chunk_count:4d}: "
                f"{chunk.byte_length:6d} bytes, timestamp={chunk.timestamp}"
            )

        def on_audio_error(error: str) -> None:
            print(f"音声エンコーダーエラー: {error}", file=sys.stderr)

        audio_encoder = AudioEncoder(on_audio_output, on_audio_error)

        audio_encoder_config: AudioEncoderConfig = {
            "codec": "mp4a.40.2",
            "sample_rate": args.audio_sample_rate,
            "number_of_channels": args.audio_channels,
            "bitrate": args.audio_bitrate,
        }

        audio_encoder.configure(audio_encoder_config)
        print("音声エンコーダーを初期化しました")
        print("  コーデック: AAC-LC (mp4a.40.2)")
        print(f"  サンプルレート: {args.audio_sample_rate} Hz")
        print(f"  ビットレート: {args.audio_bitrate} bps ({args.audio_bitrate / 1000:.0f} kbps)")
        print()

    video_frame_count = 0
    video_timestamp = 0
    video_frame_duration = 1_000_000 // args.fps

    audio_timestamp = 0
    audio_frame_size = 1024

    print("フレームのキャプチャとエンコードを開始します...")
    print("Ctrl+C で中断できます")
    print()

    start_time = time.time()
    last_frame_time = start_time
    stop_flag = False

    try:
        target_frames = args.duration * args.fps
        while video_frame_count < target_frames and not stop_flag:
            frame = video_device.get_frame()
            if frame is None:
                continue
            current_time = time.time()

            if video_frame_count < 10:
                interval = (current_time - last_frame_time) * 1000
                print(f"フレーム {video_frame_count}: 間隔 {interval:.1f} ms")
            last_frame_time = current_time

            key_frame = video_frame_count == 0 or video_frame_count % (args.fps * 2) == 0

            if use_native_buffer:
                native_buffer = frame.native_buffer()
                nv12_init: VideoFrameBufferInit = {
                    "format": VideoPixelFormat.NV12,
                    "coded_width": actual_width,
                    "coded_height": actual_height,
                    "timestamp": video_timestamp,
                }
                with VideoFrame(native_buffer, nv12_init) as video_frame:
                    video_encoder.encode(video_frame, {"key_frame": key_frame})
            else:
                y_plane, uv_plane = frame.to_nv12()
                nv12_data = np.concatenate([y_plane.flatten(), uv_plane.flatten()])

                nv12_init: VideoFrameBufferInit = {
                    "format": VideoPixelFormat.NV12,
                    "coded_width": actual_width,
                    "coded_height": actual_height,
                    "timestamp": video_timestamp,
                }
                with VideoFrame(nv12_data, nv12_init) as nv12_frame:
                    i420_size = nv12_frame.allocation_size({"format": VideoPixelFormat.I420})
                    i420_data = np.zeros(i420_size, dtype=np.uint8)
                    nv12_frame.copy_to(i420_data, {"format": VideoPixelFormat.I420})

                    if raw_file:
                        raw_file.write(b"FRAME\n")
                        raw_file.write(i420_data.tobytes())

                    i420_init: VideoFrameBufferInit = {
                        "format": VideoPixelFormat.I420,
                        "coded_width": actual_width,
                        "coded_height": actual_height,
                        "timestamp": video_timestamp,
                    }
                    with VideoFrame(i420_data, i420_init) as video_frame:
                        video_encoder.encode(video_frame, {"key_frame": key_frame})

            video_frame_count += 1
            video_timestamp += video_frame_duration

            if enable_audio and audio_stream is not None and audio_encoder is not None:
                available = audio_stream.get_read_available()
                while available >= audio_frame_size:
                    audio_data = audio_stream.read_float32(audio_frame_size)

                    with AudioData(
                        {
                            "format": AudioSampleFormat.F32,
                            "sample_rate": args.audio_sample_rate,
                            "number_of_channels": args.audio_channels,
                            "number_of_frames": audio_frame_size,
                            "timestamp": audio_timestamp,
                            "data": audio_data,
                        }
                    ) as audio:
                        audio_encoder.encode(audio)

                    audio_timestamp += int(audio_frame_size * 1_000_000 / args.audio_sample_rate)
                    available = audio_stream.get_read_available()

    except KeyboardInterrupt:
        print("\nキャプチャを中断しました")
        stop_flag = True
    finally:
        video_device.stop()
        if audio_stream is not None:
            audio_stream.stop()
            audio_stream.close()
        if raw_file:
            raw_file.close()

    elapsed_time = time.time() - start_time
    actual_capture_fps = video_frame_count / elapsed_time if elapsed_time > 0 else 0
    print(f"\n経過時間: {elapsed_time:.2f} 秒")
    print(f"実際のキャプチャレート: {actual_capture_fps:.2f} fps")

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

    print("エンコーダーをフラッシュしています...")
    video_encoder.flush()
    video_encoder.close()

    if audio_encoder is not None:
        audio_encoder.flush()
        audio_encoder.close()

    print(f"映像フレーム数: {video_frame_count}")
    print(f"エンコードされた映像チャンク数: {encoded_video_frame_count}")
    if enable_audio:
        print(f"エンコードされた音声チャンク数: {encoded_audio_chunk_count}")
    print()

    print(f"MP4 ファイルを完了しています: {args.output}")
    mp4_writer.stop()

    print(f"ファイルを保存しました: {args.output}")
    print()

    print("=== 完了 ===")
    return 0


if __name__ == "__main__":
    sys.exit(main())
