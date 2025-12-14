"""AudioData のテスト

AudioData の作成、プロパティ、配列処理のテスト
"""

import numpy as np
import pytest
from webcodecs import AudioData, AudioDataInit, AudioDataCopyToOptions, AudioSampleFormat


def test_audio_data_creation():
    """AudioData の基本的な作成とプロパティ確認"""
    data = np.zeros((960, 2), dtype=np.float32)
    init: AudioDataInit = {
        "format": AudioSampleFormat.F32,
        "sample_rate": 48000,
        "number_of_frames": 960,
        "number_of_channels": 2,
        "timestamp": 0,
        "data": data,
    }
    audio = AudioData(init)
    assert audio.number_of_channels == 2
    assert audio.sample_rate == 48000
    assert audio.number_of_frames == 960
    assert audio.format == AudioSampleFormat.F32
    assert not audio.is_closed

    audio.close()
    assert audio.is_closed


def test_audio_data_mono():
    """モノラル AudioData の作成"""
    sample_rate = 48000
    frames = 960

    # モノラル: (frames, 1)
    mono_data = np.ones((frames, 1), dtype=np.float32) * 0.5

    # AudioData を作成
    init: AudioDataInit = {
        "format": AudioSampleFormat.F32,
        "sample_rate": sample_rate,
        "number_of_frames": frames,
        "number_of_channels": 1,
        "timestamp": 0,
        "data": mono_data,
    }
    audio_data = AudioData(init)

    # チャンネル数が 1 になっていることを確認
    assert audio_data.number_of_channels == 1
    assert audio_data.number_of_frames == frames
    assert audio_data.sample_rate == sample_rate

    audio_data.close()


def test_audio_data_stereo():
    """ステレオ AudioData の作成"""
    sample_rate = 48000
    frames = 960
    channels = 2

    # ステレオ: (frames, channels)
    stereo_data = np.ones((frames, channels), dtype=np.float32) * 0.5

    # AudioData を作成
    init: AudioDataInit = {
        "format": AudioSampleFormat.F32,
        "sample_rate": sample_rate,
        "number_of_frames": frames,
        "number_of_channels": channels,
        "timestamp": 0,
        "data": stereo_data,
    }
    audio_data = AudioData(init)

    # チャンネル数が 2 になっていることを確認
    assert audio_data.number_of_channels == channels
    assert audio_data.number_of_frames == frames
    assert audio_data.sample_rate == sample_rate

    audio_data.close()


def test_audio_data_invalid_shape_error():
    """データサイズが number_of_frames と number_of_channels に一致しない場合エラー"""
    sample_rate = 48000

    # サイズが一致しないデータを作成
    invalid_data = np.ones((960, 3), dtype=np.float32)

    # エラーが発生することを確認
    with pytest.raises(RuntimeError):
        AudioData(
            {
                "format": AudioSampleFormat.F32,
                "sample_rate": sample_rate,
                "number_of_frames": 960,
                "number_of_channels": 2,
                "timestamp": 0,
                "data": invalid_data,
            }
        )


def test_audio_data_init_dict():
    """AudioDataInit dict を使った AudioData の作成 (WebCodecs API 準拠)"""
    data = np.zeros((960, 2), dtype=np.float32)
    init: AudioDataInit = {
        "format": AudioSampleFormat.F32,
        "sample_rate": 48000,
        "number_of_frames": 960,
        "number_of_channels": 2,
        "timestamp": 1234567,
        "data": data,
    }
    audio = AudioData(init)
    assert audio.number_of_channels == 2
    assert audio.sample_rate == 48000
    assert audio.number_of_frames == 960
    assert audio.format == AudioSampleFormat.F32
    assert audio.timestamp == 1234567
    assert not audio.is_closed

    audio.close()
    assert audio.is_closed


def test_audio_data_init_dict_mono():
    """AudioDataInit dict を使ったモノラル AudioData の作成"""
    sample_rate = 48000
    frames = 960
    mono_data = np.ones((frames, 1), dtype=np.float32) * 0.5

    init: AudioDataInit = {
        "format": AudioSampleFormat.F32,
        "sample_rate": sample_rate,
        "number_of_frames": frames,
        "number_of_channels": 1,
        "timestamp": 0,
        "data": mono_data,
    }
    audio_data = AudioData(init)

    assert audio_data.number_of_channels == 1
    assert audio_data.number_of_frames == frames
    assert audio_data.sample_rate == sample_rate

    audio_data.close()


def test_audio_data_init_dict_missing_required_field():
    """AudioDataInit で必須フィールドが欠けている場合エラー"""
    data = np.zeros((960, 2), dtype=np.float32)

    # format が欠けている
    with pytest.raises(ValueError):
        AudioData(
            {  # type: ignore[typeddict-item]
                "sample_rate": 48000,
                "number_of_frames": 960,
                "number_of_channels": 2,
                "timestamp": 0,
                "data": data,
            }
        )

    # data が欠けている
    with pytest.raises(ValueError):
        AudioData(
            {  # type: ignore[typeddict-item]
                "format": AudioSampleFormat.F32,
                "sample_rate": 48000,
                "number_of_frames": 960,
                "number_of_channels": 2,
                "timestamp": 0,
            }
        )


