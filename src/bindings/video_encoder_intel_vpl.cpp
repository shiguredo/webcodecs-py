// Intel VPL (Video Processing Library) バックエンドの実装
// このファイルは video_encoder.cpp から #include される

#include "video_encoder.h"

#if defined(__linux__)

#include <mfx.h>

#include <cstring>
#include <stdexcept>

#include "../dyn/vpl.h"
#include "encoded_video_chunk.h"
#include "video_frame.h"

namespace nb = nanobind;

// SPS/PPS バッファサイズ
static constexpr size_t MAX_SPS_SIZE = 256;
static constexpr size_t MAX_PPS_SIZE = 256;
// HEVC の VPS バッファサイズ
static constexpr size_t MAX_VPS_SIZE = 256;

namespace {

// コーデック ID を取得するヘルパー
mfxU32 get_codec_id(const std::string& codec) {
  if (codec.length() >= 5 &&
      (codec.substr(0, 5) == "avc1." || codec.substr(0, 5) == "avc3.")) {
    return MFX_CODEC_AVC;
  } else if (codec.length() >= 5 &&
             (codec.substr(0, 5) == "hvc1." || codec.substr(0, 5) == "hev1.")) {
    return MFX_CODEC_HEVC;
  }
  throw std::runtime_error("Unsupported codec for Intel VPL: " + codec);
}

// H.264 プロファイルを取得するヘルパー
mfxU16 get_avc_profile(const CodecParameters& codec_params) {
  if (std::holds_alternative<AVCCodecParameters>(codec_params)) {
    const auto& avc_params = std::get<AVCCodecParameters>(codec_params);
    switch (avc_params.profile_idc) {
      case 0x42:
        return MFX_PROFILE_AVC_BASELINE;
      case 0x4D:
        return MFX_PROFILE_AVC_MAIN;
      case 0x64:
        return MFX_PROFILE_AVC_HIGH;
      default:
        return MFX_PROFILE_AVC_HIGH;
    }
  }
  return MFX_PROFILE_AVC_HIGH;
}

// HEVC プロファイルを取得するヘルパー
mfxU16 get_hevc_profile(const CodecParameters& codec_params) {
  if (std::holds_alternative<HEVCCodecParameters>(codec_params)) {
    const auto& hevc_params = std::get<HEVCCodecParameters>(codec_params);
    switch (hevc_params.general_profile_idc) {
      case 1:
        return MFX_PROFILE_HEVC_MAIN;
      case 2:
        return MFX_PROFILE_HEVC_MAIN10;
      default:
        return MFX_PROFILE_HEVC_MAIN;
    }
  }
  return MFX_PROFILE_HEVC_MAIN;
}

// 16 バイトアライメント
mfxU16 align16(mfxU16 value) {
  return (value + 15) & ~15;
}

}  // namespace

