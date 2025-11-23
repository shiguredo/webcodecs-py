#include "audio_decoder.h"
#include <nanobind/ndarray.h>
#include <cstring>
#include <stdexcept>
#include <vector>
#include "audio_data.h"
#include "encoded_audio_chunk.h"

namespace {
// AAC コーデック文字列かどうかを判定するヘルパー関数
bool is_aac_codec(const std::string& codec) {
  // AAC-LC
  // mp4a.40.2 - MPEG-4 AAC LC
  // mp4a.40.02 - MPEG-4 AAC LC (leading 0 for Aud-OTI compatibility)
  // mp4a.67 - MPEG-2 AAC LC
  // aac - 簡略表記
  return codec == "mp4a.40.2" || codec == "mp4a.40.02" || codec == "mp4a.67" ||
         codec == "aac";
}
}  // namespace

AudioDecoder::AudioDecoder(nb::object output, nb::object error)
    : output_callback_(output),
      error_callback_(error),
      state_(CodecState::UNCONFIGURED),
      frame_count_(0) {
  opus_decoder_ = nullptr;
  flac_decoder_ = nullptr;
  flac_input_position_ = 0;
  flac_current_timestamp_ = 0;
#if defined(__APPLE__)
  aac_converter_ = nullptr;
#endif
  // コールバックフラグを設定
  has_output_callback_ = !output_callback_.is_none();
  has_error_callback_ = !error_callback_.is_none();
  // コンストラクタではコーデックの初期化は行わない
  // configure() で初期化する
}

AudioDecoder::~AudioDecoder() {
  stop_worker();  // ワーカースレッドを停止
  close();
}

void AudioDecoder::configure(nb::dict config_dict) {
  if (state_ == CodecState::CLOSED) {
    throw std::runtime_error("AudioDecoder is closed");
  }

  // dict から AudioDecoderConfig へ変換
  AudioDecoderConfig config;

  // 必須フィールド
  if (!config_dict.contains("codec"))
    throw nb::value_error("codec is required");
  if (!config_dict.contains("sample_rate"))
    throw nb::value_error("sample_rate is required");
  if (!config_dict.contains("number_of_channels"))
    throw nb::value_error("number_of_channels is required");

  config.codec = nb::cast<std::string>(config_dict["codec"]);
  config.sample_rate = nb::cast<uint32_t>(config_dict["sample_rate"]);
  config.number_of_channels =
      nb::cast<uint32_t>(config_dict["number_of_channels"]);

  // オプションフィールド
  if (config_dict.contains("description"))
    config.description = nb::cast<std::string>(config_dict["description"]);

  // AudioDecoderConfig を保存
  config_ = config;

  if (config_.codec == "opus") {
    init_opus_decoder();
  } else if (config_.codec == "flac") {
    init_flac_decoder();
#if defined(__APPLE__)
  } else if (is_aac_codec(config_.codec)) {
    init_aac_decoder();
#endif
  } else {
    throw std::runtime_error("Unsupported codec: " + config_.codec);
  }

  // ワーカースレッドの開始
  if (!worker_thread_.joinable()) {
    start_worker();
  }

  state_ = CodecState::CONFIGURED;
}

void AudioDecoder::decode(const EncodedAudioChunk& chunk) {
  if (state_ != CodecState::CONFIGURED) {
    throw std::runtime_error("AudioDecoder is not configured");
  }

  // タスクを作成してキューに追加
  DecodeTask task;
  task.chunk = chunk;
  task.sequence_number = next_sequence_number_++;

  {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    decode_queue_.push(task);
    pending_tasks_++;
  }

  // ワーカースレッドに通知
  queue_cv_.notify_one();

  // デキューコールバックを呼び出す
  if (has_dequeue_callback_) {
    nb::gil_scoped_acquire gil;
    if (!dequeue_callback_.is_none()) {
      dequeue_callback_();
    }
  }
}

