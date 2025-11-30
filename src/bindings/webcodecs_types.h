#pragma once

#include <nanobind/nanobind.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

namespace nb = nanobind;

// WebCodecs API の PlaneLayout 構造体
struct PlaneLayout {
  uint32_t offset;
  uint32_t stride;

  PlaneLayout() : offset(0), stride(0) {}
  PlaneLayout(uint32_t offset, uint32_t stride)
      : offset(offset), stride(stride) {}
};

// WebCodecs API の DOMRect 構造体
struct DOMRect {
  double x;
  double y;
  double width;
  double height;

  DOMRect() : x(0), y(0), width(0), height(0) {}
  DOMRect(double x, double y, double width, double height)
      : x(x), y(y), width(width), height(height) {}
};

// WebCodecs API の VideoColorSpace 構造体
struct VideoColorSpace {
  std::optional<std::string> primaries;
  std::optional<std::string> transfer;
  std::optional<std::string> matrix;
  std::optional<bool> full_range;

  VideoColorSpace() = default;
};

// WebCodecs API の LatencyMode 列挙型
enum class LatencyMode {
  QUALITY,   // 品質優先モード
  REALTIME,  // リアルタイム優先モード
};

// WebCodecs API の VideoEncoderBitrateMode 列挙型
enum class VideoEncoderBitrateMode {
  CONSTANT,   // 固定ビットレート
  VARIABLE,   // 可変ビットレート
  QUANTIZER,  // 量子化パラメータ指定
};

// WebCodecs API の BitrateMode 列挙型 (AudioEncoder 用)
enum class BitrateMode {
  CONSTANT,  // 固定ビットレート
  VARIABLE,  // 可変ビットレート
};

// WebCodecs API の AlphaOption 列挙型
enum class AlphaOption {
  KEEP,     // アルファチャンネルを保持
  DISCARD,  // アルファチャンネルを破棄
};

// WebCodecs API の HardwareAcceleration 列挙型
enum class HardwareAcceleration {
  NO_PREFERENCE,    // 指定なし
  PREFER_HARDWARE,  // ハードウェア優先
  PREFER_SOFTWARE,  // ソフトウェア優先
};

// WebCodecs API の VideoColorPrimaries 列挙型
enum class VideoColorPrimaries {
  BT709,      // ITU-R BT.709
  BT470BG,    // ITU-R BT.470BG
  SMPTE170M,  // SMPTE 170M
  BT2020,     // ITU-R BT.2020
  SMPTE432,   // SMPTE ST 432-1 (DCI-P3)
};

// WebCodecs API の VideoTransferCharacteristics 列挙型
enum class VideoTransferCharacteristics {
  BT709,         // ITU-R BT.709
  SMPTE170M,     // SMPTE 170M
  IEC61966_2_1,  // IEC 61966-2-1 (sRGB)
  LINEAR,        // リニア
  PQ,            // SMPTE ST 2084 (PQ)
  HLG,           // ARIB STD-B67 (HLG)
};

// WebCodecs API の VideoMatrixCoefficients 列挙型
enum class VideoMatrixCoefficients {
  RGB,         // RGB (行列変換なし)
  BT709,       // ITU-R BT.709
  BT470BG,     // ITU-R BT.470BG
  SMPTE170M,   // SMPTE 170M
  BT2020_NCL,  // ITU-R BT.2020 non-constant luminance
};

// WebCodecs API の VideoFrameBufferInit 構造体
struct VideoFrameBufferInit {
  // 必須フィールド
  std::string format;  // WebCodecs 互換性のため文字列としての VideoPixelFormat
  uint32_t coded_width;
  uint32_t coded_height;
  int64_t timestamp;

  // オプショナルフィールド
  std::optional<uint64_t> duration;
  std::optional<std::vector<PlaneLayout>> layout;
  std::optional<DOMRect> visible_rect;
  std::optional<uint32_t> display_width;
  std::optional<uint32_t> display_height;
  std::optional<VideoColorSpace> color_space;
  std::optional<uint32_t> rotation;  // 0, 90, 180, 270
  std::optional<bool> flip;          // 水平反転
  std::optional<nb::dict> metadata;  // 任意のメタデータ

  VideoFrameBufferInit() : coded_width(0), coded_height(0), timestamp(0) {}

  // 検証メソッド
  void validate() const;
};

// WebCodecs API の VideoEncoderConfig 構造体
struct VideoEncoderConfig {
  // 必須フィールド
  std::string codec;
  uint32_t width;
  uint32_t height;

  // オプショナルフィールド
  std::optional<uint32_t> display_width;
  std::optional<uint32_t> display_height;
  std::optional<uint64_t> bitrate;
  std::optional<double> framerate;
  HardwareAcceleration hardware_acceleration =
      HardwareAcceleration::NO_PREFERENCE;
  AlphaOption alpha = AlphaOption::DISCARD;
  std::optional<std::string> scalability_mode;
  VideoEncoderBitrateMode bitrate_mode = VideoEncoderBitrateMode::VARIABLE;
  LatencyMode latency_mode = LatencyMode::QUALITY;
  std::optional<std::string> content_hint;

  // 独自拡張 (ハードウェアアクセラレーション用)
  std::string hardware_acceleration_engine =
      "none";  // "none", "apple_video_toolbox", etc.

