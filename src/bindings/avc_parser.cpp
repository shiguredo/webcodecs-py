#include "avc_parser.h"

#include <nanobind/nanobind.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/vector.h>

#include <stdexcept>

#include "bitstream_reader.h"
#include "nal_utils.h"

namespace nb = nanobind;

// AVC NAL ユニットヘッダーをパース
AVCNalUnitHeader parse_avc_nal_unit_header(uint8_t first_byte) {
  AVCNalUnitHeader header;
  header.nal_ref_idc = (first_byte >> 5) & 0x03;
  header.nal_unit_type = first_byte & 0x1F;
  header.is_idr =
      (header.nal_unit_type == static_cast<uint8_t>(AVCNalUnitType::IDR_SLICE));
  header.is_key_frame =
      header.is_idr ||
      header.nal_unit_type == static_cast<uint8_t>(AVCNalUnitType::SPS);
  return header;
}

// AVC SPS をパース
AVCSpsInfo parse_avc_sps(const std::vector<uint8_t>& data) {
  if (data.empty()) {
    throw std::invalid_argument("SPS データが空です");
  }

  // エミュレーション防止バイトを除去
  std::vector<uint8_t> rbsp =
      remove_emulation_prevention_bytes(data.data(), data.size());
  BitstreamReader reader(rbsp);

  AVCSpsInfo sps;

  // NAL ユニットヘッダーをスキップ（1 バイト）
  reader.skip_bits(8);

  // profile_idc
  sps.profile_idc = reader.read_bits(8);

  // constraint_set_flags (6 ビット) + reserved_zero_2bits (2 ビット)
  sps.constraint_set_flags = reader.read_bits(8);

  // level_idc
  sps.level_idc = reader.read_bits(8);

  // seq_parameter_set_id
  sps.sps_id = reader.read_ue();

  // High プロファイル以上の場合の追加パラメータ
  if (sps.profile_idc == 100 || sps.profile_idc == 110 ||
      sps.profile_idc == 122 || sps.profile_idc == 244 ||
      sps.profile_idc == 44 || sps.profile_idc == 83 || sps.profile_idc == 86 ||
      sps.profile_idc == 118 || sps.profile_idc == 128 ||
      sps.profile_idc == 138 || sps.profile_idc == 139 ||
      sps.profile_idc == 134 || sps.profile_idc == 135) {
    sps.chroma_format_idc = reader.read_ue();

    if (sps.chroma_format_idc == 3) {
      // separate_colour_plane_flag
      reader.skip_bits(1);
    }

    // bit_depth_luma_minus8
    sps.bit_depth_luma = reader.read_ue() + 8;

    // bit_depth_chroma_minus8
    sps.bit_depth_chroma = reader.read_ue() + 8;

    // qpprime_y_zero_transform_bypass_flag
    reader.skip_bits(1);

    // seq_scaling_matrix_present_flag
    bool seq_scaling_matrix_present_flag = reader.read_bit();
    if (seq_scaling_matrix_present_flag) {
      int count = (sps.chroma_format_idc != 3) ? 8 : 12;
      for (int i = 0; i < count; ++i) {
        bool seq_scaling_list_present_flag = reader.read_bit();
        if (seq_scaling_list_present_flag) {
          int size = (i < 6) ? 16 : 64;
          int last_scale = 8;
          int next_scale = 8;
          for (int j = 0; j < size; ++j) {
            if (next_scale != 0) {
              int delta_scale = reader.read_se();
              next_scale = (last_scale + delta_scale + 256) % 256;
            }
            last_scale = (next_scale == 0) ? last_scale : next_scale;
          }
        }
      }
    }
  }

  // log2_max_frame_num_minus4
  reader.read_ue();

  // pic_order_cnt_type
  uint32_t pic_order_cnt_type = reader.read_ue();

  if (pic_order_cnt_type == 0) {
    // log2_max_pic_order_cnt_lsb_minus4
    reader.read_ue();
  } else if (pic_order_cnt_type == 1) {
    // delta_pic_order_always_zero_flag
    reader.skip_bits(1);
    // offset_for_non_ref_pic
    reader.read_se();
    // offset_for_top_to_bottom_field
    reader.read_se();
    // num_ref_frames_in_pic_order_cnt_cycle
    uint32_t num_ref_frames = reader.read_ue();
    for (uint32_t i = 0; i < num_ref_frames; ++i) {
      reader.read_se();
    }
  }

  // max_num_ref_frames
  reader.read_ue();

  // gaps_in_frame_num_value_allowed_flag
  reader.skip_bits(1);

  // pic_width_in_mbs_minus1
  uint32_t pic_width_in_mbs_minus1 = reader.read_ue();

  // pic_height_in_map_units_minus1
  uint32_t pic_height_in_map_units_minus1 = reader.read_ue();

  // frame_mbs_only_flag
  bool frame_mbs_only_flag = reader.read_bit();

  if (!frame_mbs_only_flag) {
    // mb_adaptive_frame_field_flag
    reader.skip_bits(1);
  }

  // direct_8x8_inference_flag
  reader.skip_bits(1);

  // frame_cropping_flag
  bool frame_cropping_flag = reader.read_bit();

  uint32_t frame_crop_left_offset = 0;
  uint32_t frame_crop_right_offset = 0;
  uint32_t frame_crop_top_offset = 0;
  uint32_t frame_crop_bottom_offset = 0;

  if (frame_cropping_flag) {
    frame_crop_left_offset = reader.read_ue();
    frame_crop_right_offset = reader.read_ue();
    frame_crop_top_offset = reader.read_ue();
    frame_crop_bottom_offset = reader.read_ue();
  }

  // 解像度を計算
  uint32_t sub_width_c = (sps.chroma_format_idc == 3) ? 1 : 2;
  uint32_t sub_height_c = (sps.chroma_format_idc == 1) ? 2 : 1;
  if (sps.chroma_format_idc == 0) {
    sub_width_c = 1;
    sub_height_c = 1;
  }

  uint32_t crop_unit_x = sub_width_c;
  uint32_t crop_unit_y = sub_height_c * (frame_mbs_only_flag ? 1 : 2);

  sps.width = (pic_width_in_mbs_minus1 + 1) * 16 -
              (frame_crop_left_offset + frame_crop_right_offset) * crop_unit_x;
  sps.height = (pic_height_in_map_units_minus1 + 1) * 16 *
                   (frame_mbs_only_flag ? 1 : 2) -
               (frame_crop_top_offset + frame_crop_bottom_offset) * crop_unit_y;

  // vui_parameters_present_flag
  bool vui_parameters_present_flag = reader.read_bit();
  if (vui_parameters_present_flag && reader.has_more_data()) {
    // aspect_ratio_info_present_flag
    bool aspect_ratio_info_present_flag = reader.read_bit();
    if (aspect_ratio_info_present_flag) {
      uint8_t aspect_ratio_idc = reader.read_bits(8);
      if (aspect_ratio_idc == 255) {
        // Extended_SAR
        reader.skip_bits(32);
      }
    }

    // overscan_info_present_flag
    if (reader.read_bit()) {
      reader.skip_bits(1);
    }

    // video_signal_type_present_flag
    if (reader.read_bit()) {
      reader.skip_bits(4);
      if (reader.read_bit()) {
        reader.skip_bits(24);
      }
    }

    // chroma_loc_info_present_flag
    if (reader.read_bit()) {
      reader.read_ue();
      reader.read_ue();
    }

    // timing_info_present_flag
    if (reader.has_more_data() && reader.read_bit()) {
      uint32_t num_units_in_tick = reader.read_bits(32);
      uint32_t time_scale = reader.read_bits(32);
      if (num_units_in_tick > 0) {
        sps.framerate =
            static_cast<double>(time_scale) / (2.0 * num_units_in_tick);
      }
    }
  }

  return sps;
}

