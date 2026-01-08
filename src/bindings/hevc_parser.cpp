#include "hevc_parser.h"

#include <nanobind/nanobind.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/vector.h>

#include <stdexcept>

#include "bitstream_reader.h"
#include "nal_utils.h"

namespace nb = nanobind;

// HEVC NAL ユニットヘッダーをパース
HEVCNalUnitHeader parse_hevc_nal_unit_header(const uint8_t* data) {
  HEVCNalUnitHeader header;
  header.nal_unit_type = (data[0] >> 1) & 0x3F;
  header.nuh_layer_id = ((data[0] & 0x01) << 5) | ((data[1] >> 3) & 0x1F);
  header.nuh_temporal_id_plus1 = data[1] & 0x07;

  // IRAP (Intra Random Access Point) 判定
  // BLA, IDR, CRA が IRAP に該当
  header.is_irap =
      (header.nal_unit_type >=
           static_cast<uint8_t>(HEVCNalUnitType::BLA_W_LP) &&
       header.nal_unit_type <= static_cast<uint8_t>(HEVCNalUnitType::CRA));

  header.is_key_frame =
      header.is_irap ||
      header.nal_unit_type == static_cast<uint8_t>(HEVCNalUnitType::VPS) ||
      header.nal_unit_type == static_cast<uint8_t>(HEVCNalUnitType::SPS);

  return header;
}

// HEVC プロファイル・ティア・レベルをパース
static void parse_hevc_profile_tier_level(BitstreamReader& reader,
                                          bool profile_present_flag,
                                          uint8_t max_sub_layers_minus1,
                                          HEVCSpsInfo& sps) {
  if (profile_present_flag) {
    // general_profile_space (2 bits)
    reader.skip_bits(2);
    // general_tier_flag
    sps.general_tier_flag = reader.read_bit();
    // general_profile_idc
    sps.general_profile_idc = reader.read_bits(5);
    // general_profile_compatibility_flags (32 bits)
    reader.skip_bits(32);
    // general_constraint_indicator_flags (48 bits)
    reader.skip_bits(48);
  }
  // general_level_idc
  sps.general_level_idc = reader.read_bits(8);

  // サブレイヤーフラグをスキップ
  for (uint8_t i = 0; i < max_sub_layers_minus1; ++i) {
    // sub_layer_profile_present_flag
    reader.skip_bits(1);
    // sub_layer_level_present_flag
    reader.skip_bits(1);
  }

  if (max_sub_layers_minus1 > 0) {
    for (uint8_t i = max_sub_layers_minus1; i < 8; ++i) {
      reader.skip_bits(2);
    }
  }

  // サブレイヤーのプロファイル・ティア・レベルは省略
  // 基本的な profile/level/tier 情報のみ抽出するため
}

// HEVC VPS をパース
HEVCVpsInfo parse_hevc_vps(const std::vector<uint8_t>& data) {
  if (data.empty()) {
    throw std::invalid_argument("VPS データが空です");
  }

  // エミュレーション防止バイトを除去
  std::vector<uint8_t> rbsp =
      remove_emulation_prevention_bytes(data.data(), data.size());
  BitstreamReader reader(rbsp);

  HEVCVpsInfo vps;

  // NAL ユニットヘッダーをスキップ（2 バイト）
  reader.skip_bits(16);

  // vps_video_parameter_set_id
  vps.vps_id = reader.read_bits(4);

  // vps_base_layer_internal_flag
  reader.skip_bits(1);
  // vps_base_layer_available_flag
  reader.skip_bits(1);

  // vps_max_layers_minus1
  vps.max_layers_minus1 = reader.read_bits(6);

  // vps_max_sub_layers_minus1
  vps.max_sub_layers_minus1 = reader.read_bits(3);

  return vps;
}

