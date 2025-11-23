"""WebCodecs API 準拠の Python バインディング"""

from enum import Enum
from typing import TypedDict, NotRequired, Literal

from ._webcodecs_py import (
    # Video types
    VideoPixelFormat,
    VideoFrame,
    PlaneLayout,
    DOMRect,
    VideoColorSpace,
    # Audio types
    AudioSampleFormat,
    AudioData,
    # Encoded types
    EncodedVideoChunkType,
    EncodedVideoChunk,
    EncodedAudioChunkType,
    EncodedAudioChunk,
    # Encoder/Decoder types
    VideoEncoder,
    AudioEncoder,
    VideoDecoder,
    AudioDecoder,
    # Codec state and enums
    CodecState,
    LatencyMode,
    VideoEncoderBitrateMode,
    BitrateMode,
    AlphaOption,
    HardwareAcceleration,
    VideoColorPrimaries,
    VideoTransferCharacteristics,
    VideoMatrixCoefficients,
    # Codec capabilities
    _HWAccelerationEngine,
    _get_video_codec_capabilities_impl,
)


# ハードウェアアクセラレーションエンジン
class HardwareAccelerationEngine(str, Enum):
    """ハードウェアアクセラレーションエンジンの種類"""

    NONE = "none"
    APPLE_VIDEO_TOOLBOX = "apple_video_toolbox"
    NVIDIA_VIDEO_CODEC = "nvidia_video_codec"
    INTEL_VPL = "intel_vpl"
    AMD_AMF = "amd_amf"


# TypedDict 定義


class EncodedAudioChunkInit(TypedDict):
    """EncodedAudioChunk コンストラクタの初期化パラメータ (WebCodecs API 準拠)"""

    # 必須フィールド
    type: EncodedAudioChunkType
    timestamp: int  # マイクロ秒
    data: bytes
    # オプションフィールド
    duration: NotRequired[int]  # マイクロ秒


class EncodedVideoChunkInit(TypedDict):
    """EncodedVideoChunk コンストラクタの初期化パラメータ (WebCodecs API 準拠)"""

    # 必須フィールド
    type: EncodedVideoChunkType
    timestamp: int  # マイクロ秒
    data: bytes
    # オプションフィールド
    duration: NotRequired[int]  # マイクロ秒


class VideoFrameBufferInit(TypedDict, total=False):
    """VideoFrame コンストラクタの初期化パラメータ (WebCodecs API 準拠)"""

    # 必須フィールド
    format: VideoPixelFormat | str  # VideoPixelFormat またはフォーマット文字列
    coded_width: int
    coded_height: int
    timestamp: int  # マイクロ秒
    # オプションフィールド
    duration: int | None  # マイクロ秒
    layout: list[PlaneLayout] | None  # PlaneLayout のリスト
    visible_rect: dict | None  # {"x": float, "y": float, "width": float, "height": float}
    display_width: int | None
    display_height: int | None
    color_space: (
        dict | None
    )  # {"primaries": str, "transfer": str, "matrix": str, "full_range": bool}
    rotation: int | None  # 0, 90, 180, 270
    flip: bool | None
    metadata: dict | None


# AVC エンコーダー設定 (WebCodecs AVC Codec Registration 準拠)
class AvcEncoderConfig(TypedDict, total=False):
    """AVC (H.264) エンコーダーの設定"""

    format: Literal["annexb", "avc"] | None  # デフォルト: "avc"


# HEVC エンコーダー設定 (WebCodecs HEVC Codec Registration 準拠)
class HevcEncoderConfig(TypedDict, total=False):
    """HEVC エンコーダーの設定"""

    format: Literal["annexb", "hevc"] | None  # デフォルト: "hevc"


class VideoEncoderConfig(TypedDict):
    """VideoEncoder.configure() の引数"""

    # 必須フィールド
    codec: str
    width: int
    height: int
    # オプションフィールド
    display_width: NotRequired[int | None]
    display_height: NotRequired[int | None]
    bitrate: NotRequired[int | None]
    framerate: NotRequired[float | None]
    hardware_acceleration: NotRequired[HardwareAcceleration | None]
    bitrate_mode: NotRequired[VideoEncoderBitrateMode | None]
    latency_mode: NotRequired[LatencyMode | None]
    content_hint: NotRequired[str | None]
    scalability_mode: NotRequired[str | None]
    alpha: NotRequired[AlphaOption | None]
    # 独自拡張
    hardware_acceleration_engine: NotRequired[HardwareAccelerationEngine | None]
    # AVC 固有のオプション (WebCodecs AVC Codec Registration 準拠)
    avc: NotRequired[AvcEncoderConfig | None]
    # HEVC 固有のオプション (WebCodecs HEVC Codec Registration 準拠)
    hevc: NotRequired[HevcEncoderConfig | None]


