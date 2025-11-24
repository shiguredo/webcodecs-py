#!/usr/bin/env python3
"""
サイン波音声を生成し、webcodecs-py で AAC エンコードして mp4-py で MP4 ファイルに出力するサンプル

必要な依存関係:
    uv add mp4-py

動作環境:
    macOS のみ (Apple Audio Toolbox を使用)

使い方:
    uv run python examples/aac_to_mp4.py
    uv run python examples/aac_to_mp4.py --output audio.mp4 --duration 10
    uv run python examples/aac_to_mp4.py --frequency 880 --channels 1
"""

import argparse
import platform
import sys

import numpy as np
from mp4 import (
    Mp4FileMuxer,
    Mp4MuxSample,
    Mp4SampleEntryMp4a,
)

from webcodecs import (
    AudioData,
    AudioEncoder,
    AudioEncoderConfig,
    AudioSampleFormat,
    EncodedAudioChunk,
)


def generate_aac_decoder_specific_info(sample_rate: int, channels: int) -> bytes:
    """
    AAC AudioSpecificConfig を生成する

    AudioSpecificConfig は以下の構造を持つ:
    - audioObjectType (5 bits): 2 = AAC-LC
    - samplingFrequencyIndex (4 bits): サンプルレートのインデックス
    - channelConfiguration (4 bits): チャンネル数
    - GASpecificConfig (frame_length_flag=0, depends_on_core_coder=0, extension_flag=0)

    Args:
        sample_rate: サンプルレート
        channels: チャンネル数

    Returns:
        AudioSpecificConfig のバイト列
    """
    # サンプルレートインデックス
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

    # AudioSpecificConfig を構築
    # audioObjectType (5 bits) = 2 (AAC-LC)
    # samplingFrequencyIndex (4 bits)
    # channelConfiguration (4 bits)
    # GASpecificConfig (3 bits) = 0

    audio_object_type = 2
    channel_config = channels

    # 最初のバイト: audioObjectType(5) + samplingFrequencyIndex上位3ビット(3)
    byte1 = (audio_object_type << 3) | (sample_rate_index >> 1)

    # 2番目のバイト: samplingFrequencyIndex下位1ビット(1) + channelConfiguration(4) + 0(3)
    byte2 = ((sample_rate_index & 1) << 7) | (channel_config << 3)

    return bytes([byte1, byte2])


class MP4AudioWriter:
    """MP4 ファイルへの音声書き込みを行うクラス"""

    def __init__(
        self,
        filename: str,
        sample_rate: int,
        channels: int,
        bitrate: int,
    ):
        self.filename = filename
        self.sample_rate = sample_rate
        self.channels = channels
        self.bitrate = bitrate
        self.timescale = sample_rate  # 音声はサンプルレートをタイムスケールとする
        self.muxer: Mp4FileMuxer | None = None
        self.sample_entry: Mp4SampleEntryMp4a | None = None
        self.chunk_count = 0

    def start(self):
        """Muxer を開始"""
        self.muxer = Mp4FileMuxer(self.filename)
        self.muxer.__enter__()

        # AAC の AudioSpecificConfig を生成
        dec_specific_info = generate_aac_decoder_specific_info(self.sample_rate, self.channels)

        # Sample entry を作成
        self.sample_entry = Mp4SampleEntryMp4a(
            channel_count=self.channels,
            sample_rate=self.sample_rate,
            dec_specific_info=dec_specific_info,
            sample_size=16,
            avg_bitrate=self.bitrate,
            max_bitrate=self.bitrate,
        )

    def write(self, chunk_data: bytes, duration_samples: int):
        """音声チャンクを書き込み"""
        if self.muxer is None:
            raise RuntimeError("Muxer が開始されていません")

        if self.sample_entry is None:
            raise RuntimeError("Sample entry が作成されていません")

        # MP4 サンプルを作成
        sample = Mp4MuxSample(
            track_kind="audio",
            sample_entry=self.sample_entry,
            keyframe=True,  # AAC は全てキーフレーム
            timescale=self.timescale,
            duration=duration_samples,
            data=chunk_data,
        )

        # サンプルを追加
        self.muxer.append_sample(sample)
        self.chunk_count += 1

    def stop(self):
        """Muxer を停止"""
        if self.muxer is not None:
            self.muxer.finalize()
            self.muxer.__exit__(None, None, None)
            self.muxer = None


def generate_sine_wave(
    frequency: float,
    sample_rate: int,
    duration_seconds: float,
    channels: int,
) -> np.ndarray:
    """
    サイン波を生成する

    Args:
        frequency: 周波数 (Hz)
        sample_rate: サンプルレート
        duration_seconds: 継続時間 (秒)
        channels: チャンネル数

    Returns:
        (frames, channels) 形状の float32 配列
    """
    num_samples = int(sample_rate * duration_seconds)
    t = np.linspace(0, duration_seconds, num_samples, dtype=np.float32)

    # サイン波を生成
    mono = np.sin(2 * np.pi * frequency * t).astype(np.float32)

    # 振幅を調整 (クリッピング防止)
    mono = mono * 0.8

    if channels == 1:
        return mono.reshape(-1, 1)
    else:
        # ステレオの場合、両チャンネルに同じ音を入れる
        return np.column_stack([mono] * channels)