  // AVC 固有のオプション (WebCodecs AVC Codec Registration 準拠)
  std::string avc_format = "avc";  // "annexb", "avc" (デフォルト: "avc")

  // HEVC 固有のオプション (WebCodecs HEVC Codec Registration 準拠)
  std::string hevc_format = "hevc";  // "annexb", "hevc" (デフォルト: "hevc")

  VideoEncoderConfig() : width(0), height(0) {}
};

// WebCodecs API の VideoDecoderConfig 構造体
struct VideoDecoderConfig {
  // 必須フィールド
  std::string codec;

  // オプショナルフィールド
  std::optional<std::vector<uint8_t>> description;  // コーデック固有の設定
  std::optional<uint32_t> coded_width;
  std::optional<uint32_t> coded_height;
  std::optional<uint32_t> display_aspect_width;
  std::optional<uint32_t> display_aspect_height;
  std::optional<VideoColorSpace> color_space;
  std::string hardware_acceleration = "no-preference";
  std::optional<bool> optimize_for_latency;
  double rotation = 0;
  bool flip = false;

  VideoDecoderConfig() = default;
};

// WebCodecs API の OpusEncoderConfig 構造体
struct OpusEncoderConfig {
  std::string format = "opus";         // "opus" または "ogg"
  std::string signal = "auto";         // "auto", "music", "voice"
  std::string application = "audio";   // "voip", "audio", "lowdelay"
  uint64_t frame_duration = 20000;     // マイクロ秒 (デフォルト 20ms)
  std::optional<uint32_t> complexity;  // 0-10
  uint32_t packetlossperc = 0;         // 0-100
  bool useinbandfec = false;           // インバンド FEC
  bool usedtx = false;                 // DTX (不連続伝送)

  OpusEncoderConfig() = default;
};

// WebCodecs API の FlacEncoderConfig 構造体
struct FlacEncoderConfig {
  uint32_t block_size = 0;      // 0 でエンコーダーが自動推定
  uint32_t compress_level = 5;  // 0-8 (0: 最速、8: 最高圧縮)

  FlacEncoderConfig() = default;
};

// WebCodecs API の AudioEncoderConfig 構造体
struct AudioEncoderConfig {
  // 必須フィールド
  std::string codec;
  uint32_t sample_rate;
  uint32_t number_of_channels;

  // オプショナルフィールド
  std::optional<uint64_t> bitrate;
  BitrateMode bitrate_mode = BitrateMode::VARIABLE;

  // コーデック固有のオプション
  std::optional<OpusEncoderConfig> opus;
  std::optional<FlacEncoderConfig> flac;

  AudioEncoderConfig() : sample_rate(0), number_of_channels(0) {}
};

// WebCodecs API の AudioDecoderConfig 構造体
struct AudioDecoderConfig {
  // 必須フィールド
  std::string codec;
  uint32_t sample_rate;
  uint32_t number_of_channels;

  // オプショナルフィールド
  std::optional<std::vector<uint8_t>> description;  // コーデック固有の設定

  AudioDecoderConfig() : sample_rate(0), number_of_channels(0) {}
};

// WebCodecs API の AudioDecoderSupport 構造体
struct AudioDecoderSupport {
  bool supported;
  AudioDecoderConfig config;

  AudioDecoderSupport() : supported(false) {}
  AudioDecoderSupport(bool supported, const AudioDecoderConfig& config)
      : supported(supported), config(config) {}
};

// WebCodecs API の VideoDecoderSupport 構造体
struct VideoDecoderSupport {
  bool supported;
  VideoDecoderConfig config;

  VideoDecoderSupport() : supported(false) {}
  VideoDecoderSupport(bool supported, const VideoDecoderConfig& config)
      : supported(supported), config(config) {}
};

// WebCodecs API の AudioEncoderSupport 構造体
struct AudioEncoderSupport {
  bool supported;
  AudioEncoderConfig config;

  AudioEncoderSupport() : supported(false) {}
  AudioEncoderSupport(bool supported, const AudioEncoderConfig& config)
      : supported(supported), config(config) {}
};

// WebCodecs API の VideoEncoderSupport 構造体
struct VideoEncoderSupport {
  bool supported;
  VideoEncoderConfig config;

  VideoEncoderSupport() : supported(false) {}
  VideoEncoderSupport(bool supported, const VideoEncoderConfig& config)
      : supported(supported), config(config) {}
};

// WebCodecs API の EncodedVideoChunkMetadata 構造体
struct EncodedVideoChunkMetadata {
  // decoderConfig: キーフレームで提供される VideoDecoderConfig
  // description には avcC/hvcC/av1C などのコーデック固有データが含まれる
  std::optional<VideoDecoderConfig> decoder_config;

  EncodedVideoChunkMetadata() = default;
};

// WebCodecs API の CodecState 列挙型
enum class CodecState { UNCONFIGURED, CONFIGURED, CLOSED };

// CodecState を文字列に変換するヘルパー関数
inline std::string codec_state_to_string(CodecState state) {
  switch (state) {
    case CodecState::UNCONFIGURED:
      return "unconfigured";
    case CodecState::CONFIGURED:
      return "configured";
    case CodecState::CLOSED:
      return "closed";
    default:
      return "unknown";
  }
}

void init_webcodecs_types(nb::module_& m);