void VideoEncoder::init_intel_vpl_encoder() {
  if (vpl_session_) {
    return;
  }

  // VPL ライブラリがロード可能かチェック
  if (!dyn::DynModule::IsLoadable(dyn::VPL_SO)) {
    throw std::runtime_error("Intel VPL library is not available");
  }

  // ローダーを作成
  mfxLoader loader = dyn::MFXLoad();
  if (!loader) {
    throw std::runtime_error("Failed to create Intel VPL loader");
  }
  vpl_loader_ = loader;

  // ハードウェア実装を選択するフィルタを設定
  mfxConfig cfg = dyn::MFXCreateConfig(loader);
  if (!cfg) {
    cleanup_intel_vpl_encoder();
    throw std::runtime_error("Failed to create Intel VPL config");
  }

  // 実装タイプをハードウェアに設定
  mfxVariant impl_value = {};
  impl_value.Type = MFX_VARIANT_TYPE_U32;
  impl_value.Data.U32 = MFX_IMPL_TYPE_HARDWARE;
  mfxStatus sts = dyn::MFXSetConfigFilterProperty(
      cfg, reinterpret_cast<const mfxU8*>("mfxImplDescription.Impl"),
      impl_value);
  if (sts != MFX_ERR_NONE) {
    cleanup_intel_vpl_encoder();
    throw std::runtime_error("Failed to set Intel VPL implementation type");
  }

  // コーデック ID を設定
  mfxU32 codec_id = get_codec_id(config_.codec);
  mfxVariant codec_value = {};
  codec_value.Type = MFX_VARIANT_TYPE_U32;
  codec_value.Data.U32 = codec_id;
  sts = dyn::MFXSetConfigFilterProperty(
      cfg,
      reinterpret_cast<const mfxU8*>(
          "mfxImplDescription.mfxEncoderDescription.encoder.CodecID"),
      codec_value);
  if (sts != MFX_ERR_NONE) {
    cleanup_intel_vpl_encoder();
    throw std::runtime_error("Failed to set Intel VPL codec ID");
  }

  // セッションを作成
  mfxSession session = nullptr;
  sts = dyn::MFXCreateSession(loader, 0, &session);
  if (sts != MFX_ERR_NONE || !session) {
    cleanup_intel_vpl_encoder();
    throw std::runtime_error("Failed to create Intel VPL session");
  }
  vpl_session_ = session;

  // エンコードパラメータを設定
  mfxVideoParam encode_params = {};
  encode_params.mfx.CodecId = codec_id;
  encode_params.mfx.TargetUsage = MFX_TARGETUSAGE_BALANCED;
  encode_params.mfx.FrameInfo.FrameRateExtN =
      static_cast<mfxU32>(config_.framerate.value_or(30.0) * 1000);
  encode_params.mfx.FrameInfo.FrameRateExtD = 1000;
  encode_params.mfx.FrameInfo.FourCC = MFX_FOURCC_NV12;
  encode_params.mfx.FrameInfo.ChromaFormat = MFX_CHROMAFORMAT_YUV420;
  encode_params.mfx.FrameInfo.Width =
      align16(static_cast<mfxU16>(config_.width));
  encode_params.mfx.FrameInfo.Height =
      align16(static_cast<mfxU16>(config_.height));
  encode_params.mfx.FrameInfo.CropX = 0;
  encode_params.mfx.FrameInfo.CropY = 0;
  encode_params.mfx.FrameInfo.CropW = static_cast<mfxU16>(config_.width);
  encode_params.mfx.FrameInfo.CropH = static_cast<mfxU16>(config_.height);
  encode_params.mfx.FrameInfo.PicStruct = MFX_PICSTRUCT_PROGRESSIVE;

  // プロファイルを設定
  if (codec_id == MFX_CODEC_AVC) {
    encode_params.mfx.CodecProfile = get_avc_profile(codec_params_);
  } else if (codec_id == MFX_CODEC_HEVC) {
    encode_params.mfx.CodecProfile = get_hevc_profile(codec_params_);
  }

  // レートコントロールの設定
  if (config_.bitrate_mode == VideoEncoderBitrateMode::CONSTANT) {
    encode_params.mfx.RateControlMethod = MFX_RATECONTROL_CBR;
  } else {
    encode_params.mfx.RateControlMethod = MFX_RATECONTROL_VBR;
  }
  encode_params.mfx.TargetKbps =
      static_cast<mfxU16>(config_.bitrate.value_or(1000000) / 1000);
  encode_params.mfx.MaxKbps =
      static_cast<mfxU16>(config_.bitrate.value_or(1000000) * 1.5 / 1000);

  // GOP 設定
  encode_params.mfx.GopPicSize =
      static_cast<mfxU16>(config_.framerate.value_or(30.0) * 2);
  encode_params.mfx.GopRefDist = 1;
  encode_params.mfx.IdrInterval = 0;

  // I/O パターン: システムメモリ
  encode_params.IOPattern = MFX_IOPATTERN_IN_SYSTEM_MEMORY;

  // エンコーダーを初期化
  sts = dyn::MFXVideoENCODE_Init(session, &encode_params);
  if (sts != MFX_ERR_NONE && sts != MFX_WRN_PARTIAL_ACCELERATION &&
      sts != MFX_WRN_INCOMPATIBLE_VIDEO_PARAM) {
    cleanup_intel_vpl_encoder();
    throw std::runtime_error("Failed to initialize Intel VPL encoder");
  }

  // SPS/PPS を抽出して avcC/hvcC 形式の description を生成
  bool is_hevc = (codec_id == MFX_CODEC_HEVC);
  {
    uint8_t sps_buf[MAX_SPS_SIZE] = {};
    uint8_t pps_buf[MAX_PPS_SIZE] = {};

    mfxExtCodingOptionSPSPPS sps_pps = {};
    sps_pps.Header.BufferId = MFX_EXTBUFF_CODING_OPTION_SPSPPS;
    sps_pps.Header.BufferSz = sizeof(sps_pps);
    sps_pps.SPSBuffer = sps_buf;
    sps_pps.SPSBufSize = MAX_SPS_SIZE;
    sps_pps.PPSBuffer = pps_buf;
    sps_pps.PPSBufSize = MAX_PPS_SIZE;

    mfxExtBuffer* ext_bufs[] = {&sps_pps.Header};

    mfxVideoParam out_params = {};
    out_params.ExtParam = ext_bufs;
    out_params.NumExtParam = 1;

    sts = dyn::MFXVideoENCODE_GetVideoParam(session, &out_params);
    if (sts == MFX_ERR_NONE && sps_pps.SPSBufSize > 0 &&
        sps_pps.PPSBufSize > 0) {
      build_vpl_description(sps_buf, sps_pps.SPSBufSize, pps_buf,
                            sps_pps.PPSBufSize, is_hevc);
    }
  }

  // ビットストリームバッファを確保
  // 最大フレームサイズを推定: width * height * 1.5 (NV12) * 0.5 (圧縮率)
  size_t buffer_size = config_.width * config_.height;
  if (buffer_size < 512 * 1024) {
    buffer_size = 512 * 1024;
  }
  vpl_bitstream_buffer_.resize(buffer_size);
}

