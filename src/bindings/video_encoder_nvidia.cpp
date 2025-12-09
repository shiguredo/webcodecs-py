// NVIDIA Video Codec SDK (NVENC) バックエンドの実装
// このファイルは video_encoder.cpp から #include される

#include "video_encoder.h"

#if defined(USE_NVIDIA_CUDA_TOOLKIT)

#include <cuda.h>
#include <nvEncodeAPI.h>

#include <cstring>
#include <stdexcept>

#include "../dyn/cuda.h"
#include "../dyn/nvenc.h"
#include "encoded_video_chunk.h"
#include "video_frame.h"

namespace nb = nanobind;

namespace {

// NVENC API 関数テーブルをロードするヘルパー
NV_ENCODE_API_FUNCTION_LIST* load_nvenc_api() {
  static NV_ENCODE_API_FUNCTION_LIST nvenc_funcs = {};
  static bool initialized = false;

  if (initialized) {
    return &nvenc_funcs;
  }

  nvenc_funcs.version = NV_ENCODE_API_FUNCTION_LIST_VER;
  NVENCSTATUS status = dyn::NvEncodeAPICreateInstance(&nvenc_funcs);
  if (status != NV_ENC_SUCCESS) {
    throw std::runtime_error("Failed to create NVENC API instance");
  }

  initialized = true;
  return &nvenc_funcs;
}

// コーデック GUID を取得するヘルパー
GUID get_codec_guid(const std::string& codec) {
  if (codec.length() >= 5 &&
      (codec.substr(0, 5) == "avc1." || codec.substr(0, 5) == "avc3.")) {
    return NV_ENC_CODEC_H264_GUID;
  } else if (codec.length() >= 5 &&
             (codec.substr(0, 5) == "hvc1." || codec.substr(0, 5) == "hev1.")) {
    return NV_ENC_CODEC_HEVC_GUID;
  } else if (codec.length() >= 5 && codec.substr(0, 5) == "av01.") {
    return NV_ENC_CODEC_AV1_GUID;
  }
  throw std::runtime_error("Unsupported codec for NVENC: " + codec);
}

// プリセット GUID を取得するヘルパー
GUID get_preset_guid(LatencyMode latency_mode) {
  // P4 はバランスの取れたプリセット
  // 低遅延モードでは P1 を使用
  if (latency_mode == LatencyMode::REALTIME) {
    return NV_ENC_PRESET_P1_GUID;
  }
  return NV_ENC_PRESET_P4_GUID;
}

// プロファイル GUID を取得するヘルパー
GUID get_profile_guid(const std::string& codec,
                      const CodecParameters& codec_params) {
  if (std::holds_alternative<AVCCodecParameters>(codec_params)) {
    const auto& avc_params = std::get<AVCCodecParameters>(codec_params);
    switch (avc_params.profile_idc) {
      case 0x42:
        return NV_ENC_H264_PROFILE_BASELINE_GUID;
      case 0x4D:
        return NV_ENC_H264_PROFILE_MAIN_GUID;
      case 0x64:
        return NV_ENC_H264_PROFILE_HIGH_GUID;
      default:
        return NV_ENC_H264_PROFILE_HIGH_GUID;
    }
  } else if (std::holds_alternative<HEVCCodecParameters>(codec_params)) {
    const auto& hevc_params = std::get<HEVCCodecParameters>(codec_params);
    switch (hevc_params.general_profile_idc) {
      case 1:
        return NV_ENC_HEVC_PROFILE_MAIN_GUID;
      case 2:
        return NV_ENC_HEVC_PROFILE_MAIN10_GUID;
      default:
        return NV_ENC_HEVC_PROFILE_MAIN_GUID;
    }
  } else if (std::holds_alternative<AV1CodecParameters>(codec_params)) {
    return NV_ENC_AV1_PROFILE_MAIN_GUID;
  }
  return NV_ENC_CODEC_PROFILE_AUTOSELECT_GUID;
}

}  // namespace