void AudioDecoder::handle_decoded_frame(std::unique_ptr<AudioData> data) {
  // 順序制御された出力処理
  handle_output(current_sequence_, std::move(data));

  // デキューコールバックが設定されている場合は呼び出す
  if (has_dequeue_callback_) {
    nb::gil_scoped_acquire gil;
    if (!dequeue_callback_.is_none()) {
      dequeue_callback_();
    }
  }
}

void AudioDecoder::flush() {
  std::unique_lock<std::mutex> lock(queue_mutex_);
  queue_cv_.wait(
      lock, [this]() { return decode_queue_.empty() && pending_tasks_ == 0; });
}

void AudioDecoder::reset() {
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

  close();
  state_ = CodecState::UNCONFIGURED;
  frame_count_ = 0;

  // ワーカースレッドを再開
  start_worker();
}

void AudioDecoder::close() {
  if (state_ == CodecState::CLOSED) {
    return;
  }

  flush();

  stop_worker();

  if (opus_decoder_) {
    opus_decoder_destroy(opus_decoder_);
    opus_decoder_ = nullptr;
  }

  if (flac_decoder_) {
    FLAC__stream_decoder_finish(flac_decoder_);
    FLAC__stream_decoder_delete(flac_decoder_);
    flac_decoder_ = nullptr;
  }

#if defined(__APPLE__)
  if (aac_converter_) {
    cleanup_aac_decoder();
  }
#endif

  state_ = CodecState::CLOSED;
}

AudioDecoderSupport AudioDecoder::is_config_supported(
    const AudioDecoderConfig& config) {
  bool supported = false;

  if (config.codec == "opus") {
    // サンプルレートの確認 (Opus は 8, 12, 16, 24, 48 kHz をサポート)
    if (config.sample_rate == 8000 || config.sample_rate == 12000 ||
        config.sample_rate == 16000 || config.sample_rate == 24000 ||
        config.sample_rate == 48000) {
      // チャンネル数の確認 (Opus は 1-2 チャンネルをサポート)
      if (config.number_of_channels >= 1 && config.number_of_channels <= 2) {
        supported = true;
      }
    }
  } else if (config.codec == "flac") {
    // FLAC は広範囲のサンプルレートをサポート (1Hz - 655350Hz)
    // 一般的なサンプルレートのみをサポート
    if (config.sample_rate >= 8000 && config.sample_rate <= 192000) {
      // チャンネル数の確認 (FLAC は 1-8 チャンネルをサポート)
      if (config.number_of_channels >= 1 && config.number_of_channels <= 8) {
        supported = true;
      }
    }
#if defined(__APPLE__)
  } else if (is_aac_codec(config.codec)) {
    // AAC-LC は一般的なサンプルレートをサポート
    if (config.sample_rate >= 8000 && config.sample_rate <= 96000) {
      // チャンネル数の確認 (AAC は 1-2 チャンネルをサポート)
      if (config.number_of_channels >= 1 && config.number_of_channels <= 2) {
        supported = true;
      }
    }
#endif
  }

  return AudioDecoderSupport(supported, config);
}

// ワーカースレッドの開始
void AudioDecoder::start_worker() {
  should_stop_ = false;
  worker_thread_ = std::thread([this]() { worker_loop(); });
}

// ワーカースレッドの停止
void AudioDecoder::stop_worker() {
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
void AudioDecoder::worker_loop() {
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
        continue;
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
    queue_cv_.notify_all();
  }
}

// デコードタスクの処理
void AudioDecoder::process_decode_task(const DecodeTask& task) {
  // 現在のシーケンス番号を保存
  current_sequence_ = task.sequence_number;

  if (config_.codec == "opus") {
    decode_frame_opus(*task.chunk);
  } else if (config_.codec == "flac") {
    decode_frame_flac(*task.chunk);
#if defined(__APPLE__)
  } else if (is_aac_codec(config_.codec)) {
    decode_frame_aac(*task.chunk);
#endif
  }
}

