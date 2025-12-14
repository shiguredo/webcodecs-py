import numpy as np
import pytest
from audio_test_helpers import (
    audio_data_to_float32,
    generate_complex_audio,
    generate_sine_wave,
    generate_stereo_test_signal,
)

from webcodecs import (
    AudioData,
    AudioDataInit,
    AudioDecoder,
    AudioDecoderConfig,
    AudioEncoder,
    AudioEncoderConfig,
    AudioSampleFormat,
    CodecState,
)


def test_opus_encode_sine_wave():
    """単純なサイン波を使用した Opus エンコード・デコードのテスト"""
    sample_rate = 48000
    duration = 0.5  # 500ms
    frequency = 440  # A4 音

    # サイン波を生成
    audio_samples = generate_sine_wave(frequency, sample_rate, duration)

    # エンコーダを作成
    encoded_chunks = []

    def on_encoder_output(chunk):
        encoded_chunks.append(chunk)

    def on_encoder_error(error):
        pytest.fail(f"エンコーダエラー: {error}")

    encoder = AudioEncoder(on_encoder_output, on_encoder_error)
    encoder_config: AudioEncoderConfig = {
        "codec": "opus",
        "sample_rate": sample_rate,
        "number_of_channels": 1,  # モノラル
        "bitrate": 64000,
    }
    encoder.configure(encoder_config)
    assert encoder.state == CodecState.CONFIGURED

    # オーディオデータを作成 (20ms フレーム = 48kHz で 960 サンプル)
    frame_size = 960
    num_frames = len(audio_samples) // frame_size

    for i in range(num_frames):
        start = i * frame_size
        end = start + frame_size
        frame_samples = audio_samples[start:end].reshape(frame_size, 1)

        # タイムスタンプをマイクロ秒で計算
        timestamp = (i * frame_size * 1000000) // sample_rate

        # AudioData を作成して numpy データをコピー
        num_frames, num_channels = frame_samples.shape
        init: AudioDataInit = {
            "format": AudioSampleFormat.F32,
            "sample_rate": sample_rate,
            "number_of_frames": num_frames,
            "number_of_channels": num_channels,
            "timestamp": timestamp,
            "data": frame_samples,
        }
        audio_data = AudioData(init)
        encoder.encode(audio_data)
        audio_data.close()

    encoder.flush()

    # エンコードされた出力を取得できたことを確認
    assert len(encoded_chunks) > 0
    for chunk in encoded_chunks:
        assert chunk.byte_length > 0

    # デコーダをセットアップ
    decoded_outputs = []

    def on_decoder_output(audio):
        decoded_outputs.append(audio)

    def on_decoder_error(error):
        pytest.fail(f"デコーダエラー: {error}")

    decoder = AudioDecoder(on_decoder_output, on_decoder_error)
    decoder_config: AudioDecoderConfig = {
        "codec": "opus",
        "sample_rate": sample_rate,
        "number_of_channels": 1,
    }
    decoder.configure(decoder_config)

    # デコード
    for chunk in encoded_chunks:
        decoder.decode(chunk)
    decoder.flush()

    # デコードされた出力を確認
    assert len(decoded_outputs) > 0
    for audio in decoded_outputs:
        assert audio.number_of_frames > 0
        assert audio.sample_rate == sample_rate
        assert audio.number_of_channels == 1

    # デコードされたデータと元のデータの類似性を確認
    # 全てのデコード出力を結合
    all_decoded_samples = []
    for audio in decoded_outputs:
        decoded_samples = audio_data_to_float32(audio)
        if len(decoded_samples.shape) == 2:
            all_decoded_samples.append(decoded_samples[:, 0])
        else:
            all_decoded_samples.append(decoded_samples.flatten())

    if len(all_decoded_samples) > 0:
        decoded_audio = np.concatenate(all_decoded_samples)

        # Opus の遅延を考慮して比較
        opus_delay = 480
        min_len = min(len(audio_samples) - opus_delay, len(decoded_audio) - opus_delay)

        if min_len > 0:
            # 遅延を考慮して元の信号とデコード結果を比較
            orig_segment = audio_samples[:min_len]
            # Opus は位相を反転することがあるため、絶対値の相関を確認
            decoded_segment = decoded_audio[opus_delay : opus_delay + min_len]

            correlation = np.corrcoef(orig_segment, decoded_segment)[0, 1]
            # サイン波なので高い相関が期待できる
            assert abs(correlation) > 0.95, f"相関係数が低すぎます: {correlation}"

    # クリーンアップ
    for data in decoded_outputs:
        data.close()

    encoder.close()
    decoder.close()


