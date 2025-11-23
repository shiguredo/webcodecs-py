import numpy as np

from webcodecs import (
    AudioData,
    AudioDecoder,
    AudioDecoderConfig,
    AudioEncoder,
    AudioEncoderConfig,
    AudioSampleFormat,
    CodecState,
    FlacEncoderConfig,
)


def test_flac_encoder_decoder_basic():
    """FLAC エンコーダーとデコーダーの基本的なテスト"""

    def on_encode_output(chunk):
        pass

    def on_encode_error(error):
        print(f"Encoder error: {error}")

    encoder = AudioEncoder(on_encode_output, on_encode_error)

    encoder_config: AudioEncoderConfig = {
        "codec": "flac",
        "sample_rate": 48000,
        "number_of_channels": 2,
    }
    encoder.configure(encoder_config)
    assert encoder.state == CodecState.CONFIGURED

    def on_decode_output(audio):
        pass

    def on_decode_error(error):
        print(f"Decoder error: {error}")

    decoder = AudioDecoder(on_decode_output, on_decode_error)

    decoder_config: AudioDecoderConfig = {
        "codec": "flac",
        "sample_rate": 48000,
        "number_of_channels": 2,
    }
    decoder.configure(decoder_config)
    assert decoder.state == CodecState.CONFIGURED

    encoder.close()
    decoder.close()


def test_flac_various_sample_rates():
    """FLAC エンコーダーのサンプルレートテスト"""
    # FLAC は広範囲のサンプルレートをサポート
    sample_rates = [8000, 16000, 22050, 44100, 48000, 96000]

    for sample_rate in sample_rates:

        def on_output(chunk):
            pass

        def on_error(error):
            pass

        encoder = AudioEncoder(on_output, on_error)
        encoder_config: AudioEncoderConfig = {
            "codec": "flac",
            "sample_rate": sample_rate,
            "number_of_channels": 2,
        }
        encoder.configure(encoder_config)
        assert encoder.state == CodecState.CONFIGURED
        encoder.close()


def test_flac_various_channel_configs():
    """FLAC エンコーダーのチャンネル数テスト"""
    # FLAC は 1-8 チャンネルをサポート
    channel_configs = [1, 2]

    for channels in channel_configs:

        def on_output(chunk):
            pass

        def on_error(error):
            pass

        encoder = AudioEncoder(on_output, on_error)
        encoder_config: AudioEncoderConfig = {
            "codec": "flac",
            "sample_rate": 48000,
            "number_of_channels": channels,
        }
        encoder.configure(encoder_config)
        assert encoder.state == CodecState.CONFIGURED
        encoder.close()


def test_flac_encoder_encode():
    """FLAC エンコードのテスト"""
    outputs = []

    def on_output(chunk):
        outputs.append(chunk)

    def on_error(error):
        print(f"Error: {error}")

    encoder = AudioEncoder(on_output, on_error)
    encoder_config: AudioEncoderConfig = {
        "codec": "flac",
        "sample_rate": 48000,
        "number_of_channels": 2,
    }
    encoder.configure(encoder_config)

    # S16 形式のオーディオデータを作成 (FLAC は整数サンプルを使用)
    frame_size = 4096  # FLAC の一般的なブロックサイズ
    data = np.zeros((frame_size, 2), dtype=np.int16)
    # 正弦波データを生成
    for i in range(frame_size):
        data[i, 0] = int(32767 * np.sin(2 * np.pi * 440 * i / 48000))
        data[i, 1] = int(32767 * np.sin(2 * np.pi * 880 * i / 48000))

    init = {
        "format": AudioSampleFormat.S16,
        "sample_rate": 48000,
        "number_of_frames": frame_size,
        "number_of_channels": 2,
        "timestamp": 0,
        "data": data,
    }
    audio_data = AudioData(init)
    encoder.encode(audio_data)
    encoder.flush()
    audio_data.close()

    assert encoder.state == CodecState.CONFIGURED
    # FLAC エンコーダーは出力を生成するはず
    assert len(outputs) > 0

    encoder.close()