void VideoEncoder::encode_frame_intel_vpl(const VideoFrame& frame,
                                          bool keyframe,
                                          std::optional<uint16_t> quantizer) {
  if (!vpl_session_) {
    throw std::runtime_error("Intel VPL encoder is not initialized");
  }

  mfxSession session = static_cast<mfxSession>(vpl_session_);

  // NV12 フォーマットに変換
  std::unique_ptr<VideoFrame> nv12;
  if (frame.format() != VideoPixelFormat::NV12) {
    nv12 = frame.convert_format(VideoPixelFormat::NV12);
  }
  const VideoFrame& src = nv12 ? *nv12 : frame;

  // サーフェスを取得
  mfxFrameSurface1* surface = nullptr;
  mfxStatus sts = dyn::MFXMemory_GetSurfaceForEncode(session, &surface);
  if (sts != MFX_ERR_NONE || !surface) {
    throw std::runtime_error("Failed to get Intel VPL encode surface");
  }

  // サーフェスをロック
  sts = surface->FrameInterface->Map(surface, MFX_MAP_WRITE);
  if (sts != MFX_ERR_NONE) {
    surface->FrameInterface->Release(surface);
    throw std::runtime_error("Failed to map Intel VPL surface");
  }

  // フレームデータをコピー
  uint32_t width = src.width();
  uint32_t height = src.height();
  const uint8_t* src_y = src.plane_ptr(0);
  const uint8_t* src_uv = src.plane_ptr(1);
  uint8_t* dst_y = surface->Data.Y;
  uint8_t* dst_uv = surface->Data.UV;
  uint32_t dst_pitch = surface->Data.Pitch;

  // Y プレーンをコピー
  for (uint32_t row = 0; row < height; ++row) {
    std::memcpy(dst_y + row * dst_pitch, src_y + row * width, width);
  }

  // UV プレーンをコピー
  uint32_t chroma_height = (height + 1) / 2;
  for (uint32_t row = 0; row < chroma_height; ++row) {
    std::memcpy(dst_uv + row * dst_pitch, src_uv + row * width, width);
  }

  // サーフェスをアンロック
  surface->FrameInterface->Unmap(surface);

  // タイムスタンプを設定
  surface->Data.TimeStamp = frame.timestamp();

  // ビットストリームを準備
  mfxBitstream bitstream = {};
  bitstream.Data = vpl_bitstream_buffer_.data();
  bitstream.MaxLength = static_cast<mfxU32>(vpl_bitstream_buffer_.size());

  // エンコード制御（キーフレーム強制）
  mfxEncodeCtrl ctrl = {};
  mfxEncodeCtrl* ctrl_ptr = nullptr;
  if (keyframe) {
    ctrl.FrameType = MFX_FRAMETYPE_I | MFX_FRAMETYPE_REF | MFX_FRAMETYPE_IDR;
    ctrl_ptr = &ctrl;
  }

  // quantizer オプション（Intel VPL は QP 制御をサポート、今回は無視）
  (void)quantizer;

  // エンコード実行
  mfxSyncPoint syncp = nullptr;
  sts = dyn::MFXVideoENCODE_EncodeFrameAsync(session, ctrl_ptr, surface,
                                             &bitstream, &syncp);

  // サーフェスを解放
  surface->FrameInterface->Release(surface);

  if (sts == MFX_ERR_MORE_DATA) {
    // さらにデータが必要（バッファリング中）
    return;
  }
  if (sts != MFX_ERR_NONE && sts != MFX_WRN_DEVICE_BUSY) {
    throw std::runtime_error("Intel VPL encode failed");
  }

  // 同期を待機
  if (syncp) {
    sts = dyn::MFXVideoCORE_SyncOperation(session, syncp, 1000);
    if (sts != MFX_ERR_NONE) {
      throw std::runtime_error("Intel VPL sync failed");
    }
  }

  // エンコード結果を取得
  if (bitstream.DataLength > 0) {
    bool is_keyframe = (bitstream.FrameType & MFX_FRAMETYPE_IDR) ||
                       (bitstream.FrameType & MFX_FRAMETYPE_I);

    std::vector<uint8_t> payload(bitstream.DataLength);
    std::memcpy(payload.data(), bitstream.Data + bitstream.DataOffset,
                bitstream.DataLength);

    // EncodedVideoChunk を作成して出力
    auto chunk = std::make_shared<EncodedVideoChunk>(
        payload,
        is_keyframe ? EncodedVideoChunkType::KEY : EncodedVideoChunkType::DELTA,
        frame.timestamp(), 0);

    // キーフレーム時は decoderConfig メタデータを含める
    std::optional<EncodedVideoChunkMetadata> metadata;
    if (is_keyframe) {
      EncodedVideoChunkMetadata meta;
      VideoDecoderConfig decoder_config;
      decoder_config.codec = config_.codec;
      decoder_config.coded_width = config_.width;
      decoder_config.coded_height = config_.height;
      // SPS/PPS から生成した avcC/hvcC を description に設定
      if (!vpl_description_.empty()) {
        decoder_config.description = vpl_description_;
      }
      meta.decoder_config = std::move(decoder_config);
      metadata = std::move(meta);
    }

    handle_output(current_sequence_, chunk, metadata);
  }
}