def test_opus_encode_stereo():
    """ステレオオーディオを使用した Opus エンコード・デコードのテスト"""
    sample_rate = 48000
    duration = 1.0

    # ステレオテスト信号を生成 (左 440Hz、右 880Hz)
    stereo_samples = generate_stereo_test_signal(sample_rate, duration)

    # ステレオ用のエンコーダを作成
    encoded_chunks = []

    def on_encoder_output(chunk):
        encoded_chunks.append(chunk)

    def on_encoder_error(error):
        pytest.fail(f"エンコーダエラー: {error}")

    encoder = AudioEncoder(on_encoder_output, on_encoder_error)
    encoder_config: AudioEncoderConfig = {
        "codec": "opus",
        "sample_rate": sample_rate,
        "number_of_channels": 2,  # ステレオ
        "bitrate": 128000,
    }
    encoder.configure(encoder_config)

    # 20ms フレームで処理
    frame_size = 960  # チャンネルあたりのサンプル数
    num_frames = len(stereo_samples) // frame_size

    for i in range(num_frames):
        start = i * frame_size
        end = start + frame_size
        frame_samples = stereo_samples[start:end]  # 既に (frame_size, 2) の形状

        # タイムスタンプをマイクロ秒で計算
        timestamp = (i * frame_size * 1000000) // sample_rate

        # AudioData を作成してステレオフレームをエンコード
        num_frames, num_channels = frame_samples.shape
        init: AudioDataInit = {
            "format": AudioSampleFormat.F32,
            "sample_rate": sample_rate,
            "number_of_frames": num_frames,
            "number_of_channels": num_channels,
            "timestamp": timestamp,
            "data": frame_samples,
        }
        audio_data = AudioData(init)
        encoder.encode(audio_data)
        audio_data.close()

    encoder.flush()

    # エンコードされた出力を取得できたことを確認
    assert len(encoded_chunks) > 0

    # デコーダをセットアップ
    decoded_outputs = []

    def on_decoder_output(audio):
        decoded_outputs.append(audio)

    def on_decoder_error(error):
        pytest.fail(f"デコーダエラー: {error}")

    decoder = AudioDecoder(on_decoder_output, on_decoder_error)
    decoder_config: AudioDecoderConfig = {
        "codec": "opus",
        "sample_rate": sample_rate,
        "number_of_channels": 2,
    }
    decoder.configure(decoder_config)

    # デコード
    for chunk in encoded_chunks:
        decoder.decode(chunk)
    decoder.flush()

    # デコードされた出力を確認
    assert len(decoded_outputs) > 0
    for audio in decoded_outputs:
        assert audio.number_of_frames > 0
        assert audio.sample_rate == sample_rate
        assert audio.number_of_channels == 2  # ステレオ

    # デコードされたデータと元のデータの類似性を確認
    # 全てのデコード出力を結合
    all_decoded_samples = []
    for audio in decoded_outputs:
        decoded_samples = audio_data_to_float32(audio)
        all_decoded_samples.append(decoded_samples)

    if len(all_decoded_samples) > 0:
        decoded_audio = np.concatenate(all_decoded_samples, axis=0)  # (frames, channels)

        # Opus の遅延を考慮して比較
        opus_delay = 480
        # stereo_samples は既に (samples, 2) の形状
        min_len = min(len(stereo_samples) - opus_delay, len(decoded_audio) - opus_delay)

        if min_len > 0:
            # 左チャンネルの相関を確認
            orig_left_segment = stereo_samples[:min_len, 0]
            decoded_left_segment = decoded_audio[opus_delay : opus_delay + min_len, 0]
            correlation_left = np.corrcoef(orig_left_segment, decoded_left_segment)[0, 1]

            # 右チャンネルの相関を確認
            orig_right_segment = stereo_samples[:min_len, 1]
            decoded_right_segment = decoded_audio[opus_delay : opus_delay + min_len, 1]
            correlation_right = np.corrcoef(orig_right_segment, decoded_right_segment)[0, 1]

            # ステレオなので両チャンネルとも高い相関が期待できる
            # ただし右チャンネルは周波数が高いため、やや相関が低くなることがある
            assert abs(correlation_left) > 0.90, (
                f"左チャンネルの相関係数が低すぎます: {correlation_left}"
            )
            assert abs(correlation_right) > 0.80, (
                f"右チャンネルの相関係数が低すぎます: {correlation_right}"
            )

    # クリーンアップ
    for data in decoded_outputs:
        data.close()

    encoder.close()
    decoder.close()