// AVC PPS をパース
AVCPpsInfo parse_avc_pps(const std::vector<uint8_t>& data) {
  if (data.empty()) {
    throw std::invalid_argument("PPS データが空です");
  }

  // エミュレーション防止バイトを除去
  std::vector<uint8_t> rbsp =
      remove_emulation_prevention_bytes(data.data(), data.size());
  BitstreamReader reader(rbsp);

  AVCPpsInfo pps;

  // NAL ユニットヘッダーをスキップ（1 バイト）
  reader.skip_bits(8);

  // pic_parameter_set_id
  pps.pps_id = reader.read_ue();

  // seq_parameter_set_id
  pps.sps_id = reader.read_ue();

  // entropy_coding_mode_flag
  pps.entropy_coding_mode_flag = reader.read_bit();

  return pps;
}

// AVC Annex B フォーマットをパース
AVCAnnexBInfo parse_avc_annexb(const std::vector<uint8_t>& data) {
  if (data.empty()) {
    throw std::invalid_argument("データが空です");
  }

  AVCAnnexBInfo info;
  auto nal_units = find_annexb_nal_units(data.data(), data.size());

  for (const auto& [offset, length] : nal_units) {
    if (length == 0) {
      continue;
    }

    AVCNalUnitHeader header = parse_avc_nal_unit_header(data[offset]);
    info.nal_units.push_back(header);

    // SPS をパース
    if (header.nal_unit_type == static_cast<uint8_t>(AVCNalUnitType::SPS)) {
      try {
        std::vector<uint8_t> sps_data(data.begin() + offset,
                                      data.begin() + offset + length);
        info.sps = parse_avc_sps(sps_data);
      } catch (...) {
        // 不正な SPS データは無視
      }
    }
    // PPS をパース
    else if (header.nal_unit_type ==
             static_cast<uint8_t>(AVCNalUnitType::PPS)) {
      try {
        std::vector<uint8_t> pps_data(data.begin() + offset,
                                      data.begin() + offset + length);
        info.pps = parse_avc_pps(pps_data);
      } catch (...) {
        // 不正な PPS データは無視
      }
    }
  }

  return info;
}

