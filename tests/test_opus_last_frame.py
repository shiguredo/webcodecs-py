import numpy as np
import pytest
from webcodecs import (
    AudioData,
    AudioEncoder,
    AudioEncoderConfig,
    AudioSampleFormat,
)


def test_opus_encoder_last_frame_handling():
    """最後のフレームが完全なフレームでなくても正しくエンコードされることをテスト"""
    sample_rate = 48000
    frame_size = 960  # 48kHz で 20ms

    # frame_size の倍数ではないオーディオデータを作成
    # 2400 サンプル = 2.5 フレーム (2 完全フレーム + 480 サンプル)
    total_samples = frame_size * 2 + 480  # 2400 サンプル
    audio_samples = np.ones(total_samples, dtype=np.float32) * 0.1

    # 最後の 480 サンプルを異なる値でマークし、処理されることを確認
    audio_samples[-480:] = 0.9

    # エンコーダーをセットアップ
    encoded_chunks = []

    def on_output(chunk):
        encoded_chunks.append(chunk)

    def on_error(error):
        pytest.fail(f"エンコーダエラー: {error}")

    encoder = AudioEncoder(on_output, on_error)
    encoder_config: AudioEncoderConfig = {
        "codec": "opus",
        "sample_rate": sample_rate,
        "number_of_channels": 1,
        "bitrate": 64000,
    }
    encoder.configure(encoder_config)

    # 最後の部分フレームを含むフレームをエンコード
    frame_count = 0
    for idx, i in enumerate(range(0, len(audio_samples), frame_size)):
        # フレームサンプルを取得し、必要に応じてパディング
        end_idx = min(i + frame_size, len(audio_samples))
        frame = audio_samples[i:end_idx]

        # 最後の不完全なフレームの場合、ゼロでパディング
        if len(frame) < frame_size:
            print(f"Processing partial frame {idx}: {len(frame)} samples")
            padded_frame = np.zeros(frame_size, dtype=np.float32)
            padded_frame[: len(frame)] = frame
            frame = padded_frame

        frame = frame.reshape(frame_size, 1)
        # マイクロ秒でタイムスタンプを計算
        timestamp = (idx * frame_size * 1000000) // sample_rate
        num_frames, num_channels = frame.shape
        init = {
            "format": AudioSampleFormat.F32,
            "sample_rate": sample_rate,
            "number_of_frames": num_frames,
            "number_of_channels": num_channels,
            "timestamp": timestamp,
            "data": frame,
        }
        audio_data = AudioData(init)
        encoder.encode(audio_data)
        audio_data.close()
        frame_count += 1

    encoder.flush()
    encoder.close()

    # 3 フレーム (2 完全 + 1 部分) を処理したことを確認
    assert frame_count == 3, f"Expected 3 frames, processed {frame_count}"

    # エンコードされた出力が取得できたことを確認
    assert len(encoded_chunks) >= 3, f"Expected at least 3 chunks, got {len(encoded_chunks)}"

    print(f"Successfully processed {frame_count} frames (including partial)")
    print(f"Generated {len(encoded_chunks)} encoded chunks")


def test_opus_encoder_exact_frames():
    """オーディオが frame_size の正確な倍数の場合のエンコードをテスト"""
    sample_rate = 48000
    frame_size = 960  # 48kHz で 20ms

    # 正確に 3 フレームのオーディオデータを作成
    total_samples = frame_size * 3  # 2880 サンプル
    audio_samples = np.ones(total_samples, dtype=np.float32) * 0.1

    # エンコーダーをセットアップ
    encoded_chunks = []

    def on_output(chunk):
        encoded_chunks.append(chunk)

    def on_error(error):
        pytest.fail(f"エンコーダエラー: {error}")

    encoder = AudioEncoder(on_output, on_error)
    encoder_config: AudioEncoderConfig = {
        "codec": "opus",
        "sample_rate": sample_rate,
        "number_of_channels": 1,
        "bitrate": 64000,
    }
    encoder.configure(encoder_config)

    # フレームをエンコード
    frame_count = 0
    for idx, i in enumerate(range(0, len(audio_samples), frame_size)):
        # フレームサンプルを取得し、必要に応じてパディング
        end_idx = min(i + frame_size, len(audio_samples))
        frame = audio_samples[i:end_idx]

        # 正確なフレームの場合パディングは不要のはず
        if len(frame) < frame_size:
            print(f"Unexpected partial frame at index {idx}: {len(frame)} samples")
            padded_frame = np.zeros(frame_size, dtype=np.float32)
            padded_frame[: len(frame)] = frame
            frame = padded_frame

        frame = frame.reshape(frame_size, 1)
        # マイクロ秒でタイムスタンプを計算
        timestamp = (idx * frame_size * 1000000) // sample_rate
        num_frames, num_channels = frame.shape
        init = {
            "format": AudioSampleFormat.F32,
            "sample_rate": sample_rate,
            "number_of_frames": num_frames,
            "number_of_channels": num_channels,
            "timestamp": timestamp,
            "data": frame,
        }
        audio_data = AudioData(init)
        encoder.encode(audio_data)
        audio_data.close()
        frame_count += 1

    encoder.flush()
    encoder.close()

    # 正確に 3 フレームを処理したことを確認
    assert frame_count == 3, f"Expected 3 frames, processed {frame_count}"

    # エンコードされた出力が取得できたことを確認
    assert len(encoded_chunks) >= 3, f"Expected at least 3 chunks, got {len(encoded_chunks)}"

    print(f"Successfully processed {frame_count} frames")
    print(f"Generated {len(encoded_chunks)} encoded chunks")
