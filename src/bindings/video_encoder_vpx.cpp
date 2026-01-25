// VP8/VP9 エンコーダー実装 (macOS / Linux)
// video_encoder.cpp から #include されるため、インクルードガードは不要

#include <cstring>
#include <regex>
#include <thread>

#include <libyuv.h>

// scalabilityMode 文字列のパース結果
struct ScalabilityModeConfig {
  uint32_t spatial_layers;
  uint32_t temporal_layers;
  bool is_valid;

  ScalabilityModeConfig()
      : spatial_layers(1), temporal_layers(1), is_valid(false) {}
};

// scalabilityMode 文字列をパース
// "L1T2" -> spatial_layers=1, temporal_layers=2
// "L1T3" -> spatial_layers=1, temporal_layers=3
static ScalabilityModeConfig parse_scalability_mode(const std::string& mode) {
  ScalabilityModeConfig config;

  if (mode.empty()) {
    return config;
  }

  // 正規表現: L[1-4]T[1-3]
  std::regex pattern("^L([1-4])T([1-3])$");
  std::smatch match;

  if (std::regex_match(mode, match, pattern)) {
    config.spatial_layers = static_cast<uint32_t>(std::stoul(match[1].str()));
    config.temporal_layers = static_cast<uint32_t>(std::stoul(match[2].str()));
    config.is_valid = true;
  }

  return config;
}

// WebRTC の NumberOfThreads ロジックに準拠
static int calculate_vpx_number_of_threads(int width,
                                           int height,
                                           int number_of_cores) {
  if (width * height > 1280 * 720 && number_of_cores > 8) {
    return 8;
  } else if (width * height >= 640 * 360 && number_of_cores > 4) {
    return 4;
  } else if (width * height >= 320 * 180 && number_of_cores > 2) {
    return 2;
  } else {
    return 1;
  }
}

