#include "codec_parser.h"
#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace {

// 文字列を指定の区切り文字で分割
std::vector<std::string> split_string(const std::string& str, char delimiter) {
  std::vector<std::string> tokens;
  std::stringstream ss(str);
  std::string token;

  while (std::getline(ss, token, delimiter)) {
    tokens.push_back(token);
  }

  return tokens;
}

// 16進数文字列を uint8_t に変換
uint8_t hex_to_uint8(const std::string& hex) {
  if (hex.length() != 2) {
    throw std::invalid_argument("Invalid hex string length: " + hex);
  }
  return static_cast<uint8_t>(std::stoi(hex, nullptr, 16));
}

// 10進数文字列を uint8_t に変換
uint8_t dec_to_uint8(const std::string& dec) {
  return static_cast<uint8_t>(std::stoi(dec));
}

}  // namespace

AV1CodecParameters parse_av1_codec_string(const std::string& codec_string) {
  // フォーマット: av01.P.LLT.DD[.M.CCC.cp.tc.mc.F]
  // 例: "av01.0.04M.08" または "av01.0.04M.10.0.112.09.16.09.0"

  if (codec_string.substr(0, 5) != "av01.") {
    throw std::invalid_argument("Invalid AV1 codec string: " + codec_string);
  }

  auto parts = split_string(codec_string, '.');
  if (parts.size() < 4) {
    throw std::invalid_argument("Invalid AV1 codec string format: " +
                                codec_string);
  }

  AV1CodecParameters params;

  // Profile (P)
  params.profile = dec_to_uint8(parts[1]);
  if (params.profile > 2) {
    throw std::invalid_argument("Invalid AV1 profile: " + parts[1]);
  }

  // Level and Tier (LLT)
  const std::string& level_tier = parts[2];
  if (level_tier.length() < 3) {
    throw std::invalid_argument("Invalid AV1 level/tier: " + level_tier);
  }

  params.level = dec_to_uint8(level_tier.substr(0, 2));
  params.tier = level_tier[2];
  if (params.tier != 'M' && params.tier != 'H') {
    throw std::invalid_argument("Invalid AV1 tier: " +
                                std::string(1, params.tier));
  }

  // Bit Depth (DD)
  params.bit_depth = dec_to_uint8(parts[3]);
  if (params.bit_depth != 8 && params.bit_depth != 10 &&
      params.bit_depth != 12) {
    throw std::invalid_argument("Invalid AV1 bit depth: " + parts[3]);
  }

  // オプションパラメータ
  if (parts.size() >= 5) {
    params.monochrome = dec_to_uint8(parts[4]);
  }
  if (parts.size() >= 6) {
    params.chroma_subsampling = static_cast<uint16_t>(std::stoi(parts[5]));
  }
  if (parts.size() >= 7) {
    params.color_primaries = dec_to_uint8(parts[6]);
  }
  if (parts.size() >= 8) {
    params.transfer_characteristics = dec_to_uint8(parts[7]);
  }
  if (parts.size() >= 9) {
    params.matrix_coefficients = dec_to_uint8(parts[8]);
  }
  if (parts.size() >= 10) {
    params.video_full_range_flag = dec_to_uint8(parts[9]);
  }

  return params;
}

AVCCodecParameters parse_avc_codec_string(const std::string& codec_string) {
  // フォーマット: avc1.PPCCLL または avc3.PPCCLL
  // 例: "avc1.42E01E" (Baseline Profile, Level 3.0)

  if (codec_string.length() < 11) {
    throw std::invalid_argument("Invalid AVC codec string length: " +
                                codec_string);
  }

  std::string prefix = codec_string.substr(0, 4);
  if (prefix != "avc1" && prefix != "avc3") {
    throw std::invalid_argument("Invalid AVC codec string prefix: " + prefix);
  }

  if (codec_string[4] != '.') {
    throw std::invalid_argument("Invalid AVC codec string format: " +
                                codec_string);
  }

  AVCCodecParameters params;
  params.prefix = prefix;

  // 6文字の16進数パラメータ (PPCCLL)
  std::string hex_params = codec_string.substr(5, 6);
  if (hex_params.length() != 6) {
    throw std::invalid_argument("Invalid AVC codec parameters length: " +
                                hex_params);
  }

  // PP: profile_idc
  params.profile_idc = hex_to_uint8(hex_params.substr(0, 2));

  // CC: constraint_set_flags
  params.constraint_set_flags = hex_to_uint8(hex_params.substr(2, 2));

  // LL: level_idc
  params.level_idc = hex_to_uint8(hex_params.substr(4, 2));

  return params;
}

