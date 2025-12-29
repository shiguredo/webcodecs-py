"""Property-Based Testing によるファジングテスト (Audio)

ランダムな ndarray を入力してクラッシュやセグフォルトが発生しないことを確認
"""

import numpy as np
from hypothesis import given, settings, HealthCheck
from hypothesis import strategies as st
from hypothesis.extra.numpy import arrays

from webcodecs import (
    AudioData,
    AudioDataCopyToOptions,
    AudioDataInit,
    AudioSampleFormat,
)


# インターリーブフォーマット
INTERLEAVED_AUDIO_FORMATS = [
    AudioSampleFormat.U8,
    AudioSampleFormat.S16,
    AudioSampleFormat.S32,
    AudioSampleFormat.F32,
]

# プレーナーフォーマット
PLANAR_AUDIO_FORMATS = [
    AudioSampleFormat.U8_PLANAR,
    AudioSampleFormat.S16_PLANAR,
    AudioSampleFormat.S32_PLANAR,
    AudioSampleFormat.F32_PLANAR,
]


def get_audio_dtype(audio_format: AudioSampleFormat) -> np.dtype:
    """AudioSampleFormat に対応する numpy dtype を返す"""
    if audio_format in (AudioSampleFormat.U8, AudioSampleFormat.U8_PLANAR):
        return np.dtype(np.uint8)
    elif audio_format in (AudioSampleFormat.S16, AudioSampleFormat.S16_PLANAR):
        return np.dtype(np.int16)
    elif audio_format in (AudioSampleFormat.S32, AudioSampleFormat.S32_PLANAR):
        return np.dtype(np.int32)
    elif audio_format in (AudioSampleFormat.F32, AudioSampleFormat.F32_PLANAR):
        return np.dtype(np.float32)
    else:
        raise ValueError(f"未知のオーディオフォーマット: {audio_format}")


def is_planar_format(audio_format: AudioSampleFormat) -> bool:
    """プレーナーフォーマットかどうかを判定"""
    return audio_format in PLANAR_AUDIO_FORMATS


@st.composite
def audio_data_strategy(draw):
    """ランダムな AudioData 設定を生成するストラテジ"""
    # フォーマットを選択
    audio_format = draw(st.sampled_from(INTERLEAVED_AUDIO_FORMATS + PLANAR_AUDIO_FORMATS))

    # パラメータを生成
    sample_rate = draw(st.sampled_from([8000, 16000, 22050, 44100, 48000, 96000]))
    number_of_channels = draw(st.integers(min_value=1, max_value=8))
    number_of_frames = draw(st.integers(min_value=1, max_value=4800))

    # timestamp
    timestamp = draw(st.integers(min_value=0, max_value=2**62))

    dtype = get_audio_dtype(audio_format)

    # データの形状を決定
    if is_planar_format(audio_format):
        shape = (number_of_channels, number_of_frames)
    else:
        shape = (number_of_frames, number_of_channels)

    # ランダムなオーディオデータを生成
    if dtype == np.float32:
        # float32 の場合は -1.0 から 1.0 の範囲
        data = draw(
            arrays(
                dtype=dtype,
                shape=shape,
                elements=st.floats(
                    min_value=-1.0, max_value=1.0, allow_nan=False, allow_infinity=False
                ),
            )
        )
    elif dtype == np.uint8:
        data = draw(
            arrays(
                dtype=dtype,
                shape=shape,
                elements=st.integers(min_value=0, max_value=255),
            )
        )
    elif dtype == np.int16:
        data = draw(
            arrays(
                dtype=dtype,
                shape=shape,
                elements=st.integers(min_value=-32768, max_value=32767),
            )
        )
    else:
        # int32
        data = draw(
            arrays(
                dtype=dtype,
                shape=shape,
                elements=st.integers(min_value=-(2**31), max_value=2**31 - 1),
            )
        )

    return {
        "format": audio_format,
        "sample_rate": sample_rate,
        "number_of_channels": number_of_channels,
        "number_of_frames": number_of_frames,
        "timestamp": timestamp,
        "data": data,
    }


@given(config=audio_data_strategy())
@settings(
    max_examples=100,
    deadline=None,
    suppress_health_check=[HealthCheck.too_slow, HealthCheck.data_too_large],
)
def prop_audio_data_random_samples(config):
    """ランダムなオーディオサンプルで AudioData を作成してクラッシュしないことを確認"""
    init: AudioDataInit = {
        "format": config["format"],
        "sample_rate": config["sample_rate"],
        "number_of_frames": config["number_of_frames"],
        "number_of_channels": config["number_of_channels"],
        "timestamp": config["timestamp"],
        "data": config["data"],
    }

    # AudioData を作成
    audio = AudioData(init)

    # 基本的なプロパティにアクセスできることを確認
    assert audio.format == config["format"]
    assert audio.sample_rate == config["sample_rate"]
    assert audio.number_of_frames == config["number_of_frames"]
    assert audio.number_of_channels == config["number_of_channels"]
    assert audio.timestamp == config["timestamp"]
    assert not audio.is_closed

    audio.close()
    assert audio.is_closed


