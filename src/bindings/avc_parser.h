#pragma once

#include <cstdint>
#include <optional>
#include <vector>

// AVC NAL ユニットタイプ
enum class AVCNalUnitType : uint8_t {
  UNSPECIFIED = 0,
  NON_IDR_SLICE = 1,
  SLICE_DATA_A = 2,
  SLICE_DATA_B = 3,
  SLICE_DATA_C = 4,
  IDR_SLICE = 5,
  SEI = 6,
  SPS = 7,
  PPS = 8,
  AUD = 9,
  END_OF_SEQUENCE = 10,
  END_OF_STREAM = 11,
  FILLER_DATA = 12,
  SPS_EXT = 13,
  PREFIX_NAL = 14,
  SUBSET_SPS = 15,
};

// AVC SPS 情報
struct AVCSpsInfo {
  uint8_t profile_idc;
  uint8_t level_idc;
  uint8_t constraint_set_flags;
  uint32_t width;
  uint32_t height;
  uint8_t bit_depth_luma;
  uint8_t bit_depth_chroma;
  uint8_t chroma_format_idc;
  std::optional<double> framerate;
  uint8_t sps_id;

  AVCSpsInfo()
      : profile_idc(0),
        level_idc(0),
        constraint_set_flags(0),
        width(0),
        height(0),
        bit_depth_luma(8),
        bit_depth_chroma(8),
        chroma_format_idc(1),
        sps_id(0) {}
};

// AVC PPS 情報
struct AVCPpsInfo {
  uint8_t pps_id;
  uint8_t sps_id;
  bool entropy_coding_mode_flag;

  AVCPpsInfo() : pps_id(0), sps_id(0), entropy_coding_mode_flag(false) {}
};

// AVC NAL ユニットヘッダー情報
struct AVCNalUnitHeader {
  uint8_t nal_unit_type;
  uint8_t nal_ref_idc;
  bool is_idr;
  bool is_key_frame;

  AVCNalUnitHeader()
      : nal_unit_type(0), nal_ref_idc(0), is_idr(false), is_key_frame(false) {}
};

// AVC Annex B 情報
struct AVCAnnexBInfo {
  std::optional<AVCSpsInfo> sps;
  std::optional<AVCPpsInfo> pps;
  std::vector<AVCNalUnitHeader> nal_units;

  AVCAnnexBInfo() {}
};

// AVC description (avcC box) 情報
struct AVCDescriptionInfo {
  std::optional<AVCSpsInfo> sps;
  std::optional<AVCPpsInfo> pps;
  std::vector<AVCNalUnitHeader> nal_units;
  uint8_t length_size;

  AVCDescriptionInfo() : length_size(4) {}
};

// AVC パース関数
AVCSpsInfo parse_avc_sps(const std::vector<uint8_t>& data);
AVCPpsInfo parse_avc_pps(const std::vector<uint8_t>& data);
AVCNalUnitHeader parse_avc_nal_unit_header(uint8_t first_byte);
AVCAnnexBInfo parse_avc_annexb(const std::vector<uint8_t>& data);
AVCDescriptionInfo parse_avc_description(const std::vector<uint8_t>& data);