// 出力データの順序制御
void AudioDecoder::handle_output(uint64_t sequence,
                                 std::unique_ptr<AudioData> data) {
  std::vector<std::unique_ptr<AudioData>> data_to_output;

  {
    std::lock_guard<std::mutex> lock(output_mutex_);

    // データをバッファに追加
    output_buffer_[sequence] = std::move(data);

    // 順序通りに出力できるデータを収集
    while (output_buffer_.find(next_output_sequence_) != output_buffer_.end()) {
      data_to_output.push_back(
          std::move(output_buffer_[next_output_sequence_]));
      output_buffer_.erase(next_output_sequence_);
      next_output_sequence_++;
    }
  }

  // コールバックを呼び出す（GIL を取得）
  if (has_output_callback_ && !data_to_output.empty()) {
    nb::gil_scoped_acquire gil;
    for (auto& audio_data : data_to_output) {
      if (!output_callback_.is_none()) {
        output_callback_(audio_data.release());
      }
    }
  }
}

void init_audio_decoder(nb::module_& m) {
  nb::class_<AudioDecoder>(m, "AudioDecoder")
      .def(nb::init<nb::object, nb::object>(), nb::arg("output"),
           nb::arg("error"),
           nb::sig(
               "def __init__(self, output: typing.Callable[[AudioData], None], "
               "error: typing.Callable[[str], None], /) -> None"))
      .def("configure", &AudioDecoder::configure, nb::arg("config"),
           nb::sig("def configure(self, config: webcodecs.AudioDecoderConfig, "
                   "/) -> None"))
      .def("decode", &AudioDecoder::decode,
           nb::call_guard<nb::gil_scoped_release>(),
           nb::sig("def decode(self, chunk: EncodedAudioChunk, /) -> None"))
      .def("flush", &AudioDecoder::flush,
           nb::call_guard<nb::gil_scoped_release>(),
           nb::sig("def flush(self, /) -> None"))
      .def("reset", &AudioDecoder::reset, nb::sig("def reset(self, /) -> None"))
      .def("close", &AudioDecoder::close, nb::sig("def close(self, /) -> None"))
      .def_prop_ro("state", &AudioDecoder::state,
                   nb::sig("def state(self, /) -> CodecState"))
      .def_prop_ro("decode_queue_size", &AudioDecoder::decode_queue_size,
                   nb::sig("def decode_queue_size(self, /) -> int"))
      .def_static(
          "is_config_supported",
          [](nb::dict config_dict) {
            // dict から AudioDecoderConfig へ変換
            AudioDecoderConfig config;

            // 必須フィールド
            if (!config_dict.contains("codec"))
              throw nb::value_error("codec is required");
            config.codec = nb::cast<std::string>(config_dict["codec"]);
            if (!config_dict.contains("sample_rate"))
              throw nb::value_error("sample_rate is required");
            config.sample_rate = nb::cast<uint32_t>(config_dict["sample_rate"]);
            if (!config_dict.contains("number_of_channels"))
              throw nb::value_error("number_of_channels is required");
            config.number_of_channels =
                nb::cast<uint32_t>(config_dict["number_of_channels"]);

            // オプションフィールド
            if (config_dict.contains("description"))
              config.description =
                  nb::cast<std::string>(config_dict["description"]);

            return AudioDecoder::is_config_supported(config);
          },
          nb::arg("config"),
          nb::sig("def is_config_supported(config: "
                  "webcodecs.AudioDecoderConfig, /) -> "
                  "webcodecs.AudioDecoderSupport"))
      .def("on_output", &AudioDecoder::on_output,
           nb::sig("def on_output(self, callback: typing.Callable[[AudioData], "
                   "None], /) -> None"))
      .def("on_error", &AudioDecoder::on_error,
           nb::sig("def on_error(self, callback: typing.Callable[[str], None], "
                   "/) -> None"))
      .def("on_dequeue", &AudioDecoder::on_dequeue,
           nb::sig("def on_dequeue(self, callback: typing.Callable[[], None], "
                   "/) -> None"));
}