@given(config=audio_data_strategy())
@settings(
    max_examples=50,
    deadline=None,
    suppress_health_check=[HealthCheck.too_slow, HealthCheck.data_too_large],
)
def prop_audio_data_operations_with_random_data(config):
    """ランダムな AudioData に対して各種操作を実行してクラッシュしないことを確認"""
    init: AudioDataInit = {
        "format": config["format"],
        "sample_rate": config["sample_rate"],
        "number_of_frames": config["number_of_frames"],
        "number_of_channels": config["number_of_channels"],
        "timestamp": config["timestamp"],
        "data": config["data"],
    }

    audio = AudioData(init)

    # allocation_size() を呼び出す
    options: AudioDataCopyToOptions = {"plane_index": 0}
    alloc_size = audio.allocation_size(options)
    assert alloc_size > 0

    # copy_to() を呼び出す
    destination = np.zeros(alloc_size, dtype=np.uint8)
    audio.copy_to(destination, options)

    # clone() を呼び出す
    cloned = audio.clone()
    assert cloned.format == audio.format
    assert cloned.sample_rate == audio.sample_rate
    assert cloned.number_of_frames == audio.number_of_frames
    assert cloned.number_of_channels == audio.number_of_channels
    cloned.close()

    # duration を確認
    duration = audio.duration
    assert duration > 0

    audio.close()


@given(
    sample_rate=st.sampled_from([8000, 16000, 48000]),
    number_of_channels=st.integers(min_value=1, max_value=2),
    number_of_frames=st.integers(min_value=1, max_value=960),
)
@settings(
    max_examples=50,
    deadline=None,
    suppress_health_check=[HealthCheck.too_slow],
)
def prop_audio_data_extreme_float_values(sample_rate, number_of_channels, number_of_frames):
    """極端な float 値でクラッシュしないことを確認"""
    shape = (number_of_frames, number_of_channels)

    # 全て 0 のデータ
    data_zeros = np.zeros(shape, dtype=np.float32)
    init: AudioDataInit = {
        "format": AudioSampleFormat.F32,
        "sample_rate": sample_rate,
        "number_of_frames": number_of_frames,
        "number_of_channels": number_of_channels,
        "timestamp": 0,
        "data": data_zeros,
    }
    audio_zeros = AudioData(init)
    assert audio_zeros.number_of_frames == number_of_frames
    audio_zeros.close()

    # -1.0 で埋める
    data_min = np.full(shape, -1.0, dtype=np.float32)
    init["data"] = data_min
    audio_min = AudioData(init)
    assert audio_min.number_of_frames == number_of_frames
    audio_min.close()

    # 1.0 で埋める
    data_max = np.full(shape, 1.0, dtype=np.float32)
    init["data"] = data_max
    audio_max = AudioData(init)
    assert audio_max.number_of_frames == number_of_frames
    audio_max.close()

    # 範囲外の大きな値 (クリッピングされる可能性があるが、クラッシュしてはいけない)
    data_large = np.full(shape, 100.0, dtype=np.float32)
    init["data"] = data_large
    audio_large = AudioData(init)
    assert audio_large.number_of_frames == number_of_frames
    audio_large.close()

    data_large_neg = np.full(shape, -100.0, dtype=np.float32)
    init["data"] = data_large_neg
    audio_large_neg = AudioData(init)
    assert audio_large_neg.number_of_frames == number_of_frames
    audio_large_neg.close()


@given(
    sample_rate=st.sampled_from([8000, 16000, 48000]),
    number_of_channels=st.integers(min_value=1, max_value=2),
    number_of_frames=st.integers(min_value=1, max_value=960),
)
@settings(
    max_examples=30,
    deadline=None,
    suppress_health_check=[HealthCheck.too_slow],
)
def prop_audio_data_integer_formats(sample_rate, number_of_channels, number_of_frames):
    """整数フォーマットのオーディオデータでクラッシュしないことを確認"""
    # U8 フォーマット
    u8_shape = (number_of_frames, number_of_channels)
    data_u8 = np.random.randint(0, 256, size=u8_shape, dtype=np.uint8)
    init_u8: AudioDataInit = {
        "format": AudioSampleFormat.U8,
        "sample_rate": sample_rate,
        "number_of_frames": number_of_frames,
        "number_of_channels": number_of_channels,
        "timestamp": 0,
        "data": data_u8,
    }
    audio_u8 = AudioData(init_u8)
    assert audio_u8.format == AudioSampleFormat.U8
    audio_u8.close()

    # S16 フォーマット
    data_s16 = np.random.randint(-32768, 32768, size=u8_shape, dtype=np.int16)
    init_s16: AudioDataInit = {
        "format": AudioSampleFormat.S16,
        "sample_rate": sample_rate,
        "number_of_frames": number_of_frames,
        "number_of_channels": number_of_channels,
        "timestamp": 0,
        "data": data_s16,
    }
    audio_s16 = AudioData(init_s16)
    assert audio_s16.format == AudioSampleFormat.S16
    audio_s16.close()

    # S32 フォーマット
    data_s32 = np.random.randint(-(2**30), 2**30, size=u8_shape, dtype=np.int32)
    init_s32: AudioDataInit = {
        "format": AudioSampleFormat.S32,
        "sample_rate": sample_rate,
        "number_of_frames": number_of_frames,
        "number_of_channels": number_of_channels,
        "timestamp": 0,
        "data": data_s32,
    }
    audio_s32 = AudioData(init_s32)
    assert audio_s32.format == AudioSampleFormat.S32
    audio_s32.close()


