#pragma once

#include <cstdint>
#include <optional>
#include <vector>

// HEVC NAL ユニットタイプ
enum class HEVCNalUnitType : uint8_t {
  TRAIL_N = 0,
  TRAIL_R = 1,
  TSA_N = 2,
  TSA_R = 3,
  STSA_N = 4,
  STSA_R = 5,
  RADL_N = 6,
  RADL_R = 7,
  RASL_N = 8,
  RASL_R = 9,
  BLA_W_LP = 16,
  BLA_W_RADL = 17,
  BLA_N_LP = 18,
  IDR_W_RADL = 19,
  IDR_N_LP = 20,
  CRA = 21,
  VPS = 32,
  SPS = 33,
  PPS = 34,
  AUD = 35,
  EOS = 36,
  EOB = 37,
  FD = 38,
  PREFIX_SEI = 39,
  SUFFIX_SEI = 40,
};

// HEVC VPS 情報
struct HEVCVpsInfo {
  uint8_t vps_id;
  uint8_t max_layers_minus1;
  uint8_t max_sub_layers_minus1;

  HEVCVpsInfo() : vps_id(0), max_layers_minus1(0), max_sub_layers_minus1(0) {}
};

// HEVC SPS 情報
struct HEVCSpsInfo {
  uint8_t general_profile_idc;
  uint8_t general_level_idc;
  uint8_t general_tier_flag;
  uint32_t width;
  uint32_t height;
  uint8_t bit_depth_luma;
  uint8_t bit_depth_chroma;
  uint8_t chroma_format_idc;
  std::optional<double> framerate;
  uint8_t sps_id;
  uint8_t vps_id;

  HEVCSpsInfo()
      : general_profile_idc(0),
        general_level_idc(0),
        general_tier_flag(0),
        width(0),
        height(0),
        bit_depth_luma(8),
        bit_depth_chroma(8),
        chroma_format_idc(1),
        sps_id(0),
        vps_id(0) {}
};

// HEVC PPS 情報
struct HEVCPpsInfo {
  uint8_t pps_id;
  uint8_t sps_id;

  HEVCPpsInfo() : pps_id(0), sps_id(0) {}
};

// HEVC NAL ユニットヘッダー情報
struct HEVCNalUnitHeader {
  uint8_t nal_unit_type;
  uint8_t nuh_layer_id;
  uint8_t nuh_temporal_id_plus1;
  bool is_irap;
  bool is_key_frame;

  HEVCNalUnitHeader()
      : nal_unit_type(0),
        nuh_layer_id(0),
        nuh_temporal_id_plus1(0),
        is_irap(false),
        is_key_frame(false) {}
};

// HEVC Annex B 情報
struct HEVCAnnexBInfo {
  std::optional<HEVCVpsInfo> vps;
  std::optional<HEVCSpsInfo> sps;
  std::optional<HEVCPpsInfo> pps;
  std::vector<HEVCNalUnitHeader> nal_units;

  HEVCAnnexBInfo() {}
};

// HEVC description (hvcC box) 情報
struct HEVCDescriptionInfo {
  std::optional<HEVCVpsInfo> vps;
  std::optional<HEVCSpsInfo> sps;
  std::optional<HEVCPpsInfo> pps;
  std::vector<HEVCNalUnitHeader> nal_units;
  uint8_t length_size;

  HEVCDescriptionInfo() : length_size(4) {}
};

// HEVC パース関数
HEVCVpsInfo parse_hevc_vps(const std::vector<uint8_t>& data);
HEVCSpsInfo parse_hevc_sps(const std::vector<uint8_t>& data);
HEVCPpsInfo parse_hevc_pps(const std::vector<uint8_t>& data);
HEVCNalUnitHeader parse_hevc_nal_unit_header(const uint8_t* data);
HEVCAnnexBInfo parse_hevc_annexb(const std::vector<uint8_t>& data);
HEVCDescriptionInfo parse_hevc_description(const std::vector<uint8_t>& data);
