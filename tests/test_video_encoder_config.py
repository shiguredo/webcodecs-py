"""VideoEncoderConfig プロパティのテスト

VideoEncoderConfig の各プロパティが正しく設定できることを確認
"""

from webcodecs import (
    AlphaOption,
    VideoEncoderBitrateMode,
    CodecState,
    HardwareAcceleration,
    LatencyMode,
    VideoEncoder,
    VideoEncoderConfig,
)


def test_video_encoder_config_display_dimensions():
    """display_width と display_height の設定テスト"""

    def on_output(chunk):
        pass

    def on_error(error):
        pass

    encoder = VideoEncoder(on_output, on_error)

    # display_width と display_height を指定
    config: VideoEncoderConfig = {
        "codec": "av01.0.04M.08",
        "width": 640,
        "height": 480,
        "display_width": 320,
        "display_height": 240,
        "bitrate": 1_000_000,
    }

    encoder.configure(config)
    assert encoder.state == CodecState.CONFIGURED
    encoder.close()


def test_video_encoder_config_alpha():
    """alpha プロパティの設定テスト"""

    def on_output(chunk):
        pass

    def on_error(error):
        pass

    encoder = VideoEncoder(on_output, on_error)

    # alpha を "keep" に設定
    config: VideoEncoderConfig = {
        "codec": "av01.0.04M.08",
        "width": 640,
        "height": 480,
        "alpha": AlphaOption.KEEP,
    }

    encoder.configure(config)
    assert encoder.state == CodecState.CONFIGURED
    encoder.close()

    # alpha を "discard" に設定
    encoder2 = VideoEncoder(on_output, on_error)
    config2: VideoEncoderConfig = {
        "codec": "av01.0.04M.08",
        "width": 640,
        "height": 480,
        "alpha": AlphaOption.DISCARD,
    }

    encoder2.configure(config2)
    assert encoder2.state == CodecState.CONFIGURED
    encoder2.close()


def test_video_encoder_config_scalability_mode():
    """scalability_mode プロパティの設定テスト"""

    def on_output(chunk):
        pass

    def on_error(error):
        pass

    encoder = VideoEncoder(on_output, on_error)

    # scalability_mode を設定
    config: VideoEncoderConfig = {
        "codec": "av01.0.04M.08",
        "width": 640,
        "height": 480,
        "scalability_mode": "L1T2",
    }

    encoder.configure(config)
    assert encoder.state == CodecState.CONFIGURED
    encoder.close()


def test_video_encoder_config_bitrate_mode():
    """bitrate_mode プロパティの設定テスト"""

    def on_output(chunk):
        pass

    def on_error(error):
        pass

    # constant モード
    encoder1 = VideoEncoder(on_output, on_error)
    config1: VideoEncoderConfig = {
        "codec": "av01.0.04M.08",
        "width": 640,
        "height": 480,
        "bitrate_mode": VideoEncoderBitrateMode.CONSTANT,
        "bitrate": 1_000_000,
    }

    encoder1.configure(config1)
    assert encoder1.state == CodecState.CONFIGURED
    encoder1.close()

    # variable モード
    encoder2 = VideoEncoder(on_output, on_error)
    config2: VideoEncoderConfig = {
        "codec": "av01.0.04M.08",
        "width": 640,
        "height": 480,
        "bitrate_mode": VideoEncoderBitrateMode.VARIABLE,
        "bitrate": 1_000_000,
    }

    encoder2.configure(config2)
    assert encoder2.state == CodecState.CONFIGURED
    encoder2.close()

    # quantizer モード
    encoder3 = VideoEncoder(on_output, on_error)
    config3: VideoEncoderConfig = {
        "codec": "av01.0.04M.08",
        "width": 640,
        "height": 480,
        "bitrate_mode": VideoEncoderBitrateMode.QUANTIZER,
    }

    encoder3.configure(config3)
    assert encoder3.state == CodecState.CONFIGURED
    encoder3.close()


def test_video_encoder_config_content_hint():
    """content_hint プロパティの設定テスト"""

    def on_output(chunk):
        pass

    def on_error(error):
        pass

    encoder = VideoEncoder(on_output, on_error)

    # content_hint を設定
    config: VideoEncoderConfig = {
        "codec": "av01.0.04M.08",
        "width": 640,
        "height": 480,
        "content_hint": "motion",
    }

    encoder.configure(config)
    assert encoder.state == CodecState.CONFIGURED
    encoder.close()


def test_video_encoder_config_all_properties():
    """全プロパティを同時に設定するテスト"""

    def on_output(chunk):
        pass

    def on_error(error):
        pass

    encoder = VideoEncoder(on_output, on_error)

    # すべてのプロパティを設定
    config: VideoEncoderConfig = {
        "codec": "av01.0.04M.08",
        "width": 640,
        "height": 480,
        "display_width": 320,
        "display_height": 240,
        "bitrate": 1_000_000,
        "framerate": 30.0,
        "hardware_acceleration": HardwareAcceleration.NO_PREFERENCE,
        "alpha": AlphaOption.DISCARD,
        "scalability_mode": "L1T2",
        "bitrate_mode": VideoEncoderBitrateMode.VARIABLE,
        "latency_mode": LatencyMode.REALTIME,
        "content_hint": "motion",
    }

    encoder.configure(config)
    assert encoder.state == CodecState.CONFIGURED
    encoder.close()
