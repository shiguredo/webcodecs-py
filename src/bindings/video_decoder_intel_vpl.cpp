// Intel VPL (Video Processing Library) デコーダーバックエンドの実装
// このファイルは video_decoder.cpp から #include される

#include "video_decoder.h"

#if defined(__linux__)

#include <mfx.h>

#include <cstring>
#include <stdexcept>

#include "../dyn/vpl.h"
#include "video_frame.h"

namespace {

// コーデック ID を取得するヘルパー
mfxU32 get_decoder_codec_id(const std::string& codec) {
  if (codec.length() >= 5 &&
      (codec.substr(0, 5) == "avc1." || codec.substr(0, 5) == "avc3.")) {
    return MFX_CODEC_AVC;
  } else if (codec.length() >= 5 &&
             (codec.substr(0, 5) == "hvc1." || codec.substr(0, 5) == "hev1.")) {
    return MFX_CODEC_HEVC;
  }
  throw std::runtime_error("Unsupported codec for Intel VPL decoder: " + codec);
}

// 16 バイトアライメント
mfxU16 align16(mfxU16 value) {
  return (value + 15) & ~15;
}

}  // namespace

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
  mfxU32 codec_id = get_decoder_codec_id(config_.codec);
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

  // デコードパラメータを設定
  mfxVideoParam decode_params = {};
  decode_params.mfx.CodecId = codec_id;
  decode_params.IOPattern = MFX_IOPATTERN_OUT_SYSTEM_MEMORY;

  // フレーム情報を設定（解像度が分かっている場合）
  if (config_.coded_width.has_value() && config_.coded_height.has_value()) {
    decode_params.mfx.FrameInfo.Width =
        align16(static_cast<mfxU16>(config_.coded_width.value()));
    decode_params.mfx.FrameInfo.Height =
        align16(static_cast<mfxU16>(config_.coded_height.value()));
    decode_params.mfx.FrameInfo.CropW =
        static_cast<mfxU16>(config_.coded_width.value());
    decode_params.mfx.FrameInfo.CropH =
        static_cast<mfxU16>(config_.coded_height.value());
  }
  decode_params.mfx.FrameInfo.FourCC = MFX_FOURCC_NV12;
  decode_params.mfx.FrameInfo.ChromaFormat = MFX_CHROMAFORMAT_YUV420;

  // デコーダーを初期化（ヘッダーがない場合は後で初期化）
  // 最初のフレームでヘッダー情報を取得してから初期化する
  // sts = dyn::MFXVideoDECODE_Init(session, &decode_params);

  // ビットストリームバッファを確保
  vpl_bitstream_buffer_.resize(4 * 1024 * 1024);  // 4MB
}

