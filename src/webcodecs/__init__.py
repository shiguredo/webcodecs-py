"""WebCodecs API 準拠の Python バインディング"""

from typing import Any, TypedDict, NotRequired, Literal

import numpy.typing

from .webcodecs_ext import (
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
    # Image types
    ImageDecoder,
    ImageTrack,
    ImageTrackList,
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
    HardwareAccelerationEngine,
    # stubgen はプライベート関数をスキップするため type: ignore が必要
    _get_video_codec_capabilities_impl,  # type: ignore[attr-defined]
    # Header parser (独自拡張)
    AVCNalUnitType,
    HEVCNalUnitType,
    AVCSpsInfo,
    AVCPpsInfo,
    AVCNalUnitHeader,
    AVCAnnexBInfo,
    AVCDescriptionInfo,
    HEVCVpsInfo,
    HEVCSpsInfo,
    HEVCPpsInfo,
    HEVCNalUnitHeader,
    HEVCAnnexBInfo,
    HEVCDescriptionInfo,
    parse_avc_annexb,
    parse_avc_description,
    parse_hevc_annexb,
    parse_hevc_description,
    parse_avc_sps,
    parse_avc_pps,
    parse_hevc_vps,
    parse_hevc_sps,
    parse_hevc_pps,
)


# TypedDict 定義


class EncodedAudioChunkInit(TypedDict):
    """EncodedAudioChunk コンストラクタの初期化パラメータ (WebCodecs API 準拠)"""

    # 必須フィールド
    type: EncodedAudioChunkType
    # マイクロ秒
    timestamp: int
    data: bytes
    # オプションフィールド
    # マイクロ秒
    duration: NotRequired[int]


class EncodedVideoChunkInit(TypedDict):
    """EncodedVideoChunk コンストラクタの初期化パラメータ (WebCodecs API 準拠)"""

    # 必須フィールド
    type: EncodedVideoChunkType
    # マイクロ秒
    timestamp: int
    data: bytes
    # オプションフィールド
    # マイクロ秒
    duration: NotRequired[int]


class VideoFrameMetadata(TypedDict, total=False):
    """WebCodecs VideoFrame Metadata Registry 準拠のメタデータフィールド

    参照: https://w3c.github.io/webcodecs/video_frame_metadata_registry.html
    すべてのフィールドは MediaCapture Extensions で定義されています。
    """

    # 型が明確なフィールド
    # DOMHighResTimeStamp (マイクロ秒)
    capture_time: float
    # DOMHighResTimeStamp (マイクロ秒)
    receive_time: float
    # RTP タイムスタンプ
    rtp_timestamp: int
    # 型が不明確なフィールド（仕様が明確になるまで Any）
    # 顔セグメンテーション
    segments: Any
    # 背景ぼかし効果ステータス
    background_blur: Any
    # 背景セグメンテーションマスク
    background_segmentation_mask: Any


class VideoFrameBufferInit(TypedDict, total=False):
    """VideoFrame コンストラクタの初期化パラメータ (WebCodecs API 準拠)"""

    # 必須フィールド
    # VideoPixelFormat またはフォーマット文字列
    format: VideoPixelFormat | str
    coded_width: int
    coded_height: int
    # マイクロ秒
    timestamp: int
    # オプションフィールド
    # マイクロ秒
    duration: int | None
    # PlaneLayout のリスト
    layout: list[PlaneLayout] | None
    # {"x": float, "y": float, "width": float, "height": float}
    visible_rect: dict | None
    display_width: int | None
    display_height: int | None
    # {"primaries": str, "transfer": str, "matrix": str, "full_range": bool}
    color_space: dict | None
    # 0, 90, 180, 270
    rotation: int | None
    flip: bool | None
    metadata: VideoFrameMetadata | dict | None


# AVC エンコーダー設定 (WebCodecs AVC Codec Registration 準拠)
class AvcEncoderConfig(TypedDict, total=False):
    """AVC (H.264) エンコーダーの設定"""

    # デフォルト: "avc"
    format: Literal["annexb", "avc"] | None