void VideoEncoder::flush_intel_vpl_encoder() {
  if (!vpl_session_) {
    return;
  }

  mfxSession session = static_cast<mfxSession>(vpl_session_);

  // ビットストリームを準備
  mfxBitstream bitstream = {};
  bitstream.Data = vpl_bitstream_buffer_.data();
  bitstream.MaxLength = static_cast<mfxU32>(vpl_bitstream_buffer_.size());

  // surface = nullptr でフラッシュ
  mfxSyncPoint syncp = nullptr;
  mfxStatus sts;
  while (true) {
    bitstream.DataLength = 0;
    bitstream.DataOffset = 0;

    sts = dyn::MFXVideoENCODE_EncodeFrameAsync(session, nullptr, nullptr,
                                               &bitstream, &syncp);
    if (sts == MFX_ERR_MORE_DATA) {
      // フラッシュ完了
      break;
    }
    if (sts != MFX_ERR_NONE && sts != MFX_WRN_DEVICE_BUSY) {
      break;
    }

    if (syncp) {
      sts = dyn::MFXVideoCORE_SyncOperation(session, syncp, 1000);
      if (sts != MFX_ERR_NONE) {
        break;
      }
    }

    // エンコード結果を出力
    if (bitstream.DataLength > 0) {
      bool is_keyframe = (bitstream.FrameType & MFX_FRAMETYPE_IDR) ||
                         (bitstream.FrameType & MFX_FRAMETYPE_I);

      std::vector<uint8_t> payload(bitstream.DataLength);
      std::memcpy(payload.data(), bitstream.Data + bitstream.DataOffset,
                  bitstream.DataLength);

      auto chunk = std::make_shared<EncodedVideoChunk>(
          payload,
          is_keyframe ? EncodedVideoChunkType::KEY
                      : EncodedVideoChunkType::DELTA,
          bitstream.TimeStamp, 0);

      handle_output(current_sequence_, chunk, std::nullopt);
    }
  }
}

