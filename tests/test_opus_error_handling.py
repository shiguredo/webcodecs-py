import pytest
from webcodecs import AudioEncoder, AudioEncoderConfig


def test_opus_unsupported_sample_rate():
    """サポートされていないサンプルレートで Opus エンコーダーを初期化するとエラーになることをテスト"""

    # 44100Hz はサポートされていない
    def on_output(chunk):
        pass

    def on_error(error):
        pass

    encoder = AudioEncoder(on_output, on_error)
    encoder_config: AudioEncoderConfig = {
        "codec": "opus",
        "sample_rate": 44100,  # サポート外のレート
        "number_of_channels": 1,
        "bitrate": 64000,
    }

    # エラーが発生することを確認
    with pytest.raises(RuntimeError) as exc_info:
        encoder.configure(encoder_config)

    # エラーメッセージを確認
    assert "NotSupportedError" in str(exc_info.value)
    assert "44100" in str(exc_info.value)
    assert "8000, 12000, 16000, 24000, or 48000" in str(exc_info.value)
