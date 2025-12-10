#include "video_decoder.h"
#include <cstring>
#include <stdexcept>

using namespace nb::literals;

VideoCodec VideoDecoder::string_to_codec(const std::string& codec) {
  // 完全なコーデック文字列フォーマットを持つ AV1 (av01.x.xxM.xx)
  if (codec.length() >= 5 && codec.substr(0, 5) == "av01.")
    return VideoCodec::AV1;
  // 完全なコーデック文字列フォーマットを持つ AVC (H.264) (avc1.xxxxxx または avc3.xxxxxx)
  if (codec.length() >= 5 &&
      (codec.substr(0, 5) == "avc1." || codec.substr(0, 5) == "avc3."))
    return VideoCodec::H264;
  // 完全なコーデック文字列フォーマットを持つ HEVC (H.265) (hvc1.x.x.xxx.xx または hev1.x.x.xxx.xx)
  if (codec.length() >= 5 &&
      (codec.substr(0, 5) == "hvc1." || codec.substr(0, 5) == "hev1."))
    return VideoCodec::H265;
  // VP8
  if (codec == "vp8")
    return VideoCodec::VP8;
  // VP9 (vp09.PP.LL.DD)
  if (codec.length() >= 5 && codec.substr(0, 5) == "vp09.")
    return VideoCodec::VP9;
  throw std::runtime_error("Unknown codec: " + codec);
}

VideoDecoder::VideoDecoder(nb::object output, nb::object error)
    : output_callback_([output](std::unique_ptr<VideoFrame> frame) {
        if (output) {
          nb::gil_scoped_acquire gil;
          output(std::move(frame));
        }
      }),
      error_callback_([error](const std::string& err) {
        if (error) {
          nb::gil_scoped_acquire gil;
          error(err);
        }
      }),
      dequeue_callback_(nullptr),
      state_(CodecState::UNCONFIGURED),
      decoder_context_(nullptr),
      config_() {  // デフォルトコンストラクタ
  // コンストラクタではコーデックの初期化は行わない
  // configure() で初期化する
}

VideoDecoder::~VideoDecoder() {
  stop_worker();  // ワーカースレッドを停止
  close();
}

void VideoDecoder::configure(nb::dict config_dict) {
  if (state_ == CodecState::CLOSED) {
    throw std::runtime_error("Decoder is closed");
  }

  // dict から VideoDecoderConfig へ変換
  VideoDecoderConfig config;

  // 必須フィールド
  if (!config_dict.contains("codec"))
    throw nb::value_error("codec is required");
  config.codec = nb::cast<std::string>(config_dict["codec"]);

  // オプションフィールド
  if (config_dict.contains("coded_width"))
    config.coded_width = nb::cast<uint32_t>(config_dict["coded_width"]);
  if (config_dict.contains("coded_height"))
    config.coded_height = nb::cast<uint32_t>(config_dict["coded_height"]);
  if (config_dict.contains("description")) {
    // description は bytes として扱う
    nb::bytes desc = nb::cast<nb::bytes>(config_dict["description"]);
    const char* ptr = desc.c_str();
    size_t size = desc.size();
    config.description =
        std::vector<uint8_t>(reinterpret_cast<const uint8_t*>(ptr),
                             reinterpret_cast<const uint8_t*>(ptr) + size);
  }
  if (config_dict.contains("hardware_acceleration_engine")) {
    config.hardware_acceleration_engine = nb::cast<HardwareAccelerationEngine>(
        config_dict["hardware_acceleration_engine"]);
  }

  // 既存のデコーダーをクリーンアップ
  if (decoder_context_) {
    cleanup_decoder();
  }

  // VideoDecoderConfig をそのまま保存
  config_ = config;

  // コーデック文字列をパースして、パラメータを抽出
  try {
    codec_params_ = parse_codec_string(config_.codec);
  } catch (const std::exception& e) {
    throw std::invalid_argument(std::string("Invalid codec string: ") +
                                e.what());
  }

  init_decoder();

  // ワーカースレッドの開始
  // VideoToolbox は独自の非同期モデルを持つため、ワーカースレッドを開始しない
#if defined(__APPLE__)
  if (!uses_apple_video_toolbox()) {
    if (!worker_thread_.joinable()) {
      start_worker();  // ワーカースレッドを開始
    }
  }
#else
  if (!worker_thread_.joinable()) {
    start_worker();  // ワーカースレッドを開始
  }
#endif

  state_ = CodecState::CONFIGURED;
}

