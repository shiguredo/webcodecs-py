#include "video_encoder.h"
#include <cstring>
#include <stdexcept>
#include "encoded_video_chunk.h"
#include "video_frame.h"
#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include <CoreVideo/CoreVideo.h>
#include <VideoToolbox/VideoToolbox.h>
#endif

using namespace nb::literals;

VideoEncoder::VideoEncoder(nb::object output, nb::object error)
    : output_callback_(output),
      error_callback_(error),
      state_(CodecState::UNCONFIGURED) {
  aom_encoder_ = nullptr;
  aom_iface_ = nullptr;
  vt_session_ = nullptr;

  // コンストラクタではコーデックの初期化は行わない
  // configure() で初期化する
}

VideoEncoder::~VideoEncoder() {
  stop_worker();  // ワーカースレッドを停止
  close();
}

void VideoEncoder::configure(nb::dict config_dict) {
  if (state_ == CodecState::CLOSED) {
    throw std::runtime_error("VideoEncoder is closed");
  }

  // dict から VideoEncoderConfig へ変換
  VideoEncoderConfig config;

  // 必須フィールドのチェックと変換
  if (!config_dict.contains("codec"))
    throw nb::value_error("codec is required");
  if (!config_dict.contains("width"))
    throw nb::value_error("width is required");
  if (!config_dict.contains("height"))
    throw nb::value_error("height is required");

  config.codec = nb::cast<std::string>(config_dict["codec"]);
  config.width = nb::cast<uint32_t>(config_dict["width"]);
  config.height = nb::cast<uint32_t>(config_dict["height"]);

  // オプションフィールド
  if (config_dict.contains("bitrate"))
    config.bitrate = nb::cast<uint64_t>(config_dict["bitrate"]);
  if (config_dict.contains("framerate"))
    config.framerate = nb::cast<double>(config_dict["framerate"]);
  if (config_dict.contains("latency_mode"))
    config.latency_mode = nb::cast<LatencyMode>(config_dict["latency_mode"]);
  if (config_dict.contains("bitrate_mode"))
    config.bitrate_mode =
        nb::cast<VideoEncoderBitrateMode>(config_dict["bitrate_mode"]);
  if (config_dict.contains("hardware_acceleration"))
    config.hardware_acceleration =
        nb::cast<HardwareAcceleration>(config_dict["hardware_acceleration"]);
  if (config_dict.contains("alpha"))
    config.alpha = nb::cast<AlphaOption>(config_dict["alpha"]);
  if (config_dict.contains("hardware_acceleration_engine"))
    config.hardware_acceleration_engine =
        nb::cast<HardwareAccelerationEngine>(config_dict["hardware_acceleration_engine"]);

  // AVC 固有のオプション
  if (config_dict.contains("avc")) {
    nb::dict avc_dict = nb::cast<nb::dict>(config_dict["avc"]);
    if (avc_dict.contains("format")) {
      config.avc_format = nb::cast<std::string>(avc_dict["format"]);
    }
  }

  // HEVC 固有のオプション
  if (config_dict.contains("hevc")) {
    nb::dict hevc_dict = nb::cast<nb::dict>(config_dict["hevc"]);
    if (hevc_dict.contains("format")) {
      config.hevc_format = nb::cast<std::string>(hevc_dict["format"]);
    }
  }

  // VideoEncoderConfig を保存
  config_ = config;

  // デフォルト値の設定
  if (!config_.bitrate.has_value()) {
    config_.bitrate = 400000;  // デフォルト値
  }
  if (!config_.framerate.has_value()) {
    config_.framerate = 30.0;  // デフォルト値
  }


  // コーデック文字列をパースして、パラメータを抽出
  try {
    codec_params_ = parse_codec_string(config_.codec);
  } catch (const std::exception& e) {
    throw std::invalid_argument(std::string("Invalid codec string: ") +
                                e.what());
  }

  // コーデックの初期化
  if (uses_nvidia_video_codec()) {
#if defined(NVIDIA_CUDA_TOOLKIT)
    init_nvenc_encoder();
#else
    throw std::runtime_error(
        "NVIDIA Video Codec SDK is not enabled in this build");
#endif
  } else if (is_av1_codec()) {
    init_aom_encoder();
  } else if (is_avc_codec() || is_hevc_codec()) {
#if defined(__APPLE__)
    if (config_.hardware_acceleration_engine == HardwareAccelerationEngine::APPLE_VIDEO_TOOLBOX) {
      init_videotoolbox_encoder();
    } else {
      throw std::runtime_error(
          "AVC/HEVC requires "
          "hardware_acceleration_engine=\"apple_video_toolbox\" on macOS");
    }
#else
    throw std::runtime_error("AVC/HEVC not supported on this platform");
#endif
  }

  // ワーカースレッドの開始
  // VideoToolbox は独自の非同期モデルを持つため、ワーカースレッドを開始しない
  if (!uses_videotoolbox()) {
    if (!worker_thread_.joinable()) {
      start_worker();  // ワーカースレッドを開始
    }
  }

  state_ = CodecState::CONFIGURED;
}