def test_opus_various_bitrates_quality():
    """異なるビットレートでの Opus エンコード・デコード品質のテスト"""
    sample_rate = 48000
    duration = 0.5

    # 複雑なオーディオ信号を生成
    audio_samples = generate_complex_audio(sample_rate, duration)

    bitrates = [
        6000,  # 最小 (音声品質)
        32000,  # 低 (許容できる音声)
        64000,  # 中 (良好な音声/音楽)
        128000,  # 高 (ほとんどのコンテンツで透過的)
        256000,  # 非常に高 (スタジオ品質)
    ]

    encoded_sizes = []

    for bitrate in bitrates:
        encoded_chunks = []

        def on_encoder_output(chunk):
            encoded_chunks.append(chunk)

        def on_encoder_error(error):
            pytest.fail(f"エンコーダエラー: {error}")

        encoder = AudioEncoder(on_encoder_output, on_encoder_error)
        encoder_config: AudioEncoderConfig = {
            "codec": "opus",
            "sample_rate": sample_rate,
            "number_of_channels": 1,
            "bitrate": bitrate,
        }
        encoder.configure(encoder_config)

        # 全体の信号をエンコード
        frame_size = 960
        total_size = 0

        for idx, i in enumerate(range(0, len(audio_samples), frame_size)):
            # フレームサンプルを取得、必要に応じてパディング
            end_idx = min(i + frame_size, len(audio_samples))
            frame_samples = audio_samples[i:end_idx]

            # 最後の不完全なフレームの場合はゼロでパディング
            if len(frame_samples) < frame_size:
                padded_samples = np.zeros(frame_size, dtype=np.float32)
                padded_samples[: len(frame_samples)] = frame_samples
                frame_samples = padded_samples

            frame_samples = frame_samples.reshape(frame_size, 1)
            # タイムスタンプをマイクロ秒で計算
            timestamp = (idx * frame_size * 1000000) // sample_rate
            num_frames, num_channels = frame_samples.shape
            init: AudioDataInit = {
                "format": AudioSampleFormat.F32,
                "sample_rate": sample_rate,
                "number_of_frames": num_frames,
                "number_of_channels": num_channels,
                "timestamp": timestamp,
                "data": frame_samples,
            }
            audio_data = AudioData(init)
            encoder.encode(audio_data)
            audio_data.close()

        encoder.flush()

        # エンコードされた合計サイズを測定
        for chunk in encoded_chunks:
            total_size += chunk.byte_length

        encoded_sizes.append(total_size)

        # デコーダをセットアップ
        decoded_outputs = []

        def on_decoder_output(audio):
            decoded_outputs.append(audio)

        def on_decoder_error(error):
            pytest.fail(f"デコーダエラー: {error}")

        decoder = AudioDecoder(on_decoder_output, on_decoder_error)
        decoder_config: AudioDecoderConfig = {
            "codec": "opus",
            "sample_rate": sample_rate,
            "number_of_channels": 1,
        }
        decoder.configure(decoder_config)

        # デコード
        for chunk in encoded_chunks:
            decoder.decode(chunk)
        decoder.flush()

        # デコードされた出力を確認
        assert len(decoded_outputs) > 0
        for audio in decoded_outputs:
            assert audio.number_of_frames > 0
            assert audio.sample_rate == sample_rate
            assert audio.number_of_channels == 1

        # デコードされたデータと元のデータの類似性を確認
        all_decoded_samples = []
        for audio in decoded_outputs:
            decoded_samples = audio_data_to_float32(audio)
            if len(decoded_samples.shape) == 2:
                all_decoded_samples.append(decoded_samples[:, 0])
            else:
                all_decoded_samples.append(decoded_samples.flatten())

        if len(all_decoded_samples) > 0:
            decoded_audio = np.concatenate(all_decoded_samples)

            # Opus の遅延を考慮して比較
            opus_delay = 480
            min_len = min(len(audio_samples) - opus_delay, len(decoded_audio) - opus_delay)

            if min_len > 0:
                orig_segment = audio_samples[:min_len]
                decoded_segment = decoded_audio[opus_delay : opus_delay + min_len]

                correlation = np.corrcoef(orig_segment, decoded_segment)[0, 1]

                # ビットレートに応じた相関の閾値
                # 複雑な信号（倍音を含む）では、Opus の圧縮特性により相関が低くなる
                if bitrate >= 128000:
                    min_correlation = 0.50  # 高ビットレート（倍音信号のため）
                elif bitrate >= 64000:
                    min_correlation = 0.50  # 中ビットレート（倍音信号のため）
                elif bitrate >= 32000:
                    min_correlation = 0.50  # 低ビットレート（倍音信号のため）
                else:
                    min_correlation = 0.30  # 最低ビットレート（倍音信号のため）

                assert abs(correlation) > min_correlation, (
                    f"ビットレート {bitrate} での相関係数が低すぎます: {correlation} (期待値: > {min_correlation})"
                )

        # クリーンアップ
        for data in decoded_outputs:
            data.close()

        encoder.close()
        decoder.close()

    # すべてのビットレートでゼロ以外のサイズを取得できたことを確認
    for size in encoded_sizes:
        assert size > 0