def main():
    parser = argparse.ArgumentParser(
        description="サイン波音声を生成し、AAC エンコードして MP4 ファイルに出力"
    )
    parser.add_argument(
        "--sample-rate",
        type=int,
        default=48000,
        help="サンプルレート (デフォルト: 48000)",
    )
    parser.add_argument(
        "--channels",
        type=int,
        default=2,
        choices=[1, 2],
        help="チャンネル数 (デフォルト: 2)",
    )
    parser.add_argument(
        "--duration",
        type=int,
        default=5,
        help="音声の長さ (秒) (デフォルト: 5)",
    )
    parser.add_argument(
        "--bitrate",
        type=int,
        default=128000,
        help="ビットレート (デフォルト: 128000)",
    )
    parser.add_argument(
        "--frequency",
        type=float,
        default=440.0,
        help="サイン波の周波数 (Hz) (デフォルト: 440.0 = A4)",
    )
    parser.add_argument(
        "--output",
        type=str,
        default="audio_output.mp4",
        help="出力ファイル名 (デフォルト: audio_output.mp4)",
    )

    args = parser.parse_args()

    # プラットフォームチェック
    if platform.system() != "Darwin":
        print(
            "エラー: このサンプルは macOS でのみ動作します (Apple Audio Toolbox を使用)",
            file=sys.stderr,
        )
        return 1

    sample_rate = args.sample_rate
    channels = args.channels
    duration = args.duration
    bitrate = args.bitrate
    frequency = args.frequency
    output_file = args.output

    print("=== 音声生成 → AAC エンコード → MP4 出力 ===")
    print(f"サンプルレート: {sample_rate} Hz")
    print(f"チャンネル数: {channels}")
    print(f"周波数: {frequency} Hz")
    print(f"長さ: {duration} 秒")
    print(f"ビットレート: {bitrate} bps ({bitrate / 1000:.0f} kbps)")
    print(f"出力ファイル: {output_file}")
    print()

    # MP4 ライターを初期化
    mp4_writer = MP4AudioWriter(output_file, sample_rate, channels, bitrate)
    mp4_writer.start()

    # エンコーダーを初期化
    encoded_chunks: list[tuple[bytes, int]] = []

    def on_output(chunk: EncodedAudioChunk) -> None:
        # チャンクデータをコピー
        destination = np.zeros(chunk.byte_length, dtype=np.uint8)
        chunk.copy_to(destination)
        chunk_data = bytes(destination)

        # AAC は 1024 サンプル/フレーム
        duration_samples = 1024

        encoded_chunks.append((chunk_data, duration_samples))

        print(
            f"  チャンク {len(encoded_chunks):4d}: "
            f"{chunk.byte_length:6d} bytes, timestamp={chunk.timestamp}"
        )

    def on_error(error: str) -> None:
        print(f"エンコーダーエラー: {error}", file=sys.stderr)

    encoder = AudioEncoder(on_output, on_error)

    encoder_config: AudioEncoderConfig = {
        "codec": "mp4a.40.2",  # AAC-LC
        "sample_rate": sample_rate,
        "number_of_channels": channels,
        "bitrate": bitrate,
    }

    encoder.configure(encoder_config)
    print("エンコーダーを初期化しました")
    print("  コーデック: AAC-LC (mp4a.40.2)")
    print()

    # 音声を生成
    print("音声を生成しています...")

    # フレームサイズ (AAC は 1024 サンプル/フレーム)
    frame_size = 1024
    total_samples = sample_rate * duration
    timestamp = 0

    # 全体の波形を生成
    full_audio = generate_sine_wave(frequency, sample_rate, duration, channels)

    print(f"総サンプル数: {total_samples}")
    print(f"フレームサイズ: {frame_size} サンプル")
    print()

    print("エンコードを開始します...")

    try:
        # フレームごとに処理
        for offset in range(0, total_samples, frame_size):
            # フレームを切り出し
            end = min(offset + frame_size, total_samples)
            frame_data = full_audio[offset:end]

            # AudioData を作成
            audio = AudioData(
                {
                    "format": AudioSampleFormat.F32,
                    "sample_rate": sample_rate,
                    "number_of_channels": channels,
                    "number_of_frames": len(frame_data),
                    "timestamp": timestamp,
                    "data": frame_data,
                }
            )

            encoder.encode(audio)

            # タイムスタンプを更新 (マイクロ秒単位)
            timestamp += int(len(frame_data) * 1_000_000 / sample_rate)

    except Exception as e:
        print(f"\nエラーが発生しました: {e}", file=sys.stderr)
        return 1

    # エンコーダーをフラッシュ
    print("\nエンコーダーをフラッシュしています...")
    encoder.flush()
    encoder.close()

    print(f"エンコードされたチャンク数: {len(encoded_chunks)}")
    print()

    # MP4 に書き込み
    print("MP4 ファイルに書き込んでいます...")
    for chunk_data, duration_samples in encoded_chunks:
        mp4_writer.write(chunk_data, duration_samples)

    # MP4 ライターを停止
    print(f"MP4 ファイルを完了しています: {output_file}")
    mp4_writer.stop()

    print(f"ファイルを保存しました: {output_file}")
    print()

    print("=== 完了 ===")
    print()
    print("再生方法:")
    print(f"  ffplay {output_file}")
    print(f"  vlc {output_file}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