// コーデック判定ヘルパーメソッドの実装
bool VideoEncoder::is_av1_codec() const {
  return config_.codec.length() >= 5 && config_.codec.substr(0, 5) == "av01.";
}

bool VideoEncoder::is_avc_codec() const {
  return config_.codec.length() >= 5 &&
         (config_.codec.substr(0, 5) == "avc1." ||
          config_.codec.substr(0, 5) == "avc3.");
}

bool VideoEncoder::is_hevc_codec() const {
  return config_.codec.length() >= 5 &&
         (config_.codec.substr(0, 5) == "hvc1." ||
          config_.codec.substr(0, 5) == "hev1.");
}

bool VideoEncoder::uses_videotoolbox() const {
#if defined(__APPLE__)
  return (is_avc_codec() || is_hevc_codec()) &&
         config_.hardware_acceleration_engine == HardwareAccelerationEngine::APPLE_VIDEO_TOOLBOX;
#else
  return false;
#endif
}

bool VideoEncoder::uses_nvidia_video_codec() const {
#if defined(NVIDIA_CUDA_TOOLKIT)
  return (is_avc_codec() || is_hevc_codec() || is_av1_codec()) &&
         config_.hardware_acceleration_engine == HardwareAccelerationEngine::NVIDIA_VIDEO_CODEC;
#else
  return false;
#endif
}

// 分割されたファイルをインクルード
#include "video_encoder_aom.cpp"
#include "video_encoder_apple_video_toolbox.cpp"
#include "video_encoder_nvidia.cpp"

void VideoEncoder::encode(const VideoFrame& frame, bool keyframe) {
  EncodeOptions options;
  options.keyframe = keyframe;
  encode(frame, options);
}

void VideoEncoder::encode(const VideoFrame& frame,
                          const EncodeOptions& options) {
  if (state_ != CodecState::CONFIGURED) {
    throw std::runtime_error("VideoEncoder is not configured");
  }

  // VideoToolbox は独自の非同期モデルを持つため、ワーカースレッドをバイパス
  if (uses_videotoolbox()) {
    // VideoToolbox エンコーダーの初期化（必要な場合）
    if (!vt_session_) {
      init_videotoolbox_encoder();
    }

    // AVC/HEVC quantizer オプションを取得
    std::optional<uint16_t> quantizer;
    if (options.avc.has_value() && options.avc->quantizer.has_value()) {
      uint16_t q = options.avc->quantizer.value();
      if (q > 51) {
        throw nb::value_error("AVC quantizer must be in range 0-51");
      }
      quantizer = q;
    }
    if (options.hevc.has_value() && options.hevc->quantizer.has_value()) {
      uint16_t q = options.hevc->quantizer.value();
      if (q > 51) {
        throw nb::value_error("HEVC quantizer must be in range 0-51");
      }
      quantizer = q;
    }

    // シーケンス番号を設定して直接エンコード
    current_sequence_ = next_sequence_number_++;
    encode_frame_videotoolbox(frame, options.keyframe, quantizer);

    // デキューコールバックを呼び出す
    if (dequeue_callback_) {
      nb::gil_scoped_acquire gil;
      dequeue_callback_();
    }
    return;
  }

  // その他のコーデックはワーカースレッドにタスクを追加
  EncodeTask task;
  task.frame =
      frame.create_encoder_copy();  // エンコーダー用の安全なコピーを作成
  task.keyframe = options.keyframe;
  task.sequence_number = next_sequence_number_++;

  // AV1 オプションを設定
  if (options.av1.has_value() && options.av1->quantizer.has_value()) {
    uint16_t q = options.av1->quantizer.value();
    if (q > 63) {
      throw nb::value_error("quantizer must be in range 0-63");
    }
    task.av1_quantizer = q;
  }

  // タスクをキューに追加
  {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    encode_queue_.push(task);
    pending_tasks_++;
  }
  queue_cv_.notify_one();

  // デキューコールバックを呼び出す
  if (dequeue_callback_) {
    // ここでは GIL を解放中なので、取得が必要
    nb::gil_scoped_acquire gil;
    dequeue_callback_();
  }
}