void VideoEncoder::init_vpx_encoder() {
  std::lock_guard<std::mutex> lock(vpx_mutex_);
  if (vpx_encoder_) {
    return;  // すでに初期化済み
  }

  // VP8 または VP9 エンコーダーを選択
  if (is_vp8_codec()) {
    vpx_iface_ = vpx_codec_vp8_cx();
  } else if (is_vp9_codec()) {
    vpx_iface_ = vpx_codec_vp9_cx();
  } else {
    throw std::runtime_error("Unknown VPX codec");
  }

  vpx_codec_err_t res =
      vpx_codec_enc_config_default(vpx_iface_, &vpx_config_, 0);
  if (res != VPX_CODEC_OK) {
    throw std::runtime_error("Failed to get default VPX encoder config");
  }

  vpx_config_.g_w = config_.width;
  vpx_config_.g_h = config_.height;
  // タイムベースは 90kHz (RTP 標準) に設定
  vpx_config_.g_timebase.num = 1;
  vpx_config_.g_timebase.den = 90000;
  vpx_config_.rc_target_bitrate =
      config_.bitrate.value_or(1000000) / 1000;  // kbps

  // ビットレートモードの設定
  if (config_.bitrate_mode == VideoEncoderBitrateMode::CONSTANT) {
    vpx_config_.rc_end_usage = VPX_CBR;
  } else if (config_.bitrate_mode == VideoEncoderBitrateMode::VARIABLE) {
    vpx_config_.rc_end_usage = VPX_VBR;
  } else if (config_.bitrate_mode == VideoEncoderBitrateMode::QUANTIZER) {
    vpx_config_.rc_end_usage = VPX_Q;
  } else {
    vpx_config_.rc_end_usage = VPX_VBR;
  }

  // スレッド数の設定
  unsigned int number_of_cores = std::thread::hardware_concurrency();
  vpx_config_.g_threads = calculate_vpx_number_of_threads(
      config_.width, config_.height, static_cast<int>(number_of_cores));

  // レート制御の設定
  vpx_config_.rc_min_quantizer = 2;
  vpx_config_.rc_max_quantizer = 56;

  if (config_.bitrate_mode == VideoEncoderBitrateMode::CONSTANT) {
    vpx_config_.rc_undershoot_pct = 0;
    vpx_config_.rc_overshoot_pct = 0;
  } else {
    vpx_config_.rc_undershoot_pct = 50;
    vpx_config_.rc_overshoot_pct = 50;
  }

  vpx_config_.rc_buf_sz = 1000;
  vpx_config_.rc_buf_initial_sz = 600;
  vpx_config_.rc_buf_optimal_sz = 600;
  vpx_config_.rc_dropframe_thresh = 0;
  vpx_config_.rc_resize_allowed = 0;

  // VP9 の場合、プロファイルとビット深度を設定
  if (is_vp9_codec() &&
      std::holds_alternative<VP9CodecParameters>(codec_params_)) {
    const auto& vp9_params = std::get<VP9CodecParameters>(codec_params_);
    vpx_config_.g_profile = vp9_params.profile;

    // ビット深度は VP9 Profile 2/3 でのみ 10/12 bit をサポート
    if (vp9_params.bit_depth == 8) {
      vpx_config_.g_bit_depth = VPX_BITS_8;
      vpx_config_.g_input_bit_depth = 8;
    } else if (vp9_params.bit_depth == 10) {
      vpx_config_.g_bit_depth = VPX_BITS_10;
      vpx_config_.g_input_bit_depth = 10;
    } else if (vp9_params.bit_depth == 12) {
      vpx_config_.g_bit_depth = VPX_BITS_12;
      vpx_config_.g_input_bit_depth = 12;
    }
  } else {
    vpx_config_.g_profile = 0;
    vpx_config_.g_bit_depth = VPX_BITS_8;
    vpx_config_.g_input_bit_depth = 8;
  }

  // キーフレームの設定
  vpx_config_.kf_mode = VPX_KF_AUTO;
  vpx_config_.kf_min_dist = 0;
  vpx_config_.kf_max_dist = 999999;

  // リアルタイムモードの設定
  if (config_.latency_mode == LatencyMode::REALTIME) {
    vpx_config_.g_usage = VPX_DL_REALTIME;
    vpx_config_.g_lag_in_frames = 0;
  } else {
    vpx_config_.g_usage = VPX_DL_GOOD_QUALITY;
    vpx_config_.g_lag_in_frames = 25;
  }

  // VP9 SVC (Scalable Video Coding) の設定
  svc_enabled_ = false;
  svc_temporal_layers_ = 1;
  svc_frame_index_.store(0);

  if (is_vp9_codec() && config_.scalability_mode.has_value()) {
    ScalabilityModeConfig svc_config =
        parse_scalability_mode(config_.scalability_mode.value());

    if (!svc_config.is_valid) {
      throw std::runtime_error("Unsupported scalability mode: " +
                               config_.scalability_mode.value());
    }

    // spatial layers > 1 は未サポート
    if (svc_config.spatial_layers > 1) {
      throw std::runtime_error(
          "Spatial SVC is not supported. Only temporal SVC (L1T*) is "
          "supported.");
    }

    uint32_t ts_layers = svc_config.temporal_layers;
    vpx_config_.ts_number_layers = ts_layers;

    if (ts_layers == 2) {
      // L1T2: 0-1-0-1... パターン
      vpx_config_.ts_periodicity = 2;
      vpx_config_.ts_layer_id[0] = 0;
      vpx_config_.ts_layer_id[1] = 1;

      // ビットレート配分: Layer 0 = 60%, Layer 1 = 100%
      uint32_t base_bitrate = vpx_config_.rc_target_bitrate;
      vpx_config_.ts_target_bitrate[0] = base_bitrate * 60 / 100;
      vpx_config_.ts_target_bitrate[1] = base_bitrate;

      // フレームレートデシメータ: Layer 0 = 2 (半分), Layer 1 = 1 (全部)
      vpx_config_.ts_rate_decimator[0] = 2;
      vpx_config_.ts_rate_decimator[1] = 1;

    } else if (ts_layers == 3) {
      // L1T3: 0-2-1-2... パターン
      vpx_config_.ts_periodicity = 4;
      vpx_config_.ts_layer_id[0] = 0;
      vpx_config_.ts_layer_id[1] = 2;
      vpx_config_.ts_layer_id[2] = 1;
      vpx_config_.ts_layer_id[3] = 2;

      // ビットレート配分: Layer 0 = 40%, Layer 1 = 60%, Layer 2 = 100%
      uint32_t base_bitrate = vpx_config_.rc_target_bitrate;
      vpx_config_.ts_target_bitrate[0] = base_bitrate * 40 / 100;
      vpx_config_.ts_target_bitrate[1] = base_bitrate * 60 / 100;
      vpx_config_.ts_target_bitrate[2] = base_bitrate;

      // フレームレートデシメータ
      vpx_config_.ts_rate_decimator[0] = 4;
      vpx_config_.ts_rate_decimator[1] = 2;
      vpx_config_.ts_rate_decimator[2] = 1;
    }

    svc_enabled_ = true;
    svc_temporal_layers_ = ts_layers;
  }

  vpx_encoder_ = new vpx_codec_ctx_t();
  res = vpx_codec_enc_init(vpx_encoder_, vpx_iface_, &vpx_config_, 0);
  if (res != VPX_CODEC_OK) {
    delete vpx_encoder_;
    vpx_encoder_ = nullptr;
    throw std::runtime_error("Failed to initialize VPX encoder: " +
                             std::string(vpx_codec_err_to_string(res)));
  }

  // cpu_used の設定（速度/品質のトレードオフ）
  // VP8: -16 ~ 16、VP9: 0 ~ 9
  int cpu_used;
  if (is_vp8_codec()) {
    if (config_.latency_mode == LatencyMode::REALTIME) {
      cpu_used = 10;  // 高速
    } else {
      cpu_used = 4;
    }
    vpx_codec_control(vpx_encoder_, VP8E_SET_CPUUSED, cpu_used);
  } else {
    // VP9
    if (config_.latency_mode == LatencyMode::REALTIME) {
      uint32_t pixel_count = config_.width * config_.height;
      if (pixel_count <= 320 * 180) {
        cpu_used = 5;
      } else if (pixel_count <= 640 * 360) {
        cpu_used = 6;
      } else if (pixel_count <= 1280 * 720) {
        cpu_used = 7;
      } else {
        cpu_used = 8;
      }
    } else {
      cpu_used = 4;
    }
    vpx_codec_control(vpx_encoder_, VP8E_SET_CPUUSED, cpu_used);

    // VP9 固有の設定
    vpx_codec_control(vpx_encoder_, VP9E_SET_ROW_MT, 1);
    vpx_codec_control(vpx_encoder_, VP9E_SET_AQ_MODE, 3);

    // VP9 SVC を有効化
    if (svc_enabled_) {
      vpx_codec_control(vpx_encoder_, VP9E_SET_SVC, 1);
    }
  }

  // ノイズ感度
  vpx_codec_control(vpx_encoder_, VP8E_SET_NOISE_SENSITIVITY, 0);

  // スタティック閾値
  vpx_codec_control(vpx_encoder_, VP8E_SET_STATIC_THRESHOLD, 1);

  // 最大イントラビットレート
  vpx_codec_control(vpx_encoder_, VP8E_SET_MAX_INTRA_BITRATE_PCT, 300);
}