// HEVC SPS をパース
HEVCSpsInfo parse_hevc_sps(const std::vector<uint8_t>& data) {
  if (data.empty()) {
    throw std::invalid_argument("SPS データが空です");
  }

  // エミュレーション防止バイトを除去
  std::vector<uint8_t> rbsp =
      remove_emulation_prevention_bytes(data.data(), data.size());
  BitstreamReader reader(rbsp);

  HEVCSpsInfo sps;

  // NAL ユニットヘッダーをスキップ（2 バイト）
  reader.skip_bits(16);

  // sps_video_parameter_set_id
  sps.vps_id = reader.read_bits(4);

  // sps_max_sub_layers_minus1
  uint8_t max_sub_layers_minus1 = reader.read_bits(3);

  // sps_temporal_id_nesting_flag
  reader.skip_bits(1);

  // profile_tier_level
  parse_hevc_profile_tier_level(reader, true, max_sub_layers_minus1, sps);

  // sps_seq_parameter_set_id
  sps.sps_id = reader.read_ue();

  // chroma_format_idc
  sps.chroma_format_idc = reader.read_ue();

  if (sps.chroma_format_idc == 3) {
    // separate_colour_plane_flag
    reader.skip_bits(1);
  }

  // pic_width_in_luma_samples
  sps.width = reader.read_ue();

  // pic_height_in_luma_samples
  sps.height = reader.read_ue();

  // conformance_window_flag
  bool conformance_window_flag = reader.read_bit();
  if (conformance_window_flag) {
    uint32_t conf_win_left_offset = reader.read_ue();
    uint32_t conf_win_right_offset = reader.read_ue();
    uint32_t conf_win_top_offset = reader.read_ue();
    uint32_t conf_win_bottom_offset = reader.read_ue();

    // クロッピングを適用
    uint32_t sub_width_c =
        (sps.chroma_format_idc == 1 || sps.chroma_format_idc == 2) ? 2 : 1;
    uint32_t sub_height_c = (sps.chroma_format_idc == 1) ? 2 : 1;

    sps.width -= (conf_win_left_offset + conf_win_right_offset) * sub_width_c;
    sps.height -= (conf_win_top_offset + conf_win_bottom_offset) * sub_height_c;
  }

  // bit_depth_luma_minus8
  sps.bit_depth_luma = reader.read_ue() + 8;

  // bit_depth_chroma_minus8
  sps.bit_depth_chroma = reader.read_ue() + 8;

  return sps;
}

// HEVC PPS をパース
HEVCPpsInfo parse_hevc_pps(const std::vector<uint8_t>& data) {
  if (data.empty()) {
    throw std::invalid_argument("PPS データが空です");
  }

  // エミュレーション防止バイトを除去
  std::vector<uint8_t> rbsp =
      remove_emulation_prevention_bytes(data.data(), data.size());
  BitstreamReader reader(rbsp);

  HEVCPpsInfo pps;

  // NAL ユニットヘッダーをスキップ（2 バイト）
  reader.skip_bits(16);

  // pps_pic_parameter_set_id
  pps.pps_id = reader.read_ue();

  // pps_seq_parameter_set_id
  pps.sps_id = reader.read_ue();

  return pps;
}

// HEVC Annex B フォーマットをパース
HEVCAnnexBInfo parse_hevc_annexb(const std::vector<uint8_t>& data) {
  if (data.empty()) {
    throw std::invalid_argument("データが空です");
  }

  HEVCAnnexBInfo info;
  auto nal_units = find_annexb_nal_units(data.data(), data.size());

  for (const auto& [offset, length] : nal_units) {
    if (length < 2) {
      continue;
    }

    HEVCNalUnitHeader header = parse_hevc_nal_unit_header(data.data() + offset);
    info.nal_units.push_back(header);

    // VPS をパース
    if (header.nal_unit_type == static_cast<uint8_t>(HEVCNalUnitType::VPS)) {
      try {
        std::vector<uint8_t> vps_data(data.begin() + offset,
                                      data.begin() + offset + length);
        info.vps = parse_hevc_vps(vps_data);
      } catch (...) {
        // 不正な VPS データは無視
      }
    }
    // SPS をパース
    else if (header.nal_unit_type ==
             static_cast<uint8_t>(HEVCNalUnitType::SPS)) {
      try {
        std::vector<uint8_t> sps_data(data.begin() + offset,
                                      data.begin() + offset + length);
        info.sps = parse_hevc_sps(sps_data);
      } catch (...) {
        // 不正な SPS データは無視
      }
    }
    // PPS をパース
    else if (header.nal_unit_type ==
             static_cast<uint8_t>(HEVCNalUnitType::PPS)) {
      try {
        std::vector<uint8_t> pps_data(data.begin() + offset,
                                      data.begin() + offset + length);
        info.pps = parse_hevc_pps(pps_data);
      } catch (...) {
        // 不正な PPS データは無視
      }
    }
  }

  return info;
}

