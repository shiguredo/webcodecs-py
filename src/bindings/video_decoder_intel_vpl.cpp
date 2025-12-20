// Intel VPL (Video Processing Library) デコーダーバックエンドの実装
// このファイルは video_decoder.cpp から #include される

#include "video_decoder.h"

#if defined(__linux__)

#include <mfx.h>

#include <cstring>
#include <stdexcept>

#include "../dyn/vpl.h"
#include "intel_vpl_helpers.h"
#include "video_frame.h"

void VideoDecoder::init_intel_vpl_decoder() {
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
    cleanup_intel_vpl_decoder();
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
    cleanup_intel_vpl_decoder();
    throw std::runtime_error("Failed to set Intel VPL implementation type");
  }

  // コーデック ID を設定
  mfxU32 codec_id = intel_vpl::get_codec_id(config_.codec);
  mfxVariant codec_value = {};
  codec_value.Type = MFX_VARIANT_TYPE_U32;
  codec_value.Data.U32 = codec_id;
  sts = dyn::MFXSetConfigFilterProperty(
      cfg,
      reinterpret_cast<const mfxU8*>(
          "mfxImplDescription.mfxDecoderDescription.decoder.CodecID"),
      codec_value);
  if (sts != MFX_ERR_NONE) {
    cleanup_intel_vpl_decoder();
    throw std::runtime_error("Failed to set Intel VPL decoder codec ID");
  }

  // セッションを作成
  mfxSession session = nullptr;
  sts = dyn::MFXCreateSession(loader, 0, &session);
  if (sts != MFX_ERR_NONE || !session) {
    cleanup_intel_vpl_decoder();
    throw std::runtime_error("Failed to create Intel VPL session");
  }
  vpl_session_ = session;

  // ビットストリームバッファを確保
  vpl_bitstream_buffer_.resize(intel_vpl::VPL_INITIAL_BITSTREAM_BUFFER_SIZE);

  vpl_initialized_ = false;
}