void VideoEncoder::handle_encoded_frame(const uint8_t* data,
                                        size_t size,
                                        int64_t timestamp,
                                        bool keyframe) {
  if (output_callback_) {
    std::vector<uint8_t> payload;
    // 生のビットストリームを出力
    payload.assign(data, data + size);

    auto chunk = std::make_shared<EncodedVideoChunk>(
        payload,
        keyframe ? EncodedVideoChunkType::KEY : EncodedVideoChunkType::DELTA,
        timestamp, 0);

    // 順序制御された出力処理
    handle_output(current_sequence_, chunk);
  }

  // Call the dequeue callback if set
  if (dequeue_callback_) {
    nb::gil_scoped_acquire gil;
    dequeue_callback_();
  }
}

void VideoEncoder::flush() {
  if (state_ != CodecState::CONFIGURED) {
    return;
  }

  // VideoToolbox は直接処理されるため、ワーカーキューの待機をスキップ
  if (!uses_videotoolbox()) {
    // 全てのペンディングタスクが完了するまで待機
    {
      std::unique_lock<std::mutex> lock(queue_mutex_);
      queue_cv_.wait(lock, [this]() {
        return encode_queue_.empty() && pending_tasks_ == 0;
      });
    }
  }

  // ワーカースレッドなしでフラッシュ処理を実行
  // 必要であればここで遅延初期化
  if (!aom_encoder_ && is_av1_codec()) {
    init_aom_encoder();
  }

  // VideoToolbox の初期化とフラッシュ
  if (uses_videotoolbox()) {
    if (!vt_session_) {
      init_videotoolbox_encoder();
    }
    // VideoToolbox のフラッシュ処理を実行
    flush_videotoolbox_encoder();
  }

  // AV1 エンコーダーは libaom の排他制御が必要なため専用処理を挟む
  if (aom_encoder_) {
    // エンドオブストリームをシグナル
    std::lock_guard<std::mutex> lock(aom_mutex_);

    // バインディング層で既に GIL を解放しているため、ここでは解放しない
    {
      // nb::gil_scoped_release gil_release;

      // AOM エンコーダーのフラッシュ処理
      // エンドオブストリームをシグナルするため nullptr を渡す
      aom_codec_err_t res = aom_codec_encode(aom_encoder_, nullptr,
                                             frame_count_.fetch_add(1), 1, 0);
      if (res != AOM_CODEC_OK) {
        // エラーをログして続行 (フラッシュ時のエラーは致命的ではない)
      }

      aom_codec_iter_t iter = nullptr;
      const aom_codec_cx_pkt_t* pkt;
      while ((pkt = aom_codec_get_cx_data(aom_encoder_, &iter)) != nullptr) {
        if (pkt->kind == AOM_CODEC_CX_FRAME_PKT) {
          bool is_keyframe = (pkt->data.frame.flags & AOM_FRAME_IS_KEY) != 0;
          // GIL なしで実行
          handle_encoded_frame(static_cast<const uint8_t*>(pkt->data.frame.buf),
                               pkt->data.frame.sz,
                               0,  // タイムスタンプ (未追跡)
                               is_keyframe);
        }
      }
    }
  }
}

