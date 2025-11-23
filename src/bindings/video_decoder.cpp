#include "video_decoder.h"
#include <cstring>
#include <stdexcept>

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
    // description は string として扱う
    config.description = nb::cast<std::string>(config_dict["description"]);
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
  VideoCodec codec = string_to_codec(config_.codec);
  if (!(codec == VideoCodec::H264 || codec == VideoCodec::H265)) {
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
  VideoCodec codec = string_to_codec(config_.codec);
  if (codec == VideoCodec::H264 || codec == VideoCodec::H265) {
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

  // VideoToolbox は直接処理されるため、ワーカーキューの待機をスキップ
#if defined(__APPLE__)
  VideoCodec codec = string_to_codec(config_.codec);
  if (!(codec == VideoCodec::H264 || codec == VideoCodec::H265)) {
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
  if (string_to_codec(config_.codec) == VideoCodec::H264 ||
      string_to_codec(config_.codec) == VideoCodec::H265) {
#if defined(__APPLE__)
    if (vt_session_) {
      // VideoToolbox デコーダーでのフラッシュ処理
      // VTDecompressionSessionWaitForAsynchronousFrames は
      // video_decoder_apple_video_toolbox.cpp で定義された関数経由で呼ぶ
      flush_videotoolbox();
    }
#endif
  }
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
    switch (codec) {
      case VideoCodec::AV1:
        supported = true;
        break;
      case VideoCodec::H264:
      case VideoCodec::H265:
#if defined(__APPLE__)
        supported = true;  // macOS で VideoToolbox をサポート
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
  VideoCodec codec = string_to_codec(config_.codec);
  switch (codec) {
    case VideoCodec::AV1:
      init_dav1d_decoder();
      break;
    case VideoCodec::H264:
    case VideoCodec::H265:
#if defined(__APPLE__)
      init_videotoolbox_decoder();
#else
      throw std::runtime_error("H.264/H.265 not supported on this platform");
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

  if (!decoder_context_) {
    return;
  }

  VideoCodec codec = string_to_codec(config_.codec);
  switch (codec) {
    case VideoCodec::AV1:
      cleanup_dav1d_decoder();
      break;
    case VideoCodec::H264:
    case VideoCodec::H265:
#if defined(__APPLE__)
      cleanup_videotoolbox_decoder();
#endif
      break;
    default:
      break;
  }

  decoder_context_ = nullptr;
}

bool VideoDecoder::decode_internal(const EncodedVideoChunk& chunk) {
  VideoCodec codec = string_to_codec(config_.codec);
  switch (codec) {
    case VideoCodec::AV1:
      return decode_dav1d(chunk);
    case VideoCodec::H264:
    case VideoCodec::H265:
#if defined(__APPLE__)
      return decode_videotoolbox(chunk);
#else
      if (error_callback_) {
        nb::gil_scoped_acquire gil;
        error_callback_("H.264/H.265 not supported on this platform");
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
          nb::init<nb::object, nb::object>(), nb::arg("output"),
          nb::arg("error"),
          nb::sig(
              "def __init__(self, output: typing.Callable[[VideoFrame], None], "
              "error: typing.Callable[[str], None], /) -> None"))
      .def("configure", &VideoDecoder::configure, nb::arg("config"),
           nb::sig("def configure(self, config: webcodecs.VideoDecoderConfig, "
                   "/) -> None"))
      .def("decode", &VideoDecoder::decode, nb::arg("chunk"),
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
            if (config_dict.contains("description"))
              config.description =
                  nb::cast<std::string>(config_dict["description"]);
            if (config_dict.contains("hardware_acceleration"))
              config.hardware_acceleration =
                  nb::cast<std::string>(config_dict["hardware_acceleration"]);
            if (config_dict.contains("optimize_for_latency"))
              config.optimize_for_latency =
                  nb::cast<bool>(config_dict["optimize_for_latency"]);

            return VideoDecoder::is_config_supported(config);
          },
          nb::arg("config"),
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