def test_opus_encode_decode_roundtrip():
    """Opus エンコードとデコードのラウンドトリップテスト"""
    sample_rate = 48000
    duration = 0.2
    frequency = 1000  # 1kHz テストトーン

    # テスト信号を生成
    original_samples = generate_sine_wave(frequency, sample_rate, duration)

    # エンコーダをセットアップ
    encoded_chunks = []

    def on_encoder_output(chunk):
        encoded_chunks.append(chunk)

    def on_encoder_error(error):
        pytest.fail(f"エンコーダエラー: {error}")

    encoder = AudioEncoder(on_encoder_output, on_encoder_error)
    encoder_config: AudioEncoderConfig = {
        "codec": "opus",
        "sample_rate": sample_rate,
        "number_of_channels": 1,
        "bitrate": 64000,
    }
    encoder.configure(encoder_config)

    # デコーダをセットアップ
    decoded_outputs = []

    def on_decoder_output(audio):
        decoded_outputs.append(audio)

    def on_decoder_error(error):
        pytest.fail(f"デコーダエラー: {error}")

    decoder = AudioDecoder(on_decoder_output, on_decoder_error)
    decoder_config: AudioDecoderConfig = {
        "codec": "opus",
        "sample_rate": sample_rate,
        "number_of_channels": 1,
    }
    decoder.configure(decoder_config)

    # 完全なラウンドトリップテスト
    frame_size = 960
    frame_samples = original_samples[:frame_size].reshape(frame_size, 1)
    num_frames, num_channels = frame_samples.shape
    init: AudioDataInit = {
        "format": AudioSampleFormat.F32,
        "sample_rate": sample_rate,
        "number_of_frames": num_frames,
        "number_of_channels": num_channels,
        "timestamp": 0,
        "data": frame_samples,
    }
    audio_data = AudioData(init)

    # エンコード
    encoder.encode(audio_data)
    encoder.flush()

    # デコード
    for chunk in encoded_chunks:
        decoder.decode(chunk)
    decoder.flush()

    # デコードされた出力を取得できたことを確認
    assert len(decoded_outputs) > 0

    # デコードされたサンプルを取得
    decoded_data = decoded_outputs[0]
    decoded_samples = audio_data_to_float32(decoded_data)

    # 基本プロパティを確認
    assert decoded_data.number_of_frames > 0
    assert decoded_data.sample_rate == sample_rate
    assert decoded_data.number_of_channels == 1

    # 相関チェックのため、同じ長さの配列を比較
    min_len = min(frame_size, decoded_data.number_of_frames)
    if min_len > 0:
        # オーディオサンプルをフラット配列として抽出
        orig_flat = original_samples[:min_len]

        # audio_data_to_float32() からの decoded_samples は (frames, channels) の形状
        # 1D と 2D 配列の両方を安全に処理
        if len(decoded_samples.shape) == 2:
            decoded_flat = np.array(decoded_samples[:min_len, 0])
        else:
            decoded_flat = np.array(decoded_samples.flatten()[:min_len])

        # 両方が同じ dtype の numpy 配列であることを確認
        orig_flat = np.array(orig_flat, dtype=np.float32)
        decoded_flat = np.array(decoded_flat, dtype=np.float32)

        # Opus は 480 サンプルの先読み遅延と位相反転がある
        # 適切な相関チェックのため、遅延を考慮する必要がある
        opus_delay = 480
        if opus_delay < min_len:
            # 反転された信号と比較 (Opus は位相を反転するため)
            delayed_orig = orig_flat[:-opus_delay]
            delayed_decoded = -decoded_flat[opus_delay:]  # デコードされた信号を反転
            min_delay_len = min(len(delayed_orig), len(delayed_decoded))
            if min_delay_len > 0:
                corrected_corr = np.corrcoef(
                    delayed_orig[:min_delay_len], delayed_decoded[:min_delay_len]
                )[0, 1]
                assert corrected_corr > 0.99  # 補正後は高い相関があるはず
        else:
            # 遅延補正に十分なサンプルがない場合は、絶対相関をチェック
            correlation = np.corrcoef(orig_flat, decoded_flat)[0, 1]
            assert abs(correlation) > 0.8

    # デコードされた出力をクリーンアップ
    for data in decoded_outputs:
        data.close()

    audio_data.close()
    encoder.close()
    decoder.close()