void VideoEncoder::cleanup_vpx_encoder() {
  if (vpx_encoder_) {
    std::lock_guard<std::mutex> lock(vpx_mutex_);
    vpx_codec_destroy(vpx_encoder_);
    delete vpx_encoder_;
    vpx_encoder_ = nullptr;
  }
}

void VideoEncoder::encode_frame_vpx(const VideoFrame& frame,
                                    bool keyframe,
                                    std::optional<uint16_t> quantizer) {
  std::lock_guard<std::mutex> lock(vpx_mutex_);
  if (!vpx_encoder_) {
    throw std::runtime_error("VPX encoder not initialized");
  }

  // quantizer モードの場合、フレーム単位で QP を設定
  if (quantizer.has_value() &&
      config_.bitrate_mode == VideoEncoderBitrateMode::QUANTIZER) {
    // VP8/VP9 の場合は min/max quantizer を同じ値に設定して固定 QP を実現
    vpx_codec_control(vpx_encoder_, VP8E_SET_CQ_LEVEL,
                      static_cast<unsigned int>(quantizer.value()));
  }

  // SVC が有効な場合、temporal layer ID を計算して設定
  uint32_t current_temporal_layer_id = 0;
  if (svc_enabled_) {
    uint64_t frame_idx = svc_frame_index_.fetch_add(1);

    // フレームインデックスから temporal layer ID を計算
    uint32_t period_idx =
        static_cast<uint32_t>(frame_idx % vpx_config_.ts_periodicity);
    current_temporal_layer_id = vpx_config_.ts_layer_id[period_idx];

    // VP9E_SET_SVC_LAYER_ID で現在のフレームのレイヤを設定
    vpx_svc_layer_id_t layer_id;
    layer_id.spatial_layer_id = 0;
    layer_id.temporal_layer_id = static_cast<int>(current_temporal_layer_id);
    vpx_codec_control(vpx_encoder_, VP9E_SET_SVC_LAYER_ID, &layer_id);
  }

  // スケーリングが必要かどうかを判定
  bool needs_scaling =
      (frame.width() != config_.width || frame.height() != config_.height);

  // スケーリング用のバッファ
  std::vector<uint8_t> scaled_buffer;
  const uint8_t* src_y = frame.plane_ptr(0);
  const uint8_t* src_u = frame.plane_ptr(1);
  const uint8_t* src_v = frame.plane_ptr(2);
  int src_stride_y = static_cast<int>(frame.width());
  int src_stride_u = static_cast<int>(frame.width() / 2);
  int src_stride_v = static_cast<int>(frame.width() / 2);

  // スケーリングが必要な場合は libyuv で変換
  if (needs_scaling) {
    uint32_t dst_width = config_.width;
    uint32_t dst_height = config_.height;
    size_t y_size = dst_width * dst_height;
    size_t uv_size = (dst_width / 2) * (dst_height / 2);
    scaled_buffer.resize(y_size + uv_size * 2);

    uint8_t* dst_y = scaled_buffer.data();
    uint8_t* dst_u = dst_y + y_size;
    uint8_t* dst_v = dst_u + uv_size;
    int dst_stride_y = static_cast<int>(dst_width);
    int dst_stride_u = static_cast<int>(dst_width / 2);
    int dst_stride_v = static_cast<int>(dst_width / 2);

    int result = libyuv::I420Scale(
        src_y, src_stride_y, src_u, src_stride_u, src_v, src_stride_v,
        static_cast<int>(frame.width()), static_cast<int>(frame.height()),
        dst_y, dst_stride_y, dst_u, dst_stride_u, dst_v, dst_stride_v,
        static_cast<int>(dst_width), static_cast<int>(dst_height),
        libyuv::kFilterBox);

    if (result != 0) {
      throw std::runtime_error("libyuv::I420Scale failed");
    }

    // スケーリング後のポインタとストライドを更新
    src_y = dst_y;
    src_u = dst_u;
    src_v = dst_v;
    src_stride_y = dst_stride_y;
    src_stride_u = dst_stride_u;
    src_stride_v = dst_stride_v;
  }

  // I420 イメージをラップ
  vpx_image_t img;
  unsigned char* base = const_cast<unsigned char*>(src_y);
  if (!vpx_img_wrap(&img, VPX_IMG_FMT_I420, config_.width, config_.height, 1,
                    base)) {
    throw std::runtime_error("Failed to wrap VPX image");
  }
  img.stride[0] = src_stride_y;
  img.stride[1] = src_stride_u;
  img.stride[2] = src_stride_v;
  img.planes[1] = const_cast<unsigned char*>(src_u);
  img.planes[2] = const_cast<unsigned char*>(src_v);

  // pts/duration in timebase units
  const vpx_codec_pts_t pts = frame_count_.fetch_add(1);
  const double fps = config_.framerate.value_or(30.0);
  const unsigned long duration = static_cast<unsigned long>(90000.0 / fps);

  vpx_enc_frame_flags_t flags = keyframe ? VPX_EFLAG_FORCE_KF : 0;
  vpx_codec_err_t res = vpx_codec_encode(vpx_encoder_, &img, pts, duration,
                                         flags, VPX_DL_REALTIME);
  if (res != VPX_CODEC_OK) {
    throw std::runtime_error("VPX encode failed: " +
                             std::string(vpx_codec_err_to_string(res)));
  }

  vpx_codec_iter_t iter = nullptr;
  const vpx_codec_cx_pkt_t* pkt;
  while ((pkt = vpx_codec_get_cx_data(vpx_encoder_, &iter)) != nullptr) {
    if (pkt->kind == VPX_CODEC_CX_FRAME_PKT) {
      bool is_keyframe = (pkt->data.frame.flags & VPX_FRAME_IS_KEY) != 0;

      // SVC メタデータを作成
      std::optional<SvcOutputMetadata> svc_metadata = std::nullopt;
      if (svc_enabled_) {
        svc_metadata = SvcOutputMetadata(current_temporal_layer_id);
      }

      handle_encoded_frame(static_cast<const uint8_t*>(pkt->data.frame.buf),
                           pkt->data.frame.sz, frame.timestamp(), is_keyframe,
                           svc_metadata);
    }
  }
  vpx_img_free(&img);
}