void VideoDecoder::decode(const EncodedVideoChunk& chunk) {
  if (state_ != CodecState::CONFIGURED) {
    throw std::runtime_error("Decoder is not configured");
  }

  // VideoToolbox は独自の非同期モデルを持つため、ワーカースレッドをバイパス
#if defined(__APPLE__)
  if (uses_apple_video_toolbox()) {
    // シーケンス番号を設定して直接デコード
    current_sequence_ = next_sequence_number_++;
    bool success = decode_internal(chunk);
    if (!success && error_callback_) {
      nb::gil_scoped_acquire gil;
      error_callback_("Decode failed");
    }

    // デキューコールバックを呼び出す
    if (dequeue_callback_) {
      nb::gil_scoped_acquire gil;
      dequeue_callback_();
    }
    return;
  }
#endif

  // その他のコーデックはワーカースレッドを使用
  DecodeTask task;
  task.chunk = chunk;  // optional への代入
  task.sequence_number = next_sequence_number_++;

  // タスクをキューに追加
  {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    decode_queue_.push(task);
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

void VideoDecoder::flush() {
  if (state_ != CodecState::CONFIGURED) {
    return;
  }

  // NVIDIA Video Codec SDK の場合
#if defined(USE_NVIDIA_CUDA_TOOLKIT)
  if (uses_nvidia_video_codec()) {
    // 全てのペンディングタスクが完了するまで待機
    {
      std::unique_lock<std::mutex> lock(queue_mutex_);
      queue_cv_.wait(lock, [this]() {
        return decode_queue_.empty() && pending_tasks_ == 0;
      });
    }
    flush_nvdec();
    return;
  }
#endif

  // Intel VPL の場合
#if defined(__linux__)
  if (uses_intel_vpl()) {
    // 全てのペンディングタスクが完了するまで待機
    {
      std::unique_lock<std::mutex> lock(queue_mutex_);
      queue_cv_.wait(lock, [this]() {
        return decode_queue_.empty() && pending_tasks_ == 0;
      });
    }
    flush_intel_vpl();

    // 出力バッファに残っているフレームを全て出力
    std::vector<std::unique_ptr<VideoFrame>> frames_to_output;
    {
      std::lock_guard<std::mutex> lock(output_mutex_);
      for (auto& pair : output_buffer_) {
        frames_to_output.push_back(std::move(pair.second));
      }
      output_buffer_.clear();
    }

    // コールバックを呼び出す（GIL を取得）
    if (output_callback_ && !frames_to_output.empty()) {
      nb::gil_scoped_acquire gil;
      for (auto& frame : frames_to_output) {
        output_callback_(std::move(frame));
      }
    }

    return;
  }
#endif

  // VideoToolbox は直接処理されるため、ワーカーキューの待機をスキップ
#if defined(__APPLE__)
  if (!uses_apple_video_toolbox()) {
#endif
    // 全てのペンディングタスクが完了するまで待機
    {
      std::unique_lock<std::mutex> lock(queue_mutex_);
      queue_cv_.wait(lock, [this]() {
        return decode_queue_.empty() && pending_tasks_ == 0;
      });
    }
#if defined(__APPLE__)
  }
#endif

  // VideoToolbox の場合
#if defined(__APPLE__)
  if (uses_apple_video_toolbox()) {
    if (vt_session_) {
      // VideoToolbox デコーダーでのフラッシュ処理
      flush_videotoolbox();
    }
  }
#endif
}

void VideoDecoder::reset() {
  if (state_ == CodecState::CLOSED) {
    throw std::runtime_error("Decoder is closed");
  }

  // ワーカースレッドを停止
  stop_worker();

  // キューをクリア
  {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    while (!decode_queue_.empty()) {
      decode_queue_.pop();
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

  // デコーダーをリセット
  if (decoder_context_) {
    cleanup_decoder();
    init_decoder();
  }

  // ワーカースレッドを再開
  start_worker();
}

void VideoDecoder::close() {
  if (state_ == CodecState::CLOSED) {
    return;
  }

  // ワーカースレッドを停止してからリソースを解放
  // stop_worker() は再入可能なので、デストラクタから呼ばれても安全
  if (worker_thread_.joinable()) {
    stop_worker();
  }
  cleanup_decoder();
  state_ = CodecState::CLOSED;
}

VideoDecoderSupport VideoDecoder::is_config_supported(
    const VideoDecoderConfig& config) {
  bool supported = false;
  try {
    VideoCodec codec = string_to_codec(config.codec);

    // NVIDIA Video Codec SDK でサポートされているかチェック
#if defined(USE_NVIDIA_CUDA_TOOLKIT)
    if (config.hardware_acceleration_engine.has_value() &&
        config.hardware_acceleration_engine.value() ==
            HardwareAccelerationEngine::NVIDIA_VIDEO_CODEC) {
      // NVDEC でサポートされているコーデック: AV1, AVC, HEVC, VP8, VP9
      if (codec == VideoCodec::AV1 || codec == VideoCodec::H264 ||
          codec == VideoCodec::H265 || codec == VideoCodec::VP8 ||
          codec == VideoCodec::VP9) {
        return VideoDecoderSupport(true, config);
      }
    }
#endif

#if defined(__APPLE__)
    // Apple Video Toolbox でサポートされているかチェック
    if (config.hardware_acceleration_engine.has_value() &&
        config.hardware_acceleration_engine.value() ==
            HardwareAccelerationEngine::APPLE_VIDEO_TOOLBOX) {
      // VideoToolbox でサポートされているコーデック: H.264, H.265, VP9, AV1
      if (codec == VideoCodec::H264 || codec == VideoCodec::H265 ||
          codec == VideoCodec::VP9 || codec == VideoCodec::AV1) {
        return VideoDecoderSupport(true, config);
      }
    }
#endif

    // Intel VPL でサポートされているかチェック
#if defined(__linux__)
    if (config.hardware_acceleration_engine.has_value() &&
        config.hardware_acceleration_engine.value() ==
            HardwareAccelerationEngine::INTEL_VPL) {
      // Intel VPL でサポートされているコーデック: AVC, HEVC, AV1
      if (codec == VideoCodec::H264 || codec == VideoCodec::H265 ||
          codec == VideoCodec::AV1) {
        // ハードウェアがサポートしているか実際に確認する必要がある
        // 今は true を返すが、実際のハードウェアサポートは初期化時にチェックされる
        return VideoDecoderSupport(true, config);
      }
    }
#endif

    switch (codec) {
      case VideoCodec::AV1:
        // AV1 は dav1d でソフトウェアデコード
        supported = true;
        break;
      case VideoCodec::H264:
      case VideoCodec::H265:
#if defined(__APPLE__)
        supported = true;  // macOS で VideoToolbox をサポート
#elif defined(USE_NVIDIA_CUDA_TOOLKIT) || defined(__linux__)
        // NVIDIA Video Codec SDK または Intel VPL が有効な場合
        supported = config.hardware_acceleration_engine.has_value() &&
                    (config.hardware_acceleration_engine.value() ==
                         HardwareAccelerationEngine::NVIDIA_VIDEO_CODEC ||
                     config.hardware_acceleration_engine.value() ==
                         HardwareAccelerationEngine::INTEL_VPL);
#else
        supported = false;  // 他のプラットフォームではまだサポートされていない
#endif
        break;
      case VideoCodec::VP8:
      case VideoCodec::VP9:
#if defined(USE_NVIDIA_CUDA_TOOLKIT)
        // NVIDIA Video Codec SDK が有効な場合は HardwareAccelerationEngine.NVIDIA_VIDEO_CODEC を使用
        if (config.hardware_acceleration_engine.has_value() &&
            config.hardware_acceleration_engine.value() ==
                HardwareAccelerationEngine::NVIDIA_VIDEO_CODEC) {
          supported = true;
        } else {
#if defined(__APPLE__) || defined(__linux__)
          supported = true;  // macOS / Linux で libvpx をサポート
#else
          supported = false;
#endif
        }
#elif defined(__APPLE__) || defined(__linux__)
        supported = true;  // macOS / Linux で libvpx をサポート
#else
        supported = false;  // 他のプラットフォームではまだサポートされていない
#endif
        break;
      default:
        supported = false;
        break;
    }
  } catch (const std::exception&) {
    // 未知のコーデックは未サポート
    supported = false;
  }
  return VideoDecoderSupport(supported, config);
}

void VideoDecoder::init_decoder() {
  // NVIDIA Video Codec SDK を使用する場合
#if defined(USE_NVIDIA_CUDA_TOOLKIT)
  if (uses_nvidia_video_codec()) {
    init_nvdec_decoder();
    return;
  }
#endif

  // Apple Video Toolbox を使用する場合
#if defined(__APPLE__)
  if (uses_apple_video_toolbox()) {
    init_videotoolbox_decoder();
    return;
  }
#endif

  // Intel VPL を使用する場合
#if defined(__linux__)
  if (uses_intel_vpl()) {
    init_intel_vpl_decoder();
    return;
  }
#endif

  VideoCodec codec = string_to_codec(config_.codec);
  switch (codec) {
    case VideoCodec::AV1:
      init_dav1d_decoder();
      break;
    case VideoCodec::H264:
    case VideoCodec::H265:
      // H.264/H.265 は uses_apple_video_toolbox()/uses_intel_vpl() で処理されるため、ここには到達しない
      throw std::runtime_error("H.264/H.265 not supported on this platform");
      break;
    case VideoCodec::VP8:
    case VideoCodec::VP9:
#if defined(__APPLE__) || defined(__linux__)
      init_vpx_decoder();
#else
      throw std::runtime_error("VP8/VP9 not supported on this platform");
#endif
      break;
    default:
      throw std::runtime_error("Unsupported codec");
  }
}

void VideoDecoder::cleanup_decoder() {
  // ワーカースレッドが完全に停止していることを確認
  if (worker_thread_.joinable()) {
    // まだワーカースレッドが動いている場合は先に停止
    stop_worker();
  }

  // NVIDIA Video Codec SDK のクリーンアップ
#if defined(USE_NVIDIA_CUDA_TOOLKIT)
  if (nvdec_decoder_) {
    cleanup_nvdec_decoder();
    return;
  }
#endif

  // Apple Video Toolbox のクリーンアップ
#if defined(__APPLE__)
  if (vt_session_) {
    cleanup_videotoolbox_decoder();
    decoder_context_ = nullptr;
    return;
  }
#endif

  // Intel VPL のクリーンアップ
#if defined(__linux__)
  if (vpl_session_) {
    cleanup_intel_vpl_decoder();
    return;
  }
#endif

  if (!decoder_context_) {
#if defined(__APPLE__) || defined(__linux__)
    // VPX デコーダーは decoder_context_ を使わないのでここでクリーンアップ
    cleanup_vpx_decoder();
#endif
    return;
  }

  VideoCodec codec = string_to_codec(config_.codec);
  switch (codec) {
    case VideoCodec::AV1:
      cleanup_dav1d_decoder();
      break;
    case VideoCodec::H264:
    case VideoCodec::H265:
      // VideoToolbox/Intel VPL は上で処理されるため、ここには到達しない
      break;
    case VideoCodec::VP8:
    case VideoCodec::VP9:
#if defined(__APPLE__) || defined(__linux__)
      cleanup_vpx_decoder();
#endif
      break;
    default:
      break;
  }

  decoder_context_ = nullptr;
}

bool VideoDecoder::decode_internal(const EncodedVideoChunk& chunk) {
  // NVIDIA Video Codec SDK を使用する場合
#if defined(USE_NVIDIA_CUDA_TOOLKIT)
  if (uses_nvidia_video_codec()) {
    return decode_nvdec(chunk);
  }
#endif

  // Apple Video Toolbox を使用する場合
#if defined(__APPLE__)
  if (uses_apple_video_toolbox()) {
    return decode_videotoolbox(chunk);
  }
#endif

  // Intel VPL を使用する場合
#if defined(__linux__)
  if (uses_intel_vpl()) {
    return decode_intel_vpl(chunk);
  }
#endif

  VideoCodec codec = string_to_codec(config_.codec);
  switch (codec) {
    case VideoCodec::AV1:
      return decode_dav1d(chunk);
    case VideoCodec::H264:
    case VideoCodec::H265:
      // H.264/H.265 は uses_apple_video_toolbox()/uses_intel_vpl() で処理されるため、ここには到達しない
      if (error_callback_) {
        nb::gil_scoped_acquire gil;
        error_callback_("H.264/H.265 not supported on this platform");
      }
      return false;
    case VideoCodec::VP8:
    case VideoCodec::VP9:
#if defined(__APPLE__) || defined(__linux__)
      return decode_vpx(chunk);
#else
      if (error_callback_) {
        nb::gil_scoped_acquire gil;
        error_callback_("VP8/VP9 not supported on this platform");
      }
      return false;
#endif
    default:
      return false;
  }
}

void VideoDecoder::flush_dav1d() {
  // AV1 デコーダーのフラッシュ処理は decode_dav1d() 側で実行される
  // ここでは特に何もしない
}

// 分割されたファイルをインクルード
#include "video_decoder_apple_video_toolbox.cpp"
#include "video_decoder_dav1d.cpp"
#include "video_decoder_nvidia.cpp"
#if defined(__APPLE__) || defined(__linux__)
#include "video_decoder_vpx.cpp"
#endif
#if defined(__linux__)
#include "video_decoder_intel_vpl.cpp"
#endif

bool VideoDecoder::uses_nvidia_video_codec() const {
#if defined(USE_NVIDIA_CUDA_TOOLKIT)
  // NVIDIA Video Codec SDK が有効な場合
  // エンコーダー/デコーダー: H.264, H.265, AV1
  // デコーダーのみ: VP8, VP9
  VideoCodec codec = string_to_codec(config_.codec);
  return (codec == VideoCodec::H264 || codec == VideoCodec::H265 ||
          codec == VideoCodec::AV1 || codec == VideoCodec::VP8 ||
          codec == VideoCodec::VP9) &&
         config_.hardware_acceleration_engine.has_value() &&
         config_.hardware_acceleration_engine.value() ==
             HardwareAccelerationEngine::NVIDIA_VIDEO_CODEC;
#else
  return false;
#endif
}

bool VideoDecoder::uses_apple_video_toolbox() const {
#if defined(__APPLE__)
  // Apple Video Toolbox が有効な場合
  // H.264, H.265: デフォルトで VideoToolbox を使用（WebCodecs API 準拠）
  // VP9, AV1: HardwareAccelerationEngine.APPLE_VIDEO_TOOLBOX 指定時のみ
  VideoCodec codec = string_to_codec(config_.codec);
  if (codec == VideoCodec::H264 || codec == VideoCodec::H265) {
    // hardware_acceleration_engine が未指定、または明示的に APPLE_VIDEO_TOOLBOX が指定された場合
    if (!config_.hardware_acceleration_engine.has_value()) {
      return true;  // 未指定の場合は自動的に VideoToolbox を使用
    }
    return config_.hardware_acceleration_engine.value() ==
           HardwareAccelerationEngine::APPLE_VIDEO_TOOLBOX;
  }
  if (codec == VideoCodec::VP9 || codec == VideoCodec::AV1) {
    return config_.hardware_acceleration_engine.has_value() &&
           config_.hardware_acceleration_engine.value() ==
               HardwareAccelerationEngine::APPLE_VIDEO_TOOLBOX;
  }
  return false;
#else
  return false;
#endif
}

bool VideoDecoder::uses_intel_vpl() const {
#if defined(__linux__)
  VideoCodec codec = string_to_codec(config_.codec);
  return (codec == VideoCodec::H264 || codec == VideoCodec::H265) &&
         config_.hardware_acceleration_engine.has_value() &&
         config_.hardware_acceleration_engine.value() ==
             HardwareAccelerationEngine::INTEL_VPL;
#else
  return false;
#endif
}

// ワーカースレッドの開始
void VideoDecoder::start_worker() {
  should_stop_ = false;
  worker_thread_ = std::thread([this]() { worker_loop(); });
}

// ワーカースレッドの停止
void VideoDecoder::stop_worker() {
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
void VideoDecoder::worker_loop() {
  while (true) {
    DecodeTask task;

    // タスクを取得
    {
      std::unique_lock<std::mutex> lock(queue_mutex_);
      queue_cv_.wait(
          lock, [this]() { return !decode_queue_.empty() || should_stop_; });

      if (should_stop_ && decode_queue_.empty()) {
        break;
      }

      if (!decode_queue_.empty()) {
        task = decode_queue_.front();
        decode_queue_.pop();
      } else {
        continue;  // タスクがない場合は次のループへ
      }
    }

    // タスクを処理
    if (task.chunk.has_value()) {
      process_decode_task(task);
    }

    // 処理待ちタスク数を減らす
    {
      std::lock_guard<std::mutex> lock(queue_mutex_);
      pending_tasks_--;
    }
    // flush() の待機側に通知
    queue_cv_.notify_all();
  }
}

// デコードタスクの処理
void VideoDecoder::process_decode_task(const DecodeTask& task) {
  // 現在のシーケンス番号を保存（デコーダーが使用するため）
  current_sequence_ = task.sequence_number;

  bool success = decode_internal(*task.chunk);

  if (!success) {
    // エラー処理
    if (error_callback_) {
      nb::gil_scoped_acquire gil;
      error_callback_(std::string("Failed to decode chunk"));
    }
  }

  // 注: decode_internal が出力フレームを生成した場合、
  // handle_output() を通じて順序制御される
}

// 出力フレームの順序制御
void VideoDecoder::handle_output(uint64_t sequence,
                                 std::unique_ptr<VideoFrame> frame) {
  std::vector<std::unique_ptr<VideoFrame>> frames_to_output;

  {
    std::lock_guard<std::mutex> lock(output_mutex_);

    // フレームをバッファに追加
    output_buffer_[sequence] = std::move(frame);

    // 順序通りに出力できるフレームを収集
    while (output_buffer_.find(next_output_sequence_) != output_buffer_.end()) {
      frames_to_output.push_back(
          std::move(output_buffer_[next_output_sequence_]));
      output_buffer_.erase(next_output_sequence_);
      next_output_sequence_++;
    }
  }

  // コールバックを呼び出す（GIL を取得）
  if (output_callback_ && !frames_to_output.empty()) {
    nb::gil_scoped_acquire gil;
    for (auto& frame : frames_to_output) {
      if (frame) {  // null チェック
        output_callback_(std::move(frame));
      }
    }
  }
}

void init_video_decoder(nb::module_& m) {
  nb::class_<VideoDecoder>(m, "VideoDecoder")
      .def(
          nb::init<nb::object, nb::object>(), "output"_a, "error"_a,
          nb::sig(
              "def __init__(self, output: typing.Callable[[VideoFrame], None], "
              "error: typing.Callable[[str], None], /) -> None"))
      .def("configure", &VideoDecoder::configure, "config"_a,
           nb::sig("def configure(self, config: webcodecs.VideoDecoderConfig, "
                   "/) -> None"))
      .def("decode", &VideoDecoder::decode, "chunk"_a,
           nb::call_guard<nb::gil_scoped_release>(),
           nb::sig("def decode(self, chunk: EncodedVideoChunk, /) -> None"))
      .def("flush", &VideoDecoder::flush,
           nb::call_guard<nb::gil_scoped_release>(),
           nb::sig("def flush(self, /) -> None"))
      .def("reset", &VideoDecoder::reset, nb::sig("def reset(self, /) -> None"))
      .def("close", &VideoDecoder::close, nb::sig("def close(self, /) -> None"))
      .def_prop_ro("state", &VideoDecoder::state,
                   nb::sig("def state(self, /) -> CodecState"))
      .def_prop_ro("decode_queue_size", &VideoDecoder::decode_queue_size,
                   nb::sig("def decode_queue_size(self, /) -> int"))
      .def_static(
          "is_config_supported",
          [](nb::dict config_dict) {
            // dict から VideoDecoderConfig へ変換
            VideoDecoderConfig config;

            // 必須フィールド
            if (!config_dict.contains("codec"))
              throw nb::value_error("codec is required");
            config.codec = nb::cast<std::string>(config_dict["codec"]);

            // オプションフィールド
            if (config_dict.contains("coded_width"))
              config.coded_width =
                  nb::cast<uint32_t>(config_dict["coded_width"]);
            if (config_dict.contains("coded_height"))
              config.coded_height =
                  nb::cast<uint32_t>(config_dict["coded_height"]);
            if (config_dict.contains("description")) {
              nb::bytes desc = nb::cast<nb::bytes>(config_dict["description"]);
              const char* ptr = desc.c_str();
              size_t size = desc.size();
              config.description = std::vector<uint8_t>(
                  reinterpret_cast<const uint8_t*>(ptr),
                  reinterpret_cast<const uint8_t*>(ptr) + size);
            }
            if (config_dict.contains("hardware_acceleration_engine"))
              config.hardware_acceleration_engine =
                  nb::cast<HardwareAccelerationEngine>(
                      config_dict["hardware_acceleration_engine"]);
            if (config_dict.contains("optimize_for_latency"))
              config.optimize_for_latency =
                  nb::cast<bool>(config_dict["optimize_for_latency"]);

            return VideoDecoder::is_config_supported(config);
          },
          "config"_a,
          nb::sig("def is_config_supported(config: "
                  "webcodecs.VideoDecoderConfig, /) -> "
                  "webcodecs.VideoDecoderSupport"))
      .def(
          "on_output",
          [](VideoDecoder& self, nb::object callback) {
            self.on_output([callback](std::unique_ptr<VideoFrame> frame) {
              nb::gil_scoped_acquire gil;
              callback(frame.release());
            });
          },
          nb::sig("def on_output(self, callback: typing.Callable[[VideoFrame], "
                  "None], /) -> None"))
      .def(
          "on_error",
          [](VideoDecoder& self, nb::object callback) {
            self.on_error([callback](const std::string& error) {
              nb::gil_scoped_acquire gil;
              callback(error);
            });
          },
          nb::sig("def on_error(self, callback: typing.Callable[[str], None], "
                  "/) -> None"))
      .def(
          "on_dequeue",
          [](VideoDecoder& self, nb::object callback) {
            self.on_dequeue([callback]() {
              nb::gil_scoped_acquire gil;
              callback();
            });
          },
          nb::sig("def on_dequeue(self, callback: typing.Callable[[], None], "
                  "/) -> None"));
}