void VideoEncoder::init_nvenc_encoder() {
  if (nvenc_encoder_) {
    return;
  }

  // CUDA ライブラリがロード可能かチェック
  if (!dyn::DynModule::IsLoadable(dyn::CUDA_SO)) {
    throw std::runtime_error("CUDA library is not available");
  }

  // CUDA の初期化
  CUresult cu_result = dyn::cuInit(0);
  if (cu_result != CUDA_SUCCESS) {
    throw std::runtime_error("Failed to initialize CUDA");
  }

  // CUDA デバイスを取得
  CUdevice cu_device;
  cu_result = dyn::cuDeviceGet(&cu_device, 0);
  if (cu_result != CUDA_SUCCESS) {
    throw std::runtime_error("Failed to get CUDA device");
  }

  // CUDA コンテキストを作成
  CUcontext cu_context;
  CUctxCreateParams ctx_params = {};
  cu_result = dyn::cuCtxCreate(&cu_context, &ctx_params, 0, cu_device);
  if (cu_result != CUDA_SUCCESS) {
    throw std::runtime_error("Failed to create CUDA context");
  }
  nvenc_cuda_context_ = cu_context;

  // NVENC API をロード
  nvenc_api_ = load_nvenc_api();

  // エンコーダーセッションを開く
  NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS session_params = {};
  session_params.version = NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER;
  session_params.device = cu_context;
  session_params.deviceType = NV_ENC_DEVICE_TYPE_CUDA;
  session_params.apiVersion = NVENCAPI_VERSION;

  void* encoder = nullptr;
  NVENCSTATUS status =
      nvenc_api_->nvEncOpenEncodeSessionEx(&session_params, &encoder);
  if (status != NV_ENC_SUCCESS) {
    dyn::cuCtxDestroy(cu_context);
    nvenc_cuda_context_ = nullptr;
    throw std::runtime_error("Failed to open NVENC encode session");
  }
  nvenc_encoder_ = encoder;

  // エンコーダーの初期化パラメータを設定
  GUID codec_guid = get_codec_guid(config_.codec);
  GUID preset_guid = get_preset_guid(config_.latency_mode);
  GUID profile_guid = get_profile_guid(config_.codec, codec_params_);

  // プリセット設定を取得
  NV_ENC_PRESET_CONFIG preset_config = {};
  preset_config.version = NV_ENC_PRESET_CONFIG_VER;
  preset_config.presetCfg.version = NV_ENC_CONFIG_VER;

  status = nvenc_api_->nvEncGetEncodePresetConfigEx(
      encoder, codec_guid, preset_guid, NV_ENC_TUNING_INFO_LOW_LATENCY,
      &preset_config);
  if (status != NV_ENC_SUCCESS) {
    cleanup_nvenc_encoder();
    throw std::runtime_error("Failed to get NVENC preset config");
  }

  // エンコード設定をカスタマイズ
  NV_ENC_CONFIG encode_config = preset_config.presetCfg;

  // GOP 長を設定 (キーフレーム間隔)
  // フレームレートの 2 秒分をデフォルトに
  encode_config.gopLength =
      static_cast<uint32_t>(config_.framerate.value_or(30.0) * 2);
  encode_config.frameIntervalP = 1;  // B フレームなし

  // レートコントロールの設定
  auto& rc_params = encode_config.rcParams;
  if (config_.bitrate_mode == VideoEncoderBitrateMode::CONSTANT) {
    rc_params.rateControlMode = NV_ENC_PARAMS_RC_CBR;
  } else {
    rc_params.rateControlMode = NV_ENC_PARAMS_RC_VBR;
  }
  rc_params.averageBitRate =
      static_cast<uint32_t>(config_.bitrate.value_or(1000000));
  rc_params.maxBitRate = static_cast<uint32_t>(
      config_.bitrate.value_or(1000000) * 1.5);  // 最大ビットレート

  // エンコーダー初期化パラメータ
  NV_ENC_INITIALIZE_PARAMS init_params = {};
  init_params.version = NV_ENC_INITIALIZE_PARAMS_VER;
  init_params.encodeGUID = codec_guid;
  init_params.presetGUID = preset_guid;
  init_params.encodeWidth = config_.width;
  init_params.encodeHeight = config_.height;
  init_params.darWidth = config_.width;
  init_params.darHeight = config_.height;
  init_params.frameRateNum =
      static_cast<uint32_t>(config_.framerate.value_or(30.0) * 1000);
  init_params.frameRateDen = 1000;
  init_params.enablePTD = 1;  // Picture Type Decision
  init_params.encodeConfig = &encode_config;
  init_params.tuningInfo = NV_ENC_TUNING_INFO_LOW_LATENCY;

  status = nvenc_api_->nvEncInitializeEncoder(encoder, &init_params);
  if (status != NV_ENC_SUCCESS) {
    cleanup_nvenc_encoder();
    throw std::runtime_error("Failed to initialize NVENC encoder");
  }

  // SPS/PPS を取得して description を生成
  bool is_hevc = (codec_guid.Data1 == NV_ENC_CODEC_HEVC_GUID.Data1);
  {
    // SPS/PPS を取得するためのバッファを確保
    std::vector<uint8_t> sps_pps_buffer(1024);
    uint32_t out_size = 0;

    NV_ENC_SEQUENCE_PARAM_PAYLOAD seq_params = {};
    seq_params.version = NV_ENC_SEQUENCE_PARAM_PAYLOAD_VER;
    seq_params.spsppsBuffer = sps_pps_buffer.data();
    seq_params.inBufferSize = static_cast<uint32_t>(sps_pps_buffer.size());
    seq_params.outSPSPPSPayloadSize = &out_size;

    status = nvenc_api_->nvEncGetSequenceParams(encoder, &seq_params);
    if (status == NV_ENC_SUCCESS && out_size > 0) {
      // Annex B 形式のデータから SPS/PPS を抽出して description を生成
      const uint8_t* data = sps_pps_buffer.data();
      size_t size = out_size;

      // NAL ユニットを分離
      std::vector<std::pair<const uint8_t*, size_t>> nalus;
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
          nalus.push_back({data + start, end - start});
        }
        pos = end;
      }

      if (!nalus.empty()) {
        if (is_hevc) {
          // HEVC: VPS, SPS, PPS を分類
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
              vps_data = nalu.first;
              vps_len = nalu.second;
            } else if (nal_type == 33 && !sps_data) {
              sps_data = nalu.first;
              sps_len = nalu.second;
            } else if (nal_type == 34 && !pps_data) {
              pps_data = nalu.first;
              pps_len = nalu.second;
            }
          }

          if (sps_data && sps_len > 0) {
            // hvcC を構築
            build_nvenc_description(sps_data, sps_len, pps_data,
                                    pps_len ? pps_len : 0, true);
            // VPS も含める場合は別途処理が必要
            // 簡略化のため、VPS を含めた完全な hvcC を構築
            if (vps_data && vps_len > 0) {
              // 完全な hvcC を再構築
              nvenc_description_.clear();
              build_nvenc_hevc_description(vps_data, vps_len, sps_data, sps_len,
                                           pps_data, pps_len);
            }
          }
        } else {
          // H.264: SPS, PPS を分類
          const uint8_t* sps_data = nullptr;
          size_t sps_len = 0;
          const uint8_t* pps_data = nullptr;
          size_t pps_len = 0;

          for (const auto& nalu : nalus) {
            if (nalu.second < 1)
              continue;
            uint8_t nal_type = nalu.first[0] & 0x1F;
            if (nal_type == 7 && !sps_data) {
              sps_data = nalu.first;
              sps_len = nalu.second;
            } else if (nal_type == 8 && !pps_data) {
              pps_data = nalu.first;
              pps_len = nalu.second;
            }
          }

          if (sps_data && sps_len > 0 && pps_data && pps_len > 0) {
            build_nvenc_description(sps_data, sps_len, pps_data, pps_len,
                                    false);
          }
        }
      }
    }
  }

  // 入力バッファを作成
  NV_ENC_CREATE_INPUT_BUFFER create_input_buffer = {};
  create_input_buffer.version = NV_ENC_CREATE_INPUT_BUFFER_VER;
  create_input_buffer.width = config_.width;
  create_input_buffer.height = config_.height;
  create_input_buffer.bufferFmt = NV_ENC_BUFFER_FORMAT_NV12;

  status = nvenc_api_->nvEncCreateInputBuffer(encoder, &create_input_buffer);
  if (status != NV_ENC_SUCCESS) {
    cleanup_nvenc_encoder();
    throw std::runtime_error("Failed to create NVENC input buffer");
  }
  nvenc_input_buffer_ = create_input_buffer.inputBuffer;

  // 出力バッファを作成
  NV_ENC_CREATE_BITSTREAM_BUFFER create_output_buffer = {};
  create_output_buffer.version = NV_ENC_CREATE_BITSTREAM_BUFFER_VER;

  status =
      nvenc_api_->nvEncCreateBitstreamBuffer(encoder, &create_output_buffer);
  if (status != NV_ENC_SUCCESS) {
    cleanup_nvenc_encoder();
    throw std::runtime_error("Failed to create NVENC output buffer");
  }
  nvenc_output_buffer_ = create_output_buffer.bitstreamBuffer;
}

