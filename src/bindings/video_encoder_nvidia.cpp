// NVIDIA Video Codec SDK (NVENC) バックエンドの実装
// このファイルは video_encoder.cpp から #include される

#include "video_encoder.h"

#if defined(NVIDIA_CUDA_TOOLKIT)

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
}

#endif  // defined(NVIDIA_CUDA_TOOLKIT)