// HEVC description (hvcC box) フォーマットをパース
HEVCDescriptionInfo parse_hevc_description(const std::vector<uint8_t>& data) {
  if (data.size() < 23) {
    throw std::invalid_argument("hvcC データが短すぎます");
  }

  HEVCDescriptionInfo info;

  // lengthSizeMinusOne (下位 2 ビット)
  info.length_size = (data[21] & 0x03) + 1;

  // HEVCDecoderConfigurationRecord の構造をパース
  // offset 22: numOfArrays
  size_t offset = 22;
  uint8_t num_arrays = data[offset];
  offset++;

  // 各配列を処理
  for (uint8_t i = 0; i < num_arrays; ++i) {
    if (offset + 3 > data.size()) {
      break;
    }

    // NAL ユニットタイプ（下位 6 ビット）
    uint8_t nal_type = data[offset] & 0x3F;
    offset++;

    // numNalus
    uint16_t num_nalus = (static_cast<uint16_t>(data[offset]) << 8) |
                         static_cast<uint16_t>(data[offset + 1]);
    offset += 2;

    // 各 NAL ユニットを処理
    for (uint16_t j = 0; j < num_nalus; ++j) {
      if (offset + 2 > data.size()) {
        break;
      }

      uint16_t nal_length = (static_cast<uint16_t>(data[offset]) << 8) |
                            static_cast<uint16_t>(data[offset + 1]);
      offset += 2;

      if (offset + nal_length > data.size()) {
        break;
      }

      std::vector<uint8_t> nal_data(data.begin() + offset,
                                    data.begin() + offset + nal_length);

      if (nal_data.size() >= 2) {
        HEVCNalUnitHeader header = parse_hevc_nal_unit_header(nal_data.data());
        info.nal_units.push_back(header);

        try {
          // VPS をパース
          if (nal_type == static_cast<uint8_t>(HEVCNalUnitType::VPS)) {
            info.vps = parse_hevc_vps(nal_data);
          }
          // SPS をパース
          else if (nal_type == static_cast<uint8_t>(HEVCNalUnitType::SPS)) {
            info.sps = parse_hevc_sps(nal_data);
          }
          // PPS をパース
          else if (nal_type == static_cast<uint8_t>(HEVCNalUnitType::PPS)) {
            info.pps = parse_hevc_pps(nal_data);
          }
        } catch (...) {
          // 不正な VPS/SPS/PPS データは無視
        }
      }

      offset += nal_length;
    }
  }

  return info;
}