bool VideoDecoder::decode_intel_vpl(const EncodedVideoChunk& chunk) {
  if (!vpl_session_) {
    throw std::runtime_error("Intel VPL decoder is not initialized");
  }

  mfxSession session = static_cast<mfxSession>(vpl_session_);
  mfxU32 codec_id = intel_vpl::get_codec_id(config_.codec);

  // ビットストリームを取得または作成
  mfxBitstream* bitstream = static_cast<mfxBitstream*>(vpl_bitstream_);
  if (!bitstream) {
    bitstream = new mfxBitstream{};
    vpl_bitstream_buffer_.resize(intel_vpl::VPL_INITIAL_BITSTREAM_BUFFER_SIZE);
    bitstream->Data = vpl_bitstream_buffer_.data();
    bitstream->MaxLength = static_cast<mfxU32>(vpl_bitstream_buffer_.size());
    bitstream->DataLength = 0;
    bitstream->DataOffset = 0;
    vpl_bitstream_ = bitstream;
  }

  // 新しいデータを追加する前に、バッファを拡張する必要があるか確認
  const std::vector<uint8_t> data = chunk.data_vector();
  if (bitstream->MaxLength < bitstream->DataLength + data.size()) {
    vpl_bitstream_buffer_.resize(bitstream->DataLength + data.size());
    bitstream->MaxLength = static_cast<mfxU32>(vpl_bitstream_buffer_.size());
    bitstream->Data = vpl_bitstream_buffer_.data();
  }

  // 既存のデータを先頭に移動
  if (bitstream->DataOffset > 0) {
    std::memmove(bitstream->Data, bitstream->Data + bitstream->DataOffset,
                 bitstream->DataLength);
    bitstream->DataOffset = 0;
  }

  // 新しいデータを追加
  std::memcpy(bitstream->Data + bitstream->DataLength, data.data(),
              data.size());
  bitstream->DataLength += static_cast<mfxU32>(data.size());
  bitstream->TimeStamp = chunk.timestamp();

  // 初回のキーフレームでデコーダーを初期化
  if (!vpl_initialized_ && chunk.type() == EncodedVideoChunkType::KEY) {
    // デコードパラメータを設定
    mfxVideoParam decode_params = {};
    decode_params.mfx.CodecId = codec_id;
    decode_params.IOPattern = MFX_IOPATTERN_OUT_SYSTEM_MEMORY;

    // HEVC の場合はプロファイルを設定
    if (codec_id == MFX_CODEC_HEVC) {
      decode_params.mfx.CodecProfile = MFX_PROFILE_HEVC_MAIN;
    }

    // フレーム情報を設定
    if (config_.coded_width.has_value() && config_.coded_height.has_value()) {
      decode_params.mfx.FrameInfo.Width =
          intel_vpl::align16(static_cast<mfxU16>(config_.coded_width.value()));
      decode_params.mfx.FrameInfo.Height =
          intel_vpl::align16(static_cast<mfxU16>(config_.coded_height.value()));
      decode_params.mfx.FrameInfo.CropW =
          static_cast<mfxU16>(config_.coded_width.value());
      decode_params.mfx.FrameInfo.CropH =
          static_cast<mfxU16>(config_.coded_height.value());
      decode_params.mfx.FrameInfo.CropX = 0;
      decode_params.mfx.FrameInfo.CropY = 0;
    }
    decode_params.mfx.FrameInfo.FourCC = MFX_FOURCC_NV12;
    decode_params.mfx.FrameInfo.ChromaFormat = MFX_CHROMAFORMAT_YUV420;
    decode_params.mfx.FrameInfo.PicStruct = MFX_PICSTRUCT_PROGRESSIVE;

    decode_params.mfx.GopRefDist = 1;
    decode_params.AsyncDepth = 1;

    // ヘッダーをデコードしてパラメータを取得
    mfxStatus sts =
        dyn::MFXVideoDECODE_DecodeHeader(session, bitstream, &decode_params);
    if (sts != MFX_ERR_NONE && sts != MFX_WRN_PARTIAL_ACCELERATION) {
      throw std::runtime_error(
          intel_vpl::make_error_message("Decode header", sts));
    }

    // Query を呼んでパラメータを正規化
    mfxVideoParam query_params = decode_params;
    sts = dyn::MFXVideoDECODE_Query(session, &decode_params, &query_params);
    if (sts < MFX_ERR_NONE) {
      throw std::runtime_error(
          intel_vpl::make_error_message("Query decoder parameters", sts));
    }
    decode_params = query_params;

    // QueryIOSurf を呼ぶ
    mfxFrameAllocRequest alloc_request = {};
    sts = dyn::MFXVideoDECODE_QueryIOSurf(session, &decode_params,
                                          &alloc_request);
    if (sts != MFX_ERR_NONE) {
      throw std::runtime_error(
          intel_vpl::make_error_message("Query IO surface requirements", sts));
    }

    // デコーダーを初期化
    sts = dyn::MFXVideoDECODE_Init(session, &decode_params);
    if (sts != MFX_ERR_NONE && sts != MFX_WRN_PARTIAL_ACCELERATION &&
        sts != MFX_WRN_INCOMPATIBLE_VIDEO_PARAM) {
      throw std::runtime_error(
          intel_vpl::make_error_message("Initialize Intel VPL decoder", sts));
    }

    // サーフェスプールを初期化
    intel_vpl::SurfacePool* pool = new intel_vpl::SurfacePool();
    pool->init(alloc_request.NumFrameSuggested, alloc_request.Info,
               vpl_surface_buffer_);
    vpl_surface_pool_ = pool;

    vpl_initialized_ = true;
  }

  if (!vpl_initialized_) {
    return true;
  }

  // サーフェスプールから未使用のサーフェスを取得
  intel_vpl::SurfacePool* pool =
      static_cast<intel_vpl::SurfacePool*>(vpl_surface_pool_);
  mfxFrameSurface1* surface = pool->acquire();

  if (!surface) {
    throw std::runtime_error("No available surface for decoding");
  }

  // デコード実行
  while (true) {
    mfxFrameSurface1* surface_out = nullptr;
    mfxSyncPoint syncp = nullptr;

    mfxStatus sts = dyn::MFXVideoDECODE_DecodeFrameAsync(
        session, bitstream, surface, &surface_out, &syncp);

    if (sts == MFX_ERR_MORE_DATA) {
      pool->release(surface);
      break;
    }
    if (sts == MFX_ERR_MORE_SURFACE) {
      continue;
    }
    if (!syncp) {
      pool->release(surface);
      break;
    }
    if (sts != MFX_ERR_NONE && sts != MFX_WRN_DEVICE_BUSY &&
        sts != MFX_WRN_VIDEO_PARAM_CHANGED) {
      pool->release(surface);
      throw std::runtime_error(intel_vpl::make_error_message("Decode", sts));
    }

    // 同期を待機
    if (surface_out) {
      sts = dyn::MFXVideoCORE_SyncOperation(session, syncp,
                                            intel_vpl::VPL_SYNC_TIMEOUT_MS);
      if (sts != MFX_ERR_NONE) {
        pool->release(surface);
        throw std::runtime_error(intel_vpl::make_error_message("Sync", sts));
      }

      // VideoFrame を作成
      uint32_t width = surface_out->Info.CropW;
      uint32_t height = surface_out->Info.CropH;
      uint32_t pitch = surface_out->Data.Pitch;

      auto frame = std::make_unique<VideoFrame>(
          width, height, VideoPixelFormat::NV12, surface_out->Data.TimeStamp);

      // Y プレーンをコピー
      uint8_t* dst_y = frame->mutable_plane_ptr(0);
      const uint8_t* src_y = surface_out->Data.Y;
      for (uint32_t row = 0; row < height; ++row) {
        std::memcpy(dst_y + row * width, src_y + row * pitch, width);
      }

      // UV プレーンをコピー
      uint8_t* dst_uv = frame->mutable_plane_ptr(1);
      const uint8_t* src_uv = surface_out->Data.U;
      uint32_t chroma_height = (height + 1) / 2;
      for (uint32_t row = 0; row < chroma_height; ++row) {
        std::memcpy(dst_uv + row * width, src_uv + row * pitch, width);
      }

      handle_output(current_sequence_, std::move(frame));
    }
  }

  return true;
}