void VideoEncoder::cleanup_intel_vpl_encoder() {
  if (vpl_session_) {
    mfxSession session = static_cast<mfxSession>(vpl_session_);
    dyn::MFXVideoENCODE_Close(session);
    dyn::MFXClose(session);
    vpl_session_ = nullptr;
  }

  if (vpl_loader_) {
    mfxLoader loader = static_cast<mfxLoader>(vpl_loader_);
    dyn::MFXUnload(loader);
    vpl_loader_ = nullptr;
  }

  vpl_bitstream_buffer_.clear();
  vpl_description_.clear();
}

// SPS/PPS から avcC (H.264) または hvcC (HEVC) 形式の description を生成
void VideoEncoder::build_vpl_description(const uint8_t* sps,
                                         uint16_t sps_size,
                                         const uint8_t* pps,
                                         uint16_t pps_size,
                                         bool is_hevc) {
  vpl_description_.clear();

  if (is_hevc) {
    // hvcC 形式の生成 (ISO/IEC 14496-15 Section 8.3.3.1.2)
    // HEVC の場合、VPS/SPS/PPS が必要だが、Intel VPL の SPS バッファには
    // VPS + SPS が含まれている可能性があるため、NAL ユニットを分離する必要がある

    // 簡易実装: VPS/SPS/PPS を NAL ユニットとして分離
    std::vector<std::pair<const uint8_t*, size_t>> nalus;

    // SPS バッファから NAL ユニットを抽出
    // Intel VPL は Annex B 形式 (start code 付き) で返す
    auto extract_nalus = [](const uint8_t* data, size_t size,
                            std::vector<std::pair<const uint8_t*, size_t>>& out) {
      size_t pos = 0;
      while (pos < size) {
        // start code を探す (0x000001 or 0x00000001)
        size_t start = pos;
        while (start + 3 <= size) {
          if (data[start] == 0 && data[start + 1] == 0) {
            if (data[start + 2] == 1) {
              start += 3;
              break;
            } else if (start + 4 <= size && data[start + 2] == 0 &&
                       data[start + 3] == 1) {
              start += 4;
              break;
            }
          }
          start++;
        }
        if (start >= size)
          break;

        // 次の start code または終端を探す
        size_t end = start;
        while (end + 3 <= size) {
          if (data[end] == 0 && data[end + 1] == 0 &&
              (data[end + 2] == 1 ||
               (end + 4 <= size && data[end + 2] == 0 && data[end + 3] == 1))) {
            break;
          }
          end++;
        }
        if (end == start)
          end = size;

        // trailing zero を除去
        while (end > start && data[end - 1] == 0)
          end--;

        if (end > start) {
          out.push_back({data + start, end - start});
        }
        pos = end;
      }
    };

    extract_nalus(sps, sps_size, nalus);
    extract_nalus(pps, pps_size, nalus);

    if (nalus.empty()) {
      return;
    }

    // VPS, SPS, PPS を分類
    const uint8_t* vps_data = nullptr;
    size_t vps_len = 0;
    const uint8_t* sps_data = nullptr;
    size_t sps_len = 0;
    const uint8_t* pps_data = nullptr;
    size_t pps_len = 0;

    for (const auto& nalu : nalus) {
      if (nalu.second < 2)
        continue;
      uint8_t nal_type = (nalu.first[0] >> 1) & 0x3F;
      if (nal_type == 32 && !vps_data) {
        // VPS
        vps_data = nalu.first;
        vps_len = nalu.second;
      } else if (nal_type == 33 && !sps_data) {
        // SPS
        sps_data = nalu.first;
        sps_len = nalu.second;
      } else if (nal_type == 34 && !pps_data) {
        // PPS
        pps_data = nalu.first;
        pps_len = nalu.second;
      }
    }

    if (!sps_data || sps_len < 2) {
      return;
    }

    // hvcC 構造を構築
    vpl_description_.reserve(256);

    // configurationVersion
    vpl_description_.push_back(1);

    // general_profile_space, general_tier_flag, general_profile_idc
    // SPS から取得する必要があるが、簡易実装としてデフォルト値を使用
    uint8_t profile_byte = 0x01;  // Main profile
    if (sps_len > 2) {
      // profile_tier_level は SPS の先頭付近にある
      profile_byte = sps_data[1] & 0x1F;  // general_profile_idc の下位 5 ビット
    }
    vpl_description_.push_back(profile_byte);

    // general_profile_compatibility_flags (4 bytes)
    vpl_description_.push_back(0x60);
    vpl_description_.push_back(0x00);
    vpl_description_.push_back(0x00);
    vpl_description_.push_back(0x00);

    // general_constraint_indicator_flags (6 bytes)
    vpl_description_.push_back(0x90);
    vpl_description_.push_back(0x00);
    vpl_description_.push_back(0x00);
    vpl_description_.push_back(0x00);
    vpl_description_.push_back(0x00);
    vpl_description_.push_back(0x00);

    // general_level_idc
    vpl_description_.push_back(0x5D);  // Level 3.1

    // min_spatial_segmentation_idc (4 bits reserved + 12 bits)
    vpl_description_.push_back(0xF0);
    vpl_description_.push_back(0x00);

    // parallelismType (6 bits reserved + 2 bits)
    vpl_description_.push_back(0xFC);

    // chromaFormat (6 bits reserved + 2 bits)
    vpl_description_.push_back(0xFD);  // 4:2:0

    // bitDepthLumaMinus8 (5 bits reserved + 3 bits)
    vpl_description_.push_back(0xF8);

    // bitDepthChromaMinus8 (5 bits reserved + 3 bits)
    vpl_description_.push_back(0xF8);

    // avgFrameRate (16 bits)
    vpl_description_.push_back(0x00);
    vpl_description_.push_back(0x00);

    // constantFrameRate, numTemporalLayers, temporalIdNested, lengthSizeMinusOne
    vpl_description_.push_back(0x0F);  // lengthSizeMinusOne = 3 (4 bytes)

    // numOfArrays
    uint8_t num_arrays = 0;
    if (vps_data)
      num_arrays++;
    if (sps_data)
      num_arrays++;
    if (pps_data)
      num_arrays++;
    vpl_description_.push_back(num_arrays);

    // VPS array
    if (vps_data) {
      vpl_description_.push_back(0xA0);  // array_completeness=1, NAL_unit_type=32
      vpl_description_.push_back(0x00);
      vpl_description_.push_back(0x01);  // numNalus = 1
      vpl_description_.push_back((vps_len >> 8) & 0xFF);
      vpl_description_.push_back(vps_len & 0xFF);
      vpl_description_.insert(vpl_description_.end(), vps_data,
                              vps_data + vps_len);
    }

    // SPS array
    if (sps_data) {
      vpl_description_.push_back(0xA1);  // array_completeness=1, NAL_unit_type=33
      vpl_description_.push_back(0x00);
      vpl_description_.push_back(0x01);  // numNalus = 1
      vpl_description_.push_back((sps_len >> 8) & 0xFF);
      vpl_description_.push_back(sps_len & 0xFF);
      vpl_description_.insert(vpl_description_.end(), sps_data,
                              sps_data + sps_len);
    }

    // PPS array
    if (pps_data) {
      vpl_description_.push_back(0xA2);  // array_completeness=1, NAL_unit_type=34
      vpl_description_.push_back(0x00);
      vpl_description_.push_back(0x01);  // numNalus = 1
      vpl_description_.push_back((pps_len >> 8) & 0xFF);
      vpl_description_.push_back(pps_len & 0xFF);
      vpl_description_.insert(vpl_description_.end(), pps_data,
                              pps_data + pps_len);
    }
  } else {
    // avcC 形式の生成 (ISO/IEC 14496-15 Section 5.2.4.1.1)

    // SPS から profile, profile_compat, level を抽出
    // SPS は Annex B 形式で返される可能性があるため、start code をスキップ
    const uint8_t* sps_nalu = sps;
    size_t sps_nalu_size = sps_size;

    // start code をスキップ (0x000001 or 0x00000001)
    if (sps_size >= 4 && sps[0] == 0 && sps[1] == 0 && sps[2] == 0 &&
        sps[3] == 1) {
      sps_nalu = sps + 4;
      sps_nalu_size = sps_size - 4;
    } else if (sps_size >= 3 && sps[0] == 0 && sps[1] == 0 && sps[2] == 1) {
      sps_nalu = sps + 3;
      sps_nalu_size = sps_size - 3;
    }

    const uint8_t* pps_nalu = pps;
    size_t pps_nalu_size = pps_size;

    if (pps_size >= 4 && pps[0] == 0 && pps[1] == 0 && pps[2] == 0 &&
        pps[3] == 1) {
      pps_nalu = pps + 4;
      pps_nalu_size = pps_size - 4;
    } else if (pps_size >= 3 && pps[0] == 0 && pps[1] == 0 && pps[2] == 1) {
      pps_nalu = pps + 3;
      pps_nalu_size = pps_size - 3;
    }

    // trailing zero を除去
    while (sps_nalu_size > 0 && sps_nalu[sps_nalu_size - 1] == 0) {
      sps_nalu_size--;
    }
    while (pps_nalu_size > 0 && pps_nalu[pps_nalu_size - 1] == 0) {
      pps_nalu_size--;
    }

    if (sps_nalu_size < 4 || pps_nalu_size < 1) {
      return;
    }

    // avcC 構造を構築
    // SPS の NAL ヘッダー (1 byte) の後に profile_idc, constraint_flags, level_idc がある
    uint8_t profile_idc = sps_nalu[1];
    uint8_t profile_compat = sps_nalu[2];
    uint8_t level_idc = sps_nalu[3];

    vpl_description_.reserve(11 + sps_nalu_size + pps_nalu_size);

    // configurationVersion
    vpl_description_.push_back(1);
    // AVCProfileIndication
    vpl_description_.push_back(profile_idc);
    // profile_compatibility
    vpl_description_.push_back(profile_compat);
    // AVCLevelIndication
    vpl_description_.push_back(level_idc);
    // reserved (6 bits) + lengthSizeMinusOne (2 bits) = 0xFF (4 bytes NALU length)
    vpl_description_.push_back(0xFF);
    // reserved (3 bits) + numOfSequenceParameterSets (5 bits) = 0xE1 (1 SPS)
    vpl_description_.push_back(0xE1);
    // sequenceParameterSetLength (big endian)
    vpl_description_.push_back((sps_nalu_size >> 8) & 0xFF);
    vpl_description_.push_back(sps_nalu_size & 0xFF);
    // sequenceParameterSetNALUnit
    vpl_description_.insert(vpl_description_.end(), sps_nalu,
                            sps_nalu + sps_nalu_size);
    // numOfPictureParameterSets
    vpl_description_.push_back(1);
    // pictureParameterSetLength (big endian)
    vpl_description_.push_back((pps_nalu_size >> 8) & 0xFF);
    vpl_description_.push_back(pps_nalu_size & 0xFF);
    // pictureParameterSetNALUnit
    vpl_description_.insert(vpl_description_.end(), pps_nalu,
                            pps_nalu + pps_nalu_size);
  }
}

#endif  // defined(__linux__)
