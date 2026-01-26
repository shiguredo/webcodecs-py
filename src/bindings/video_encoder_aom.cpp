#include <aom/aom_encoder.h>
#include <aom/aomcx.h>
#include <cstring>
#include <thread>

#include <libyuv.h>

#include "video_encoder.h"

// WebRTC の NumberOfThreads ロジックに準拠
// タイル数（1, 2, 4, 8）に合わせてスレッド数を決定
static int calculate_number_of_threads(int width,
                                       int height,
                                       int number_of_cores) {
  // Keep the number of encoder threads equal to the possible number of
  // column/row tiles, which is (1, 2, 4, 8). See comments below for
  // AV1E_SET_TILE_COLUMNS/ROWS.
  if (width * height > 1280 * 720 && number_of_cores > 8) {
    return 8;
  } else if (width * height >= 640 * 360 && number_of_cores > 4) {
    return 4;
  } else if (width * height >= 320 * 180 && number_of_cores > 2) {
    return 2;
  } else {
    // 1 thread less than VGA.
    return 1;
  }
}

void VideoEncoder::init_aom_encoder() {
  std::lock_guard<std::mutex> lock(aom_mutex_);
  if (aom_encoder_) {
    return;  // すでに初期化済み
  }
  // AV1 エンコーダーを選択
  aom_iface_ = aom_codec_av1_cx();

  aom_codec_err_t res =
      aom_codec_enc_config_default(aom_iface_, &aom_config_, 0);
  if (res != AOM_CODEC_OK) {
    throw std::runtime_error("Failed to get default AOM encoder config");
  }

  aom_config_.g_w = config_.width;
  aom_config_.g_h = config_.height;
  // タイムベースは 90kHz (RTP 標準) に設定
  aom_config_.g_timebase.num = 1;
  aom_config_.g_timebase.den = 90000;
  // Annex-B 形式はデフォルト（1）のままにする
  // aom_config_.save_as_annexb = 0;
  aom_config_.rc_target_bitrate =
      config_.bitrate.value_or(1000000) / 1000;  // kbps

  // ビットレートモードの設定（WebCodecs API に準拠）
  if (config_.bitrate_mode == VideoEncoderBitrateMode::CONSTANT) {
    aom_config_.rc_end_usage = AOM_CBR;  // Constant Bitrate
  } else if (config_.bitrate_mode == VideoEncoderBitrateMode::VARIABLE) {
    aom_config_.rc_end_usage = AOM_VBR;  // Variable Bitrate
  } else if (config_.bitrate_mode == VideoEncoderBitrateMode::QUANTIZER) {
    aom_config_.rc_end_usage = AOM_Q;  // Constant Quality
  } else {
    // デフォルトは VBR（WebCodecs API のデフォルト）
    aom_config_.rc_end_usage = AOM_VBR;
  }

  // WebRTC の NumberOfThreads ロジックに準拠してスレッド数を決定
  // 解像度とコア数に応じて動的に設定（1, 2, 4, 8）
  unsigned int number_of_cores = std::thread::hardware_concurrency();
  aom_config_.g_threads = calculate_number_of_threads(
      config_.width, config_.height, static_cast<int>(number_of_cores));

  // レート制御の詳細設定（WebRTC の設定に準拠）
  aom_config_.rc_min_quantizer = 10;  // 最小 QP（WebRTC と同じ）
  aom_config_.rc_max_quantizer = 56;  // 最大 QP（WebRTC デフォルト）

  // CBR モードでビットレートを確保するため WebRTC とは異なる設定を使用
  // WebRTC 設定: rc_min_quantizer=10, rc_max_quantizer=56, undershoot_pct=50, overshoot_pct=50
  // webcodecs-py CBR 設定: より厳密なビットレート制御のため以下のように変更
  if (config_.bitrate_mode == VideoEncoderBitrateMode::CONSTANT) {
    aom_config_.rc_min_quantizer = 2;   // WebRTC: 10 → 2（高品質を強制）
    aom_config_.rc_max_quantizer = 35;  // WebRTC: 56 → 35（低品質を防止）
    aom_config_.rc_undershoot_pct =
        0;  // WebRTC: 50 → 0（ビットレート削減を禁止）
    aom_config_.rc_overshoot_pct = 0;  // WebRTC: 50 → 0（厳密な CBR 制御）
    // 理由: WebRTC 設定では静的シーンで大幅にビットレートが下がる（指定値の13%程度）
    //       この設定により指定ビットレートの約130%を維持し、高品質を確保
  } else {
    aom_config_.rc_undershoot_pct =
        50;  // アンダーシュートの許容率 50%（WebRTC と同じ）
    aom_config_.rc_overshoot_pct =
        50;  // オーバーシュートの許容率 50%（WebRTC と同じ）
  }
  aom_config_.rc_buf_sz = 1000;         // バッファサイズ（ミリ秒）
  aom_config_.rc_buf_initial_sz = 600;  // 初期バッファサイズ（ミリ秒）
  aom_config_.rc_buf_optimal_sz = 600;  // 最適バッファサイズ（ミリ秒）
  aom_config_.rc_dropframe_thresh = 0;  // フレームドロップ無効化
  aom_config_.rc_resize_mode = 0;       // 動的リサイズ無効化

  // コーデック文字列からパースしたパラメータを使用
  if (std::holds_alternative<AV1CodecParameters>(codec_params_)) {
    const auto& av1_params = std::get<AV1CodecParameters>(codec_params_);
    aom_config_.g_profile = av1_params.profile;

    // ビット深度の設定
    switch (av1_params.bit_depth) {
      case 8:
        aom_config_.g_bit_depth = AOM_BITS_8;
        break;
      case 10:
        aom_config_.g_bit_depth = AOM_BITS_10;
        break;
      case 12:
        aom_config_.g_bit_depth = AOM_BITS_12;
        break;
      default:
        throw std::runtime_error("Unsupported bit depth: " +
                                 std::to_string(av1_params.bit_depth));
    }
    aom_config_.g_input_bit_depth = av1_params.bit_depth;
  } else {
    // デフォルト値（後方互換性のため）
    aom_config_.g_profile = 0;  // Main profile
    aom_config_.g_bit_depth = AOM_BITS_8;
    aom_config_.g_input_bit_depth = 8;
  }
  // WebCodecs API に準拠: キーフレームはアプリケーション側で明示的に制御
  // encode(frame, {key_frame: true}) でのみキーフレームを挿入
  // kf_max_dist を非常に大きな値に設定して、自動キーフレーム挿入を事実上無効化
  // 注意: kf_max_dist = 0 にすると全フレームがキーフレームになるため避ける
  aom_config_.kf_mode = AOM_KF_AUTO;
  aom_config_.kf_min_dist = 0;
  aom_config_.kf_max_dist = 999999;  // 事実上無制限（30fps で約 9 時間）

  // SVC 使用時は REALTIME モードが必須
  // libaom の SVC は REALTIME モードでのみサポートされている
  bool svc_requested = config_.scalability_mode.has_value() &&
                       !config_.scalability_mode.value().empty();
  auto svc_config_check =
      svc_requested ? parse_scalability_mode(config_.scalability_mode.value())
                    : ScalabilityModeConfig();
  bool force_realtime = svc_requested && svc_config_check.is_valid &&
                        svc_config_check.spatial_layers == 1 &&
                        svc_config_check.temporal_layers > 1;

  // Realtime vs quality
  if (config_.latency_mode == LatencyMode::REALTIME || force_realtime) {
    aom_config_.g_usage = AOM_USAGE_REALTIME;
    aom_config_.g_lag_in_frames = 0;
  } else {
    aom_config_.g_usage = AOM_USAGE_GOOD_QUALITY;
    aom_config_.g_lag_in_frames = 25;
  }

  aom_encoder_ = new aom_codec_ctx_t();
  res = aom_codec_enc_init(aom_encoder_, aom_iface_, &aom_config_, 0);
  if (res != AOM_CODEC_OK) {
    delete aom_encoder_;
    aom_encoder_ = nullptr;
    throw std::runtime_error("Failed to initialize AOM encoder: " +
                             std::string(aom_codec_err_to_string(res)));
  }

  // Tune speed/quality（WebRTC の GetCpuSpeed に準拠）
  // cpu_used: 0 = 最高品質・最遅、10 = 最低品質・最速
  // 640x480 (307200 pixels) の場合、ComplexityMax で cpu_used = 8
  int cpu_used;
  if (config_.latency_mode == LatencyMode::REALTIME) {
    uint32_t pixel_count = config_.width * config_.height;
    if (pixel_count <= 320 * 180) {
      cpu_used = 6;
    } else if (pixel_count <= 640 * 360) {
      cpu_used = 7;
    } else if (pixel_count <= 1280 * 720) {
      cpu_used = 8;
    } else {
      cpu_used = 9;
    }
  } else {
    cpu_used = 4;
  }
  aom_codec_control(aom_encoder_, AOME_SET_CPUUSED, cpu_used);

  // WebRTC の追加設定（品質とパフォーマンスの最適化）
  aom_codec_control(aom_encoder_, AV1E_SET_ENABLE_CDEF, 1);
  aom_codec_control(aom_encoder_, AV1E_SET_ENABLE_TPL_MODEL, 0);
  aom_codec_control(aom_encoder_, AV1E_SET_DELTAQ_MODE, 0);
  aom_codec_control(aom_encoder_, AV1E_SET_ENABLE_ORDER_HINT, 0);
  aom_codec_control(aom_encoder_, AV1E_SET_AQ_MODE, 3);  // 適応量子化モード
  aom_codec_control(aom_encoder_, AOME_SET_MAX_INTRA_BITRATE_PCT, 300);
  aom_codec_control(aom_encoder_, AV1E_SET_COEFF_COST_UPD_FREQ, 3);
  aom_codec_control(aom_encoder_, AV1E_SET_MODE_COST_UPD_FREQ, 3);
  aom_codec_control(aom_encoder_, AV1E_SET_MV_COST_UPD_FREQ, 3);

  // タイリングとマルチスレッド
  aom_codec_control(aom_encoder_, AV1E_SET_AUTO_TILES, 1);
  aom_codec_control(aom_encoder_, AV1E_SET_ROW_MT, 1);

  // スーパーブロックサイズ（解像度に応じて設定）
  // 640x480 以下: 64, それ以上: 128（WebRTC の設定に準拠）
  int superblock_size = (config_.width * config_.height <= 640 * 480)
                            ? AOM_SUPERBLOCK_SIZE_64X64
                            : AOM_SUPERBLOCK_SIZE_128X128;
  aom_codec_control(aom_encoder_, AV1E_SET_SUPERBLOCK_SIZE, superblock_size);

  // ノイズ感度とモーション推定
  aom_codec_control(aom_encoder_, AV1E_SET_NOISE_SENSITIVITY, 0);

  // 機能の無効化（リアルタイムパフォーマンス向上）
  aom_codec_control(aom_encoder_, AV1E_SET_ENABLE_OBMC, 0);
  aom_codec_control(aom_encoder_, AV1E_SET_ENABLE_WARPED_MOTION, 0);
  aom_codec_control(aom_encoder_, AV1E_SET_ENABLE_GLOBAL_MOTION, 0);
  aom_codec_control(aom_encoder_, AV1E_SET_ENABLE_REF_FRAME_MVS, 0);

  // パレットモード（通常のビデオでは無効）
  aom_codec_control(aom_encoder_, AV1E_SET_ENABLE_PALETTE, 0);

  // イントラ予測の最適化
  aom_codec_control(aom_encoder_, AV1E_SET_ENABLE_CFL_INTRA, 0);
  aom_codec_control(aom_encoder_, AV1E_SET_ENABLE_SMOOTH_INTRA, 0);
  aom_codec_control(aom_encoder_, AV1E_SET_ENABLE_ANGLE_DELTA, 0);
  aom_codec_control(aom_encoder_, AV1E_SET_ENABLE_FILTER_INTRA, 0);
  aom_codec_control(aom_encoder_, AV1E_SET_INTRA_DEFAULT_TX_ONLY, 1);

  // 量子化の最適化
  aom_codec_control(aom_encoder_, AV1E_SET_DISABLE_TRELLIS_QUANT, 1);

  // インター予測の最適化
  aom_codec_control(aom_encoder_, AV1E_SET_ENABLE_DIST_WTD_COMP, 0);
  aom_codec_control(aom_encoder_, AV1E_SET_ENABLE_DIFF_WTD_COMP, 0);
  aom_codec_control(aom_encoder_, AV1E_SET_ENABLE_DUAL_FILTER, 0);
  aom_codec_control(aom_encoder_, AV1E_SET_ENABLE_INTERINTRA_COMP, 0);
  aom_codec_control(aom_encoder_, AV1E_SET_ENABLE_INTERINTRA_WEDGE, 0);
  aom_codec_control(aom_encoder_, AV1E_SET_ENABLE_INTRA_EDGE_FILTER, 0);
  aom_codec_control(aom_encoder_, AV1E_SET_ENABLE_INTRABC, 0);
  aom_codec_control(aom_encoder_, AV1E_SET_ENABLE_MASKED_COMP, 0);
  aom_codec_control(aom_encoder_, AV1E_SET_ENABLE_PAETH_INTRA, 0);

  // その他の機能無効化
  aom_codec_control(aom_encoder_, AV1E_SET_ENABLE_QM, 0);
  aom_codec_control(aom_encoder_, AV1E_SET_ENABLE_RECT_PARTITIONS, 0);
  aom_codec_control(aom_encoder_, AV1E_SET_ENABLE_RESTORATION, 0);
  aom_codec_control(aom_encoder_, AV1E_SET_ENABLE_SMOOTH_INTERINTRA, 0);
  aom_codec_control(aom_encoder_, AV1E_SET_ENABLE_TX64, 0);

  // 参照フレーム数の制限
  aom_codec_control(aom_encoder_, AV1E_SET_MAX_REFERENCE_FRAMES, 3);

  // SVC (Scalable Video Coding) の設定
  svc_enabled_ = false;
  svc_temporal_layers_ = 1;
  svc_frame_index_.store(0);

  if (config_.scalability_mode.has_value() &&
      !config_.scalability_mode.value().empty()) {
    auto svc_config = parse_scalability_mode(config_.scalability_mode.value());

    if (!svc_config.is_valid) {
      throw std::runtime_error("Invalid scalability_mode: " +
                               config_.scalability_mode.value());
    }

    // Spatial SVC は未対応
    if (svc_config.spatial_layers > 1) {
      throw std::runtime_error(
          "Spatial SVC (L2T*, L3T*, L4T*) is not supported for AV1. "
          "Only temporal SVC (L1T2, L1T3) is supported.");
    }

    uint32_t ts_layers = svc_config.temporal_layers;
    if (ts_layers > 1) {
      svc_enabled_ = true;
      svc_temporal_layers_ = ts_layers;

      // SVC パラメータの設定
      aom_svc_params_t svc_params;
      memset(&svc_params, 0, sizeof(svc_params));

      svc_params.number_spatial_layers = 1;
      svc_params.number_temporal_layers = static_cast<int>(ts_layers);

      // 各層の量子化パラメータ (spatial_layers * temporal_layers 分)
      for (unsigned int i = 0; i < ts_layers; ++i) {
        svc_params.max_quantizers[i] =
            static_cast<int>(aom_config_.rc_max_quantizer);
        svc_params.min_quantizers[i] =
            static_cast<int>(aom_config_.rc_min_quantizer);
      }

      // ビットレート配分（VP9 と同様）
      uint32_t total_bitrate = aom_config_.rc_target_bitrate;
      if (ts_layers == 2) {
        // L1T2: Layer 0 = 60%, Layer 1 = 100%
        svc_params.layer_target_bitrate[0] =
            static_cast<int>(total_bitrate * 60 / 100);
        svc_params.layer_target_bitrate[1] = static_cast<int>(total_bitrate);
      } else if (ts_layers == 3) {
        // L1T3: Layer 0 = 25%, Layer 1 = 50%, Layer 2 = 100%
        svc_params.layer_target_bitrate[0] =
            static_cast<int>(total_bitrate * 25 / 100);
        svc_params.layer_target_bitrate[1] =
            static_cast<int>(total_bitrate * 50 / 100);
        svc_params.layer_target_bitrate[2] = static_cast<int>(total_bitrate);
      }

      // スケーリングファクター（空間層は 1 なので 1:1）
      // scaling_factor_den は 0 以外の値が必要
      svc_params.scaling_factor_num[0] = 1;
      svc_params.scaling_factor_den[0] = 1;

      aom_codec_control(aom_encoder_, AV1E_SET_SVC_PARAMS, &svc_params);
    }
  }
}