def test_flac_is_config_supported():
    """FLAC の is_config_supported テスト"""
    # サポートされる設定
    supported_config: AudioEncoderConfig = {
        "codec": "flac",
        "sample_rate": 48000,
        "number_of_channels": 2,
    }
    support = AudioEncoder.is_config_supported(supported_config)
    assert support["supported"] is True

    # サポートされないサンプルレート (範囲外)
    unsupported_config: AudioEncoderConfig = {
        "codec": "flac",
        "sample_rate": 500000,  # 範囲外
        "number_of_channels": 2,
    }
    support = AudioEncoder.is_config_supported(unsupported_config)
    assert support["supported"] is False

    # デコーダーでも確認
    decoder_config: AudioDecoderConfig = {
        "codec": "flac",
        "sample_rate": 44100,
        "number_of_channels": 2,
    }
    support = AudioDecoder.is_config_supported(decoder_config)
    assert support["supported"] is True


def test_flac_encoder_states():
    """FLAC エンコーダーの状態遷移テスト"""

    def on_output(chunk):
        pass

    def on_error(error):
        pass

    encoder = AudioEncoder(on_output, on_error)
    assert encoder.state == CodecState.UNCONFIGURED

    encoder_config: AudioEncoderConfig = {
        "codec": "flac",
        "sample_rate": 48000,
        "number_of_channels": 2,
    }
    encoder.configure(encoder_config)
    assert encoder.state == CodecState.CONFIGURED

    encoder.close()


def test_flac_44100_sample_rate():
    """FLAC の 44.1kHz サンプルレートテスト (CD 品質)"""
    outputs = []

    def on_output(chunk):
        outputs.append(chunk)

    def on_error(error):
        print(f"Error: {error}")

    encoder = AudioEncoder(on_output, on_error)
    encoder_config: AudioEncoderConfig = {
        "codec": "flac",
        "sample_rate": 44100,
        "number_of_channels": 2,
    }
    encoder.configure(encoder_config)

    # CD 品質の 1 フレーム (1/75 秒 = 588 サンプル)
    frame_size = 588
    data = np.zeros((frame_size, 2), dtype=np.int16)

    init = {
        "format": AudioSampleFormat.S16,
        "sample_rate": 44100,
        "number_of_frames": frame_size,
        "number_of_channels": 2,
        "timestamp": 0,
        "data": data,
    }
    audio_data = AudioData(init)
    encoder.encode(audio_data)
    encoder.flush()
    audio_data.close()

    assert len(outputs) > 0
    encoder.close()


def test_flac_mono():
    """FLAC のモノラルテスト"""
    outputs = []

    def on_output(chunk):
        outputs.append(chunk)

    def on_error(error):
        print(f"Error: {error}")

    encoder = AudioEncoder(on_output, on_error)
    encoder_config: AudioEncoderConfig = {
        "codec": "flac",
        "sample_rate": 48000,
        "number_of_channels": 1,
    }
    encoder.configure(encoder_config)

    frame_size = 4096
    data = np.zeros((frame_size, 1), dtype=np.int16)

    init = {
        "format": AudioSampleFormat.S16,
        "sample_rate": 48000,
        "number_of_frames": frame_size,
        "number_of_channels": 1,
        "timestamp": 0,
        "data": data,
    }
    audio_data = AudioData(init)
    encoder.encode(audio_data)
    encoder.flush()
    audio_data.close()

    assert len(outputs) > 0
    encoder.close()


def test_flac_encoder_config_options():
    """FlacEncoderConfig のオプションテスト"""
    outputs = []

    def on_output(chunk):
        outputs.append(chunk)

    def on_error(error):
        print(f"Error: {error}")

    encoder = AudioEncoder(on_output, on_error)

    # FlacEncoderConfig を使用
    flac_config: FlacEncoderConfig = {
        "block_size": 4096,
        "compress_level": 8,  # 最高圧縮
    }
    encoder_config: AudioEncoderConfig = {
        "codec": "flac",
        "sample_rate": 48000,
        "number_of_channels": 2,
        "flac": flac_config,
    }
    encoder.configure(encoder_config)
    assert encoder.state == CodecState.CONFIGURED

    frame_size = 4096
    data = np.zeros((frame_size, 2), dtype=np.int16)

    init = {
        "format": AudioSampleFormat.S16,
        "sample_rate": 48000,
        "number_of_frames": frame_size,
        "number_of_channels": 2,
        "timestamp": 0,
        "data": data,
    }
    audio_data = AudioData(init)
    encoder.encode(audio_data)
    encoder.flush()
    audio_data.close()

    assert len(outputs) > 0
    encoder.close()


