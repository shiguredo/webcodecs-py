# webcodecs-py

> [!CAUTION]
> webcodecs-py はまだ開発中であり、安定版ではありません。将来的に API が変更される可能性があります。
> 開発状況は [webcodecs-py 対応状況](docs/PYTHON_INTERFACE.md) をご確認ください。

[![PyPI](https://img.shields.io/pypi/v/webcodecs-py)](https://pypi.org/project/webcodecs-py/)
[![image](https://img.shields.io/pypi/pyversions/webcodecs-py.svg)](https://pypi.python.org/pypi/webcodecs-py)
[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](https://opensource.org/licenses/Apache-2.0)
[![Actions status](https://github.com/shiguredo/webcodecs-py/workflows/wheel/badge.svg)](https://github.com/shiguredo/webcodecs-py/actions)

## About Shiguredo's open source software

We will not respond to PRs or issues that have not been discussed on Discord. Also, Discord is only available in Japanese.

Please read <https://github.com/shiguredo/oss/blob/master/README.en.md> before use.

## 時雨堂のオープンソースソフトウェアについて

利用前に <https://github.com/shiguredo/oss> をお読みください。

## webcodecs-py について

webcodecs-py は [WebCodecs API](https://www.w3.org/TR/webcodecs/) API を Python で利用できるようにするライブラリです。

## 特徴

- WebCodecs API の Python バインディング
- Opus、FLAC、AAC、AV1、H.264、H.265 コーデックをサポート
  - AAC は macOS の AudioToolbox を利用
  - H.264 と H.265 は macOS の VideoToolbox を利用
- クロスプラットフォーム対応
  - macOS
  - Ubuntu
  - Windows

## 実装しない機能

- ImageDecoder（画像デコード機能は PIL/Pillow や OpenCV を使用してください）

## サンプルコード

### Opus オーディオエンコード

```python
import numpy as np

from webcodecs import (
    AudioData,
    AudioDataInit,
    AudioEncoder,
    AudioEncoderConfig,
    AudioSampleFormat,
)

sample_rate = 48000
frame_size = 960  # 20ms @ 48kHz

# エンコーダを作成
encoded_chunks = []


def on_output(chunk):
    encoded_chunks.append(chunk)


def on_error(error):
    raise RuntimeError(f"エンコーダエラー: {error}")


encoder = AudioEncoder(on_output, on_error)
encoder_config: AudioEncoderConfig = {
    "codec": "opus",
    "sample_rate": sample_rate,
    "number_of_channels": 1,
    "bitrate": 64000,
}
encoder.configure(encoder_config)

# サイン波を生成してエンコード
t = np.linspace(0, frame_size / sample_rate, frame_size, dtype=np.float32)
audio_samples = (np.sin(2 * np.pi * 440 * t) * 0.5).reshape(frame_size, 1)

init: AudioDataInit = {
    "format": AudioSampleFormat.F32,
    "sample_rate": sample_rate,
    "number_of_frames": frame_size,
    "number_of_channels": 1,
    "timestamp": 0,
    "data": audio_samples,
}
audio_data = AudioData(init)
encoder.encode(audio_data)
encoder.flush()

print(f"エンコード完了: {len(encoded_chunks)} チャンク")

audio_data.close()
encoder.close()
```

### AV1 ビデオエンコード

```python
import numpy as np

from webcodecs import (
    LatencyMode,
    VideoEncoder,
    VideoEncoderConfig,
    VideoFrame,
    VideoFrameBufferInit,
    VideoPixelFormat,
)

width, height = 320, 240

# エンコーダを作成
encoded_chunks = []


def on_output(chunk):
    encoded_chunks.append(chunk)


def on_error(error):
    raise RuntimeError(f"エンコーダエラー: {error}")


encoder = VideoEncoder(on_output, on_error)
encoder_config: VideoEncoderConfig = {
    "codec": "av01.0.04M.08",
    "width": width,
    "height": height,
    "bitrate": 500_000,
    "framerate": 30.0,
    "latency_mode": LatencyMode.REALTIME,
}
encoder.configure(encoder_config)

# I420 フォーマットのテストフレームを作成
data_size = width * height * 3 // 2
frame_data = np.zeros(data_size, dtype=np.uint8)
init: VideoFrameBufferInit = {
    "format": VideoPixelFormat.I420,
    "coded_width": width,
    "coded_height": height,
    "timestamp": 0,
}
frame = VideoFrame(frame_data, init)

# エンコード
encoder.encode(frame, {"keyFrame": True})
encoder.flush()

print(f"エンコード完了: {len(encoded_chunks)} チャンク, {encoded_chunks[0].byte_length} bytes")

frame.close()
encoder.close()
```

## インストール

`uv add webcodecs-py`

## コーデック

- Opus
  - <https://github.com/xiph/opus>
- FLAC
  - <https://github.com/xiph/flac>
- AAC
  - <https://developer.apple.com/documentation/audiotoolbox>
- AV1
  - <https://aomedia.googlesource.com/aom>
  - <https://github.com/videolan/dav1d>
- H.264 (AVC)
  - <https://developer.apple.com/documentation/videotoolbox>
- H.265 (HEVC)
  - <https://developer.apple.com/documentation/videotoolbox>

## Python

- 3.14
- 3.13

## プラットフォーム

- macOS 26 arm64
- macOS 15 arm64
- Ubuntu 24.04 LTS x86_64
- Ubuntu 24.04 LTS arm64
- Ubuntu 22.04 LTS x86_64
- Ubuntu 22.04 LTS arm64
- Windows 11 x86_64

## ビルド

```bash
make develop
```

## テスト

```bash
uv sync
make test
```

## サンプル

```bash
uv sync --group example
make develop
uv run python examples/blend2d_to_mp4.py
```

## ライセンス

Apache License 2.0

```text
Copyright 2025-2025, Shiguredo Inc.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
```