# HEVC エンコーダー設定 (WebCodecs HEVC Codec Registration 準拠)
class HevcEncoderConfig(TypedDict, total=False):
    """HEVC エンコーダーの設定"""

    # デフォルト: "hevc"
    format: Literal["annexb", "hevc"] | None


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
    description: NotRequired[bytes | None]
    hardware_acceleration: NotRequired[str | None]
    optimize_for_latency: NotRequired[bool | None]
    color_space: NotRequired[str | None]
    rotation: NotRequired[int | None]
    flip: NotRequired[bool | None]
    # 独自拡張
    hardware_acceleration_engine: NotRequired[HardwareAccelerationEngine | None]


class OpusEncoderConfig(TypedDict):
    """Opus エンコーダーの設定"""

    # 出力フォーマット
    format: NotRequired[Literal["opus", "ogg"] | None]
    # 信号タイプ
    signal: NotRequired[Literal["auto", "music", "voice"] | None]
    # アプリケーションモード
    application: NotRequired[Literal["voip", "audio", "lowdelay"] | None]
    # フレーム期間 (マイクロ秒)
    frame_duration: NotRequired[int | None]
    # 0-10 (高い値は品質が良いが処理が遅い)
    complexity: NotRequired[int | None]
    # 0-100 (パケットロス率)
    packetlossperc: NotRequired[int | None]
    # インバンド FEC
    useinbandfec: NotRequired[bool | None]
    # DTX (不連続伝送)
    usedtx: NotRequired[bool | None]


class FlacEncoderConfig(TypedDict):
    """FLAC エンコーダーの設定"""

    # 0 でエンコーダーが自動推定
    block_size: NotRequired[int | None]
    # 0-8 (0: 最速、8: 最高圧縮)
    compress_level: NotRequired[int | None]


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
    description: NotRequired[bytes | None]


class AudioDataInit(TypedDict):
    """AudioData コンストラクタの初期化パラメータ (WebCodecs API 準拠)"""

    # 必須フィールド
    format: AudioSampleFormat
    sample_rate: int
    number_of_frames: int
    number_of_channels: int
    # マイクロ秒
    timestamp: int
    # type: ignore[name-defined]
    data: "numpy.typing.NDArray"


class AudioDataCopyToOptions(TypedDict):
    """AudioData.copy_to() / allocation_size() のオプション (WebCodecs API 準拠)"""

    # 必須フィールド
    # コピー元のプレーンインデックス
    plane_index: int
    # オプションフィールド
    # デフォルト 0
    frame_offset: NotRequired[int]
    # 省略時は残り全フレーム
    frame_count: NotRequired[int | None]
    # 省略時は元フォーマット（未実装）
    format: NotRequired[AudioSampleFormat | None]


class VideoFrameCopyToOptions(TypedDict, total=False):
    """VideoFrame.copy_to() のオプション"""

    rect: DOMRect | None
    layout: list[PlaneLayout] | None
    format: VideoPixelFormat | None
    color_space: str | None


# VideoEncoder.encode() のオプション
class VideoEncoderEncodeOptionsForAv1(TypedDict, total=False):
    """AV1 エンコードオプション (WebCodecs AV1 Codec Registration 準拠)"""

    # 0-63 の範囲
    quantizer: int | None


class VideoEncoderEncodeOptionsForAvc(TypedDict, total=False):
    """AVC (H.264) エンコードオプション (WebCodecs AVC Codec Registration 準拠)"""

    # 0-51 の範囲
    quantizer: int | None


class VideoEncoderEncodeOptionsForHevc(TypedDict, total=False):
    """HEVC (H.265) エンコードオプション (WebCodecs HEVC Codec Registration 準拠)"""

    # 0-51 の範囲
    quantizer: int | None


class VideoEncoderEncodeOptionsForVp8(TypedDict, total=False):
    """VP8 エンコードオプション (WebCodecs VP8 Codec Registration 準拠)"""

    # 0-63 の範囲
    quantizer: int | None


class VideoEncoderEncodeOptionsForVp9(TypedDict, total=False):
    """VP9 エンコードオプション (WebCodecs VP9 Codec Registration 準拠)"""

    # 0-63 の範囲
    quantizer: int | None