def test_flac_encoder_compress_levels():
    """FLAC の圧縮レベルテスト"""
    # FLAC は 0-8 の圧縮レベルをサポート
    compress_levels = [0, 2, 5, 8]

    for level in compress_levels:

        def on_output(chunk):
            pass

        def on_error(error):
            pass

        encoder = AudioEncoder(on_output, on_error)
        flac_config: FlacEncoderConfig = {
            "compress_level": level,
        }
        encoder_config: AudioEncoderConfig = {
            "codec": "flac",
            "sample_rate": 48000,
            "number_of_channels": 2,
            "flac": flac_config,
        }
        encoder.configure(encoder_config)
        assert encoder.state == CodecState.CONFIGURED
        encoder.close()


def test_flac_encode_decode_lossless():
    """FLAC のエンコード→デコードでバイナリが一致することを確認

    FLAC はロスレスコーデックなので、エンコード→デコード後のデータは
    元のデータと完全に一致する必要がある。

    注意:
    - FLAC エンコーダーは S16 データを受け取る
    - FLAC デコーダーは F32 データを出力する
    - 比較時は S16 に戻して比較する

    このテストでは全チャンクを結合して 1 つのチャンクとしてデコーダーに渡す。
    """
    encoded_chunks = []
    decoded_audios = []

    def on_encode_output(chunk):
        # チャンクをコピーして保持
        destination = np.zeros(chunk.byte_length, dtype=np.uint8)
        chunk.copy_to(destination)
        encoded_chunks.append((bytes(destination), chunk.type, chunk.timestamp, chunk.duration))

    def on_encode_error(error):
        raise RuntimeError(f"Encoder error: {error}")

    def on_decode_output(audio):
        # デコードされたオーディオデータをコピーして保持
        # FLAC デコーダーは F32 形式で出力する
        options = {"plane_index": 0}
        destination = np.zeros(audio.allocation_size(options), dtype=np.uint8)
        audio.copy_to(destination, options)
        # F32 形式として解釈
        decoded_data = np.frombuffer(destination, dtype=np.float32).reshape(
            audio.number_of_frames, audio.number_of_channels
        )
        decoded_audios.append(decoded_data.copy())

    def on_decode_error(error):
        raise RuntimeError(f"Decoder error: {error}")

    # エンコーダー設定
    encoder = AudioEncoder(on_encode_output, on_encode_error)
    encoder_config: AudioEncoderConfig = {
        "codec": "flac",
        "sample_rate": 48000,
        "number_of_channels": 2,
    }
    encoder.configure(encoder_config)

    # テスト用オーディオデータを作成 (S16 形式)
    frame_size = 4096
    original_data = np.zeros((frame_size, 2), dtype=np.int16)
    # 正弦波データを生成
    for i in range(frame_size):
        original_data[i, 0] = int(32767 * np.sin(2 * np.pi * 440 * i / 48000))
        original_data[i, 1] = int(32767 * np.sin(2 * np.pi * 880 * i / 48000))

    # エンコード
    init = {
        "format": AudioSampleFormat.S16,
        "sample_rate": 48000,
        "number_of_frames": frame_size,
        "number_of_channels": 2,
        "timestamp": 0,
        "data": original_data,
    }
    audio_data = AudioData(init)
    encoder.encode(audio_data)
    encoder.flush()
    audio_data.close()
    encoder.close()

    assert len(encoded_chunks) > 0, "エンコード出力がない"

    # 全チャンクを結合して 1 つの FLAC ストリームとして扱う
    # FLAC はストリーム全体で 1 つのファイルとして構成される
    combined_data = b"".join([data for data, _, _, _ in encoded_chunks])

    # デコーダー設定
    decoder = AudioDecoder(on_decode_output, on_decode_error)
    decoder_config: AudioDecoderConfig = {
        "codec": "flac",
        "sample_rate": 48000,
        "number_of_channels": 2,
    }
    decoder.configure(decoder_config)

    # デコード (結合したデータを 1 つのチャンクとして渡す)
    from webcodecs import EncodedAudioChunk, EncodedAudioChunkType

    chunk = EncodedAudioChunk(combined_data, EncodedAudioChunkType.KEY, 0, 0)
    decoder.decode(chunk)

    decoder.flush()
    decoder.close()

    assert len(decoded_audios) > 0, "デコード出力がない"

    # デコードされたデータを結合
    decoded_combined = np.concatenate(decoded_audios, axis=0)

    # F32 から S16 に変換して比較
    # FLAC デコーダーは [-1.0, 1.0] の範囲で出力するので、32767 を掛けて S16 に変換
    decoded_s16 = (decoded_combined * 32768.0).astype(np.int16)

    # FLAC はロスレスなので、元のデータと一致するはず
    min_length = min(len(original_data), len(decoded_s16))
    np.testing.assert_array_equal(
        decoded_s16[:min_length],
        original_data[:min_length],
        err_msg="FLAC エンコード→デコードでデータが一致しない",
    )