void VideoEncoder::cleanup_aom_encoder() {
  if (aom_encoder_) {
    std::lock_guard<std::mutex> lock(aom_mutex_);
    aom_codec_destroy(aom_encoder_);
    delete aom_encoder_;
    aom_encoder_ = nullptr;
  }
}

void VideoEncoder::encode_frame_aom(const VideoFrame& frame,
                                    bool keyframe,
                                    std::optional<uint16_t> quantizer) {
  std::lock_guard<std::mutex> lock(aom_mutex_);
  if (!aom_encoder_) {
    throw std::runtime_error("AOM encoder not initialized");
  }

  // quantizer モードで quantizer が指定されている場合、フレーム単位で QP を設定
  if (quantizer.has_value() &&
      config_.bitrate_mode == VideoEncoderBitrateMode::QUANTIZER) {
    // AV1E_SET_QUANTIZER_ONE_PASS は 1 パスモードでのフレーム単位 QP 設定
    aom_codec_control(aom_encoder_, AV1E_SET_QUANTIZER_ONE_PASS,
                      static_cast<int>(quantizer.value()));
  }

  // SVC temporal layer ID の計算
  std::optional<SvcOutputMetadata> svc_metadata;
  if (svc_enabled_) {
    uint64_t frame_idx = svc_frame_index_.fetch_add(1);

    // temporal layer ID の計算
    // L1T2: 周期 2 (0, 1, 0, 1, ...)
    // L1T3: 周期 4 (0, 2, 1, 2, 0, 2, 1, 2, ...)
    uint32_t temporal_layer_id = 0;
    if (svc_temporal_layers_ == 2) {
      temporal_layer_id = frame_idx % 2;
    } else if (svc_temporal_layers_ == 3) {
      // 周期 4 のパターン: 0, 2, 1, 2
      static const uint32_t pattern_l1t3[] = {0, 2, 1, 2};
      temporal_layer_id = pattern_l1t3[frame_idx % 4];
    }

    // AV1E_SET_SVC_LAYER_ID で層 ID を設定
    aom_svc_layer_id_t layer_id;
    layer_id.spatial_layer_id = 0;
    layer_id.temporal_layer_id = static_cast<int>(temporal_layer_id);
    aom_codec_control(aom_encoder_, AV1E_SET_SVC_LAYER_ID, &layer_id);

    svc_metadata = SvcOutputMetadata(temporal_layer_id);
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

  // Wrap I420 memory from VideoFrame or scaled buffer
  aom_image_t img;
  unsigned char* base = const_cast<unsigned char*>(src_y);
  if (!aom_img_wrap(&img, AOM_IMG_FMT_I420, config_.width, config_.height, 1,
                    base)) {
    throw std::runtime_error("Failed to wrap AOM image");
  }
  // stride を明示
  img.stride[0] = src_stride_y;
  img.stride[1] = src_stride_u;
  img.stride[2] = src_stride_v;
  // U/V の先頭ポインタを正しく設定
  img.planes[1] = const_cast<unsigned char*>(src_u);
  img.planes[2] = const_cast<unsigned char*>(src_v);

  // pts/duration in timebase units
  const aom_codec_pts_t pts = frame_count_.fetch_add(1);
  // duration は timebase 単位（90kHz）で、1 フレームの時間を表す
  // framerate が fps の場合、1 フレームは 90000/fps ティック
  const double fps = config_.framerate.value_or(30.0);
  const unsigned long duration = static_cast<unsigned long>(90000.0 / fps);
  aom_codec_err_t res = aom_codec_encode(aom_encoder_, &img, pts, duration,
                                         keyframe ? AOM_EFLAG_FORCE_KF : 0);
  if (res != AOM_CODEC_OK) {
    // aom_img_wrap では img_data_owner=0 のため解放不要
    throw std::runtime_error("AOM encode failed: " +
                             std::string(aom_codec_err_to_string(res)));
  }

  aom_codec_iter_t iter = nullptr;
  const aom_codec_cx_pkt_t* pkt;
  while ((pkt = aom_codec_get_cx_data(aom_encoder_, &iter)) != nullptr) {
    if (pkt->kind == AOM_CODEC_CX_FRAME_PKT) {
      bool is_keyframe = (pkt->data.frame.flags & AOM_FRAME_IS_KEY) != 0;
      if (svc_enabled_) {
        handle_encoded_frame(static_cast<const uint8_t*>(pkt->data.frame.buf),
                             pkt->data.frame.sz, frame.timestamp(), is_keyframe,
                             svc_metadata);
      } else {
        handle_encoded_frame(static_cast<const uint8_t*>(pkt->data.frame.buf),
                             pkt->data.frame.sz, frame.timestamp(),
                             is_keyframe);
      }
    }
  }
  // aom_img_wrap では img_data_owner=0 のため解放不要だが、
  // API 的に aom_img_free を呼んでも安全（データ本体は解放されない）
  aom_img_free(&img);
}