void VideoDecoder::flush_intel_vpl() {
  if (!vpl_session_ || !vpl_initialized_) {
    return;
  }

  mfxSession session = static_cast<mfxSession>(vpl_session_);

  // サーフェスプールから未使用のサーフェスを取得
  intel_vpl::SurfacePool* pool =
      static_cast<intel_vpl::SurfacePool*>(vpl_surface_pool_);
  mfxFrameSurface1* surface = pool->acquire();

  if (!surface) {
    return;
  }

  mfxFrameSurface1* surface_out = nullptr;
  mfxSyncPoint syncp = nullptr;
  mfxStatus sts;

  while (true) {
    sts = dyn::MFXVideoDECODE_DecodeFrameAsync(session, nullptr, surface,
                                               &surface_out, &syncp);
    if (sts == MFX_ERR_MORE_DATA) {
      break;
    }
    if (sts != MFX_ERR_NONE && sts != MFX_WRN_DEVICE_BUSY) {
      break;
    }

    if (!syncp || !surface_out) {
      continue;
    }

    sts = dyn::MFXVideoCORE_SyncOperation(session, syncp,
                                          intel_vpl::VPL_FLUSH_SYNC_TIMEOUT_MS);
    if (sts != MFX_ERR_NONE) {
      break;
    }

    // VideoFrame を作成
    uint32_t width = surface_out->Info.CropW;
    uint32_t height = surface_out->Info.CropH;
    uint32_t pitch = surface_out->Data.Pitch;

    auto frame = std::make_unique<VideoFrame>(
        width, height, VideoPixelFormat::NV12, surface_out->Data.TimeStamp);

    // Y プレーンをコピー
    uint8_t* dst_y = frame->mutable_plane_ptr(0);
    const uint8_t* src_y = surface_out->Data.Y;
    for (uint32_t row = 0; row < height; ++row) {
      std::memcpy(dst_y + row * width, src_y + row * pitch, width);
    }

    // UV プレーンをコピー
    uint8_t* dst_uv = frame->mutable_plane_ptr(1);
    const uint8_t* src_uv = surface_out->Data.U;
    uint32_t chroma_height = (height + 1) / 2;
    for (uint32_t row = 0; row < chroma_height; ++row) {
      std::memcpy(dst_uv + row * width, src_uv + row * pitch, width);
    }

    // flush では直接コールバックを呼ぶ
    nb::object output_cb;
    bool has_output;
    {
      nb::ft_lock_guard guard(callback_mutex_);
      output_cb = output_callback_;
      has_output = has_output_callback_;
    }
    if (has_output && !output_cb.is_none()) {
      nb::gil_scoped_acquire gil;
      output_cb(nb::cast(frame.release(), nb::rv_policy::take_ownership));
    }
  }

  pool->release(surface);
}

void VideoDecoder::cleanup_intel_vpl_decoder() {
  if (vpl_session_) {
    mfxSession session = static_cast<mfxSession>(vpl_session_);
    dyn::MFXVideoDECODE_Close(session);
    dyn::MFXClose(session);
    vpl_session_ = nullptr;
  }

  if (vpl_loader_) {
    mfxLoader loader = static_cast<mfxLoader>(vpl_loader_);
    dyn::MFXUnload(loader);
    vpl_loader_ = nullptr;
  }

  if (vpl_surface_pool_) {
    delete static_cast<intel_vpl::SurfacePool*>(vpl_surface_pool_);
    vpl_surface_pool_ = nullptr;
  }

  if (vpl_bitstream_) {
    delete static_cast<mfxBitstream*>(vpl_bitstream_);
    vpl_bitstream_ = nullptr;
  }

  vpl_bitstream_buffer_.clear();
  vpl_surface_buffer_.clear();
  vpl_initialized_ = false;
}

#endif  // defined(__linux__)