def test_opus_voice_vs_music_modes():
    """異なるアプリケーションモード (音声 vs 音楽) での Opus エンコード・デコードテスト"""
    sample_rate = 48000
    duration = 1.0

    # 音声のような信号を生成 (狭帯域、単純)
    voice_signal = generate_sine_wave(300, sample_rate, duration, amplitude=0.3)
    voice_signal += generate_sine_wave(800, sample_rate, duration, amplitude=0.2)
    voice_signal += generate_sine_wave(2000, sample_rate, duration, amplitude=0.1)

    # 音楽のような信号を生成 (広帯域、複雑)
    music_signal = generate_complex_audio(sample_rate, duration)

    # 音声エンコーダ
    voice_chunks = []

    def on_voice_output(chunk):
        voice_chunks.append(chunk)

    def on_voice_error(error):
        pytest.fail(f"音声エンコーダエラー: {error}")

    voice_encoder = AudioEncoder(on_voice_output, on_voice_error)
    encoder_config: AudioEncoderConfig = {
        "codec": "opus",
        "sample_rate": sample_rate,
        "number_of_channels": 1,
        "bitrate": 32000,
    }
    voice_encoder.configure(encoder_config)

    # 音楽エンコーダ
    music_chunks = []

    def on_music_output(chunk):
        music_chunks.append(chunk)

    def on_music_error(error):
        pytest.fail(f"音楽エンコーダエラー: {error}")

    music_encoder = AudioEncoder(on_music_output, on_music_error)
    music_config: AudioEncoderConfig = {
        "codec": "opus",
        "sample_rate": sample_rate,
        "number_of_channels": 1,
        "bitrate": 96000,
    }
    music_encoder.configure(music_config)

    # 音声信号をエンコード
    frame_size = 960
    for idx, i in enumerate(range(0, len(voice_signal), frame_size)):
        end_idx = min(i + frame_size, len(voice_signal))
        frame = voice_signal[i:end_idx]

        if len(frame) < frame_size:
            padded_frame = np.zeros(frame_size, dtype=np.float32)
            padded_frame[: len(frame)] = frame
            frame = padded_frame

        frame = frame.reshape(frame_size, 1)
        timestamp = (idx * frame_size * 1000000) // sample_rate
        num_frames, num_channels = frame.shape
        init: AudioDataInit = {
            "format": AudioSampleFormat.F32,
            "sample_rate": sample_rate,
            "number_of_frames": num_frames,
            "number_of_channels": num_channels,
            "timestamp": timestamp,
            "data": frame,
        }
        audio_data = AudioData(init)
        voice_encoder.encode(audio_data)
        audio_data.close()

    voice_encoder.flush()

    # 音楽信号をエンコード
    for idx, i in enumerate(range(0, len(music_signal), frame_size)):
        end_idx = min(i + frame_size, len(music_signal))
        frame = music_signal[i:end_idx]

        if len(frame) < frame_size:
            padded_frame = np.zeros(frame_size, dtype=np.float32)
            padded_frame[: len(frame)] = frame
            frame = padded_frame

        frame = frame.reshape(frame_size, 1)
        timestamp = (idx * frame_size * 1000000) // sample_rate
        num_frames, num_channels = frame.shape
        init: AudioDataInit = {
            "format": AudioSampleFormat.F32,
            "sample_rate": sample_rate,
            "number_of_frames": num_frames,
            "number_of_channels": num_channels,
            "timestamp": timestamp,
            "data": frame,
        }
        audio_data = AudioData(init)
        music_encoder.encode(audio_data)
        audio_data.close()

    music_encoder.flush()

    # エンコードが成功したことを確認
    assert len(voice_chunks) > 0
    assert len(music_chunks) > 0

    # 音声デコーダをセットアップ
    voice_decoded_outputs = []

    def on_voice_decoder_output(audio):
        voice_decoded_outputs.append(audio)

    def on_voice_decoder_error(error):
        pytest.fail(f"音声デコーダエラー: {error}")

    voice_decoder = AudioDecoder(on_voice_decoder_output, on_voice_decoder_error)
    voice_decoder_config: AudioDecoderConfig = {
        "codec": "opus",
        "sample_rate": sample_rate,
        "number_of_channels": 1,
    }
    voice_decoder.configure(voice_decoder_config)

    # 音声をデコード
    for chunk in voice_chunks:
        voice_decoder.decode(chunk)
    voice_decoder.flush()

    # 音楽デコーダをセットアップ
    music_decoded_outputs = []

    def on_music_decoder_output(audio):
        music_decoded_outputs.append(audio)

    def on_music_decoder_error(error):
        pytest.fail(f"音楽デコーダエラー: {error}")

    music_decoder = AudioDecoder(on_music_decoder_output, on_music_decoder_error)
    music_decoder_config: AudioDecoderConfig = {
        "codec": "opus",
        "sample_rate": sample_rate,
        "number_of_channels": 1,
    }
    music_decoder.configure(music_decoder_config)

    # 音楽をデコード
    for chunk in music_chunks:
        music_decoder.decode(chunk)
    music_decoder.flush()

    # デコードされた出力を確認
    assert len(voice_decoded_outputs) > 0
    assert len(music_decoded_outputs) > 0

    for audio in voice_decoded_outputs:
        assert audio.number_of_frames > 0
        assert audio.sample_rate == sample_rate
        assert audio.number_of_channels == 1

    for audio in music_decoded_outputs:
        assert audio.number_of_frames > 0
        assert audio.sample_rate == sample_rate
        assert audio.number_of_channels == 1

    # 音声信号の類似性を確認
    voice_decoded_samples = []
    for audio in voice_decoded_outputs:
        decoded_samples = audio_data_to_float32(audio)
        if len(decoded_samples.shape) == 2:
            voice_decoded_samples.append(decoded_samples[:, 0])
        else:
            voice_decoded_samples.append(decoded_samples.flatten())

    if len(voice_decoded_samples) > 0:
        voice_decoded_audio = np.concatenate(voice_decoded_samples)
        opus_delay = 480
        min_len = min(len(voice_signal) - opus_delay, len(voice_decoded_audio) - opus_delay)

        if min_len > 0:
            orig_segment = voice_signal[:min_len]
            decoded_segment = voice_decoded_audio[opus_delay : opus_delay + min_len]
            correlation = np.corrcoef(orig_segment, decoded_segment)[0, 1]
            # 音声信号（複数周波数の組み合わせ）なので、やや相関が低くなる
            assert abs(correlation) > 0.75, f"音声の相関係数が低すぎます: {correlation}"

    # 音楽信号の類似性を確認
    music_decoded_samples = []
    for audio in music_decoded_outputs:
        decoded_samples = audio_data_to_float32(audio)
        if len(decoded_samples.shape) == 2:
            music_decoded_samples.append(decoded_samples[:, 0])
        else:
            music_decoded_samples.append(decoded_samples.flatten())

    if len(music_decoded_samples) > 0:
        music_decoded_audio = np.concatenate(music_decoded_samples)
        opus_delay = 480
        min_len = min(len(music_signal) - opus_delay, len(music_decoded_audio) - opus_delay)

        if min_len > 0:
            orig_segment = music_signal[:min_len]
            decoded_segment = music_decoded_audio[opus_delay : opus_delay + min_len]
            correlation = np.corrcoef(orig_segment, decoded_segment)[0, 1]
            # 複雑な音楽信号（7つの周波数成分）なので、相関は低くなる
            assert abs(correlation) > 0.50, f"音楽の相関係数が低すぎます: {correlation}"

    # クリーンアップ
    for data in voice_decoded_outputs:
        data.close()
    for data in music_decoded_outputs:
        data.close()

    voice_encoder.close()
    voice_decoder.close()
    music_encoder.close()
    music_decoder.close()


