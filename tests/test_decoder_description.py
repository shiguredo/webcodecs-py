"""デコーダの description フィールドのテスト

WebCodecs API 仕様では description は AllowSharedBufferSource（バイナリデータ）として
定義されているため、bytes として渡せることを確認する。
"""

import pytest
from webcodecs import VideoDecoder, AudioDecoder


class TestVideoDecoderDescription:
    """VideoDecoder の description フィールドのテスト"""

    def test_description_with_bytes(self):
        """description に bytes を渡せることを確認する"""
        decoded_frames = []
        errors = []

        def on_output(frame):
            decoded_frames.append(frame)

        def on_error(error):
            errors.append(error)

        decoder = VideoDecoder(on_output, on_error)

        # AVC (H.264) の extradata（SPS/PPS）のサンプルデータ
        # 実際の extradata ではないが、bytes として渡せることを確認
        extradata = bytes([0x01, 0x64, 0x00, 0x1F, 0xFF, 0xE1, 0x00, 0x1B])

        config = {
            "codec": "avc1.640028",
            "description": extradata,
            "coded_width": 1920,
            "coded_height": 1080,
        }

        # configure が bytes の description を受け入れることを確認
        decoder.configure(config)

        decoder.close()

    def test_description_with_empty_bytes(self):
        """空の bytes を description に渡せることを確認する"""
        decoder = VideoDecoder(
            lambda f: None,
            lambda e: None,
        )

        config = {
            "codec": "avc1.640028",
            "description": b"",
            "coded_width": 1920,
            "coded_height": 1080,
        }

        decoder.configure(config)
        decoder.close()

    def test_description_without_description(self):
        """description なしでも configure できることを確認する"""
        decoder = VideoDecoder(
            lambda f: None,
            lambda e: None,
        )

        config = {
            "codec": "av01.0.04M.08",
        }

        decoder.configure(config)
        decoder.close()


class TestAudioDecoderDescription:
    """AudioDecoder の description フィールドのテスト"""

    def test_description_with_bytes(self):
        """description に bytes を渡せることを確認する"""
        decoded_data = []
        errors = []

        def on_output(data):
            decoded_data.append(data)

        def on_error(error):
            errors.append(error)

        decoder = AudioDecoder(on_output, on_error)

        # AAC の AudioSpecificConfig のサンプルデータ
        # 実際の extradata ではないが、bytes として渡せることを確認
        extradata = bytes([0x11, 0x90])

        config = {
            "codec": "mp4a.40.2",
            "sample_rate": 48000,
            "number_of_channels": 2,
            "description": extradata,
        }

        # configure が bytes の description を受け入れることを確認
        decoder.configure(config)

        decoder.close()

    def test_description_with_empty_bytes(self):
        """空の bytes を description に渡せることを確認する"""
        decoder = AudioDecoder(
            lambda f: None,
            lambda e: None,
        )

        config = {
            "codec": "opus",
            "sample_rate": 48000,
            "number_of_channels": 2,
            "description": b"",
        }

        decoder.configure(config)
        decoder.close()

    def test_description_without_description(self):
        """description なしでも configure できることを確認する"""
        decoder = AudioDecoder(
            lambda f: None,
            lambda e: None,
        )

        config = {
            "codec": "opus",
            "sample_rate": 48000,
            "number_of_channels": 2,
        }

        decoder.configure(config)
        decoder.close()