void VideoEncoder::reset() {
  // ワーカースレッドを停止
  stop_worker();

  // キューをクリア
  {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    while (!encode_queue_.empty()) {
      encode_queue_.pop();
    }
    pending_tasks_ = 0;
  }

  // 出力バッファをクリア
  {
    std::lock_guard<std::mutex> lock(output_mutex_);
    output_buffer_.clear();
    next_output_sequence_ = 0;
  }

  // シーケンス番号をリセット
  next_sequence_number_ = 0;

  close();
  state_ = CodecState::UNCONFIGURED;
  frame_count_ = 0;

  // ワーカースレッドを再開
  start_worker();
}

void VideoEncoder::close() {
  if (state_ == CodecState::CLOSED) {
    return;
  }

  // ワーカースレッドを停止してからリソースを解放
  stop_worker();

  cleanup_aom_encoder();

#if defined(__APPLE__)
  // VideoToolbox セッションが存在する場合はクリーンアップ
  if (vt_session_) {
    VTCompressionSessionRef s = (VTCompressionSessionRef)vt_session_;
    VTCompressionSessionInvalidate(s);
    CFRelease(s);
    vt_session_ = nullptr;
  }
#endif

#if defined(NVIDIA_CUDA_TOOLKIT)
  // NVENC リソースをクリーンアップ
  cleanup_nvenc_encoder();
#endif

  state_ = CodecState::CLOSED;
}

VideoEncoderSupport VideoEncoder::is_config_supported(
    const VideoEncoderConfig& config) {
  bool supported = false;

  try {
    // コーデック文字列をパースして、パラメータを抽出
    CodecParameters codec_params = parse_codec_string(config.codec);

    // NVIDIA Video Codec SDK でサポートされているかチェック
#if defined(NVIDIA_CUDA_TOOLKIT)
    if (config.hardware_acceleration_engine == HardwareAccelerationEngine::NVIDIA_VIDEO_CODEC) {
      // NVENC は AV1, AVC, HEVC をサポート
      if (std::holds_alternative<AV1CodecParameters>(codec_params) ||
          std::holds_alternative<AVCCodecParameters>(codec_params) ||
          std::holds_alternative<HEVCCodecParameters>(codec_params)) {
        return VideoEncoderSupport(true, config);
      }
    }
#endif

    if (std::holds_alternative<AV1CodecParameters>(codec_params)) {
      supported = true;
    } else if (std::holds_alternative<AVCCodecParameters>(codec_params) ||
               std::holds_alternative<HEVCCodecParameters>(codec_params)) {
#if defined(__APPLE__)
      supported = true;  // macOS で VideoToolbox をサポート
#elif defined(NVIDIA_CUDA_TOOLKIT)
      // NVIDIA Video Codec SDK が有効な場合は nvidia_video_codec を使用
      supported = config.hardware_acceleration_engine == HardwareAccelerationEngine::NVIDIA_VIDEO_CODEC;
#else
      supported = false;  // 他のプラットフォームではまだサポートされていない
#endif
    } else {
      supported = false;
    }
  } catch (const std::exception&) {
    // パースに失敗した場合は未サポート
    supported = false;
  }

  return VideoEncoderSupport(supported, config);
}

// ワーカースレッドの開始
void VideoEncoder::start_worker() {
  should_stop_ = false;
  worker_thread_ = std::thread([this]() { worker_loop(); });
}

// ワーカースレッドの停止
void VideoEncoder::stop_worker() {
  {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    should_stop_ = true;
  }
  queue_cv_.notify_all();

  if (worker_thread_.joinable()) {
    worker_thread_.join();
  }
}