def test_flac_streaming_decode():
    """FLAC のストリーミングデコードテスト

    エンコーダーから出力されたチャンクを個別にデコーダーに渡して
    ストリーミングデコードが正しく動作することを確認する。
    """
    from webcodecs import EncodedAudioChunk, EncodedAudioChunkType

    encoded_chunks = []
    decoded_audios = []

    def on_encode_output(chunk):
        # チャンクをコピーして保持
        destination = np.zeros(chunk.byte_length, dtype=np.uint8)
        chunk.copy_to(destination)
        encoded_chunks.append((bytes(destination), chunk.type, chunk.timestamp, chunk.duration))

    def on_encode_error(error):
        raise RuntimeError(f"Encoder error: {error}")

    def on_decode_output(audio):
        # デコードされたオーディオデータをコピーして保持
        # FLAC デコーダーは F32 形式で出力する
        options = {"plane_index": 0}
        destination = np.zeros(audio.allocation_size(options), dtype=np.uint8)
        audio.copy_to(destination, options)
        # F32 形式として解釈
        decoded_data = np.frombuffer(destination, dtype=np.float32).reshape(
            audio.number_of_frames, audio.number_of_channels
        )
        decoded_audios.append(decoded_data.copy())

    def on_decode_error(error):
        raise RuntimeError(f"Decoder error: {error}")

    # エンコーダー設定
    encoder = AudioEncoder(on_encode_output, on_encode_error)
    encoder_config: AudioEncoderConfig = {
        "codec": "flac",
        "sample_rate": 48000,
        "number_of_channels": 2,
    }
    encoder.configure(encoder_config)

    # テスト用オーディオデータを作成 (S16 形式)
    frame_size = 4096
    original_data = np.zeros((frame_size, 2), dtype=np.int16)
    # 正弦波データを生成
    for i in range(frame_size):
        original_data[i, 0] = int(32767 * np.sin(2 * np.pi * 440 * i / 48000))
        original_data[i, 1] = int(32767 * np.sin(2 * np.pi * 880 * i / 48000))

    # エンコード
    init = {
        "format": AudioSampleFormat.S16,
        "sample_rate": 48000,
        "number_of_frames": frame_size,
        "number_of_channels": 2,
        "timestamp": 0,
        "data": original_data,
    }
    audio_data = AudioData(init)
    encoder.encode(audio_data)
    encoder.flush()
    audio_data.close()
    encoder.close()

    assert len(encoded_chunks) > 0, "エンコード出力がない"

    # デコーダー設定
    decoder = AudioDecoder(on_decode_output, on_decode_error)
    decoder_config: AudioDecoderConfig = {
        "codec": "flac",
        "sample_rate": 48000,
        "number_of_channels": 2,
    }
    decoder.configure(decoder_config)

    # ストリーミングデコード: 各チャンクを個別に渡す
    for data, chunk_type, timestamp, duration in encoded_chunks:
        chunk_type_enum = (
            EncodedAudioChunkType.KEY
            if chunk_type == EncodedAudioChunkType.KEY
            else EncodedAudioChunkType.DELTA
        )
        chunk = EncodedAudioChunk(data, chunk_type_enum, timestamp, duration)
        decoder.decode(chunk)

    decoder.flush()
    decoder.close()

    assert len(decoded_audios) > 0, "デコード出力がない"

    # デコードされたデータを結合
    decoded_combined = np.concatenate(decoded_audios, axis=0)

    # F32 から S16 に変換して比較
    decoded_s16 = (decoded_combined * 32768.0).astype(np.int16)

    # FLAC はロスレスなので、元のデータと一致するはず
    min_length = min(len(original_data), len(decoded_s16))
    np.testing.assert_array_equal(
        decoded_s16[:min_length],
        original_data[:min_length],
        err_msg="FLAC ストリーミングデコードでデータが一致しない",
    )
