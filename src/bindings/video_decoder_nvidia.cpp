// NVIDIA Video Codec SDK (NVDEC) バックエンドの実装
// このファイルは video_decoder.cpp から #include される

#include "video_decoder.h"

#if defined(NVIDIA_CUDA_TOOLKIT)

#include <cuda.h>
#include <cuviddec.h>
#include <nvcuvid.h>

#include <cstring>
#include <stdexcept>

#include "../dyn/cuda.h"
#include "../dyn/nvcuvid.h"
#include "video_frame.h"

namespace nb = nanobind;

namespace {

// コーデックタイプを変換するヘルパー
cudaVideoCodec get_cuda_video_codec(const std::string& codec) {
  if (codec.length() >= 5 &&
      (codec.substr(0, 5) == "avc1." || codec.substr(0, 5) == "avc3.")) {
    return cudaVideoCodec_H264;
  } else if (codec.length() >= 5 &&
             (codec.substr(0, 5) == "hvc1." || codec.substr(0, 5) == "hev1.")) {
    return cudaVideoCodec_HEVC;
  } else if (codec.length() >= 5 && codec.substr(0, 5) == "av01.") {
    return cudaVideoCodec_AV1;
  } else if (codec.length() >= 5 && codec.substr(0, 5) == "vp09.") {
    return cudaVideoCodec_VP9;
  } else if (codec == "vp8" || codec.substr(0, 3) == "vp8") {
    return cudaVideoCodec_VP8;
  }
  throw std::runtime_error("Unsupported codec for NVDEC: " + codec);
}

// デコーダーコンテキスト構造体
struct NVDecContext {
  VideoDecoder* decoder;
  CUvideodecoder cuda_decoder;
  CUvideoparser cuda_parser;
  CUcontext cuda_context;
  uint32_t width;
  uint32_t height;
  uint32_t decode_surface_count;
  uint64_t sequence_number;
  std::vector<std::unique_ptr<VideoFrame>> pending_frames;
};

}  // namespace

// パーサーコールバック: シーケンスヘッダー検出時
int VideoDecoder::handle_video_sequence(void* user_data,
                                        void* video_format_ptr) {
  auto* ctx = static_cast<NVDecContext*>(user_data);
  auto* video_format = static_cast<CUVIDEOFORMAT*>(video_format_ptr);

  // デコーダーの再作成が必要かチェック
  bool decoder_needs_recreate = (ctx->cuda_decoder == nullptr) ||
                                (ctx->width != video_format->coded_width) ||
                                (ctx->height != video_format->coded_height);

  if (decoder_needs_recreate) {
    // 既存のデコーダーを破棄
    if (ctx->cuda_decoder) {
      dyn::cuvidDestroyDecoder(ctx->cuda_decoder);
      ctx->cuda_decoder = nullptr;
    }

    // デコーダー作成パラメータを設定
    CUVIDDECODECREATEINFO decode_create_info = {};
    decode_create_info.CodecType = video_format->codec;
    decode_create_info.ChromaFormat = video_format->chroma_format;
    decode_create_info.OutputFormat = cudaVideoSurfaceFormat_NV12;
    decode_create_info.bitDepthMinus8 = video_format->bit_depth_luma_minus8;
    decode_create_info.DeinterlaceMode = cudaVideoDeinterlaceMode_Adaptive;
    decode_create_info.ulNumOutputSurfaces = 2;
    decode_create_info.ulNumDecodeSurfaces =
        video_format->min_num_decode_surfaces + 4;
    decode_create_info.ulCreationFlags = cudaVideoCreate_PreferCUVID;
    decode_create_info.ulWidth = video_format->coded_width;
    decode_create_info.ulHeight = video_format->coded_height;
    decode_create_info.ulMaxWidth = video_format->coded_width;
    decode_create_info.ulMaxHeight = video_format->coded_height;
    decode_create_info.ulTargetWidth = video_format->coded_width;
    decode_create_info.ulTargetHeight = video_format->coded_height;

    // 表示領域を設定
    decode_create_info.display_area.left = video_format->display_area.left;
    decode_create_info.display_area.top = video_format->display_area.top;
    decode_create_info.display_area.right = video_format->display_area.right;
    decode_create_info.display_area.bottom = video_format->display_area.bottom;

    CUresult result =
        dyn::cuvidCreateDecoder(&ctx->cuda_decoder, &decode_create_info);
    if (result != CUDA_SUCCESS) {
      return 0;  // デコーダー作成失敗
    }

    ctx->width = video_format->coded_width;
    ctx->height = video_format->coded_height;
    ctx->decode_surface_count = decode_create_info.ulNumDecodeSurfaces;
  }

  // デコードサーフェス数を返す
  return ctx->decode_surface_count;
}