// AVC description (avcC box) フォーマットをパース
AVCDescriptionInfo parse_avc_description(const std::vector<uint8_t>& data) {
  if (data.size() < 7) {
    throw std::invalid_argument("avcC データが短すぎます");
  }

  AVCDescriptionInfo info;

  // lengthSizeMinusOne (下位 2 ビット)
  info.length_size = (data[4] & 0x03) + 1;

  size_t offset = 5;

  // numOfSequenceParameterSets (下位 5 ビット)
  uint8_t num_sps = data[offset] & 0x1F;
  offset++;

  // SPS を読み取る
  for (uint8_t i = 0; i < num_sps; ++i) {
    if (offset + 2 > data.size()) {
      break;
    }

    uint16_t sps_length = (static_cast<uint16_t>(data[offset]) << 8) |
                          static_cast<uint16_t>(data[offset + 1]);
    offset += 2;

    if (offset + sps_length > data.size() || sps_length == 0) {
      break;
    }

    std::vector<uint8_t> sps_data(data.begin() + offset,
                                  data.begin() + offset + sps_length);
    AVCNalUnitHeader header = parse_avc_nal_unit_header(sps_data[0]);
    info.nal_units.push_back(header);
    try {
      info.sps = parse_avc_sps(sps_data);
    } catch (...) {
      // 不正な SPS データは無視
    }

    offset += sps_length;
  }

  // numOfPictureParameterSets
  if (offset >= data.size()) {
    return info;
  }
  uint8_t num_pps = data[offset];
  offset++;

  // PPS を読み取る
  for (uint8_t i = 0; i < num_pps; ++i) {
    if (offset + 2 > data.size()) {
      break;
    }

    uint16_t pps_length = (static_cast<uint16_t>(data[offset]) << 8) |
                          static_cast<uint16_t>(data[offset + 1]);
    offset += 2;

    if (offset + pps_length > data.size() || pps_length == 0) {
      break;
    }

    std::vector<uint8_t> pps_data(data.begin() + offset,
                                  data.begin() + offset + pps_length);
    AVCNalUnitHeader header = parse_avc_nal_unit_header(pps_data[0]);
    info.nal_units.push_back(header);
    try {
      info.pps = parse_avc_pps(pps_data);
    } catch (...) {
      // 不正な PPS データは無視
    }

    offset += pps_length;
  }

  return info;
}