void VideoEncoder::encode_frame_nvenc(const VideoFrame& frame,
                                      bool keyframe,
                                      std::optional<uint16_t> quantizer) {
  if (!nvenc_encoder_) {
    throw std::runtime_error("NVENC encoder is not initialized");
  }

  // NV12 フォーマットに変換
  std::unique_ptr<VideoFrame> nv12;
  if (frame.format() != VideoPixelFormat::NV12) {
    nv12 = frame.convert_format(VideoPixelFormat::NV12);
  }
  const VideoFrame& src = nv12 ? *nv12 : frame;

  // 入力バッファをロック
  NV_ENC_LOCK_INPUT_BUFFER lock_input_buffer = {};
  lock_input_buffer.version = NV_ENC_LOCK_INPUT_BUFFER_VER;
  lock_input_buffer.inputBuffer = nvenc_input_buffer_;

  NVENCSTATUS status =
      nvenc_api_->nvEncLockInputBuffer(nvenc_encoder_, &lock_input_buffer);
  if (status != NV_ENC_SUCCESS) {
    throw std::runtime_error("Failed to lock NVENC input buffer");
  }

  // フレームデータをコピー
  uint8_t* dst_y = static_cast<uint8_t*>(lock_input_buffer.bufferDataPtr);
  uint32_t dst_pitch = lock_input_buffer.pitch;
  uint32_t width = src.width();
  uint32_t height = src.height();

  const uint8_t* src_y = src.plane_ptr(0);
  const uint8_t* src_uv = src.plane_ptr(1);

  // Y プレーンをコピー
  for (uint32_t row = 0; row < height; ++row) {
    std::memcpy(dst_y + row * dst_pitch, src_y + row * width, width);
  }

  // UV プレーンをコピー
  uint8_t* dst_uv = dst_y + dst_pitch * height;
  uint32_t chroma_height = (height + 1) / 2;
  for (uint32_t row = 0; row < chroma_height; ++row) {
    std::memcpy(dst_uv + row * dst_pitch, src_uv + row * width, width);
  }

  // 入力バッファをアンロック
  status =
      nvenc_api_->nvEncUnlockInputBuffer(nvenc_encoder_, nvenc_input_buffer_);
  if (status != NV_ENC_SUCCESS) {
    throw std::runtime_error("Failed to unlock NVENC input buffer");
  }

  // エンコードパラメータを設定
  NV_ENC_PIC_PARAMS pic_params = {};
  pic_params.version = NV_ENC_PIC_PARAMS_VER;
  pic_params.inputBuffer = nvenc_input_buffer_;
  pic_params.outputBitstream = nvenc_output_buffer_;
  pic_params.bufferFmt = NV_ENC_BUFFER_FORMAT_NV12;
  pic_params.inputWidth = width;
  pic_params.inputHeight = height;
  pic_params.pictureStruct = NV_ENC_PIC_STRUCT_FRAME;
  pic_params.inputTimeStamp = frame.timestamp();

  // キーフレームを強制
  if (keyframe) {
    pic_params.encodePicFlags = NV_ENC_PIC_FLAG_FORCEIDR;
  }

  // quantizer オプション（NVENC は QP マップをサポートするが、簡略化のため無視）
  (void)quantizer;

  // エンコード実行
  status = nvenc_api_->nvEncEncodePicture(nvenc_encoder_, &pic_params);
  if (status != NV_ENC_SUCCESS) {
    throw std::runtime_error("NVENC encode failed");
  }

  // 出力バッファをロック
  NV_ENC_LOCK_BITSTREAM lock_bitstream = {};
  lock_bitstream.version = NV_ENC_LOCK_BITSTREAM_VER;
  lock_bitstream.outputBitstream = nvenc_output_buffer_;

  status = nvenc_api_->nvEncLockBitstream(nvenc_encoder_, &lock_bitstream);
  if (status != NV_ENC_SUCCESS) {
    throw std::runtime_error("Failed to lock NVENC bitstream");
  }

  // エンコード結果を取得
  bool is_keyframe = (lock_bitstream.pictureType == NV_ENC_PIC_TYPE_IDR ||
                      lock_bitstream.pictureType == NV_ENC_PIC_TYPE_I);

  std::vector<uint8_t> payload(lock_bitstream.bitstreamSizeInBytes);
  std::memcpy(payload.data(), lock_bitstream.bitstreamBufferPtr,
              lock_bitstream.bitstreamSizeInBytes);

  // 出力バッファをアンロック
  status =
      nvenc_api_->nvEncUnlockBitstream(nvenc_encoder_, nvenc_output_buffer_);
  if (status != NV_ENC_SUCCESS) {
    throw std::runtime_error("Failed to unlock NVENC bitstream");
  }

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
    if (!nvenc_description_.empty()) {
      decoder_config.description = nvenc_description_;
    }
    meta.decoder_config = std::move(decoder_config);
    metadata = std::move(meta);
  }

  handle_output(current_sequence_, chunk, metadata);
}

