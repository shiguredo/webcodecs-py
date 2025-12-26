import numpy as np
import pytest
from webcodecs import (
    AudioData,
    AudioDataInit,
    AudioEncoder,
    AudioEncoderConfig,
    AudioSampleFormat,
)


def test_opus_encoder_float32_padding():
    """float32 パディングが最後のフレームで正しく動作することをテスト"""
    sample_rate = 48000
    frame_size = 960  # 48kHz で 20ms

    # パディングが必要なオーディオデータを作成
    # 保持されることを確認するために特徴的な値を使用
    partial_frame_size = 480
    audio_samples = np.ones(partial_frame_size, dtype=np.float32) * 0.9

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

    # 正しい dtype を使用してゼロでパディング
    padded_samples = np.zeros(frame_size, dtype=np.float32)
    padded_samples[:partial_frame_size] = audio_samples

    # パディングを確認
    assert padded_samples.dtype == np.float32, f"Expected float32, got {padded_samples.dtype}"
    assert np.all(padded_samples[:partial_frame_size] == 0.9), "First half should be 0.9"
    assert np.all(padded_samples[partial_frame_size:] == 0.0), "Second half should be 0.0"

    # AudioData を作成してエンコード
    frame = padded_samples.reshape(frame_size, 1)
    num_frames, num_channels = frame.shape
    init: AudioDataInit = {
        "format": AudioSampleFormat.F32,
        "sample_rate": sample_rate,
        "number_of_frames": num_frames,
        "number_of_channels": num_channels,
        "timestamp": 0,
        "data": frame,
    }
    audio_data = AudioData(init)
    encoder.encode(audio_data)
    audio_data.close()

    encoder.flush()
    encoder.close()

    # エンコードされた出力が取得できたことを確認
    assert len(encoded_chunks) > 0, "Expected at least one encoded chunk"

    print("Successfully encoded padded float32 frame")


def test_float64_vs_float32_padding():
    """float64 と float32 パディングの違いを示す"""
    sample_rate = 48000
    frame_size = 960

    # 特徴的な値でテストデータを作成
    test_value = 0.123456789
    partial_frame_size = 480
    test_samples = np.full(partial_frame_size, test_value, dtype=np.float32)

    # 間違い: float64 パディング (デフォルトの np.zeros)
    wrong_padded = np.zeros(frame_size)  # デフォルトで dtype=float64
    wrong_padded[:partial_frame_size] = test_samples

    # 正しい: float32 パディング
    correct_padded = np.zeros(frame_size, dtype=np.float32)
    correct_padded[:partial_frame_size] = test_samples

    print(f"Wrong padding dtype: {wrong_padded.dtype}")
    print(f"Correct padding dtype: {correct_padded.dtype}")

    # F32 フォーマットの AudioData が float64 を受け取ると、切り捨てや誤解釈が発生する可能性がある
    # 正しいアプローチはデータの整合性を保証する
    assert correct_padded.dtype == np.float32
    assert np.allclose(correct_padded[:partial_frame_size], test_value, rtol=1e-6)

    # 正しい float32 配列で AudioData を作成
    frame = correct_padded.reshape(frame_size, 1)
    num_frames, num_channels = frame.shape
    init: AudioDataInit = {
        "format": AudioSampleFormat.F32,
        "sample_rate": sample_rate,
        "number_of_frames": num_frames,
        "number_of_channels": num_channels,
        "timestamp": 0,
        "data": frame,
    }
    audio_data = AudioData(init)

    # AudioData が float32 を正しく受け付けることを確認
    assert audio_data.format == AudioSampleFormat.F32
    assert audio_data.number_of_frames == frame_size

    audio_data.close()