def test_audio_data_clone():
    """clone() でオーディオデータを複製"""
    sample_rate = 48000
    frames = 960
    channels = 2

    # 元データを作成
    original_data = np.ones((frames, channels), dtype=np.float32) * 0.5

    # AudioData を作成
    init: AudioDataInit = {
        "format": AudioSampleFormat.F32,
        "sample_rate": sample_rate,
        "number_of_frames": frames,
        "number_of_channels": channels,
        "timestamp": 1234567,
        "data": original_data,
    }
    original = AudioData(init)

    # clone() で複製
    cloned = original.clone()

    # プロパティが複製されていることを確認
    assert cloned.format == original.format
    assert cloned.sample_rate == original.sample_rate
    assert cloned.number_of_frames == original.number_of_frames
    assert cloned.number_of_channels == original.number_of_channels
    assert cloned.timestamp == original.timestamp

    # 複製したデータは元のデータとは独立していることを確認
    # (AudioData は内部でコピーを保持しているため、元データを変更しても影響しない)
    original.close()

    # 複製は閉じられていない
    assert not cloned.is_closed

    # 複製したデータの allocation_size を確認
    options: AudioDataCopyToOptions = {"plane_index": 0}
    assert cloned.allocation_size(options) == frames * channels * 4

    cloned.close()


def test_audio_data_copy_to_interleaved():
    """copy_to() でインターリーブデータをコピー (WebCodecs API 準拠)"""
    sample_rate = 48000
    frames = 100
    channels = 2

    # ステレオのインターリーブデータを作成
    original_data = np.arange(frames * channels, dtype=np.float32).reshape(frames, channels)
    init: AudioDataInit = {
        "format": AudioSampleFormat.F32,
        "sample_rate": sample_rate,
        "number_of_frames": frames,
        "number_of_channels": channels,
        "timestamp": 0,
        "data": original_data,
    }
    audio = AudioData(init)

    # インターリーブフォーマットでは plane_index=0 のみ有効
    options: AudioDataCopyToOptions = {"plane_index": 0}
    size = audio.allocation_size(options)
    assert size == frames * channels * 4

    # destination バッファを作成してコピー
    destination = np.zeros(size, dtype=np.uint8)
    audio.copy_to(destination, options)

    # データが正しくコピーされたことを確認
    copied_data = np.frombuffer(destination, dtype=np.float32).reshape(frames, channels)
    np.testing.assert_array_equal(copied_data, original_data)

    audio.close()


def test_audio_data_copy_to_planar():
    """copy_to() でプレーナーデータをコピー (WebCodecs API 準拠)"""
    sample_rate = 48000
    frames = 100
    channels = 2

    # ステレオのプレーナーデータを作成 (channels, frames)
    original_data = np.arange(frames * channels, dtype=np.float32).reshape(channels, frames)
    init: AudioDataInit = {
        "format": AudioSampleFormat.F32_PLANAR,
        "sample_rate": sample_rate,
        "number_of_frames": frames,
        "number_of_channels": channels,
        "timestamp": 0,
        "data": original_data,
    }
    audio = AudioData(init)

    # プレーナーフォーマットでは各チャンネルが別のプレーン
    # plane_index=0 は最初のチャンネル
    options: AudioDataCopyToOptions = {"plane_index": 0}
    size = audio.allocation_size(options)
    assert size == frames * 4

    # destination バッファを作成してコピー
    destination = np.zeros(size, dtype=np.uint8)
    audio.copy_to(destination, options)

    # データが正しくコピーされたことを確認
    copied_data = np.frombuffer(destination, dtype=np.float32)
    np.testing.assert_array_equal(copied_data, original_data[0])

    # plane_index=1 で2番目のチャンネルをコピー
    options: AudioDataCopyToOptions = {"plane_index": 1}
    audio.copy_to(destination, options)
    copied_data = np.frombuffer(destination, dtype=np.float32)
    np.testing.assert_array_equal(copied_data, original_data[1])

    audio.close()


def test_audio_data_copy_to_with_frame_offset():
    """copy_to() で frame_offset を指定して部分コピー"""
    sample_rate = 48000
    frames = 100
    channels = 2

    # データを作成
    original_data = np.arange(frames * channels, dtype=np.float32).reshape(frames, channels)
    init: AudioDataInit = {
        "format": AudioSampleFormat.F32,
        "sample_rate": sample_rate,
        "number_of_frames": frames,
        "number_of_channels": channels,
        "timestamp": 0,
        "data": original_data,
    }
    audio = AudioData(init)

    # frame_offset=50 で後半50フレームをコピー
    options: AudioDataCopyToOptions = {"plane_index": 0, "frame_offset": 50}
    size = audio.allocation_size(options)
    assert size == 50 * channels * 4

    destination = np.zeros(size, dtype=np.uint8)
    audio.copy_to(destination, options)

    # 後半50フレームが正しくコピーされたことを確認
    copied_data = np.frombuffer(destination, dtype=np.float32).reshape(50, channels)
    np.testing.assert_array_equal(copied_data, original_data[50:])

    audio.close()