// Python バインディング初期化関数
void init_avc_parser(nb::module_& m) {
  // AVC NAL ユニットタイプ enum (IntEnum 相当)
  nb::enum_<AVCNalUnitType>(m, "AVCNalUnitType", nb::is_arithmetic())
      .value("UNSPECIFIED", AVCNalUnitType::UNSPECIFIED)
      .value("NON_IDR_SLICE", AVCNalUnitType::NON_IDR_SLICE)
      .value("SLICE_DATA_A", AVCNalUnitType::SLICE_DATA_A)
      .value("SLICE_DATA_B", AVCNalUnitType::SLICE_DATA_B)
      .value("SLICE_DATA_C", AVCNalUnitType::SLICE_DATA_C)
      .value("IDR_SLICE", AVCNalUnitType::IDR_SLICE)
      .value("SEI", AVCNalUnitType::SEI)
      .value("SPS", AVCNalUnitType::SPS)
      .value("PPS", AVCNalUnitType::PPS)
      .value("AUD", AVCNalUnitType::AUD)
      .value("END_OF_SEQUENCE", AVCNalUnitType::END_OF_SEQUENCE)
      .value("END_OF_STREAM", AVCNalUnitType::END_OF_STREAM)
      .value("FILLER_DATA", AVCNalUnitType::FILLER_DATA)
      .value("SPS_EXT", AVCNalUnitType::SPS_EXT)
      .value("PREFIX_NAL", AVCNalUnitType::PREFIX_NAL)
      .value("SUBSET_SPS", AVCNalUnitType::SUBSET_SPS);

  // parse_avc_annexb 関数
  m.def(
      "parse_avc_annexb",
      [](nb::bytes data) {
        const uint8_t* ptr = reinterpret_cast<const uint8_t*>(data.c_str());
        size_t size = data.size();
        std::vector<uint8_t> vec(ptr, ptr + size);
        return parse_avc_annexb(vec);
      },
      nb::arg("data"),
      nb::sig("def parse_avc_annexb(data: bytes) -> AVCAnnexBInfo"),
      "AVC (H.264) Annex B フォーマットをパースして SPS/PPS および NAL "
      "ユニットヘッダー情報を返す");

  // parse_avc_description 関数
  m.def(
      "parse_avc_description",
      [](nb::bytes data) {
        const uint8_t* ptr = reinterpret_cast<const uint8_t*>(data.c_str());
        size_t size = data.size();
        std::vector<uint8_t> vec(ptr, ptr + size);
        return parse_avc_description(vec);
      },
      nb::arg("data"),
      nb::sig("def parse_avc_description(data: bytes) -> AVCDescriptionInfo"),
      "AVC (H.264) description (avcC) をパースして SPS/PPS および NAL "
      "ユニットヘッダー情報を返す");

  // parse_avc_sps 関数
  m.def(
      "parse_avc_sps",
      [](nb::bytes data) {
        const uint8_t* ptr = reinterpret_cast<const uint8_t*>(data.c_str());
        size_t size = data.size();
        std::vector<uint8_t> vec(ptr, ptr + size);
        return parse_avc_sps(vec);
      },
      nb::arg("data"), nb::sig("def parse_avc_sps(data: bytes) -> AVCSpsInfo"),
      "AVC (H.264) SPS をパースする");

  // parse_avc_pps 関数
  m.def(
      "parse_avc_pps",
      [](nb::bytes data) {
        const uint8_t* ptr = reinterpret_cast<const uint8_t*>(data.c_str());
        size_t size = data.size();
        std::vector<uint8_t> vec(ptr, ptr + size);
        return parse_avc_pps(vec);
      },
      nb::arg("data"), nb::sig("def parse_avc_pps(data: bytes) -> AVCPpsInfo"),
      "AVC (H.264) PPS をパースする");

  // AVCSpsInfo クラス
  nb::class_<AVCSpsInfo>(m, "AVCSpsInfo")
      .def(nb::init<>())
      .def_ro("profile_idc", &AVCSpsInfo::profile_idc)
      .def_ro("level_idc", &AVCSpsInfo::level_idc)
      .def_ro("constraint_set_flags", &AVCSpsInfo::constraint_set_flags)
      .def_ro("width", &AVCSpsInfo::width)
      .def_ro("height", &AVCSpsInfo::height)
      .def_ro("bit_depth_luma", &AVCSpsInfo::bit_depth_luma)
      .def_ro("bit_depth_chroma", &AVCSpsInfo::bit_depth_chroma)
      .def_ro("chroma_format_idc", &AVCSpsInfo::chroma_format_idc)
      .def_ro("framerate", &AVCSpsInfo::framerate)
      .def_ro("sps_id", &AVCSpsInfo::sps_id);

  // AVCPpsInfo クラス
  nb::class_<AVCPpsInfo>(m, "AVCPpsInfo")
      .def(nb::init<>())
      .def_ro("pps_id", &AVCPpsInfo::pps_id)
      .def_ro("sps_id", &AVCPpsInfo::sps_id)
      .def_ro("entropy_coding_mode_flag",
              &AVCPpsInfo::entropy_coding_mode_flag);

  // AVCNalUnitHeader クラス
  nb::class_<AVCNalUnitHeader>(m, "AVCNalUnitHeader")
      .def(nb::init<>())
      .def_ro("nal_unit_type", &AVCNalUnitHeader::nal_unit_type)
      .def_ro("nal_ref_idc", &AVCNalUnitHeader::nal_ref_idc)
      .def_ro("is_idr", &AVCNalUnitHeader::is_idr)
      .def_ro("is_key_frame", &AVCNalUnitHeader::is_key_frame);

  // AVCAnnexBInfo クラス
  nb::class_<AVCAnnexBInfo>(m, "AVCAnnexBInfo")
      .def(nb::init<>())
      .def_ro("sps", &AVCAnnexBInfo::sps)
      .def_ro("pps", &AVCAnnexBInfo::pps)
      .def_ro("nal_units", &AVCAnnexBInfo::nal_units);

  // AVCDescriptionInfo クラス
  nb::class_<AVCDescriptionInfo>(m, "AVCDescriptionInfo")
      .def(nb::init<>())
      .def_ro("sps", &AVCDescriptionInfo::sps)
      .def_ro("pps", &AVCDescriptionInfo::pps)
      .def_ro("nal_units", &AVCDescriptionInfo::nal_units)
      .def_ro("length_size", &AVCDescriptionInfo::length_size);
}