void VideoEncoder::flush_nvenc_encoder() {
  if (!nvenc_encoder_) {
    return;
  }

  // EOS フレームを送信してフラッシュ
  NV_ENC_PIC_PARAMS pic_params = {};
  pic_params.version = NV_ENC_PIC_PARAMS_VER;
  pic_params.encodePicFlags = NV_ENC_PIC_FLAG_EOS;

  NVENCSTATUS status =
      nvenc_api_->nvEncEncodePicture(nvenc_encoder_, &pic_params);
  if (status != NV_ENC_SUCCESS && status != NV_ENC_ERR_NEED_MORE_INPUT) {
    // フラッシュ時のエラーは致命的ではない
  }
}

void VideoEncoder::cleanup_nvenc_encoder() {
  if (nvenc_encoder_ && nvenc_api_) {
    // 入力バッファを破棄
    if (nvenc_input_buffer_) {
      nvenc_api_->nvEncDestroyInputBuffer(nvenc_encoder_, nvenc_input_buffer_);
      nvenc_input_buffer_ = nullptr;
    }

    // 出力バッファを破棄
    if (nvenc_output_buffer_) {
      nvenc_api_->nvEncDestroyBitstreamBuffer(nvenc_encoder_,
                                              nvenc_output_buffer_);
      nvenc_output_buffer_ = nullptr;
    }

    // エンコーダーを破棄
    nvenc_api_->nvEncDestroyEncoder(nvenc_encoder_);
    nvenc_encoder_ = nullptr;
  }

  // CUDA コンテキストを破棄
  if (nvenc_cuda_context_) {
    dyn::cuCtxDestroy(static_cast<CUcontext>(nvenc_cuda_context_));
    nvenc_cuda_context_ = nullptr;
  }

  nvenc_api_ = nullptr;
  nvenc_description_.clear();
}