def test_audio_data_copy_to_with_frame_count():
    """copy_to() で frame_count を指定して部分コピー"""
    sample_rate = 48000
    frames = 100
    channels = 2

    # データを作成
    original_data = np.arange(frames * channels, dtype=np.float32).reshape(frames, channels)
    init: AudioDataInit = {
        "format": AudioSampleFormat.F32,
        "sample_rate": sample_rate,
        "number_of_frames": frames,
        "number_of_channels": channels,
        "timestamp": 0,
        "data": original_data,
    }
    audio = AudioData(init)

    # frame_offset=10, frame_count=20 で10-29フレームをコピー
    options: AudioDataCopyToOptions = {"plane_index": 0, "frame_offset": 10, "frame_count": 20}
    size = audio.allocation_size(options)
    assert size == 20 * channels * 4

    destination = np.zeros(size, dtype=np.uint8)
    audio.copy_to(destination, options)

    # 10-29フレームが正しくコピーされたことを確認
    copied_data = np.frombuffer(destination, dtype=np.float32).reshape(20, channels)
    np.testing.assert_array_equal(copied_data, original_data[10:30])

    audio.close()


def test_audio_data_copy_to_invalid_plane_index():
    """copy_to() で無効な plane_index を指定するとエラー"""
    sample_rate = 48000
    frames = 100
    channels = 2

    # インターリーブデータを作成
    original_data = np.zeros((frames, channels), dtype=np.float32)
    init: AudioDataInit = {
        "format": AudioSampleFormat.F32,
        "sample_rate": sample_rate,
        "number_of_frames": frames,
        "number_of_channels": channels,
        "timestamp": 0,
        "data": original_data,
    }
    audio = AudioData(init)

    # インターリーブフォーマットでは plane_index=0 のみ有効
    options: AudioDataCopyToOptions = {"plane_index": 1}
    with pytest.raises(RuntimeError):
        audio.allocation_size(options)

    audio.close()


def test_audio_data_allocation_size_requires_options():
    """allocation_size() は options が必須"""
    sample_rate = 48000
    frames = 100
    channels = 2

    original_data = np.zeros((frames, channels), dtype=np.float32)
    init: AudioDataInit = {
        "format": AudioSampleFormat.F32,
        "sample_rate": sample_rate,
        "number_of_frames": frames,
        "number_of_channels": channels,
        "timestamp": 0,
        "data": original_data,
    }
    audio = AudioData(init)

    # options なしで呼び出すとエラー
    with pytest.raises(TypeError):
        audio.allocation_size()  # type: ignore[call-arg]

    audio.close()


def test_audio_data_context_manager():
    """AudioData の context manager 対応テスト"""
    sample_rate = 48000
    frames = 960
    channels = 2

    data = np.ones((frames, channels), dtype=np.float32) * 0.5
    init: AudioDataInit = {
        "format": AudioSampleFormat.F32,
        "sample_rate": sample_rate,
        "number_of_frames": frames,
        "number_of_channels": channels,
        "timestamp": 0,
        "data": data,
    }

    # with 文で AudioData を使用
    with AudioData(init) as audio:
        assert not audio.is_closed
        assert audio.number_of_channels == channels
        assert audio.number_of_frames == frames

    # with 文を抜けると自動的に close される
    assert audio.is_closed


def test_audio_data_context_manager_exception():
    """AudioData の context manager で例外が発生しても close される"""
    sample_rate = 48000
    frames = 960
    channels = 2

    data = np.ones((frames, channels), dtype=np.float32) * 0.5
    init: AudioDataInit = {
        "format": AudioSampleFormat.F32,
        "sample_rate": sample_rate,
        "number_of_frames": frames,
        "number_of_channels": channels,
        "timestamp": 0,
        "data": data,
    }

    audio = None
    with pytest.raises(ValueError):
        with AudioData(init) as audio:
            assert not audio.is_closed
            raise ValueError("test exception")

    # 例外が発生しても close される
    assert audio is not None
    assert audio.is_closed


def test_audio_data_context_manager_returns_self():
    """AudioData の __enter__ は self を返す"""
    sample_rate = 48000
    frames = 960
    channels = 2

    data = np.ones((frames, channels), dtype=np.float32) * 0.5
    init: AudioDataInit = {
        "format": AudioSampleFormat.F32,
        "sample_rate": sample_rate,
        "number_of_frames": frames,
        "number_of_channels": channels,
        "timestamp": 0,
        "data": data,
    }

    audio_outer = AudioData(init)
    with audio_outer as audio_inner:
        # __enter__ は self を返すので同じオブジェクト
        assert audio_outer is audio_inner

    audio_outer.close()