// パーサーコールバック: ピクチャデコード
int VideoDecoder::handle_decode_picture(void* user_data, void* pic_params_ptr) {
  auto* ctx = static_cast<NVDecContext*>(user_data);
  auto* pic_params = static_cast<CUVIDPICPARAMS*>(pic_params_ptr);

  if (!ctx->cuda_decoder) {
    return 0;
  }

  CUresult result = dyn::cuvidDecodePicture(ctx->cuda_decoder, pic_params);
  return (result == CUDA_SUCCESS) ? 1 : 0;
}

// パーサーコールバック: 表示可能なピクチャ
int VideoDecoder::handle_display_picture(void* user_data, void* disp_info_ptr) {
  auto* ctx = static_cast<NVDecContext*>(user_data);
  auto* disp_info = static_cast<CUVIDPARSERDISPINFO*>(disp_info_ptr);

  if (!ctx->cuda_decoder || disp_info->picture_index < 0) {
    return 0;
  }

  // デコードされたフレームをマップ
  CUVIDPROCPARAMS proc_params = {};
  proc_params.progressive_frame = disp_info->progressive_frame;
  proc_params.second_field = 0;
  proc_params.top_field_first = disp_info->top_field_first;
  proc_params.unpaired_field = (disp_info->repeat_first_field < 0);

  CUdeviceptr mapped_frame = 0;
  unsigned int pitch = 0;

  CUresult result =
      dyn::cuvidMapVideoFrame(ctx->cuda_decoder, disp_info->picture_index,
                              &mapped_frame, &pitch, &proc_params);
  if (result != CUDA_SUCCESS) {
    return 0;
  }

  // NV12 フレームデータをホストメモリにコピー
  uint32_t width = ctx->width;
  uint32_t height = ctx->height;

  // VideoFrame 用のバッファを確保
  size_t y_size = static_cast<size_t>(width) * height;
  size_t uv_size = static_cast<size_t>(width) * ((height + 1) / 2);
  std::vector<uint8_t> frame_data(y_size + uv_size);

  // CUDA メモリからホストメモリにコピー
  CUDA_MEMCPY2D copy_params = {};
  copy_params.srcMemoryType = CU_MEMORYTYPE_DEVICE;
  copy_params.srcDevice = mapped_frame;
  copy_params.srcPitch = pitch;
  copy_params.dstMemoryType = CU_MEMORYTYPE_HOST;
  copy_params.dstHost = frame_data.data();
  copy_params.dstPitch = width;
  copy_params.WidthInBytes = width;
  copy_params.Height = height;

  // Y プレーンをコピー
  result = dyn::cuMemcpy2D(&copy_params);
  if (result != CUDA_SUCCESS) {
    dyn::cuvidUnmapVideoFrame(ctx->cuda_decoder, mapped_frame);
    return 0;
  }

  // UV プレーンをコピー
  copy_params.srcDevice = mapped_frame + pitch * height;
  copy_params.dstHost = frame_data.data() + y_size;
  copy_params.Height = (height + 1) / 2;
  result = dyn::cuMemcpy2D(&copy_params);

  dyn::cuvidUnmapVideoFrame(ctx->cuda_decoder, mapped_frame);

  if (result != CUDA_SUCCESS) {
    return 0;
  }

  // VideoFrame を作成
  auto video_frame = std::make_unique<VideoFrame>(
      width, height, VideoPixelFormat::NV12, disp_info->timestamp);

  // mutable_plane_ptr を使って直接データをコピー
  // Y プレーン
  uint8_t* y_dst = video_frame->mutable_plane_ptr(0);
  std::memcpy(y_dst, frame_data.data(), y_size);

  // UV プレーン
  uint8_t* uv_dst = video_frame->mutable_plane_ptr(1);
  std::memcpy(uv_dst, frame_data.data() + y_size, uv_size);

  ctx->decoder->handle_output(ctx->sequence_number++, std::move(video_frame));

  return 1;
}