// SPS/PPS から avcC (H.264) 形式の description を生成
void VideoEncoder::build_nvenc_description(const uint8_t* sps,
                                           size_t sps_size,
                                           const uint8_t* pps,
                                           size_t pps_size,
                                           bool is_hevc) {
  nvenc_description_.clear();

  if (is_hevc) {
    // HEVC の場合は build_nvenc_hevc_description を使用
    // この関数は H.264 用のシンプルなケースのみ処理
    return;
  }

  // avcC 形式の生成 (ISO/IEC 14496-15 Section 5.2.4.1.1)
  if (sps_size < 4 || pps_size < 1) {
    return;
  }

  uint8_t profile_idc = sps[1];
  uint8_t profile_compat = sps[2];
  uint8_t level_idc = sps[3];

  nvenc_description_.reserve(11 + sps_size + pps_size);

  // configurationVersion
  nvenc_description_.push_back(1);
  // AVCProfileIndication
  nvenc_description_.push_back(profile_idc);
  // profile_compatibility
  nvenc_description_.push_back(profile_compat);
  // AVCLevelIndication
  nvenc_description_.push_back(level_idc);
  // lengthSizeMinusOne (4バイト長 - 1 = 3) + reserved (6 bits = 0x3F)
  nvenc_description_.push_back(0xFF);
  // numOfSequenceParameterSets + reserved (3 bits = 0xE0)
  nvenc_description_.push_back(0xE1);
  // sequenceParameterSetLength (2 bytes, big endian)
  nvenc_description_.push_back((sps_size >> 8) & 0xFF);
  nvenc_description_.push_back(sps_size & 0xFF);
  // sequenceParameterSetNALUnit
  nvenc_description_.insert(nvenc_description_.end(), sps, sps + sps_size);
  // numOfPictureParameterSets
  nvenc_description_.push_back(1);
  // pictureParameterSetLength (2 bytes, big endian)
  nvenc_description_.push_back((pps_size >> 8) & 0xFF);
  nvenc_description_.push_back(pps_size & 0xFF);
  // pictureParameterSetNALUnit
  nvenc_description_.insert(nvenc_description_.end(), pps, pps + pps_size);
}