@given(
    sample_rate=st.sampled_from([8000, 16000, 48000]),
    number_of_channels=st.integers(min_value=1, max_value=4),
    number_of_frames=st.integers(min_value=1, max_value=960),
)
@settings(
    max_examples=30,
    deadline=None,
    suppress_health_check=[HealthCheck.too_slow],
)
def prop_audio_data_planar_formats(sample_rate, number_of_channels, number_of_frames):
    """プレーナーフォーマットのオーディオデータでクラッシュしないことを確認"""
    # プレーナーフォーマットは (channels, frames) の形状
    planar_shape = (number_of_channels, number_of_frames)

    # F32_PLANAR フォーマット
    data_f32 = np.random.uniform(-1.0, 1.0, size=planar_shape).astype(np.float32)
    init_f32: AudioDataInit = {
        "format": AudioSampleFormat.F32_PLANAR,
        "sample_rate": sample_rate,
        "number_of_frames": number_of_frames,
        "number_of_channels": number_of_channels,
        "timestamp": 0,
        "data": data_f32,
    }
    audio_f32 = AudioData(init_f32)
    assert audio_f32.format == AudioSampleFormat.F32_PLANAR
    audio_f32.close()

    # S16_PLANAR フォーマット
    data_s16 = np.random.randint(-32768, 32768, size=planar_shape, dtype=np.int16)
    init_s16: AudioDataInit = {
        "format": AudioSampleFormat.S16_PLANAR,
        "sample_rate": sample_rate,
        "number_of_frames": number_of_frames,
        "number_of_channels": number_of_channels,
        "timestamp": 0,
        "data": data_s16,
    }
    audio_s16 = AudioData(init_s16)
    assert audio_s16.format == AudioSampleFormat.S16_PLANAR
    audio_s16.close()


@given(
    number_of_channels=st.integers(min_value=1, max_value=4),
    number_of_frames=st.integers(min_value=10, max_value=960),
    frame_offset=st.integers(min_value=0, max_value=9),
)
@settings(
    max_examples=30,
    deadline=None,
    suppress_health_check=[HealthCheck.too_slow],
)
def prop_audio_data_copy_to_with_offset(number_of_channels, number_of_frames, frame_offset):
    """copy_to() で frame_offset を使用してクラッシュしないことを確認"""
    shape = (number_of_frames, number_of_channels)
    data = np.random.uniform(-1.0, 1.0, size=shape).astype(np.float32)

    init: AudioDataInit = {
        "format": AudioSampleFormat.F32,
        "sample_rate": 48000,
        "number_of_frames": number_of_frames,
        "number_of_channels": number_of_channels,
        "timestamp": 0,
        "data": data,
    }
    audio = AudioData(init)

    # frame_offset を指定して copy_to
    options: AudioDataCopyToOptions = {"plane_index": 0, "frame_offset": frame_offset}
    alloc_size = audio.allocation_size(options)
    destination = np.zeros(alloc_size, dtype=np.uint8)
    audio.copy_to(destination, options)

    audio.close()


@given(
    number_of_channels=st.integers(min_value=1, max_value=4),
    number_of_frames=st.integers(min_value=10, max_value=960),
    frame_count=st.integers(min_value=1, max_value=9),
)
@settings(
    max_examples=30,
    deadline=None,
    suppress_health_check=[HealthCheck.too_slow],
)
def prop_audio_data_copy_to_with_frame_count(number_of_channels, number_of_frames, frame_count):
    """copy_to() で frame_count を使用してクラッシュしないことを確認"""
    shape = (number_of_frames, number_of_channels)
    data = np.random.uniform(-1.0, 1.0, size=shape).astype(np.float32)

    init: AudioDataInit = {
        "format": AudioSampleFormat.F32,
        "sample_rate": 48000,
        "number_of_frames": number_of_frames,
        "number_of_channels": number_of_channels,
        "timestamp": 0,
        "data": data,
    }
    audio = AudioData(init)

    # frame_count を指定して copy_to
    options: AudioDataCopyToOptions = {"plane_index": 0, "frame_count": frame_count}
    alloc_size = audio.allocation_size(options)
    destination = np.zeros(alloc_size, dtype=np.uint8)
    audio.copy_to(destination, options)

    audio.close()