// ワーカースレッドのメインループ
void VideoEncoder::worker_loop() {
  while (true) {
    EncodeTask task;

    // タスクを取得
    {
      std::unique_lock<std::mutex> lock(queue_mutex_);
      queue_cv_.wait(
          lock, [this]() { return !encode_queue_.empty() || should_stop_; });

      if (should_stop_ && encode_queue_.empty()) {
        break;
      }

      if (!encode_queue_.empty()) {
        task = encode_queue_.front();
        encode_queue_.pop();
      } else {
        continue;
      }
    }

    // タスクを処理
    if (task.frame) {
      try {
        process_encode_task(task);
      } catch (const std::exception& e) {
        // エラーが発生した場合、エラーコールバックを呼び出す
        if (error_callback_) {
          nb::gil_scoped_acquire gil;
          try {
            error_callback_(std::string(e.what()));
          } catch (...) {
            // エラーコールバック自体のエラーは無視
          }
        }
        // pending_tasks_ を減らしてから続行 (ワーカースレッドは停止しない)
        {
          std::lock_guard<std::mutex> lock(queue_mutex_);
          pending_tasks_--;
        }
        queue_cv_.notify_all();
        continue;
      }
    }

    // 処理待ちタスク数を減らす
    {
      std::lock_guard<std::mutex> lock(queue_mutex_);
      pending_tasks_--;
    }
    // flush() 待機側へ進捗通知
    queue_cv_.notify_all();
  }
}

// エンコードタスクの処理
void VideoEncoder::process_encode_task(const EncodeTask& task) {
  // 現在のシーケンス番号を保存
  current_sequence_ = task.sequence_number;

  // 遅延初期化 (初回エンコード時)
#if defined(NVIDIA_CUDA_TOOLKIT)
  if (uses_nvidia_video_codec() && !nvenc_encoder_) {
    init_nvenc_encoder();
  }
#endif

  if (is_av1_codec() && !aom_encoder_ && !uses_nvidia_video_codec()) {
    init_aom_encoder();
  }

  if (uses_videotoolbox() && !vt_session_) {
    init_videotoolbox_encoder();
  }

  // バインディング層で既に GIL を解放しているため、ここでは解放しない
  // nb::gil_scoped_release gil_release;

#if defined(NVIDIA_CUDA_TOOLKIT)
  if (uses_nvidia_video_codec()) {
    encode_frame_nvenc(*task.frame, task.keyframe, task.av1_quantizer);
    return;
  }
#endif

  if (is_av1_codec()) {
    encode_frame_aom(*task.frame, task.keyframe, task.av1_quantizer);
  } else if (is_avc_codec() || is_hevc_codec()) {
#if defined(__APPLE__)
    if (config_.hardware_acceleration_engine != HardwareAccelerationEngine::APPLE_VIDEO_TOOLBOX) {
      throw std::runtime_error(
          "AVC/HEVC requires "
          "hardware_acceleration_engine=\"apple_video_toolbox\" on macOS");
    }
    encode_frame_videotoolbox(*task.frame, task.keyframe);
#else
    throw std::runtime_error("AVC/HEVC not supported on this platform");
#endif
  }
}

// 出力フレームの順序制御
void VideoEncoder::handle_output(
    uint64_t sequence,
    std::shared_ptr<EncodedVideoChunk> chunk,
    std::optional<EncodedVideoChunkMetadata> metadata) {
  std::vector<OutputEntry> entries_to_output;

  {
    std::lock_guard<std::mutex> lock(output_mutex_);

    // チャンクと metadata をバッファに追加
    output_buffer_[sequence] = OutputEntry{chunk, metadata};

    // 順序通りに出力できるチャンクを収集
    while (output_buffer_.find(next_output_sequence_) != output_buffer_.end()) {
      entries_to_output.push_back(output_buffer_[next_output_sequence_]);
      output_buffer_.erase(next_output_sequence_);
      next_output_sequence_++;
    }
  }

  // コールバックを呼び出す (GIL を取得)
  // WebCodecs API では callback は (chunk, metadata?) で metadata は optional
  // Python では常に 2 引数で呼び出し、callback 側で metadata=None のデフォルト引数を使用する
  if (output_callback_ && !entries_to_output.empty()) {
    nb::gil_scoped_acquire gil;
    for (auto& entry : entries_to_output) {
      // コピーを作成して渡す (Python 側で所有権を持つ)
      EncodedVideoChunk chunk_copy = *entry.chunk;

      // metadata を dict に変換 (存在しない場合は空の dict)
      nb::dict metadata_dict;
      if (entry.metadata.has_value() &&
          entry.metadata->decoder_config.has_value()) {
        const auto& config = entry.metadata->decoder_config.value();
        nb::dict decoder_config_dict;
        decoder_config_dict["codec"] = config.codec;
        if (config.coded_width.has_value()) {
          decoder_config_dict["codedWidth"] = config.coded_width.value();
        }
        if (config.coded_height.has_value()) {
          decoder_config_dict["codedHeight"] = config.coded_height.value();
        }
        if (config.description.has_value()) {
          const auto& desc = config.description.value();
          decoder_config_dict["description"] = nb::bytes(
              reinterpret_cast<const char*>(desc.data()), desc.size());
        }
        metadata_dict["decoderConfig"] = decoder_config_dict;
      }

      // callback を呼び出す
      // Python 側では def on_output(chunk, metadata=None): と定義することを推奨
      // 後方互換性のため、まず 2 引数で呼び出しを試み、失敗したら 1 引数で呼び出す
      try {
        output_callback_(chunk_copy, metadata_dict);
      } catch (const nb::python_error&) {
        // 2 引数呼び出しが失敗した場合は 1 引数で呼び出す (後方互換性)
        PyErr_Clear();
        output_callback_(chunk_copy);
      }
    }
  }
}

