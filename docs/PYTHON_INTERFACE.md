# webcodecs-py 対応状況

webcodecs-py は WebCodecs API を Python から扱うためのバインディングであり、リアルタイム処理向けに最適化しています。

- 最終更新: 2025-11-23
- 基準仕様: [W3C WebCodecs](https://w3c.github.io/webcodecs/)
  - 日付: 2025-11-19
  - commit: 66a81b2

## 基本方針

webcodecs-py は WebCodecs API にできるだけ準拠しつつ、以下の方針で実装しています：

1. **Python エコシステムとの親和性**: Python の命名規則やイディオムに従う
2. **リアルタイム用途の最適化**: WebRTC、Media over QUIC でのリアルタイム処理を想定
3. **ndarray の活用**: バッファの入出力に numpy.ndarray を前提とし、Python エコシステムとの連携を容易にする
4. **型安全性**: TypedDict による型ヒントで IDE サポートと型チェックを提供

## 実装ステータス概要

| セクション | 主な対象 | Python | 補足 |
|-----------|---------|---------|------|
| 辞書型インターフェース | AudioDataInit / VideoFrameBufferInit / 各 Config | 必須項目は実装済み | hardware_acceleration は未対応 |
| Video クラス | VideoFrame / EncodedVideoChunk / VideoEncoder / VideoDecoder | 全メソッド実装済み | |
| Audio クラス | AudioData / EncodedAudioChunk / AudioEncoder / AudioDecoder | 全メソッド実装済み | |
| 補助型と列挙型 | PlaneLayout / DOMRect / VideoColorSpace / CodecState など | 必要項目を実装済み | 列挙値の未実装分はテーブルで明記 |
| 独自拡張 | HardwareAccelerationEngine / VideoFrame 拡張 / AudioData 拡張 / get_video_codec_capabilities() | planes() とハードウェアアクセラレーション | 仕様逸脱理由を各節で説明 |

## WebCodecs API との主な差異

### 1. 命名規則

WebCodecs API の JavaScript camelCase から Python の snake_case に変換：

| WebCodecs API (JavaScript) | webcodecs-py (Python) |
|---------------------------|----------------------|
| `numberOfChannels` | `number_of_channels` |
| `numberOfFrames` | `number_of_frames` |
| `sampleRate` | `sample_rate` |
| `encodeQueueSize` | `encode_queue_size` |
| `decodeQueueSize` | `decode_queue_size` |
| `byteLength` | `byte_length` |
| `codedWidth` | `coded_width` |
| `codedHeight` | `coded_height` |
| `allocationSize` | `allocation_size` |
| `isConfigSupported` | `is_config_supported` |

### 2. イベントハンドリング

#### WebCodecs API (JavaScript)

```javascript
encoder.addEventListener('dequeue', () => {
  // イベントハンドラ
});
encoder.ondequeue = () => {
  // イベントハンドラ
};
```

#### webcodecs-py (Python)

```python
# コンストラクタでコールバックを指定
encoder = VideoEncoder(
    on_output=lambda chunk: ...,
    on_error=lambda error: ...
)
# dequeue イベントは後から設定可能
encoder.on_dequeue(lambda: ...)
```

**理由**: Python には JavaScript の EventTarget に相当する標準的なイベントシステムがないため、コールバック方式を採用しています。

### 3. 非同期処理

#### WebCodecs API (JavaScript)

```javascript
await encoder.flush();  // Promise を返す
const support = await VideoEncoder.isConfigSupported(config);
```

#### webcodecs-py (Python)

```python
# 現在は同期的に実行
encoder.flush()
# is_config_supported は同期的
support = VideoDecoder.is_config_supported(config)
```

**理由**: Python では同期的な処理を採用しています。

**注**: WebCodecs 仕様に準拠した並列処理は実装済みです。`encode()` / `decode()` メソッドは即座に返り、実際の処理はバックグラウンドのワーカースレッドで実行されます。

### 4. コンストラクタ引数

#### WebCodecs API (JavaScript)

```javascript
const encoder = new VideoEncoder({
  output: (chunk, metadata) => { ... },
  error: (error) => { ... }
});
```

#### webcodecs-py (Python)

```python
# コールバックを直接コンストラクタに渡す
encoder = VideoEncoder(
    on_output=lambda chunk: ...,
    on_error=lambda error: ...
)
# または位置引数で
encoder = VideoEncoder(output_callback, error_callback)
```

**理由**: Python のシンプルさを重視し、コールバックは直接コンストラクタで指定する方式を採用しています。

### 5. TypedDict による設定辞書

WebCodecs API の辞書型インターフェースを TypedDict で実装：

```python
from webcodecs import LatencyMode, VideoEncoder, VideoEncoderConfig

# TypedDict による型チェック
config: VideoEncoderConfig = {
    "codec": "av1",
    "width": 1920,
    "height": 1080,
    "bitrate": 1000000,
    "latency_mode": LatencyMode.REALTIME,
}

encoder = VideoEncoder(on_output, on_error)
encoder.configure(config)  # dict として渡す
```

**利点**:

- IDE による補完と型チェック
- 実行時は通常の dict として動作（パフォーマンスへの影響なし）
- WebCodecs API の辞書型との互換性を維持

**実装済み TypedDict**:

Init 系:

- `VideoFrameBufferInit` - VideoFrame コンストラクタ用
- `AudioDataInit` - AudioData コンストラクタ用
- `EncodedVideoChunkInit` - EncodedVideoChunk コンストラクタ用
- `EncodedAudioChunkInit` - EncodedAudioChunk コンストラクタ用

Config 系:

- `AudioEncoderConfig` - AudioEncoder.configure() 用
- `AudioDecoderConfig` - AudioDecoder.configure() 用
- `VideoEncoderConfig` - VideoEncoder.configure() 用
- `VideoDecoderConfig` - VideoDecoder.configure() 用

コーデック固有設定系:

- `OpusEncoderConfig` - AudioEncoderConfig.opus 用
- `FlacEncoderConfig` - AudioEncoderConfig.flac 用
- `AvcEncoderConfig` - VideoEncoderConfig.avc 用
- `HevcEncoderConfig` - VideoEncoderConfig.hevc 用

Options 系:

- `AudioDataCopyToOptions` - AudioData.copy_to() のオプション
- `VideoFrameCopyToOptions` - VideoFrame.copy_to() のオプション
- `VideoEncoderEncodeOptions` - VideoEncoder.encode() のオプション
- `VideoEncoderEncodeOptionsForAv1` - AV1 固有のエンコードオプション
- `VideoEncoderEncodeOptionsForAvc` - AVC 固有のエンコードオプション
- `VideoEncoderEncodeOptionsForHevc` - HEVC 固有のエンコードオプション

Support 系 (is_config_supported() の戻り値):

- `AudioEncoderSupport` - AudioEncoder.is_config_supported() 用
- `AudioDecoderSupport` - AudioDecoder.is_config_supported() 用
- `VideoEncoderSupport` - VideoEncoder.is_config_supported() 用
- `VideoDecoderSupport` - VideoDecoder.is_config_supported() 用

### 6. Promise の代替

- JavaScript の Promise を返すメソッドは、Python では通常の同期メソッドとして実装
- 非同期処理は内部的に処理

### 7. planes() メソッド

- VideoFrame と AudioData の内部バッファへのビューを返す独自拡張
- `planes()` メソッドはコピーなしで内部データにアクセス可能

### 8. copy_to() の実装

WebCodecs API の copyTo() 仕様に準拠した実装：

- **VideoFrame.copy_to()**: destination バッファに書き込み、PlaneLayout のリストを返す
- **AudioData.copy_to()**: destination バッファに書き込み、戻り値なし
- **EncodedVideoChunk.copy_to()**: destination バッファに書き込み、戻り値なし
- **EncodedAudioChunk.copy_to()**: destination バッファに書き込み、戻り値なし

```python
import numpy as np
from webcodecs import AudioData, AudioSampleFormat

# 音声データを作成 (ステレオ、1024 フレーム、float32)
sample_rate = 48000
number_of_channels = 2
number_of_frames = 1024
data = np.zeros(number_of_frames * number_of_channels, dtype=np.float32)

init = {
    "format": AudioSampleFormat.F32,
    "sample_rate": sample_rate,
    "number_of_frames": number_of_frames,
    "number_of_channels": number_of_channels,
    "timestamp": 0,
    "data": data,
}
audio = AudioData(init)

# 指定したプレーンをコピー
options = {"plane_index": 0}
destination = np.zeros(audio.allocation_size(options), dtype=np.uint8)
audio.copy_to(destination, options)
```

```python
import numpy as np
from webcodecs import VideoFrame, VideoPixelFormat

# 映像データを作成 (I420 フォーマット、640x480)
width = 640
height = 480
# I420: Y プレーン + U プレーン (1/4) + V プレーン (1/4)
data = np.zeros(width * height * 3 // 2, dtype=np.uint8)

init = {
    "format": VideoPixelFormat.I420,
    "coded_width": width,
    "coded_height": height,
    "timestamp": 0,
}
frame = VideoFrame(data, init)

# 全プレーンをコピー
destination = np.zeros(frame.allocation_size(), dtype=np.uint8)
layouts = frame.copy_to(destination)

# フォーマット変換してコピー（I420 → RGBA）
rgba_size = frame.allocation_size({"format": VideoPixelFormat.RGBA})
rgba_buffer = np.zeros(rgba_size, dtype=np.uint8)
frame.copy_to(rgba_buffer, {"format": VideoPixelFormat.RGBA})
```

```python
import numpy as np
from webcodecs import EncodedVideoChunk, EncodedVideoChunkType

# エンコード済みデータ (実際にはエンコーダーから取得)
data = b"\x00" * 1000

chunk = EncodedVideoChunk({
    "type": EncodedVideoChunkType.KEY,
    "timestamp": 0,
    "data": data,
})

# エンコード済みデータをコピー
destination = np.zeros(chunk.byte_length, dtype=np.uint8)
chunk.copy_to(destination)
```

**ゼロコピーアクセス**: コピーが不要な場合は `planes()` メソッド（独自拡張）を使用してください。

## 基本的な利用例

### AudioDecoder の例

```python
from webcodecs import AudioDecoder, AudioDecoderConfig


def on_output(audio_data):
    print(f"デコード完了: {audio_data.number_of_frames} frames")


def on_error(error):
    print(f"エラー: {error}")


decoder = AudioDecoder(on_output, on_error)

config: AudioDecoderConfig = {
    "codec": "opus",
    "sample_rate": 48000,
    "number_of_channels": 2,
}
decoder.configure(config)
```

### AudioEncoder の例

```python
from webcodecs import AudioEncoder, AudioEncoderConfig


def on_output(chunk):
    print(f"エンコード完了: {chunk.byte_length} bytes")


def on_error(error):
    print(f"エラー: {error}")


encoder = AudioEncoder(on_output, on_error)

config: AudioEncoderConfig = {
    "codec": "opus",
    "sample_rate": 48000,
    "number_of_channels": 2,
    "bitrate": 64000,
}
encoder.configure(config)
```

### VideoDecoder の例

```python
from webcodecs import VideoDecoder, VideoDecoderConfig


def on_output(frame):
    print(f"デコード完了: {frame.coded_width}x{frame.coded_height}")


def on_error(error):
    print(f"エラー: {error}")


decoder = VideoDecoder(on_output, on_error)

config: VideoDecoderConfig = {
    "codec": "av01.0.08M.08",
    "coded_width": 1920,
    "coded_height": 1080,
}
decoder.configure(config)
```

### VideoEncoder の例

```python
from webcodecs import VideoEncoder, VideoEncoderConfig


def on_output(chunk):
    print(f"エンコード完了: {chunk.byte_length} bytes")


def on_error(error):
    print(f"エラー: {error}")


encoder = VideoEncoder(on_output, on_error)

config: VideoEncoderConfig = {
    "codec": "av1",
    "width": 1920,
    "height": 1080,
    "bitrate": 1000000,
}
encoder.configure(config)
```

## 実装済みインターフェース

### 辞書型インターフェース (Config)

#### AudioDataInit

| プロパティ | Python | WebCodecs API | テスト | 備考 |
|-----------|---------|-------------|--------|------|
| `format` | o | o | o | AudioSampleFormat を受け入れ、**必須** |
| `sample_rate` | o | o | o | **必須** |
| `number_of_frames` | o | o | o | **必須** |
| `number_of_channels` | o | o | o | **必須** |
| `timestamp` | o | o | o | **必須** |
| `data` | o | o | o | **必須** |
| `transfer` | x | o | - | **未実装** |

#### AudioDecoderConfig

| プロパティ | Python | WebCodecs API | テスト | 備考 |
|-----------|---------|-------------|--------|------|
| `codec` | o | o | o | **必須** |
| `sample_rate` | o | o | o | **必須** |
| `number_of_channels` | o | o | o | **必須** |
| `description` | o | o | x | Codec-specific configuration |

#### AudioEncoderConfig

| プロパティ | Python | WebCodecs API | テスト | 備考 |
|-----------|---------|-------------|--------|------|
| `codec` | o | o | o | **必須** |
| `sample_rate` | o | o | o | **必須** |
| `number_of_channels` | o | o | o | **必須** |
| `bitrate` | o | o | o | |
| `bitrate_mode` | o | o | o | BitrateMode enum |

#### VideoFrameBufferInit

| プロパティ | Python | WebCodecs API | テスト | 備考 |
|-----------|---------|-------------|--------|------|
| `format` | o | o | o | VideoPixelFormat を受け入れ |
| `coded_width` | o | o | o | **必須** |
| `coded_height` | o | o | o | **必須** |
| `timestamp` | o | o | o | **必須** |
| `duration` | o | o | o | |
| `layout` | o | o | o | PlaneLayout の配列 |
| `visible_rect` | o | o | o | DOMRect または dict |
| `display_width` | o | o | o | |
| `display_height` | o | o | o | |
| `color_space` | o | o | o | VideoColorSpace または dict |
| `rotation` | o | * | o | 0, 90, 180, 270 のみ対応（WebCodecs は任意の double 値） |
| `flip` | o | o | o | |
| `metadata` | o | o | o | dict |
| `transfer` | x | o | - | **未実装** |

#### VideoDecoderConfig

| プロパティ | Python | WebCodecs API | テスト | 備考 |
|-----------|---------|-------------|--------|------|
| `codec` | o | o | o | **必須** |
| `description` | o | o | x | Codec-specific configuration |
| `coded_width` | o | o | o | |
| `coded_height` | o | o | o | |
| `display_aspect_width` | x | o | - | **未実装** |
| `display_aspect_height` | x | o | - | **未実装** |
| `color_space` | x | o | - | **未実装** |
| `hardware_acceleration` | x | o | - | **未実装** |
| `optimize_for_latency` | x | o | - | **未実装** |
| `rotation` | x | o | - | **未実装** |
| `flip` | x | o | - | **未実装** |

#### VideoEncoderConfig

| プロパティ | Python | WebCodecs API | テスト | 備考 |
|-----------|---------|-------------|--------|------|
| `codec` | o | o | o | **必須** |
| `width` | o | o | o | **必須** |
| `height` | o | o | o | **必須** |
| `display_width` | x | o | - | **未実装** |
| `display_height` | x | o | - | **未実装** |
| `bitrate` | o | o | o | |
| `framerate` | o | o | o | |
| `hardware_acceleration` | x | o | - | **未実装** |
| `alpha` | o | o | o | AlphaOption enum |
| `scalability_mode` | x | o | - | **未実装** |
| `bitrate_mode` | o | o | o | VideoEncoderBitrateMode enum |
| `latency_mode` | o | o | o | LatencyMode enum |
| `content_hint` | x | o | - | **未実装** |
| `avc` | o | o | o | AvcEncoderConfig (format: "annexb" \| "avc") |
| `hevc` | o | o | o | HevcEncoderConfig (format: "annexb" \| "hevc") |
| **`hardware_acceleration_engine`** | o | x | o | **独自拡張**: HardwareAccelerationEngine ENUM（実際に使用される） |

### Audio インターフェース

#### AudioData

| メソッド/プロパティ | Python | WebCodecs API | テスト | 備考 |
|-----------------|---------|-------------|--------|------|
| `constructor(init)` | o | o | o | AudioDataInit を使用 |
| `format` | o | o | o | AudioSampleFormat |
| `sample_rate` | o | o | o | |
| `number_of_frames` | o | o | o | |
| `number_of_channels` | o | o | o | |
| `duration` | o | o | o | |
| `timestamp` | o | o | o | |
| `allocation_size(options)` | o | o | o | AudioDataCopyToOptions に基づいてサイズを計算 |
| `copy_to(destination, options)` | o | o | o | AudioDataCopyToOptions に基づいて destination に書き込み（format 指定で変換も可能） |
| `clone()` | o | o | o | |
| `close()` | o | o | o | |
| **`is_closed`** | o | x | o | **独自拡張**: プロパティ |
| **`get_channel_data()`** | o | x | o | **独自拡張**: 特定チャンネルのデータを返す |

#### EncodedAudioChunk

| メソッド/プロパティ | Python | WebCodecs API | テスト | 備考 |
|-----------------|---------|-------------|--------|------|
| `constructor(init)` | o | o | o | `EncodedAudioChunkInit` (dict) を受け取る |
| `type` | o | o | o | "key" または "delta" |
| `timestamp` | o | o | o | |
| `duration` | o | o | o | |
| `byte_length` | o | o | o | |
| `copy_to()` | o | o | o | destination に書き込み |

#### AudioDecoder

| メソッド/プロパティ | Python | WebCodecs API | テスト | 備考 |
|-----------------|---------|-------------|--------|------|
| `constructor(output, error)` | o | * | o | **Python 実装: コールバックを直接渡す** (WebCodecs は init 辞書) |
| `state` | o | o | o | CodecState |
| `decode_queue_size` | o | o | o | |
| `on_dequeue` | o | o | o | EventHandler |
| `configure(config)` | o | o | o | |
| `decode(chunk)` | o | o | o | |
| `flush()` | o | o | o | |
| `reset()` | o | o | o | |
| `close()` | o | o | o | |
| `is_config_supported()` | o | o | o | 静的メソッド |
| **`on_output(callback)`** | o | x | o | **独自拡張**: コールバック設定 (WebCodecs はコンストラクタで指定) |
| **`on_error(callback)`** | o | x | o | **独自拡張**: コールバック設定 (WebCodecs はコンストラクタで指定) |

#### AudioEncoder

| メソッド/プロパティ | Python | WebCodecs API | テスト | 備考 |
|-----------------|---------|-------------|--------|------|
| `constructor(output, error)` | o | * | o | **Python 実装: コールバックを直接渡す** (WebCodecs は init 辞書) |
| `state` | o | o | o | CodecState |
| `encode_queue_size` | o | o | o | |
| `on_dequeue` | o | o | o | EventHandler |
| `configure(config)` | o | o | o | |
| `encode(data)` | o | o | o | |
| `flush()` | o | o | o | |
| `reset()` | o | o | o | |
| `close()` | o | o | o | |
| `is_config_supported()` | o | o | o | 静的メソッド |
| **`on_output(callback)`** | o | x | o | **独自拡張**: コールバック設定 (WebCodecs はコンストラクタで指定) |
| **`on_error(callback)`** | o | x | o | **独自拡張**: コールバック設定 (WebCodecs はコンストラクタで指定) |

### Video インターフェース

#### VideoFrame

| メソッド/プロパティ | Python | WebCodecs API | テスト | 備考 |
|-----------------|---------|-------------|--------|------|
| `constructor(image, init)` | - | o | - | **実装しない** (CanvasImageSource はブラウザ固有機能) |
| `constructor(data, init)` | o | o | o | VideoFrameBufferInit を使用 |
| `format` | o | o | o | VideoPixelFormat を返す |
| `coded_width` | o | o | o | |
| `coded_height` | o | o | o | |
| `coded_rect` | x | o | - | **未実装** |
| `visible_rect` | o | o | o | DOMRect を返す |
| `rotation` | o | * | o | 0, 90, 180, 270 のみ対応（WebCodecs は任意の double 値） |
| `flip` | o | o | o | 水平反転 |
| `display_width` | o | o | o | |
| `display_height` | o | o | o | |
| `duration` | o | o | o | |
| `timestamp` | o | o | o | |
| `color_space` | o | o | o | VideoColorSpace を返す |
| `metadata()` | o | o | o | dict を返す |
| `allocation_size(options)` | o | o | o | copy_to() に必要なバッファサイズを返す |
| `copy_to(destination, options)` | o | * | o | destination に書き込み、PlaneLayout のリストを返す（format 指定で変換も可能、colorSpace オプションは未実装） |
| `clone()` | o | o | o | |
| `close()` | o | o | o | |
| **`is_closed`** | o | x | o | **独自拡張**: プロパティ |
| **`planes()`** | o | x | o | **独自拡張**: 全プレーン (Y, U, V) をタプルで返す（I420/I422/I444 のみ） |
| **`plane()`** | o | x | o | **独自拡張**: 指定したプレーンを返す（全フォーマット対応） |

#### EncodedVideoChunk

| メソッド/プロパティ | Python | WebCodecs API | テスト | 備考 |
|-----------------|---------|-------------|--------|------|
| `constructor(init)` | o | o | o | `EncodedVideoChunkInit` (dict) を受け取る |
| `type` | o | o | o | "key" または "delta" |
| `timestamp` | o | o | o | |
| `duration` | o | o | o | |
| `byte_length` | o | o | o | |
| `copy_to()` | o | o | o | destination に書き込み |

#### VideoDecoder

| メソッド/プロパティ | Python | WebCodecs API | テスト | 備考 |
|-----------------|---------|-------------|--------|------|
| `constructor(output, error)` | o | * | o | **Python 実装: コールバックを直接渡す** (WebCodecs は init 辞書) |
| `state` | o | o | o | CodecState |
| `decode_queue_size` | o | o | o | |
| `on_dequeue` | o | o | o | EventHandler |
| `configure(config)` | o | o | o | |
| `decode(chunk)` | o | o | o | |
| `flush()` | o | o | o | |
| `reset()` | o | o | o | |
| `close()` | o | o | o | |
| `is_config_supported()` | o | o | o | 静的メソッド |
| **`on_output(callback)`** | o | x | o | **独自拡張**: コールバック設定 (WebCodecs はコンストラクタで指定) |
| **`on_error(callback)`** | o | x | o | **独自拡張**: コールバック設定 (WebCodecs はコンストラクタで指定) |

#### VideoEncoder

| メソッド/プロパティ | Python | WebCodecs API | テスト | 備考 |
|-----------------|---------|-------------|--------|------|
| `constructor(output, error)` | o | * | o | **Python 実装: コールバックを直接渡す** (WebCodecs は init 辞書) |
| `state` | o | o | o | CodecState |
| `encode_queue_size` | o | o | o | |
| `on_dequeue` | o | o | o | EventHandler |
| `configure(config)` | o | o | o | |
| `encode(frame, options)` | o | o | o | VideoEncoderEncodeOptions (keyFrame, av1.quantizer, avc.quantizer, hevc.quantizer) |
| `flush()` | o | o | o | |
| `reset()` | o | o | o | |
| `close()` | o | o | o | |
| `is_config_supported()` | o | o | o | 静的メソッド |
| **`on_output(callback)`** | o | x | o | **独自拡張**: コールバック設定 (WebCodecs はコンストラクタで指定) |
| **`on_error(callback)`** | o | x | o | **独自拡張**: コールバック設定 (WebCodecs はコンストラクタで指定) |

**注**: `avc.quantizer` / `hevc.quantizer` は VideoToolbox (Apple) ではフレームごとの指定がサポートされていないため無視される。

## 独自インターフェース

### VideoFrame 拡張

#### planes() メソッド

**ゼロコピービューを返す独自拡張メソッド**

```python
def planes() -> tuple[ndarray, ndarray, ndarray]
```

- **目的**: 高速なメモリアクセスが必要な場合に、データのコピーを作成せずに直接プレーンへのビューを提供
- **対応フォーマット**: I420, I422, I444
- **戻り値**: (Y プレーン, U プレーン, V プレーン) のタプル
- **注意事項**:
  - 返されるビューは元の VideoFrame のメモリを参照している
  - VideoFrame が close() されるとビューは無効になる
  - ビューへの書き込みは元のデータを変更する

**使用例**:

```python
import numpy as np
from webcodecs import VideoFrame, VideoFrameBufferInit, VideoPixelFormat

# データを作成
data = np.zeros(width * height * 3 // 2, dtype=np.uint8)

init: VideoFrameBufferInit = {
    "format": VideoPixelFormat.I420,
    "coded_width": width,
    "coded_height": height,
    "timestamp": 0,
}

frame = VideoFrame(data, init)

# ゼロコピービューを取得
y_plane, u_plane, v_plane = frame.planes()

# ビューへの書き込みは元のデータを変更
y_plane[:] = 235  # 元の data も変更される
```

### VideoFrame のメモリ管理

VideoFrame は以下の 2 つのモードで動作します：

1. **外部メモリ参照モード** (コンストラクタで ndarray を渡した場合)
   - 元の ndarray への参照を保持
   - planes() メソッドはゼロコピービューを返す
   - copy_to() メソッドはデータのコピーを返す

2. **内部メモリ所有モード** (width, height, format で作成した場合)
   - 内部でメモリを確保し所有
   - planes() メソッドは内部メモリへのビューを返す
   - copy_to() メソッドはデータのコピーを返す

## その他の型定義

### 補助型

#### PlaneLayout

| プロパティ | Python | WebCodecs API | 備考 |
|-----------|---------|-------------|------|
| `offset` | o | o | |
| `stride` | o | o | |

#### DOMRect

| プロパティ | Python | WebCodecs API | 備考 |
|-----------|---------|-------------|------|
| `x` | o | o | |
| `y` | o | o | |
| `width` | o | o | |
| `height` | o | o | |

#### VideoColorSpace

| プロパティ | Python | WebCodecs API | 備考 |
|-----------|---------|-------------|------|
| `primaries` | o | o | |
| `transfer` | o | o | |
| `matrix` | o | o | |
| `full_range` | o | o | |

### 列挙型

#### CodecState

- `UNCONFIGURED` - 未設定状態
- `CONFIGURED` - 設定済み状態
- `CLOSED` - クローズ済み状態

#### VideoPixelFormat

実装済みのフォーマット:

- `I420`, `I422`, `I444` - YUV プレーナーフォーマット
- `NV12` - YUV セミプレーナーフォーマット
- `RGBA`, `BGRA` - 4:4:4 RGBA フォーマット
- `RGB`, `BGR` - 4:4:4 RGB フォーマット（独自拡張、下記参照）

未実装のフォーマット (WebCodecs API で定義):

- `I420P10`, `I420P12` - 10/12bit YUV 4:2:0
- `I420A`, `I420AP10`, `I420AP12` - アルファ付き YUV 4:2:0
- `I422P10`, `I422P12` - 10/12bit YUV 4:2:2
- `I422A`, `I422AP10`, `I422AP12` - アルファ付き YUV 4:2:2
- `I444P10`, `I444P12` - 10/12bit YUV 4:4:4
- `I444A`, `I444AP10`, `I444AP12` - アルファ付き YUV 4:4:4
- `RGBX`, `BGRX` - 不透明 RGB フォーマット

**RGB/BGR が独自拡張である理由**:

WebCodecs API では RGB 系フォーマットとして `RGBA`, `RGBX`, `BGRA`, `BGRX` の 4 種類のみを定義しており、すべて 4 バイト/ピクセル（32 ビット境界）です。これは GPU やハードウェアアクセラレーションとの互換性、およびメモリアライメントの効率を考慮した設計です。

一方、Python エコシステム（NumPy、PIL/Pillow、OpenCV、matplotlib 等）では 3 バイト/ピクセルの RGB/BGR フォーマットが広く使用されています。webcodecs-py ではこれらのライブラリとの相互運用性を重視し、独自拡張として `RGB` と `BGR` をサポートしています。

```python
import numpy as np
from webcodecs import VideoFrame, VideoPixelFormat

# PIL/Pillow との連携例
from PIL import Image
img = Image.open("image.png").convert("RGB")
rgb_data = np.array(img)  # shape: (height, width, 3)

# OpenCV との連携例
import cv2
bgr_data = cv2.imread("image.png")  # OpenCV は BGR を使用
```

#### AudioSampleFormat

実装済みのフォーマット:

- `U8`, `S16`, `S32`, `F32` - インターリーブフォーマット
- `U8_PLANAR`, `S16_PLANAR`, `S32_PLANAR`, `F32_PLANAR` - プレーナーフォーマット

#### EncodedVideoChunkType / EncodedAudioChunkType

- `KEY` - キーフレーム
- `DELTA` - 差分フレーム

#### HardwareAccelerationEngine（独自拡張）

ハードウェアアクセラレーションエンジンを指定する ENUM：

- `NONE` - ソフトウェアエンコード/デコード（デフォルト）
- `APPLE_VIDEO_TOOLBOX` - macOS の VideoToolbox（H.264/H.265 のみ）
- `NVIDIA_VIDEO_CODEC` - NVIDIA GPU（未実装）
- `INTEL_VPL` - Intel VPL（未実装）
- `AMD_AMF` - AMD AMF（未実装）

**使用例**:

```python
from webcodecs import VideoEncoder, VideoEncoderConfig, HardwareAccelerationEngine

config: VideoEncoderConfig = {
    "codec": "h264",
    "width": 1920,
    "height": 1080,
    "hardware_acceleration_engine": HardwareAccelerationEngine.APPLE_VIDEO_TOOLBOX
}
```

#### LatencyMode

エンコーダーのレイテンシーモードを指定する ENUM：

- `QUALITY` - 品質優先モード（デフォルト）
- `REALTIME` - リアルタイム優先モード

**使用例**:

```python
from webcodecs import VideoEncoder, VideoEncoderConfig, LatencyMode

config: VideoEncoderConfig = {
    "codec": "av1",
    "width": 1920,
    "height": 1080,
    "latency_mode": LatencyMode.REALTIME
}
```

#### VideoEncoderBitrateMode

VideoEncoder のビットレートモードを指定する ENUM：

- `CONSTANT` - 固定ビットレート
- `VARIABLE` - 可変ビットレート（デフォルト）
- `QUANTIZER` - 量子化パラメータ指定

**使用例**:

```python
from webcodecs import VideoEncoder, VideoEncoderConfig, VideoEncoderBitrateMode

config: VideoEncoderConfig = {
    "codec": "av1",
    "width": 1920,
    "height": 1080,
    "bitrate_mode": VideoEncoderBitrateMode.CONSTANT,
    "bitrate": 1000000
}
```

#### BitrateMode

AudioEncoder のビットレートモードを指定する ENUM：

- `CONSTANT` - 固定ビットレート
- `VARIABLE` - 可変ビットレート（デフォルト）

**使用例**:

```python
from webcodecs import AudioEncoder, AudioEncoderConfig, BitrateMode

config: AudioEncoderConfig = {
    "codec": "opus",
    "sample_rate": 48000,
    "number_of_channels": 2,
    "bitrate_mode": BitrateMode.CONSTANT,
    "bitrate": 64000
}
```

#### AlphaOption

アルファチャンネルの処理方法を指定する ENUM：

- `KEEP` - アルファチャンネルを保持
- `DISCARD` - アルファチャンネルを破棄（デフォルト）

**使用例**:

```python
from webcodecs import VideoEncoder, VideoEncoderConfig, AlphaOption

config: VideoEncoderConfig = {
    "codec": "av1",
    "width": 1920,
    "height": 1080,
    "alpha": AlphaOption.DISCARD
}
```

#### HardwareAcceleration

ハードウェアアクセラレーションの優先度を指定する ENUM：

- `NO_PREFERENCE` - 指定なし（デフォルト）
- `PREFER_HARDWARE` - ハードウェア優先
- `PREFER_SOFTWARE` - ソフトウェア優先

**注**: このオプションは現在フィールド定義のみで、実際には `HardwareAccelerationEngine` 独自拡張を使用してください。

#### VideoColorPrimaries

色空間の原色を指定する ENUM：

- `BT709` - ITU-R BT.709
- `BT470BG` - ITU-R BT.470BG
- `SMPTE170M` - SMPTE 170M
- `BT2020` - ITU-R BT.2020
- `SMPTE432` - SMPTE ST 432-1 (DCI-P3)

#### VideoTransferCharacteristics

伝達特性を指定する ENUM：

- `BT709` - ITU-R BT.709
- `SMPTE170M` - SMPTE 170M
- `IEC61966_2_1` - IEC 61966-2-1 (sRGB)
- `LINEAR` - リニア
- `PQ` - SMPTE ST 2084 (PQ)
- `HLG` - ARIB STD-B67 (HLG)

#### VideoMatrixCoefficients

行列係数を指定する ENUM：

- `RGB` - RGB (行列変換なし)
- `BT709` - ITU-R BT.709
- `BT470BG` - ITU-R BT.470BG
- `SMPTE170M` - SMPTE 170M
- `BT2020_NCL` - ITU-R BT.2020 non-constant luminance

## 独自関数

### get_video_codec_capabilities()

**独自拡張関数 - WebCodecs API にはない**

実行環境で利用可能なビデオコーデックとハードウェアアクセラレーションエンジンの詳細情報を返します。

```python
def get_video_codec_capabilities() -> dict[HardwareAccelerationEngine, dict]
```

**戻り値**:

`HardwareAccelerationEngine` をキーとした辞書。各エンジンの情報には以下が含まれます：

- `available` (bool) - エンジンが利用可能かどうか
- `platform` (str) - 対応プラットフォーム (`"darwin"`, `"linux"`, `"windows"`, `"all"`)
- `codecs` (dict) - コーデック名をキーとした辞書
  - 各コーデックには以下が含まれる:
    - `encoder` (bool) - エンコーダーが利用可能かどうか
    - `decoder` (bool) - デコーダーが利用可能かどうか

**使用例 (macOS)**:

```python
from webcodecs import get_video_codec_capabilities, HardwareAccelerationEngine

capabilities = get_video_codec_capabilities()

# 結果の例
# {
#     HardwareAccelerationEngine.NONE: {
#         "available": True,
#         "platform": "all",
#         "codecs": {
#             "av01": {"encoder": True, "decoder": True}
#         }
#     },
#     HardwareAccelerationEngine.APPLE_VIDEO_TOOLBOX: {
#         "available": True,
#         "platform": "darwin",
#         "codecs": {
#             "avc1": {"encoder": True, "decoder": True},
#             "hvc1": {"encoder": True, "decoder": True}
#         }
#     }
# }

# 特定のコーデックが利用可能か確認
vt_info = capabilities.get(HardwareAccelerationEngine.APPLE_VIDEO_TOOLBOX)
if vt_info and vt_info["available"]:
    if "avc1" in vt_info["codecs"] and vt_info["codecs"]["avc1"]["encoder"]:
        print("H.264 ハードウェアエンコーダーが利用可能")
```

**使用例 (Linux/Windows)**:

```python
capabilities = get_video_codec_capabilities()

# 結果の例
# {
#     HardwareAccelerationEngine.NONE: {
#         "available": True,
#         "platform": "all",
#         "codecs": {
#             "av01": {"encoder": True, "decoder": True}
#         }
#     }
# }
```

**コーデック名について**:

WebCodecs の codec format 仕様に準拠した名前を使用しています：

- `av01` - AV1 (WebCodecs 標準)
- `avc1` - H.264 (WebCodecs 標準、`h264` ではない)
- `hvc1` - H.265/HEVC (WebCodecs 標準、`h265` や `hevc` ではない)

**実装詳細**:

- macOS では VideoToolbox の実際の利用可能性を `VTCompressionSessionCreate()` で確認
- 各プラットフォームで実際にサポートされているコーデックのみを返す
- 未実装のエンジン (NVIDIA、INTEL、AMD) は結果に含まれない

## 未実装の機能

### 実装しない機能

以下の機能は webcodecs-py では実装しません:

- **ImageDecoder**: 画像デコード機能は実装対象外（PIL/Pillow や OpenCV を使用してください）
- **CanvasImageSource**: VideoFrame の CanvasImageSource コンストラクタはブラウザ固有機能のため実装対象外

### 未実装の辞書型

| 辞書型 | 備考 |
|--------|------|
| `VideoColorSpaceInit` | `VideoColorSpace` クラスで代替 |
| `EncodedAudioChunkMetadata` | メタデータサポート未実装 |
| `EncodedVideoChunkMetadata` | メタデータサポート未実装 |
| `SvcOutputMetadata` | SVC サポート未実装 |
| `VideoFrameMetadata` | `metadata()` は dict を返すが TypedDict は未定義 |

### 未実装の列挙型

| 列挙型 | 備考 |
|--------|------|
| `PredefinedColorSpace` | 未実装 |

## サポートされているコーデック

### Video コーデック

| コーデック | エンコード | デコード | ライブラリ/API | プラットフォーム |
|----------|-----------|----------|---------------|----------------|
| AV1 | o | o | libaom / dav1d | All |
| H.264 | o | o | VideoToolbox* | macOS |
| H.265 | o | o | VideoToolbox* | macOS |

*ハードウェアアクセラレーション使用

### Audio コーデック

| コーデック | エンコード | デコード | ライブラリ | プラットフォーム |
|----------|-----------|----------|-----------|----------------|
| Opus | o | o | libopus | All |
| FLAC | o | o | libFLAC | All |

## パフォーマンス最適化

### 並列処理実装

WebCodecs 仕様に準拠した並列処理を全てのコーデックで実装：

- **非ブロッキング API**: `encode()` / `decode()` メソッドは即座に返る（< 1ms）
- **ワーカースレッド**: バックグラウンドでのエンコード/デコード処理
- **順序保証**: 出力フレーム/チャンクの順序を保持
- **キュー管理**: 複数のタスクを同時にスケジュール可能
- **スレッドセーフ**: 複数スレッドからの同時呼び出しに対応

```python
# 並列処理の例 - 前の処理を待たずに次の処理を開始
encoder.encode(frame1)  # 即座に返る
encoder.encode(frame2)  # frame1 の完了を待たない
encoder.encode(frame3)  # frame2 の完了を待たない

# キューサイズの確認
print(encoder.encode_queue_size)  # 処理待ちタスク数
```

### planes() によるビューアクセス

- `plane()`, `planes()` メソッドは内部バッファへのビューを返す（コピーなし）

### メモリ管理

- Python のガベージコレクションと C++ オブジェクトのライフサイクル管理を適切に統合
- `close()` メソッドによる明示的なリソース解放をサポート
- ワーカースレッドでの shared_ptr 使用によるメモリ安全性の確保

## メモリ管理とパフォーマンス

### メモリ管理の実装方式

1. **初期化時**：データのコピーが発生（安全性重視）
2. **planes() メソッド**：内部データのビューを返す（コピーなし）
3. **copy_to() メソッド**：destination バッファに書き込み（WebCodecs API 準拠）
4. **エンコーダー/デコーダー**：自動的に内部コピーを作成（セグフォ防止）

### 使い分けガイドライン

| 用途 | 推奨メソッド | 理由 |
|------|------------|------|
| データの読み取り | `planes()` | 内部データへの高速アクセス |
| データの保存・処理 | `copy_to()` | 独立したコピーが必要な場合 |
| エンコード/デコード | 通常通り `encode()` / `decode()` | 自動的に安全なコピーを作成 |
| VideoFrame のフォーマット変換 | `copy_to(dest, {"format": ...})` | WebCodecs API 準拠の変換 |
| AudioData のフォーマット変換 | `copy_to(dest, {"format": ...})` | WebCodecs API 準拠の変換 |

### パフォーマンスの考慮事項

- **初期化コスト**：VideoFrame 作成時に 1 回のコピーが発生
- **エンコード時**：内部で安全なコピーを自動作成（追加コピー）
- **planes() 使用時**：コピーなし（高速）
- **copy_to() 使用時**：destination バッファに書き込み（WebCodecs API 準拠）

この実装により、セグメンテーションフォルトを防ぎつつ、読み取り操作では高速なアクセスを提供しています。

## 注意事項

1. **メモリ管理**
   - planes() でビューを取得した場合、VideoFrame/AudioData の生存期間に注意
   - ハードウェアエンコーダーを使用する場合は copy_to() を推奨

2. **スレッドセーフティ**
   - エンコーダー/デコーダーはシングルスレッドでの使用を想定
   - 複数スレッドから同時アクセスする場合は外部で同期が必要

3. **プラットフォーム依存**
   - VideoToolbox (H.264/H.265) は macOS のみ

4. **H.264/H.265 ビットストリームフォーマット**
   - **VideoDecoder は Annex B 形式のみ対応**
     - スタートコード（0x00 0x00 0x01 または 0x00 0x00 0x00 0x01）で区切られた NAL ユニット
     - キーフレームには SPS/PPS（H.264）または VPS/SPS/PPS（H.265）が含まれる必要あり
   - **VideoEncoder はデフォルトで length-prefixed 形式（avc/hevc）を出力**
     - WebCodecs API 仕様に準拠
     - Annex B 形式で出力する場合は `avc: {"format": "annexb"}` または `hevc: {"format": "annexb"}` を指定
   - **用途別推奨設定**:
     - ライブストリーミング（WebRTC、RTP）: `"avc": {"format": "annexb"}` を指定
     - MP4 ファイル保存: デフォルト（avc/hevc 形式）を使用し、そのまま muxer に渡す
     - エンコード→デコードのパイプライン: `"avc": {"format": "annexb"}` を指定

## 今後の実装予定

1. **色空間サポート**
   - VideoColorSpace インターフェースの完全実装
   - 色空間変換の改善

2. **メタデータサポート**
   - エンコード/デコード時のメタデータ処理
   - フレームメタデータの管理

3. **追加コーデックサポート**
   - AAC オーディオコーデック (Apple Audio Toolbox)

4. **ハードウェアアクセラレーション**
   - Windows/Linux でのハードウェアアクセラレーション対応

## 参考資料

- [W3C WebCodecs API 仕様](https://w3c.github.io/webcodecs/)
- [WebCodecs Explainer](https://github.com/w3c/webcodecs/blob/main/explainer.md)
- [WebCodecs インターフェース定義](./WEBCODECS_INTERFACE.md)