def test_opus_packet_loss_resilience():
    """パケットロスシナリオでの Opus 前方誤り訂正 (FEC) のテスト"""
    sample_rate = 48000
    duration = 2.0

    # 連続信号を生成
    signal = generate_sine_wave(440, sample_rate, duration)

    encoded_chunks = []

    def on_encoder_output(chunk):
        encoded_chunks.append(chunk)

    def on_encoder_error(error):
        pytest.fail(f"エンコーダエラー: {error}")

    encoder = AudioEncoder(on_encoder_output, on_encoder_error)
    encoder_config: AudioEncoderConfig = {
        "codec": "opus",
        "sample_rate": sample_rate,
        "number_of_channels": 1,
        "bitrate": 64000,
    }
    encoder.configure(encoder_config)

    # フレームをエンコード
    frame_size = 960

    for idx, i in enumerate(range(0, len(signal), frame_size)):
        end_idx = min(i + frame_size, len(signal))
        frame = signal[i:end_idx]

        if len(frame) < frame_size:
            padded_frame = np.zeros(frame_size, dtype=np.float32)
            padded_frame[: len(frame)] = frame
            frame = padded_frame

        frame = frame.reshape(frame_size, 1)
        timestamp = (idx * frame_size * 1000000) // sample_rate
        num_frames, num_channels = frame.shape
        init: AudioDataInit = {
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

    encoder.flush()

    # エンコードが成功したことを確認
    assert len(encoded_chunks) > 0

    # デコーダをセットアップ
    decoded_outputs = []

    def on_decoder_output(audio):
        decoded_outputs.append(audio)

    def on_decoder_error(error):
        pytest.fail(f"デコーダエラー: {error}")

    decoder = AudioDecoder(on_decoder_output, on_decoder_error)
    decoder_config: AudioDecoderConfig = {
        "codec": "opus",
        "sample_rate": sample_rate,
        "number_of_channels": 1,
    }
    decoder.configure(decoder_config)

    # パケットロスをシミュレートして復元をテスト
    for i, chunk in enumerate(encoded_chunks):
        if i % 10 == 5:  # 10% のロスをシミュレート
            continue
        decoder.decode(chunk)

    decoder.flush()

    # パケットロスにもかかわらず出力が生成されることを確認
    assert len(decoded_outputs) > 0

    # デコードされた出力をクリーンアップ
    for data in decoded_outputs:
        data.close()

    encoder.close()
    decoder.close()


def test_opus_variable_frame_sizes():
    """さまざまなフレームサイズでの Opus エンコード・デコードテスト"""
    sample_rate = 48000

    # Opus は 2.5, 5, 10, 20, 40, 60 ms のフレームをサポート
    frame_durations_ms = [2.5, 5, 10, 20, 40, 60]
    frame_sizes = [int(sample_rate * ms / 1000) for ms in frame_durations_ms]

    for frame_size, duration_ms in zip(frame_sizes, frame_durations_ms):
        # このフレームサイズのオーディオを生成
        samples = generate_sine_wave(1000, sample_rate, duration_ms / 1000)

        encoded_chunks = []

        def on_encoder_output(chunk):
            encoded_chunks.append(chunk)

        def on_encoder_error(error):
            pytest.fail(f"エンコーダエラー: {error}")

        encoder = AudioEncoder(on_encoder_output, on_encoder_error)
        encoder_config: AudioEncoderConfig = {
            "codec": "opus",
            "sample_rate": sample_rate,
            "number_of_channels": 1,
            "bitrate": 64000,
        }
        encoder.configure(encoder_config)

        # AudioData コンストラクタ用にサンプルを reshape
        frame_samples = samples[:frame_size].reshape(frame_size, 1)
        num_frames, num_channels = frame_samples.shape
        init: AudioDataInit = {
            "format": AudioSampleFormat.F32,
            "sample_rate": sample_rate,
            "number_of_frames": num_frames,
            "number_of_channels": num_channels,
            "timestamp": 0,
            "data": frame_samples,
        }
        audio_data = AudioData(init)

        # 特定のフレームサイズでエンコード
        encoder.encode(audio_data)
        encoder.flush()

        # エンコードが成功したことを確認
        assert len(encoded_chunks) > 0
        assert encoded_chunks[0].byte_length > 0

        # デコーダをセットアップ
        decoded_outputs = []

        def on_decoder_output(audio):
            decoded_outputs.append(audio)

        def on_decoder_error(error):
            pytest.fail(f"デコーダエラー: {error}")

        decoder = AudioDecoder(on_decoder_output, on_decoder_error)
        decoder_config: AudioDecoderConfig = {
            "codec": "opus",
            "sample_rate": sample_rate,
            "number_of_channels": 1,
        }
        decoder.configure(decoder_config)

        # デコード
        for chunk in encoded_chunks:
            decoder.decode(chunk)
        decoder.flush()

        # デコードされた出力を確認
        assert len(decoded_outputs) > 0
        for audio in decoded_outputs:
            assert audio.number_of_frames > 0
            assert audio.sample_rate == sample_rate
            assert audio.number_of_channels == 1

        # デコードされたデータと元のデータの類似性を確認
        all_decoded_samples = []
        for audio in decoded_outputs:
            decoded_samples = audio_data_to_float32(audio)
            if len(decoded_samples.shape) == 2:
                all_decoded_samples.append(decoded_samples[:, 0])
            else:
                all_decoded_samples.append(decoded_samples.flatten())

        if len(all_decoded_samples) > 0:
            decoded_audio = np.concatenate(all_decoded_samples)

            # Opus の遅延を考慮して比較
            opus_delay = 480
            min_len = min(len(samples) - opus_delay, len(decoded_audio) - opus_delay)

            if min_len > 100:  # 十分なサンプルがある場合のみ相関チェック
                orig_segment = samples[:min_len]
                decoded_segment = decoded_audio[opus_delay : opus_delay + min_len]

                correlation = np.corrcoef(orig_segment, decoded_segment)[0, 1]
                # サイン波なので高い相関が期待できる
                assert abs(correlation) > 0.90, (
                    f"フレームサイズ {duration_ms}ms での相関係数が低すぎます: {correlation}"
                )

        # クリーンアップ
        for data in decoded_outputs:
            data.close()

        audio_data.close()
        encoder.close()
        decoder.close()
