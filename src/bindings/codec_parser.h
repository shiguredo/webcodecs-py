#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <variant>

// AV1 コーデックパラメータ
// フォーマット: av01.P.LLT.DD[.M.CCC.cp.tc.mc.F]
struct AV1CodecParameters {
  uint8_t profile;                    // 0 = Main, 1 = High, 2 = Professional
  uint8_t level;                      // 例: 4 = Level 3.0
  char tier;                          // 'M' = Main, 'H' = High
  uint8_t bit_depth;                  // 8, 10, 12
  std::optional<uint8_t> monochrome;  // 0 = カラー, 1 = モノクロ
  std::optional<uint16_t>
      chroma_subsampling;  // 例: 112 = 4:2:0 (1=4:2:0, 0=4:2:2, etc.)
  std::optional<uint8_t> color_primaries;
  std::optional<uint8_t> transfer_characteristics;
  std::optional<uint8_t> matrix_coefficients;
  std::optional<uint8_t>
      video_full_range_flag;  // 0 = スタジオ範囲, 1 = フル範囲

  AV1CodecParameters() : profile(0), level(0), tier('M'), bit_depth(8) {}
};

// AVC/H.264 コーデックパラメータ
// フォーマット: avc1.PPCCLL または avc3.PPCCLL
struct AVCCodecParameters {
  std::string prefix;            // "avc1" または "avc3"
  uint8_t profile_idc;           // プロファイル ID (例: 0x42 = Baseline)
  uint8_t constraint_set_flags;  // 制約セットフラグ
  uint8_t level_idc;             // レベル ID (例: 0x1E = Level 3.0)

  AVCCodecParameters()
      : profile_idc(0), constraint_set_flags(0), level_idc(0) {}
};

// HEVC/H.265 コーデックパラメータ
// フォーマット: hvc1.X.X.X.X または hev1.X.X.X.X
struct HEVCCodecParameters {
  std::string prefix;  // "hvc1" または "hev1"
  std::string general_profile_space;
  uint8_t general_profile_idc;
  std::string general_profile_compatibility_flags;
  std::string general_tier_flag;
  uint8_t general_level_idc;
  std::string general_constraint_indicator_flags;

  HEVCCodecParameters() : general_profile_idc(0), general_level_idc(0) {}
};

// コーデックパラメータを保持する variant
using CodecParameters = std::variant<std::monostate,
                                     AV1CodecParameters,
                                     AVCCodecParameters,
                                     HEVCCodecParameters>;

// AV1 コーデック文字列をパース
// 例: "av01.0.04M.08" -> AV1CodecParameters
AV1CodecParameters parse_av1_codec_string(const std::string& codec_string);

// AVC/H.264 コーデック文字列をパース
// 例: "avc1.42E01E" -> AVCCodecParameters
AVCCodecParameters parse_avc_codec_string(const std::string& codec_string);

// HEVC/H.265 コーデック文字列をパース
// 例: "hvc1.1.6.L93.B0" -> HEVCCodecParameters
HEVCCodecParameters parse_hevc_codec_string(const std::string& codec_string);

// コーデック文字列を自動判定してパース
CodecParameters parse_codec_string(const std::string& codec_string);