class VideoEncoderEncodeOptions(TypedDict, total=False):
    """VideoEncoder.encode() のオプション"""

    # キーフレームを強制
    key_frame: bool | None
    # AV1 固有のオプション
    av1: VideoEncoderEncodeOptionsForAv1 | None
    # AVC 固有のオプション
    avc: VideoEncoderEncodeOptionsForAvc | None
    # HEVC 固有のオプション
    hevc: VideoEncoderEncodeOptionsForHevc | None
    # VP8 固有のオプション
    vp8: VideoEncoderEncodeOptionsForVp8 | None
    # VP9 固有のオプション
    vp9: VideoEncoderEncodeOptionsForVp9 | None


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


# ImageDecoder 関連の型定義


class ImageDecoderInit(TypedDict, total=False):
    """ImageDecoder コンストラクタの初期化パラメータ (WebCodecs API 準拠)"""

    # 必須フィールド
    type: str
    data: bytes
    # オプションフィールド
    color_space_conversion: Literal["default", "none"] | None
    desired_width: int | None
    desired_height: int | None
    prefer_animation: bool | None


class ImageDecodeOptions(TypedDict, total=False):
    """ImageDecoder.decode() のオプション (WebCodecs API 準拠)"""

    frame_index: int
    complete_frames_only: bool


class ImageDecodeResult(TypedDict):
    """ImageDecoder.decode() の戻り値 (WebCodecs API 準拠)"""

    image: VideoFrame
    complete: bool


# Metadata 型定義 (VideoEncoder output callback の第 2 引数)
class EncodedVideoChunkMetadataDecoderConfig(TypedDict, total=False):
    """EncodedVideoChunkMetadata の decoder_config"""

    codec: str
    coded_width: int
    coded_height: int
    # avcC / hvcC / av1C などのコーデック固有データ
    description: bytes


class EncodedVideoChunkMetadata(TypedDict, total=False):
    """VideoEncoder の output callback で提供される metadata

    キーフレーム時のみ decoder_config が含まれる。
    """

    decoder_config: EncodedVideoChunkMetadataDecoderConfig


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

    capabilities: dict[HardwareAccelerationEngine, dict] = {}
    for engine, engine_support in cpp_capabilities.items():
        # CodecSupport を dict に変換
        codecs_dict = {}
        for codec_name, codec_support in engine_support.codecs.items():
            codecs_dict[codec_name] = {
                "encoder": codec_support.encoder,
                "decoder": codec_support.decoder,
            }

        capabilities[engine] = {
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
    "VideoFrameMetadata",
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
    # Image types
    "ImageDecoder",
    "ImageTrack",
    "ImageTrackList",
    "ImageDecoderInit",
    "ImageDecodeOptions",
    "ImageDecodeResult",
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
    "VideoEncoderEncodeOptionsForVp8",
    "VideoEncoderEncodeOptionsForVp9",
    # Support types
    "VideoEncoderSupport",
    "VideoDecoderSupport",
    "AudioEncoderSupport",
    "AudioDecoderSupport",
    # Metadata types
    "EncodedVideoChunkMetadata",
    "EncodedVideoChunkMetadataDecoderConfig",
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
    # Header parser (独自拡張)
    "AVCNalUnitType",
    "HEVCNalUnitType",
    "AVCSpsInfo",
    "AVCPpsInfo",
    "AVCNalUnitHeader",
    "AVCAnnexBInfo",
    "AVCDescriptionInfo",
    "HEVCVpsInfo",
    "HEVCSpsInfo",
    "HEVCPpsInfo",
    "HEVCNalUnitHeader",
    "HEVCAnnexBInfo",
    "HEVCDescriptionInfo",
    "parse_avc_annexb",
    "parse_avc_description",
    "parse_hevc_annexb",
    "parse_hevc_description",
    "parse_avc_sps",
    "parse_avc_pps",
    "parse_hevc_vps",
    "parse_hevc_sps",
    "parse_hevc_pps",
]

# WebCodecs 互換: encode(frame, options?: dict)
# C++ 側は encode(frame) と encode(frame, options: dict) を公開しているため、
# Python レイヤで options の処理を行う
