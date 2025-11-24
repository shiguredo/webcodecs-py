import numpy as np

from webcodecs import (
    AudioData,
    AudioDecoder,
    AudioDecoderConfig,
    AudioEncoder,
    AudioEncoderConfig,
    AudioSampleFormat,
    CodecState,
    EncodedAudioChunk,
    EncodedAudioChunkType,
)


def test_opus_encoder_decoder_basic():
    """Test basic Opus encoding and decoding."""

    # Create encoder
    def on_encode_output(chunk):
        pass

    def on_encode_error(error):
        print(f"Encoder error: {error}")

    encoder = AudioEncoder(on_encode_output, on_encode_error)

    encoder_config: AudioEncoderConfig = {
        "codec": "opus",
        "sample_rate": 48000,
        "number_of_channels": 2,
        "bitrate": 128000,
    }
    encoder.configure(encoder_config)
    assert encoder.state == CodecState.CONFIGURED

    # Create decoder
    def on_decode_output(audio):
        pass

    def on_decode_error(error):
        print(f"Decoder error: {error}")

    decoder = AudioDecoder(on_decode_output, on_decode_error)

    decoder_config: AudioDecoderConfig = {
        "codec": "opus",
        "sample_rate": 48000,
        "number_of_channels": 2,
    }
    decoder.configure(decoder_config)
    assert decoder.state == CodecState.CONFIGURED


def test_opus_various_sample_rates():
    """Test Opus encoder with various sample rates."""
    # Opus supports 8, 12, 16, 24, 48 kHz (48 kHz is recommended)
    sample_rates = [8000, 12000, 16000, 24000, 48000]

    for sample_rate in sample_rates:

        def on_output(chunk):
            pass

        def on_error(error):
            pass

        encoder = AudioEncoder(on_output, on_error)
        encoder_config: AudioEncoderConfig = {
            "codec": "opus",
            "sample_rate": sample_rate,
            "number_of_channels": 2,
            "bitrate": 64000,
        }
        encoder.configure(encoder_config)
        assert encoder.state == CodecState.CONFIGURED
        encoder.close()


def test_opus_various_channel_configs():
    """Test Opus encoder with various channel configurations."""
    channel_configs = [
        1,  # Mono
        2,  # Stereo
    ]

    for channels in channel_configs:

        def on_output(chunk):
            pass

        def on_error(error):
            pass

        encoder = AudioEncoder(on_output, on_error)
        encoder_config: AudioEncoderConfig = {
            "codec": "opus",
            "sample_rate": 48000,
            "number_of_channels": channels,
            "bitrate": 64000 * channels,
        }
        encoder.configure(encoder_config)
        assert encoder.state == CodecState.CONFIGURED
        encoder.close()


def test_opus_various_bitrates():
    """Test Opus encoder with various bitrates."""
    # Opus supports 6-510 kbps
    bitrates = [
        6000,  # Minimum
        16000,  # Low quality speech
        32000,  # Medium quality speech
        64000,  # High quality speech
        96000,  # Music
        128000,  # High quality music
        256000,  # Very high quality
        510000,  # Maximum
    ]

    for bitrate in bitrates:

        def on_output(chunk):
            pass

        def on_error(error):
            pass

        encoder = AudioEncoder(on_output, on_error)
        encoder_config: AudioEncoderConfig = {
            "codec": "opus",
            "sample_rate": 48000,
            "number_of_channels": 2,
            "bitrate": bitrate,
        }
        encoder.configure(encoder_config)
        assert encoder.state == CodecState.CONFIGURED
        encoder.close()


def test_opus_frame_durations():
    """Test Opus with various frame durations."""
    # Opus supports 2.5, 5, 10, 20, 40, 60 ms frames
    # Frame sizes at 48kHz
    frame_sizes = [
        120,  # 2.5ms
        240,  # 5ms
        480,  # 10ms
        960,  # 20ms (most common)
        1920,  # 40ms
        2880,  # 60ms
    ]

    for frame_size in frame_sizes:
        data = np.zeros((frame_size, 2), dtype=np.float32)
        init = {
            "format": AudioSampleFormat.F32,
            "sample_rate": 48000,
            "number_of_frames": frame_size,
            "number_of_channels": 2,
            "timestamp": 0,
            "data": data,
        }
        audio = AudioData(init)
        assert audio.number_of_frames == frame_size
        audio.close()


def test_opus_encoded_chunk():
    """Test Opus encoded chunk creation."""
    # Create fake Opus packet data
    opus_packet_data = b"OpusHead" + b"\x00" * 50  # Simplified Opus data

    chunk = EncodedAudioChunk(
        {
            "type": EncodedAudioChunkType.KEY,
            "timestamp": 0,
            "data": opus_packet_data,
        }
    )

    assert chunk.type == EncodedAudioChunkType.KEY
    assert chunk.timestamp == 0
    assert chunk.byte_length == len(opus_packet_data)


def test_opus_timestamp_sequence():
    """Test creating a sequence of Opus chunks with proper timestamps."""
    # 20ms frames at 48kHz = 960 samples per frame
    # Timestamp in microseconds
    timestamps = [0, 20000, 40000, 60000, 80000, 100000]

    chunks = []
    for ts in timestamps:
        opus_data = b"opus_frame" + bytes([ts // 1000])  # Simple fake data
        chunk = EncodedAudioChunk(
            {
                "type": EncodedAudioChunkType.KEY,  # All Opus chunks are "key"
                "timestamp": ts,
                "data": opus_data,
            }
        )
        chunks.append(chunk)
        assert chunk.timestamp == ts

    assert len(chunks) == len(timestamps)


def test_opus_encoder_decoder_states():
    """Test Opus encoder and decoder state transitions."""

    # Create encoder
    def on_encoder_output(chunk):
        pass

    def on_error(error):
        pass

    encoder = AudioEncoder(on_encoder_output, on_error)
    encoder_config: AudioEncoderConfig = {
        "codec": "opus",
        "sample_rate": 48000,
        "number_of_channels": 2,
        "bitrate": 128000,
    }
    encoder.configure(encoder_config)
    assert encoder.state == CodecState.CONFIGURED

    # encode() が実装されたため、簡単なエンコード動作を確認する
    outputs = []

    def on_output(chunk):
        outputs.append(chunk)

    encoder.on_output(on_output)

    # 20ms 相当のフレームを S16 で作成 (F32 同士の変換は未対応のため)
    data = np.zeros((960, 2), dtype=np.int16)
    init = {
        "format": AudioSampleFormat.S16,
        "sample_rate": 48000,
        "number_of_frames": 960,
        "number_of_channels": 2,
        "timestamp": 0,
        "data": data,
    }
    audio_data = AudioData(init)
    encoder.encode(audio_data)
    encoder.flush()
    audio_data.close()

    # 状態は configured のまま、出力が 1 件以上生成されること
    assert encoder.state == CodecState.CONFIGURED
    assert len(outputs) > 0

    encoder.close()
    assert encoder.state == CodecState.CLOSED

    # Create decoder
    def on_decode_output(audio):
        pass

    def on_decode_error(error):
        pass

    decoder = AudioDecoder(on_decode_output, on_decode_error)
    decoder_config: AudioDecoderConfig = {
        "codec": "opus",
        "sample_rate": 48000,
        "number_of_channels": 2,
    }
    decoder.configure(decoder_config)
    assert decoder.state == CodecState.CONFIGURED

    decoder.close()
    assert decoder.state == CodecState.CLOSED