class VideoDecoderConfig(TypedDict):
    """VideoDecoder.configure() の引数"""

    # 必須フィールド
    codec: str
    # オプションフィールド
    coded_width: NotRequired[int | None]
    coded_height: NotRequired[int | None]
    display_aspect_width: NotRequired[int | None]
    display_aspect_height: NotRequired[int | None]
    description: NotRequired[str | None]
    hardware_acceleration: NotRequired[str | None]
    optimize_for_latency: NotRequired[bool | None]
    color_space: NotRequired[str | None]
    rotation: NotRequired[int | None]
    flip: NotRequired[bool | None]


class OpusEncoderConfig(TypedDict):
    """Opus エンコーダーの設定"""

    format: NotRequired[Literal["opus", "ogg"] | None]  # 出力フォーマット
    signal: NotRequired[Literal["auto", "music", "voice"] | None]  # 信号タイプ
    application: NotRequired[Literal["voip", "audio", "lowdelay"] | None]  # アプリケーションモード
    frame_duration: NotRequired[int | None]  # フレーム期間 (マイクロ秒)
    complexity: NotRequired[int | None]  # 0-10 (高い値は品質が良いが処理が遅い)
    packetlossperc: NotRequired[int | None]  # 0-100 (パケットロス率)
    useinbandfec: NotRequired[bool | None]  # インバンド FEC
    usedtx: NotRequired[bool | None]  # DTX (不連続伝送)


class FlacEncoderConfig(TypedDict):
    """FLAC エンコーダーの設定"""

    block_size: NotRequired[int | None]  # 0 でエンコーダーが自動推定
    compress_level: NotRequired[int | None]  # 0-8 (0: 最速、8: 最高圧縮)


class AudioEncoderConfig(TypedDict):
    """AudioEncoder.configure() の引数"""

    # 必須フィールド
    codec: str
    sample_rate: int
    number_of_channels: int
    # オプションフィールド
    bitrate: NotRequired[int | None]
    bitrate_mode: NotRequired[BitrateMode | None]
    # Opus 固有のオプション
    opus: NotRequired[OpusEncoderConfig | None]
    # FLAC 固有のオプション
    flac: NotRequired[FlacEncoderConfig | None]


class AudioDecoderConfig(TypedDict):
    """AudioDecoder.configure() の引数"""

    # 必須フィールド
    codec: str
    sample_rate: int
    number_of_channels: int
    # オプションフィールド
    description: NotRequired[str | None]


class AudioDataInit(TypedDict):
    """AudioData コンストラクタの初期化パラメータ (WebCodecs API 準拠)"""

    # 必須フィールド
    format: AudioSampleFormat
    sample_rate: int
    number_of_frames: int
    number_of_channels: int
    timestamp: int  # マイクロ秒
    data: "numpy.typing.NDArray"  # type: ignore[name-defined]


class AudioDataCopyToOptions(TypedDict):
    """AudioData.copy_to() / allocation_size() のオプション (WebCodecs API 準拠)"""

    # 必須フィールド
    plane_index: int  # コピー元のプレーンインデックス
    # オプションフィールド
    frame_offset: NotRequired[int]  # デフォルト 0
    frame_count: NotRequired[int | None]  # 省略時は残り全フレーム
    format: NotRequired[AudioSampleFormat | None]  # 省略時は元フォーマット（未実装）


class VideoFrameCopyToOptions(TypedDict, total=False):
    """VideoFrame.copy_to() のオプション"""

    rect: DOMRect | None
    layout: list[PlaneLayout] | None
    format: VideoPixelFormat | None
    colorSpace: str | None


# VideoEncoder.encode() のオプション
class VideoEncoderEncodeOptionsForAv1(TypedDict, total=False):
    """AV1 エンコードオプション (WebCodecs AV1 Codec Registration 準拠)"""

    quantizer: int | None  # 0-63 の範囲


class VideoEncoderEncodeOptionsForAvc(TypedDict, total=False):
    """AVC (H.264) エンコードオプション (WebCodecs AVC Codec Registration 準拠)"""

    quantizer: int | None  # 0-51 の範囲


class VideoEncoderEncodeOptionsForHevc(TypedDict, total=False):
    """HEVC (H.265) エンコードオプション (WebCodecs HEVC Codec Registration 準拠)"""

    quantizer: int | None  # 0-51 の範囲


class VideoEncoderEncodeOptions(TypedDict, total=False):
    """VideoEncoder.encode() のオプション"""

    keyFrame: bool | None  # キーフレームを強制
    av1: VideoEncoderEncodeOptionsForAv1 | None  # AV1 固有のオプション
    avc: VideoEncoderEncodeOptionsForAvc | None  # AVC 固有のオプション
    hevc: VideoEncoderEncodeOptionsForHevc | None  # HEVC 固有のオプション