HEVCCodecParameters parse_hevc_codec_string(const std::string& codec_string) {
  // フォーマット: hvc1.X.X.X.X または hev1.X.X.X.X
  // 例: "hvc1.1.6.L93.B0"
  // ISO/IEC 14496-15 Section E.3 に準拠

  if (codec_string.length() < 5) {
    throw std::invalid_argument("Invalid HEVC codec string length: " +
                                codec_string);
  }

  std::string prefix = codec_string.substr(0, 4);
  if (prefix != "hvc1" && prefix != "hev1") {
    throw std::invalid_argument("Invalid HEVC codec string prefix: " + prefix);
  }

  if (codec_string[4] != '.') {
    throw std::invalid_argument("Invalid HEVC codec string format: " +
                                codec_string);
  }

  auto parts = split_string(codec_string, '.');
  if (parts.size() < 4) {
    throw std::invalid_argument("Invalid HEVC codec string format: " +
                                codec_string);
  }

  HEVCCodecParameters params;
  params.prefix = prefix;

  // ISO/IEC 14496-15 Section E.3 に従って解析
  // 詳細な解析は複雑なため、現時点では基本的な情報のみを格納
  // parts[1]: general_profile_space と general_profile_idc
  // parts[2]: general_profile_compatibility_flags
  // parts[3]: general_tier_flag と general_level_idc
  // parts[4]: general_constraint_indicator_flags

  if (parts.size() >= 2) {
    params.general_profile_space = parts[1];
    // 簡易的に profile_idc を抽出
    try {
      params.general_profile_idc = dec_to_uint8(parts[1]);
    } catch (...) {
      // パースできない場合は 0 にする
      params.general_profile_idc = 0;
    }
  }

  if (parts.size() >= 3) {
    params.general_profile_compatibility_flags = parts[2];
  }

  if (parts.size() >= 4) {
    params.general_tier_flag = parts[3];
    // Level の解析（例: "L93" から 93 を抽出）
    if (parts[3].length() > 1 && parts[3][0] == 'L') {
      try {
        params.general_level_idc = dec_to_uint8(parts[3].substr(1));
      } catch (...) {
        params.general_level_idc = 0;
      }
    }
  }

  if (parts.size() >= 5) {
    params.general_constraint_indicator_flags = parts[4];
  }

  return params;
}

VP8CodecParameters parse_vp8_codec_string(const std::string& codec_string) {
  // フォーマット: "vp8"
  if (codec_string != "vp8") {
    throw std::invalid_argument("Invalid VP8 codec string: " + codec_string);
  }

  return VP8CodecParameters();
}

VP9CodecParameters parse_vp9_codec_string(const std::string& codec_string) {
  // フォーマット: vp09.PP.LL.DD[.CC.CP.TC.MC.FF]
  // 例: "vp09.00.10.08" (Profile 0, Level 1.0, 8-bit)

  if (codec_string.substr(0, 5) != "vp09.") {
    throw std::invalid_argument("Invalid VP9 codec string: " + codec_string);
  }

  auto parts = split_string(codec_string, '.');
  if (parts.size() < 4) {
    throw std::invalid_argument("Invalid VP9 codec string format: " +
                                codec_string);
  }

  VP9CodecParameters params;

  // Profile (PP)
  params.profile = dec_to_uint8(parts[1]);
  if (params.profile > 3) {
    throw std::invalid_argument("Invalid VP9 profile: " + parts[1]);
  }

  // Level (LL)
  params.level = dec_to_uint8(parts[2]);

  // Bit Depth (DD)
  params.bit_depth = dec_to_uint8(parts[3]);
  if (params.bit_depth != 8 && params.bit_depth != 10 &&
      params.bit_depth != 12) {
    throw std::invalid_argument("Invalid VP9 bit depth: " + parts[3]);
  }

  // オプションパラメータ
  if (parts.size() >= 5) {
    params.chroma_subsampling = dec_to_uint8(parts[4]);
  }
  if (parts.size() >= 6) {
    params.color_primaries = dec_to_uint8(parts[5]);
  }
  if (parts.size() >= 7) {
    params.transfer_characteristics = dec_to_uint8(parts[6]);
  }
  if (parts.size() >= 8) {
    params.matrix_coefficients = dec_to_uint8(parts[7]);
  }
  if (parts.size() >= 9) {
    params.video_full_range_flag = dec_to_uint8(parts[8]);
  }

  return params;
}

CodecParameters parse_codec_string(const std::string& codec_string) {
  // VP8 は特殊ケース（3文字のみ）
  if (codec_string == "vp8") {
    return parse_vp8_codec_string(codec_string);
  }

  if (codec_string.length() < 5) {
    throw std::invalid_argument("Invalid codec string: " + codec_string);
  }

  std::string prefix = codec_string.substr(0, 5);

  if (prefix == "av01.") {
    return parse_av1_codec_string(codec_string);
  } else if (prefix == "avc1." || prefix == "avc3.") {
    return parse_avc_codec_string(codec_string);
  } else if (prefix == "hvc1." || prefix == "hev1.") {
    return parse_hevc_codec_string(codec_string);
  } else if (prefix == "vp09.") {
    return parse_vp9_codec_string(codec_string);
  } else {
    throw std::invalid_argument("Unsupported codec string: " + codec_string);
  }
}
