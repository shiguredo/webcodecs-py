"""オーディオテスト用ユーティリティ関数"""

import numpy as np

from webcodecs import AudioData, AudioSampleFormat


def audio_data_to_float32(audio: AudioData) -> np.ndarray:
    """AudioData を float32 の numpy 配列に変換する

    Args:
        audio: 変換する AudioData

    Returns:
        float32 の numpy 配列 (frames, channels)
    """
    number_of_frames = audio.number_of_frames
    number_of_channels = audio.number_of_channels

    # 必要なサイズを計算
    buffer = np.empty((number_of_frames, number_of_channels), dtype=np.float32)

    # copy_to で F32 フォーマットに変換してコピー
    audio.copy_to(buffer, {"plane_index": 0, "format": AudioSampleFormat.F32})

    return buffer


def generate_sine_wave(
    frequency: float, sample_rate: int, duration: float, amplitude: float = 0.5
) -> np.ndarray:
    """正弦波を生成する

    Args:
        frequency: 周波数 (Hz)
        sample_rate: サンプリングレート (Hz)
        duration: 長さ（秒）
        amplitude: 振幅 (0.0 - 1.0)

    Returns:
        正弦波のサンプル配列
    """
    samples = int(sample_rate * duration)
    t = np.arange(samples) / sample_rate
    signal = amplitude * np.sin(2 * np.pi * frequency * t)
    return signal.astype(np.float32)


def generate_stereo_test_signal(
    sample_rate: int, duration: float, left_freq: float = 440, right_freq: float = 880
) -> np.ndarray:
    """ステレオテスト信号を生成する

    Args:
        sample_rate: サンプリングレート (Hz)
        duration: 長さ（秒）
        left_freq: 左チャンネルの周波数 (Hz)
        right_freq: 右チャンネルの周波数 (Hz)

    Returns:
        ステレオ信号配列 (samples, 2)
    """
    samples = int(sample_rate * duration)
    t = np.arange(samples) / sample_rate

    # 左チャンネル: left_freq Hz 正弦波
    left = 0.3 * np.sin(2 * np.pi * left_freq * t)

    # 右チャンネル: right_freq Hz 正弦波
    right = 0.3 * np.sin(2 * np.pi * right_freq * t)

    # ステレオ信号として結合
    stereo = np.column_stack((left, right))

    return stereo.astype(np.float32)


def generate_complex_audio(sample_rate: int, duration: float) -> np.ndarray:
    """複数の周波数と倍音を含む複雑なオーディオ信号を生成する

    Args:
        sample_rate: サンプリングレート (Hz)
        duration: 長さ（秒）

    Returns:
        複雑なオーディオ信号配列
    """
    t = np.linspace(0, duration, int(sample_rate * duration), endpoint=False)

    # 基本周波数 (A4)
    base = 440

    # 倍音を生成
    signal = np.zeros_like(t, dtype=np.float32)
    harmonics = [1, 2, 3, 4, 5]  # 基音 + 倍音
    amplitudes = [1.0, 0.5, 0.33, 0.25, 0.2]  # 減衰する振幅

    for harmonic, amplitude in zip(harmonics, amplitudes):
        signal += amplitude * np.sin(2 * np.pi * base * harmonic * t)

    # クリッピングを防ぐため正規化
    signal = signal / np.max(np.abs(signal)) * 0.8

    return signal.astype(np.float32)
