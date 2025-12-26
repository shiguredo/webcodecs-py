import numpy as np
import pytest

from webcodecs import AudioData, AudioDataInit, AudioSampleFormat


def test_duration_computed_from_frames_constructor_interleaved():
    # 960 samples @ 48 kHz = 20 ms = 20000 us
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
    assert audio.duration == 20000
    assert audio.number_of_channels == 2
    assert audio.number_of_frames == 960


def test_duration_computed_from_ndarray_interleaved():
    # Shape (frames, channels)
    frames = 960
    channels = 2
    arr = np.zeros((frames, channels), dtype=np.float32)
    init: AudioDataInit = {
        "format": AudioSampleFormat.F32,
        "sample_rate": 48000,
        "number_of_frames": frames,
        "number_of_channels": channels,
        "timestamp": 0,
        "data": arr,
    }
    audio = AudioData(init)
    assert audio.duration == 20000
    assert audio.number_of_channels == channels
    assert audio.number_of_frames == frames


def test_duration_computed_from_ndarray_planar():
    # Shape (channels, frames) for planar
    frames = 960
    channels = 2
    arr = np.zeros((channels, frames), dtype=np.float32)
    init: AudioDataInit = {
        "format": AudioSampleFormat.F32_PLANAR,
        "sample_rate": 48000,
        "number_of_frames": frames,
        "number_of_channels": channels,
        "timestamp": 0,
        "data": arr,
    }
    audio = AudioData(init)
    assert audio.duration == 20000
    assert audio.number_of_channels == channels
    assert audio.number_of_frames == frames


def test_duration_rounding_down_int_division():
    # 1 sample @ 48 kHz = ~20.833 us -> floor to 20 us
    data = np.zeros((1, 1), dtype=np.float32)
    init: AudioDataInit = {
        "format": AudioSampleFormat.F32,
        "sample_rate": 48000,
        "number_of_frames": 1,
        "number_of_channels": 1,
        "timestamp": 0,
        "data": data,
    }
    audio = AudioData(init)
    assert audio.duration == 20


def test_duration_is_readonly():
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
    # duration is now readonly, computed from frames and sample rate
    assert audio.duration == 20000
    # Trying to set duration should raise an error

    with pytest.raises(AttributeError):
        audio.duration = 1234  # type: ignore[misc]


def test_duration_preserved_on_clone():
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
    assert audio.duration == 20000
    clone = audio.clone()
    assert clone.duration == 20000
