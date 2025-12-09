# webcodecs-py

[![PyPI](https://img.shields.io/pypi/v/webcodecs-py)](https://pypi.org/project/webcodecs-py/)
[![SPEC 0 — Minimum Supported Dependencies](https://img.shields.io/badge/SPEC-0-green?labelColor=%23004811&color=%235CA038)](https://scientific-python.org/specs/spec-0000/)
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
- Opus、FLAC、AAC、VP8、VP9、AV1、H.264、H.265 コーデックをサポート
  - AAC は macOS の AudioToolbox を利用
  - H.264 と H.265 は macOS の VideoToolbox または NVIDIA Video Codec を利用
- ImageDecoder による画像デコードをサポート (macOS)
  - JPEG、PNG、GIF、WebP、BMP、TIFF、HEIC/HEIF に対応
  - macOS の Image I/O フレームワークを利用
- Apple Audio Toolbox と Video Toolbox を利用したハードウェアアクセラレーション対応 (macOS)
- NVIDIA Video Codec SDK を利用したハードウェアアクセラレーション対応 (Ubuntu x86_64)
  - NVIDIA Video Codec を利用する場合は NVIDIA ドライバー 570.0 以降が必要
- NumPy の ndarray を直接利用できる
- クロスプラットフォーム対応
  - macOS arm64
  - Ubuntu x86_64 および arm64
  - Windows x86_64
    - Windows はソフトウェアエンコード/デコードのみ対応

開発状況は [webcodecs-py 対応状況](docs/PYTHON_INTERFACE.md) をご確認ください。

## 実装しない機能

- CanvasImageSource: VideoFrame の CanvasImageSource コンストラクタはブラウザ固有機能のため実装対象外

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

# with 文で AudioData を使用（自動的に close される）
with AudioData(init) as audio_data:
    encoder.encode(audio_data)

encoder.flush()

print(f"エンコード完了: {len(encoded_chunks)} チャンク")

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

# with 文で VideoFrame を使用（自動的に close される）
with VideoFrame(frame_data, init) as frame:
    encoder.encode(frame, {"key_frame": True})

encoder.flush()

print(f"エンコード完了: {len(encoded_chunks)} チャンク, {encoded_chunks[0].byte_length} bytes")

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
- VP8
  - <https://chromium.googlesource.com/webm/libvpx>
  - <https://docs.nvidia.com/video-technologies/video-codec-sdk/13.0/index.html>
- VP9
  - <https://chromium.googlesource.com/webm/libvpx>
  - <https://docs.nvidia.com/video-technologies/video-codec-sdk/13.0/index.html>
- AV1
  - <https://aomedia.googlesource.com/aom>
  - <https://github.com/videolan/dav1d>
  - <https://docs.nvidia.com/video-technologies/video-codec-sdk/13.0/index.html>
- H.264 (AVC)
  - <https://developer.apple.com/documentation/videotoolbox>
  - <https://docs.nvidia.com/video-technologies/video-codec-sdk/13.0/index.html>
- H.265 (HEVC)
  - <https://developer.apple.com/documentation/videotoolbox>
  - <https://docs.nvidia.com/video-technologies/video-codec-sdk/13.0/index.html>

## Python

- 3.14
- 3.13
- 3.12

## プラットフォーム

- macOS 26 arm64
- macOS 15 arm64
- Ubuntu 24.04 LTS x86_64
- Ubuntu 24.04 LTS arm64
- Ubuntu 22.04 LTS x86_64
- Ubuntu 22.04 LTS arm64
- Windows 11 x86_64
- Windows Server 2025 x86_64

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

## NVIDIA Video Codec SDK

<https://docs.nvidia.com/video-technologies/video-codec-sdk/13.0/index.html>

<https://docs.nvidia.com/video-technologies/video-codec-sdk/13.0/license/index.html>

```text
“This software contains source code provided by NVIDIA Corporation.”
```