# Support 型定義（is_config_supported の戻り値）
class VideoEncoderSupport(TypedDict):
    """VideoEncoder.is_config_supported() の戻り値"""

    supported: bool
    config: VideoEncoderConfig


class VideoDecoderSupport(TypedDict):
    """VideoDecoder.is_config_supported() の戻り値"""

    supported: bool
    config: VideoDecoderConfig


class AudioEncoderSupport(TypedDict):
    """AudioEncoder.is_config_supported() の戻り値"""

    supported: bool
    config: AudioEncoderConfig


class AudioDecoderSupport(TypedDict):
    """AudioDecoder.is_config_supported() の戻り値"""

    supported: bool
    config: AudioDecoderConfig


def get_video_codec_capabilities() -> dict[HardwareAccelerationEngine, dict]:
    """
    実行環境で利用可能なビデオコーデックとその実装方法の詳細情報を返す

    Returns:
        dict[HardwareAccelerationEngine, dict]: ハードウェアアクセラレーションエンジンをキーとした辞書
            各エンジンの情報には以下が含まれる:
            - available (bool): エンジンが利用可能かどうか
            - platform (str): 対応プラットフォーム ("darwin", "linux", "all")
            - codecs (dict): コーデック名をキーとした辞書
                各コーデックには以下が含まれる:
                - encoder (bool): エンコーダーが利用可能かどうか
                - decoder (bool): デコーダーが利用可能かどうか
    """
    # C++ 実装から取得
    cpp_capabilities = _get_video_codec_capabilities_impl()

    # C++ の enum を Python の HardwareAccelerationEngine に変換
    engine_mapping = {
        _HWAccelerationEngine.NONE: HardwareAccelerationEngine.NONE,
        _HWAccelerationEngine.APPLE_VIDEO_TOOLBOX: HardwareAccelerationEngine.APPLE_VIDEO_TOOLBOX,
        _HWAccelerationEngine.NVIDIA_VIDEO_CODEC: HardwareAccelerationEngine.NVIDIA_VIDEO_CODEC,
        _HWAccelerationEngine.INTEL_VPL: HardwareAccelerationEngine.INTEL_VPL,
        _HWAccelerationEngine.AMD_AMF: HardwareAccelerationEngine.AMD_AMF,
    }

    capabilities: dict[HardwareAccelerationEngine, dict] = {}
    for cpp_engine, engine_support in cpp_capabilities.items():
        python_engine = engine_mapping[cpp_engine]

        # CodecSupport を dict に変換
        codecs_dict = {}
        for codec_name, codec_support in engine_support.codecs.items():
            codecs_dict[codec_name] = {
                "encoder": codec_support.encoder,
                "decoder": codec_support.decoder,
            }

        capabilities[python_engine] = {
            "available": engine_support.available,
            "platform": engine_support.platform,
            "codecs": codecs_dict,
        }

    return capabilities


__all__ = [
    # Video types
    "VideoPixelFormat",
    "VideoFrame",
    "VideoFrameBufferInit",
    "PlaneLayout",
    "VideoColorSpace",
    "DOMRect",
    # Audio types
    "AudioSampleFormat",
    "AudioData",
    "AudioDataInit",
    # Encoded types
    "EncodedVideoChunkType",
    "EncodedVideoChunk",
    "EncodedVideoChunkInit",
    "EncodedAudioChunkType",
    "EncodedAudioChunk",
    "EncodedAudioChunkInit",
    # Encoder/Decoder types
    "VideoEncoder",
    "AudioEncoder",
    "VideoDecoder",
    "AudioDecoder",
    # Config types
    "VideoEncoderConfig",
    "VideoDecoderConfig",
    "AudioEncoderConfig",
    "AudioDecoderConfig",
    "OpusEncoderConfig",
    "FlacEncoderConfig",
    "AvcEncoderConfig",
    "HevcEncoderConfig",
    # Options types
    "AudioDataCopyToOptions",
    "VideoFrameCopyToOptions",
    "VideoEncoderEncodeOptions",
    "VideoEncoderEncodeOptionsForAv1",
    "VideoEncoderEncodeOptionsForAvc",
    "VideoEncoderEncodeOptionsForHevc",
    # Support types
    "VideoEncoderSupport",
    "VideoDecoderSupport",
    "AudioEncoderSupport",
    "AudioDecoderSupport",
    # Enums
    "CodecState",
    "LatencyMode",
    "VideoEncoderBitrateMode",
    "BitrateMode",
    "AlphaOption",
    "HardwareAcceleration",
    "VideoColorPrimaries",
    "VideoTransferCharacteristics",
    "VideoMatrixCoefficients",
    "HardwareAccelerationEngine",
    # Functions
    "get_video_codec_capabilities",
]

# WebCodecs 互換: encode(frame, options?: dict)
# C++ 側は encode(frame) と encode(frame, options: dict) を公開しているため、
# Python レイヤで options の処理を行う
