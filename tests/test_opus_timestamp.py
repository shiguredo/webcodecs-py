import numpy as np
import pytest
from webcodecs import (
    AudioData,
    AudioDataInit,
    AudioEncoder,
    AudioEncoderConfig,
    AudioSampleFormat,
)


def test_opus_encoder_timestamp_propagation():
    """エンコーダーを通してタイムスタンプが正しく伝播されることをテスト"""
    sample_rate = 48000
    frame_size = 960  # 20ms at 48kHz
    num_frames = 5

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

    # 増加するタイムスタンプで複数のフレームをエンコード
    timestamps_in = []
    for i in range(num_frames):
        # シンプルなオーディオデータを作成
        frame_samples = np.zeros((frame_size, 1), dtype=np.float32)
        frame_samples[0] = 0.1 * i  # 各フレームで異なる値

        # マイクロ秒でタイムスタンプを計算
        timestamp = (i * frame_size * 1000000) // sample_rate
        timestamps_in.append(timestamp)

        # AudioData を作成してエンコード
        number_of_frames, number_of_channels = frame_samples.shape
        init: AudioDataInit = {
            "format": AudioSampleFormat.F32,
            "sample_rate": sample_rate,
            "number_of_frames": number_of_frames,
            "number_of_channels": number_of_channels,
            "timestamp": timestamp,
            "data": frame_samples,
        }
        audio_data = AudioData(init)
        encoder.encode(audio_data)
        audio_data.close()

    encoder.flush()
    encoder.close()

    # エンコードされたチャンクが取得できたことを確認
    assert len(encoded_chunks) > 0

    # エンコードされたチャンクからタイムスタンプを抽出
    timestamps_out = [chunk.timestamp for chunk in encoded_chunks]

    # タイムスタンプが単調増加していることを確認
    for i in range(1, len(timestamps_out)):
        assert timestamps_out[i] > timestamps_out[i - 1], (
            f"Timestamps not monotonically increasing: {timestamps_out[i - 1]} >= {timestamps_out[i]}"
        )

    # 最初のタイムスタンプが入力と一致することを確認
    assert timestamps_out[0] == timestamps_in[0], (
        f"First timestamp mismatch: expected {timestamps_in[0]}, got {timestamps_out[0]}"
    )

    # デバッグ用に出力
    print(f"Input timestamps: {timestamps_in}")
    print(f"Output timestamps: {timestamps_out}")


def test_opus_encoder_multiple_frames_single_encode():
    """一度に複数のフレームをエンコードする際のタイムスタンプの挙動をテスト"""
    sample_rate = 48000
    frame_size = 960
    num_frames = 3

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

    # 複数フレームのオーディオデータを作成
    total_samples = frame_size * num_frames
    audio_samples = np.zeros((total_samples, 1), dtype=np.float32)

    # 初期タイムスタンプを設定
    initial_timestamp = 123456  # マイクロ秒での任意のゼロでない値

    # 1つの大きな AudioData としてエンコード
    number_of_frames, number_of_channels = audio_samples.shape
    init: AudioDataInit = {
        "format": AudioSampleFormat.F32,
        "sample_rate": sample_rate,
        "number_of_frames": number_of_frames,
        "number_of_channels": number_of_channels,
        "timestamp": initial_timestamp,
        "data": audio_samples,
    }
    audio_data = AudioData(init)
    encoder.encode(audio_data)
    audio_data.close()

    encoder.flush()
    encoder.close()

    # エンコードされたチャンクが取得できたことを確認
    assert len(encoded_chunks) > 0

    # 最初のチャンクが期待されるタイムスタンプを持つことを確認
    assert encoded_chunks[0].timestamp == initial_timestamp

    # 複数のチャンクが生成された場合、単調増加していることを確認
    if len(encoded_chunks) > 1:
        for i in range(1, len(encoded_chunks)):
            assert encoded_chunks[i].timestamp > encoded_chunks[i - 1].timestamp, (
                f"Timestamps not increasing: {encoded_chunks[i - 1].timestamp} >= {encoded_chunks[i].timestamp}"
            )


def test_opus_encoder_zero_timestamp():
    """ゼロタイムスタンプが有効で正しく処理されることをテスト"""
    sample_rate = 48000
    frame_size = 960

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

    # タイムスタンプ 0 でオーディオデータを作成
    frame_samples = np.zeros((frame_size, 1), dtype=np.float32)
    number_of_frames, number_of_channels = frame_samples.shape
    init: AudioDataInit = {
        "format": AudioSampleFormat.F32,
        "sample_rate": sample_rate,
        "number_of_frames": number_of_frames,
        "number_of_channels": number_of_channels,
        "timestamp": 0,
        "data": frame_samples,
    }
    audio_data = AudioData(init)
    encoder.encode(audio_data)
    audio_data.close()

    encoder.flush()
    encoder.close()

    # タイムスタンプ 0 のエンコードされたチャンクが取得できたことを確認
    assert len(encoded_chunks) > 0
    assert encoded_chunks[0].timestamp == 0