void VideoDecoder::init_nvdec_decoder() {
  if (nvdec_decoder_) {
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
  nvdec_cuda_context_ = cu_context;

  // デコーダーコンテキストを作成
  auto* ctx = new NVDecContext();
  ctx->decoder = this;
  ctx->cuda_context = cu_context;
  ctx->cuda_decoder = nullptr;
  ctx->cuda_parser = nullptr;
  ctx->width = 0;
  ctx->height = 0;
  ctx->decode_surface_count = 0;
  ctx->sequence_number = 0;
  nvdec_decoder_ = ctx;

  // ビデオパーサーを作成
  cudaVideoCodec codec = get_cuda_video_codec(config_.codec);

  CUVIDPARSERPARAMS parser_params = {};
  parser_params.CodecType = codec;
  parser_params.ulMaxNumDecodeSurfaces = 20;
  parser_params.ulMaxDisplayDelay = 0;  // 低遅延モード
  parser_params.pUserData = ctx;
  parser_params.pfnSequenceCallback = reinterpret_cast<PFNVIDSEQUENCECALLBACK>(
      &VideoDecoder::handle_video_sequence);
  parser_params.pfnDecodePicture = reinterpret_cast<PFNVIDDECODECALLBACK>(
      &VideoDecoder::handle_decode_picture);
  parser_params.pfnDisplayPicture = reinterpret_cast<PFNVIDDISPLAYCALLBACK>(
      &VideoDecoder::handle_display_picture);

  CUresult result =
      dyn::cuvidCreateVideoParser(&ctx->cuda_parser, &parser_params);
  if (result != CUDA_SUCCESS) {
    cleanup_nvdec_decoder();
    throw std::runtime_error("Failed to create NVDEC video parser");
  }
  nvdec_video_parser_ = ctx->cuda_parser;
}

bool VideoDecoder::decode_nvdec(const EncodedVideoChunk& chunk) {
  if (!nvdec_decoder_) {
    throw std::runtime_error("NVDEC decoder is not initialized");
  }

  auto* ctx = static_cast<NVDecContext*>(nvdec_decoder_);

  // CUDA コンテキストをアクティブにする
  CUresult cu_result = dyn::cuCtxPushCurrent(ctx->cuda_context);
  if (cu_result != CUDA_SUCCESS) {
    return false;
  }

  // パケットを準備
  auto chunk_data = chunk.data_vector();
  CUVIDSOURCEDATAPACKET packet = {};
  packet.payload = chunk_data.data();
  packet.payload_size = static_cast<unsigned long>(chunk_data.size());
  packet.flags = CUVID_PKT_TIMESTAMP;
  packet.timestamp = chunk.timestamp();

  // キーフレームの場合は不連続フラグを設定
  if (chunk.type() == EncodedVideoChunkType::KEY) {
    packet.flags |= CUVID_PKT_DISCONTINUITY;
  }

  // パーサーにデータを送信
  CUresult result = dyn::cuvidParseVideoData(ctx->cuda_parser, &packet);

  dyn::cuCtxPopCurrent(nullptr);

  return (result == CUDA_SUCCESS);
}

void VideoDecoder::flush_nvdec() {
  if (!nvdec_decoder_) {
    return;
  }

  auto* ctx = static_cast<NVDecContext*>(nvdec_decoder_);

  if (ctx->cuda_parser) {
    // CUDA コンテキストをアクティブにする
    dyn::cuCtxPushCurrent(ctx->cuda_context);

    // EOS パケットを送信
    CUVIDSOURCEDATAPACKET packet = {};
    packet.flags = CUVID_PKT_ENDOFSTREAM;
    dyn::cuvidParseVideoData(ctx->cuda_parser, &packet);

    dyn::cuCtxPopCurrent(nullptr);
  }
}

void VideoDecoder::cleanup_nvdec_decoder() {
  if (nvdec_decoder_) {
    auto* ctx = static_cast<NVDecContext*>(nvdec_decoder_);

    if (ctx->cuda_context) {
      dyn::cuCtxPushCurrent(ctx->cuda_context);

      // パーサーを破棄
      if (ctx->cuda_parser) {
        dyn::cuvidDestroyVideoParser(ctx->cuda_parser);
        ctx->cuda_parser = nullptr;
      }

      // デコーダーを破棄
      if (ctx->cuda_decoder) {
        dyn::cuvidDestroyDecoder(ctx->cuda_decoder);
        ctx->cuda_decoder = nullptr;
      }

      dyn::cuCtxPopCurrent(nullptr);
    }

    delete ctx;
    nvdec_decoder_ = nullptr;
  }

  // CUDA コンテキストを破棄
  if (nvdec_cuda_context_) {
    dyn::cuCtxDestroy(static_cast<CUcontext>(nvdec_cuda_context_));
    nvdec_cuda_context_ = nullptr;
  }

  nvdec_video_parser_ = nullptr;
}

#endif  // defined(NVIDIA_CUDA_TOOLKIT)