// VPS/SPS/PPS から hvcC (HEVC) 形式の description を生成
void VideoEncoder::build_nvenc_hevc_description(const uint8_t* vps,
                                                size_t vps_size,
                                                const uint8_t* sps,
                                                size_t sps_size,
                                                const uint8_t* pps,
                                                size_t pps_size) {
  nvenc_description_.clear();

  if (!sps || sps_size < 2) {
    return;
  }

  // hvcC 構造を構築 (ISO/IEC 14496-15 Section 8.3.3.1.2)
  nvenc_description_.reserve(256);

  // configurationVersion
  nvenc_description_.push_back(1);

  // general_profile_space, general_tier_flag, general_profile_idc
  uint8_t profile_byte = 0x01;
  if (sps_size > 2) {
    profile_byte = sps[1] & 0x1F;
  }
  nvenc_description_.push_back(profile_byte);

  // general_profile_compatibility_flags (4 bytes)
  nvenc_description_.push_back(0x60);
  nvenc_description_.push_back(0x00);
  nvenc_description_.push_back(0x00);
  nvenc_description_.push_back(0x00);

  // general_constraint_indicator_flags (6 bytes)
  nvenc_description_.push_back(0x90);
  nvenc_description_.push_back(0x00);
  nvenc_description_.push_back(0x00);
  nvenc_description_.push_back(0x00);
  nvenc_description_.push_back(0x00);
  nvenc_description_.push_back(0x00);

  // general_level_idc
  nvenc_description_.push_back(0x5D);

  // min_spatial_segmentation_idc (4 bits reserved + 12 bits)
  nvenc_description_.push_back(0xF0);
  nvenc_description_.push_back(0x00);

  // parallelismType (6 bits reserved + 2 bits)
  nvenc_description_.push_back(0xFC);

  // chromaFormat (6 bits reserved + 2 bits)
  nvenc_description_.push_back(0xFD);

  // bitDepthLumaMinus8 (5 bits reserved + 3 bits)
  nvenc_description_.push_back(0xF8);

  // bitDepthChromaMinus8 (5 bits reserved + 3 bits)
  nvenc_description_.push_back(0xF8);

  // avgFrameRate (16 bits)
  nvenc_description_.push_back(0x00);
  nvenc_description_.push_back(0x00);

  // constantFrameRate, numTemporalLayers, temporalIdNested, lengthSizeMinusOne
  nvenc_description_.push_back(0x0F);

  // numOfArrays
  uint8_t num_arrays = 0;
  if (vps && vps_size > 0)
    num_arrays++;
  if (sps && sps_size > 0)
    num_arrays++;
  if (pps && pps_size > 0)
    num_arrays++;
  nvenc_description_.push_back(num_arrays);

  // VPS array (NAL type 32)
  if (vps && vps_size > 0) {
    // array_completeness(1) + reserved(1) + NAL_unit_type(6)
    nvenc_description_.push_back(0xA0);  // 1 01 100000 = completeness + VPS
    // numNalus (2 bytes)
    nvenc_description_.push_back(0x00);
    nvenc_description_.push_back(0x01);
    // nalUnitLength (2 bytes)
    nvenc_description_.push_back((vps_size >> 8) & 0xFF);
    nvenc_description_.push_back(vps_size & 0xFF);
    // nalUnit
    nvenc_description_.insert(nvenc_description_.end(), vps, vps + vps_size);
  }

  // SPS array (NAL type 33)
  if (sps && sps_size > 0) {
    nvenc_description_.push_back(0xA1);  // 1 01 100001 = completeness + SPS
    nvenc_description_.push_back(0x00);
    nvenc_description_.push_back(0x01);
    nvenc_description_.push_back((sps_size >> 8) & 0xFF);
    nvenc_description_.push_back(sps_size & 0xFF);
    nvenc_description_.insert(nvenc_description_.end(), sps, sps + sps_size);
  }

  // PPS array (NAL type 34)
  if (pps && pps_size > 0) {
    nvenc_description_.push_back(0xA2);  // 1 01 100010 = completeness + PPS
    nvenc_description_.push_back(0x00);
    nvenc_description_.push_back(0x01);
    nvenc_description_.push_back((pps_size >> 8) & 0xFF);
    nvenc_description_.push_back(pps_size & 0xFF);
    nvenc_description_.insert(nvenc_description_.end(), pps, pps + pps_size);
  }
}

#endif  // defined(USE_NVIDIA_CUDA_TOOLKIT)