void init_video_encoder(nb::module_& m) {
  nb::class_<VideoEncoder>(m, "VideoEncoder")
      .def(nb::init<nb::object, nb::object>(), "output"_a, "error"_a,
           nb::sig("def __init__(self, output: "
                   "typing.Callable[[EncodedVideoChunk], None], "
                   "error: typing.Callable[[str], None], /) -> None"))
      .def("configure", &VideoEncoder::configure, "config"_a,
           nb::sig("def configure(self, config: webcodecs.VideoEncoderConfig, "
                   "/) -> None"))
      // WebCodecs 互換: encode(frame) または encode(frame, {"keyFrame": True})
      .def(
          "encode",
          [](VideoEncoder& self, const VideoFrame& frame) {
            self.encode(frame, false);
          },
          "frame"_a, nb::call_guard<nb::gil_scoped_release>(),
          nb::sig("def encode(self, frame: VideoFrame, /) -> None"))
      .def(
          "encode",
          [](VideoEncoder& self, const VideoFrame& frame, nb::dict options) {
            VideoEncoder::EncodeOptions encode_options;

            // dict アクセスには GIL が必要なので、ここでは解放しない
            if (options.contains("keyFrame")) {
              encode_options.keyframe = nb::cast<bool>(options["keyFrame"]);
            }

            // AV1 オプションを解析
            if (options.contains("av1")) {
              nb::dict av1_dict = nb::cast<nb::dict>(options["av1"]);
              VideoEncoder::AV1EncodeOptions av1_options;
              if (av1_dict.contains("quantizer")) {
                av1_options.quantizer =
                    nb::cast<uint16_t>(av1_dict["quantizer"]);
              }
              encode_options.av1 = av1_options;
            }

            // AVC オプションを解析
            if (options.contains("avc")) {
              nb::dict avc_dict = nb::cast<nb::dict>(options["avc"]);
              VideoEncoder::AVCEncodeOptions avc_options;
              if (avc_dict.contains("quantizer")) {
                avc_options.quantizer =
                    nb::cast<uint16_t>(avc_dict["quantizer"]);
              }
              encode_options.avc = avc_options;
            }

            // HEVC オプションを解析
            if (options.contains("hevc")) {
              nb::dict hevc_dict = nb::cast<nb::dict>(options["hevc"]);
              VideoEncoder::HEVCEncodeOptions hevc_options;
              if (hevc_dict.contains("quantizer")) {
                hevc_options.quantizer =
                    nb::cast<uint16_t>(hevc_dict["quantizer"]);
              }
              encode_options.hevc = hevc_options;
            }

            // GIL を手動で解放してエンコード実行
            {
              nb::gil_scoped_release gil;
              self.encode(frame, encode_options);
            }
          },
          "frame"_a, "options"_a,
          nb::sig("def encode(self, frame: VideoFrame, options: "
                  "webcodecs.VideoEncoderEncodeOptions, /) -> None"))
      .def("flush", &VideoEncoder::flush,
           nb::call_guard<nb::gil_scoped_release>(),
           nb::sig("def flush(self, /) -> None"))
      .def("reset", &VideoEncoder::reset, nb::sig("def reset(self, /) -> None"))
      .def("close", &VideoEncoder::close, nb::sig("def close(self, /) -> None"))
      .def_prop_ro("state", &VideoEncoder::state,
                   nb::sig("def state(self, /) -> CodecState"))
      .def_prop_ro("encode_queue_size", &VideoEncoder::encode_queue_size,
                   nb::sig("def encode_queue_size(self, /) -> int"))
      .def_static(
          "is_config_supported",
          [](nb::dict config_dict) {
            // dict から VideoEncoderConfig へ変換
            VideoEncoderConfig config;

            // 必須フィールド
            if (!config_dict.contains("codec"))
              throw nb::value_error("codec is required");
            config.codec = nb::cast<std::string>(config_dict["codec"]);
            if (!config_dict.contains("width"))
              throw nb::value_error("width is required");
            config.width = nb::cast<uint32_t>(config_dict["width"]);
            if (!config_dict.contains("height"))
              throw nb::value_error("height is required");
            config.height = nb::cast<uint32_t>(config_dict["height"]);

            // オプションフィールド
            if (config_dict.contains("display_width"))
              config.display_width =
                  nb::cast<uint32_t>(config_dict["display_width"]);
            if (config_dict.contains("display_height"))
              config.display_height =
                  nb::cast<uint32_t>(config_dict["display_height"]);
            if (config_dict.contains("bitrate"))
              config.bitrate = nb::cast<uint64_t>(config_dict["bitrate"]);
            if (config_dict.contains("framerate"))
              config.framerate = nb::cast<double>(config_dict["framerate"]);
            if (config_dict.contains("hardware_acceleration"))
              config.hardware_acceleration = nb::cast<HardwareAcceleration>(
                  config_dict["hardware_acceleration"]);
            if (config_dict.contains("alpha"))
              config.alpha = nb::cast<AlphaOption>(config_dict["alpha"]);
            if (config_dict.contains("scalability_mode"))
              config.scalability_mode =
                  nb::cast<std::string>(config_dict["scalability_mode"]);
            if (config_dict.contains("bitrate_mode"))
              config.bitrate_mode = nb::cast<VideoEncoderBitrateMode>(
                  config_dict["bitrate_mode"]);
            if (config_dict.contains("latency_mode"))
              config.latency_mode =
                  nb::cast<LatencyMode>(config_dict["latency_mode"]);
            if (config_dict.contains("content_hint"))
              config.content_hint =
                  nb::cast<std::string>(config_dict["content_hint"]);
            if (config_dict.contains("hardware_acceleration_engine"))
              config.hardware_acceleration_engine = nb::cast<HardwareAccelerationEngine>(
                  config_dict["hardware_acceleration_engine"]);

            return VideoEncoder::is_config_supported(config);
          },
          "config"_a,
          nb::sig("def is_config_supported(config: "
                  "webcodecs.VideoEncoderConfig, /) -> "
                  "webcodecs.VideoEncoderSupport"))
      .def("on_output", &VideoEncoder::on_output,
           nb::sig("def on_output(self, callback: "
                   "typing.Callable[[EncodedVideoChunk], None], /) -> None"))
      .def("on_error", &VideoEncoder::on_error,
           nb::sig("def on_error(self, callback: typing.Callable[[str], None], "
                   "/) -> None"))
      .def("on_dequeue", &VideoEncoder::on_dequeue,
           nb::sig("def on_dequeue(self, callback: typing.Callable[[], None], "
                   "/) -> None"));
}

#if !defined(__APPLE__)
// Stubs for non-Apple platforms (ensure link succeeds)
void VideoEncoder::init_videotoolbox_encoder() {
  throw std::runtime_error("VideoToolbox is only available on macOS");
}
void VideoEncoder::encode_frame_videotoolbox(const VideoFrame&,
                                             bool,
                                             std::optional<uint16_t>) {
  throw std::runtime_error("VideoToolbox is only available on macOS");
}
void VideoEncoder::flush_videotoolbox_encoder() {}
void VideoEncoder::cleanup_videotoolbox_encoder() {}
#endif