// Python バインディング初期化関数
void init_hevc_parser(nb::module_& m) {
  // HEVC NAL ユニットタイプ enum (IntEnum 相当)
  nb::enum_<HEVCNalUnitType>(m, "HEVCNalUnitType", nb::is_arithmetic())
      .value("TRAIL_N", HEVCNalUnitType::TRAIL_N)
      .value("TRAIL_R", HEVCNalUnitType::TRAIL_R)
      .value("TSA_N", HEVCNalUnitType::TSA_N)
      .value("TSA_R", HEVCNalUnitType::TSA_R)
      .value("STSA_N", HEVCNalUnitType::STSA_N)
      .value("STSA_R", HEVCNalUnitType::STSA_R)
      .value("RADL_N", HEVCNalUnitType::RADL_N)
      .value("RADL_R", HEVCNalUnitType::RADL_R)
      .value("RASL_N", HEVCNalUnitType::RASL_N)
      .value("RASL_R", HEVCNalUnitType::RASL_R)
      .value("BLA_W_LP", HEVCNalUnitType::BLA_W_LP)
      .value("BLA_W_RADL", HEVCNalUnitType::BLA_W_RADL)
      .value("BLA_N_LP", HEVCNalUnitType::BLA_N_LP)
      .value("IDR_W_RADL", HEVCNalUnitType::IDR_W_RADL)
      .value("IDR_N_LP", HEVCNalUnitType::IDR_N_LP)
      .value("CRA", HEVCNalUnitType::CRA)
      .value("VPS", HEVCNalUnitType::VPS)
      .value("SPS", HEVCNalUnitType::SPS)
      .value("PPS", HEVCNalUnitType::PPS)
      .value("AUD", HEVCNalUnitType::AUD)
      .value("EOS", HEVCNalUnitType::EOS)
      .value("EOB", HEVCNalUnitType::EOB)
      .value("FD", HEVCNalUnitType::FD)
      .value("PREFIX_SEI", HEVCNalUnitType::PREFIX_SEI)
      .value("SUFFIX_SEI", HEVCNalUnitType::SUFFIX_SEI);

  // parse_hevc_annexb 関数
  m.def(
      "parse_hevc_annexb",
      [](nb::bytes data) {
        const uint8_t* ptr = reinterpret_cast<const uint8_t*>(data.c_str());
        size_t size = data.size();
        std::vector<uint8_t> vec(ptr, ptr + size);
        return parse_hevc_annexb(vec);
      },
      nb::arg("data"),
      nb::sig("def parse_hevc_annexb(data: bytes) -> HEVCAnnexBInfo"),
      "HEVC (H.265) Annex B フォーマットをパースして VPS/SPS/PPS および NAL "
      "ユニットヘッダー情報を返す");

  // parse_hevc_description 関数
  m.def(
      "parse_hevc_description",
      [](nb::bytes data) {
        const uint8_t* ptr = reinterpret_cast<const uint8_t*>(data.c_str());
        size_t size = data.size();
        std::vector<uint8_t> vec(ptr, ptr + size);
        return parse_hevc_description(vec);
      },
      nb::arg("data"),
      nb::sig("def parse_hevc_description(data: bytes) -> HEVCDescriptionInfo"),
      "HEVC (H.265) description (hvcC) をパースして VPS/SPS/PPS および NAL "
      "ユニットヘッダー情報を返す");

  // parse_hevc_vps 関数
  m.def(
      "parse_hevc_vps",
      [](nb::bytes data) {
        const uint8_t* ptr = reinterpret_cast<const uint8_t*>(data.c_str());
        size_t size = data.size();
        std::vector<uint8_t> vec(ptr, ptr + size);
        return parse_hevc_vps(vec);
      },
      nb::arg("data"),
      nb::sig("def parse_hevc_vps(data: bytes) -> HEVCVpsInfo"),
      "HEVC (H.265) VPS をパースする");

  // parse_hevc_sps 関数
  m.def(
      "parse_hevc_sps",
      [](nb::bytes data) {
        const uint8_t* ptr = reinterpret_cast<const uint8_t*>(data.c_str());
        size_t size = data.size();
        std::vector<uint8_t> vec(ptr, ptr + size);
        return parse_hevc_sps(vec);
      },
      nb::arg("data"),
      nb::sig("def parse_hevc_sps(data: bytes) -> HEVCSpsInfo"),
      "HEVC (H.265) SPS をパースする");

  // parse_hevc_pps 関数
  m.def(
      "parse_hevc_pps",
      [](nb::bytes data) {
        const uint8_t* ptr = reinterpret_cast<const uint8_t*>(data.c_str());
        size_t size = data.size();
        std::vector<uint8_t> vec(ptr, ptr + size);
        return parse_hevc_pps(vec);
      },
      nb::arg("data"),
      nb::sig("def parse_hevc_pps(data: bytes) -> HEVCPpsInfo"),
      "HEVC (H.265) PPS をパースする");

  // HEVCVpsInfo クラス
  nb::class_<HEVCVpsInfo>(m, "HEVCVpsInfo")
      .def(nb::init<>())
      .def_ro("vps_id", &HEVCVpsInfo::vps_id)
      .def_ro("max_layers_minus1", &HEVCVpsInfo::max_layers_minus1)
      .def_ro("max_sub_layers_minus1", &HEVCVpsInfo::max_sub_layers_minus1);

  // HEVCSpsInfo クラス
  nb::class_<HEVCSpsInfo>(m, "HEVCSpsInfo")
      .def(nb::init<>())
      .def_ro("general_profile_idc", &HEVCSpsInfo::general_profile_idc)
      .def_ro("general_level_idc", &HEVCSpsInfo::general_level_idc)
      .def_ro("general_tier_flag", &HEVCSpsInfo::general_tier_flag)
      .def_ro("width", &HEVCSpsInfo::width)
      .def_ro("height", &HEVCSpsInfo::height)
      .def_ro("bit_depth_luma", &HEVCSpsInfo::bit_depth_luma)
      .def_ro("bit_depth_chroma", &HEVCSpsInfo::bit_depth_chroma)
      .def_ro("chroma_format_idc", &HEVCSpsInfo::chroma_format_idc)
      .def_ro("framerate", &HEVCSpsInfo::framerate)
      .def_ro("sps_id", &HEVCSpsInfo::sps_id)
      .def_ro("vps_id", &HEVCSpsInfo::vps_id);

  // HEVCPpsInfo クラス
  nb::class_<HEVCPpsInfo>(m, "HEVCPpsInfo")
      .def(nb::init<>())
      .def_ro("pps_id", &HEVCPpsInfo::pps_id)
      .def_ro("sps_id", &HEVCPpsInfo::sps_id);

  // HEVCNalUnitHeader クラス
  nb::class_<HEVCNalUnitHeader>(m, "HEVCNalUnitHeader")
      .def(nb::init<>())
      .def_ro("nal_unit_type", &HEVCNalUnitHeader::nal_unit_type)
      .def_ro("nuh_layer_id", &HEVCNalUnitHeader::nuh_layer_id)
      .def_ro("nuh_temporal_id_plus1",
              &HEVCNalUnitHeader::nuh_temporal_id_plus1)
      .def_ro("is_irap", &HEVCNalUnitHeader::is_irap)
      .def_ro("is_key_frame", &HEVCNalUnitHeader::is_key_frame);

  // HEVCAnnexBInfo クラス
  nb::class_<HEVCAnnexBInfo>(m, "HEVCAnnexBInfo")
      .def(nb::init<>())
      .def_ro("vps", &HEVCAnnexBInfo::vps)
      .def_ro("sps", &HEVCAnnexBInfo::sps)
      .def_ro("pps", &HEVCAnnexBInfo::pps)
      .def_ro("nal_units", &HEVCAnnexBInfo::nal_units);

  // HEVCDescriptionInfo クラス
  nb::class_<HEVCDescriptionInfo>(m, "HEVCDescriptionInfo")
      .def(nb::init<>())
      .def_ro("vps", &HEVCDescriptionInfo::vps)
      .def_ro("sps", &HEVCDescriptionInfo::sps)
      .def_ro("pps", &HEVCDescriptionInfo::pps)
      .def_ro("nal_units", &HEVCDescriptionInfo::nal_units)
      .def_ro("length_size", &HEVCDescriptionInfo::length_size);
}