bool VideoDecoder::decode_intel_vpl(const EncodedVideoChunk& chunk) {
  if (!vpl_session_) {
    throw std::runtime_error("Intel VPL decoder is not initialized");
  }

  mfxSession session = static_cast<mfxSession>(vpl_session_);

  // ビットストリームを準備
  const std::vector<uint8_t> data = chunk.data_vector();
  if (data.size() > vpl_bitstream_buffer_.size()) {
    vpl_bitstream_buffer_.resize(data.size() * 2);
  }
  std::memcpy(vpl_bitstream_buffer_.data(), data.data(), data.size());

  mfxBitstream bitstream = {};
  bitstream.Data = vpl_bitstream_buffer_.data();
  bitstream.DataLength = static_cast<mfxU32>(data.size());
  bitstream.MaxLength = static_cast<mfxU32>(vpl_bitstream_buffer_.size());
  bitstream.TimeStamp = chunk.timestamp();

  // キーフレームの場合はデコーダーを初期化
  if (chunk.type() == EncodedVideoChunkType::KEY) {
    mfxVideoParam decode_params = {};
    decode_params.mfx.CodecId = get_decoder_codec_id(config_.codec);
    decode_params.IOPattern = MFX_IOPATTERN_OUT_SYSTEM_MEMORY;

    // ヘッダーをデコードしてパラメータを取得
    mfxStatus sts =
        dyn::MFXVideoDECODE_DecodeHeader(session, &bitstream, &decode_params);
    if (sts == MFX_ERR_NONE || sts == MFX_WRN_PARTIAL_ACCELERATION) {
      // デコーダーをリセットして再初期化
      dyn::MFXVideoDECODE_Close(session);
      sts = dyn::MFXVideoDECODE_Init(session, &decode_params);
      if (sts != MFX_ERR_NONE && sts != MFX_WRN_PARTIAL_ACCELERATION &&
          sts != MFX_WRN_INCOMPATIBLE_VIDEO_PARAM) {
        throw std::runtime_error("Failed to initialize Intel VPL decoder");
      }
    }
  }

  // デコード実行
  mfxFrameSurface1* surface_out = nullptr;
  mfxSyncPoint syncp = nullptr;

  mfxStatus sts = dyn::MFXVideoDECODE_DecodeFrameAsync(
      session, &bitstream, nullptr, &surface_out, &syncp);

  if (sts == MFX_ERR_MORE_DATA) {
    // さらにデータが必要
    return true;
  }
  if (sts == MFX_ERR_MORE_SURFACE) {
    // サーフェスが不足（再試行が必要）
    return true;
  }
  if (sts != MFX_ERR_NONE && sts != MFX_WRN_DEVICE_BUSY &&
      sts != MFX_WRN_VIDEO_PARAM_CHANGED) {
    throw std::runtime_error("Intel VPL decode failed");
  }

  // 同期を待機
  if (syncp && surface_out) {
    sts = surface_out->FrameInterface->Synchronize(surface_out, 1000);
    if (sts != MFX_ERR_NONE) {
      surface_out->FrameInterface->Release(surface_out);
      throw std::runtime_error("Intel VPL sync failed");
    }

    // サーフェスをロック
    sts = surface_out->FrameInterface->Map(surface_out, MFX_MAP_READ);
    if (sts != MFX_ERR_NONE) {
      surface_out->FrameInterface->Release(surface_out);
      throw std::runtime_error("Failed to map Intel VPL surface");
    }

    // VideoFrame を作成
    uint32_t width = surface_out->Info.CropW;
    uint32_t height = surface_out->Info.CropH;
    uint32_t pitch = surface_out->Data.Pitch;

    // NV12 フォーマットで VideoFrame を作成
    auto frame = std::make_unique<VideoFrame>(width, height,
                                              VideoPixelFormat::NV12,
                                              chunk.timestamp());

    // Y プレーンをコピー
    uint8_t* dst_y = frame->mutable_plane_ptr(0);
    const uint8_t* src_y = surface_out->Data.Y;
    for (uint32_t row = 0; row < height; ++row) {
      std::memcpy(dst_y + row * width, src_y + row * pitch, width);
    }

    // UV プレーンをコピー
    uint8_t* dst_uv = frame->mutable_plane_ptr(1);
    const uint8_t* src_uv = surface_out->Data.UV;
    uint32_t chroma_height = (height + 1) / 2;
    for (uint32_t row = 0; row < chroma_height; ++row) {
      std::memcpy(dst_uv + row * width, src_uv + row * pitch, width);
    }

    // サーフェスをアンロックして解放
    surface_out->FrameInterface->Unmap(surface_out);
    surface_out->FrameInterface->Release(surface_out);

    // 出力
    handle_output(current_sequence_, std::move(frame));
  }

  return true;
}

void VideoDecoder::flush_intel_vpl() {
  if (!vpl_session_) {
    return;
  }

  mfxSession session = static_cast<mfxSession>(vpl_session_);

  // bitstream = nullptr でフラッシュ
  mfxFrameSurface1* surface_out = nullptr;
  mfxSyncPoint syncp = nullptr;
  mfxStatus sts;

  while (true) {
    sts = dyn::MFXVideoDECODE_DecodeFrameAsync(session, nullptr, nullptr,
                                               &surface_out, &syncp);
    if (sts == MFX_ERR_MORE_DATA) {
      // フラッシュ完了
      break;
    }
    if (sts != MFX_ERR_NONE && sts != MFX_WRN_DEVICE_BUSY) {
      break;
    }

    if (syncp && surface_out) {
      sts = surface_out->FrameInterface->Synchronize(surface_out, 1000);
      if (sts != MFX_ERR_NONE) {
        surface_out->FrameInterface->Release(surface_out);
        continue;
      }

      // サーフェスをロック
      sts = surface_out->FrameInterface->Map(surface_out, MFX_MAP_READ);
      if (sts != MFX_ERR_NONE) {
        surface_out->FrameInterface->Release(surface_out);
        continue;
      }

      // VideoFrame を作成
      uint32_t width = surface_out->Info.CropW;
      uint32_t height = surface_out->Info.CropH;
      uint32_t pitch = surface_out->Data.Pitch;

      auto frame = std::make_unique<VideoFrame>(
          width, height, VideoPixelFormat::NV12,
          static_cast<int64_t>(surface_out->Data.TimeStamp));

      // Y プレーンをコピー
      uint8_t* dst_y = frame->mutable_plane_ptr(0);
      const uint8_t* src_y = surface_out->Data.Y;
      for (uint32_t row = 0; row < height; ++row) {
        std::memcpy(dst_y + row * width, src_y + row * pitch, width);
      }

      // UV プレーンをコピー
      uint8_t* dst_uv = frame->mutable_plane_ptr(1);
      const uint8_t* src_uv = surface_out->Data.UV;
      uint32_t chroma_height = (height + 1) / 2;
      for (uint32_t row = 0; row < chroma_height; ++row) {
        std::memcpy(dst_uv + row * width, src_uv + row * pitch, width);
      }

      // サーフェスをアンロックして解放
      surface_out->FrameInterface->Unmap(surface_out);
      surface_out->FrameInterface->Release(surface_out);

      // 出力
      handle_output(current_sequence_, std::move(frame));
    }
  }
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

  vpl_bitstream_buffer_.clear();
}

#endif  // defined(__linux__)
