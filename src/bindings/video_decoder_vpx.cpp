// VP8/VP9 デコーダー実装 (macOS のみ)
// video_decoder.cpp から #include されるため、インクルードガードは不要

#include <cstring>

void VideoDecoder::init_vpx_decoder() {
  std::lock_guard<std::mutex> lock(vpx_mutex_);
  if (vpx_decoder_) {
    return;  // すでに初期化済み
  }

  // VP8 または VP9 デコーダーを選択
  const vpx_codec_iface_t* iface;
  VideoCodec codec = string_to_codec(config_.codec);
  if (codec == VideoCodec::VP8) {
    iface = vpx_codec_vp8_dx();
  } else if (codec == VideoCodec::VP9) {
    iface = vpx_codec_vp9_dx();
  } else {
    throw std::runtime_error("Unknown VPX codec");
  }

  vpx_codec_ctx_t* ctx = new vpx_codec_ctx_t();
  vpx_codec_dec_cfg_t cfg = {};
  cfg.threads = 1;  // シングルスレッド（ワーカースレッドで処理するため）

  vpx_codec_err_t res = vpx_codec_dec_init(ctx, iface, &cfg, 0);
  if (res != VPX_CODEC_OK) {
    delete ctx;
    throw std::runtime_error("Failed to initialize VPX decoder: " +
                             std::string(vpx_codec_err_to_string(res)));
  }

  vpx_decoder_ = ctx;
}

void VideoDecoder::cleanup_vpx_decoder() {
  if (vpx_decoder_) {
    std::lock_guard<std::mutex> lock(vpx_mutex_);
    vpx_codec_ctx_t* ctx = static_cast<vpx_codec_ctx_t*>(vpx_decoder_);
    vpx_codec_destroy(ctx);
    delete ctx;
    vpx_decoder_ = nullptr;
  }
}

bool VideoDecoder::decode_vpx(const EncodedVideoChunk& chunk) {
  std::lock_guard<std::mutex> lock(vpx_mutex_);
  if (!vpx_decoder_) {
    return false;
  }

  vpx_codec_ctx_t* ctx = static_cast<vpx_codec_ctx_t*>(vpx_decoder_);

  auto pkt = chunk.data_vector();

  vpx_codec_err_t res = vpx_codec_decode(
      ctx, pkt.data(), static_cast<unsigned int>(pkt.size()), nullptr, 0);
  if (res != VPX_CODEC_OK) {
    return false;
  }

  vpx_codec_iter_t iter = nullptr;
  vpx_image_t* img;
  bool got = false;

  while ((img = vpx_codec_get_frame(ctx, &iter)) != nullptr) {
    got = true;

    // I420 フォーマットのみサポート
    if (img->fmt != VPX_IMG_FMT_I420) {
      continue;
    }

    // 有効な画像データがあるか確認
    if (img->d_w > 0 && img->d_h > 0 && img->planes[0] && img->planes[1] &&
        img->planes[2]) {
      // サイズが極端に大きくないかチェック
      if (img->d_w > 8192 || img->d_h > 8192) {
        continue;
      }

      // VideoFrame を作成
      auto frame = std::make_unique<VideoFrame>(
          img->d_w, img->d_h, VideoPixelFormat::I420, chunk.timestamp());

      const uint32_t frame_width = frame->width();
      const uint32_t frame_height = frame->height();
      const uint32_t chroma_width = frame_width / 2;
      const uint32_t chroma_height = frame_height / 2;

      // Y プレーン
      uint8_t* y_dst = frame->mutable_plane_ptr(0);
      for (uint32_t row = 0; row < frame_height; ++row) {
        memcpy(y_dst + row * frame_width, img->planes[0] + row * img->stride[0],
               frame_width);
      }

      // U プレーン
      uint8_t* u_dst = frame->mutable_plane_ptr(1);
      for (uint32_t row = 0; row < chroma_height; ++row) {
        memcpy(u_dst + row * chroma_width,
               img->planes[1] + row * img->stride[1], chroma_width);
      }

      // V プレーン
      uint8_t* v_dst = frame->mutable_plane_ptr(2);
      for (uint32_t row = 0; row < chroma_height; ++row) {
        memcpy(v_dst + row * chroma_width,
               img->planes[2] + row * img->stride[2], chroma_width);
      }

      frame->set_duration(chunk.duration());

      // 順序制御された出力処理
      handle_output(current_sequence_, std::move(frame));
    }
  }

  return got;
}

void VideoDecoder::flush_vpx() {
  // VPX デコーダーのフラッシュ処理
  // 残っているフレームを全て取り出す
  if (!vpx_decoder_) {
    return;
  }

  std::lock_guard<std::mutex> lock(vpx_mutex_);
  vpx_codec_ctx_t* ctx = static_cast<vpx_codec_ctx_t*>(vpx_decoder_);

  vpx_codec_iter_t iter = nullptr;
  vpx_image_t* img;
  while ((img = vpx_codec_get_frame(ctx, &iter)) != nullptr) {
    // 残っているフレームを消費する
  }
}
