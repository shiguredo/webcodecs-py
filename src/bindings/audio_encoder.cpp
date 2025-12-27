#include "audio_encoder.h"
#include <cstring>
#include <stdexcept>
#include <vector>
#include "audio_data.h"
#include "encoded_audio_chunk.h"

using namespace nb::literals;

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

AudioEncoder::AudioEncoder(nb::object output, nb::object error)
    : output_callback_(output),
      error_callback_(error),
      state_(CodecState::UNCONFIGURED),
      frame_count_(0) {
  opus_encoder_ = nullptr;
  flac_encoder_ = nullptr;
  flac_current_timestamp_ = 0;
#if defined(__APPLE__)
  aac_converter_ = nullptr;
  aac_current_timestamp_ = 0;
  aac_samples_encoded_ = 0;
#endif
  // コールバックフラグを設定
  has_output_callback_ = !output_callback_.is_none();
  has_error_callback_ = !error_callback_.is_none();
  // コンストラクタではコーデックの初期化は行わない
  // configure() で初期化する
}

AudioEncoder::~AudioEncoder() {
  stop_worker();  // ワーカースレッドを停止
  close();
}

void AudioEncoder::configure(nb::dict config_dict) {
  if (state_ == CodecState::CLOSED) {
    throw std::runtime_error("AudioEncoder is closed");
  }

  // dict から AudioEncoderConfig へ変換
  AudioEncoderConfig config;

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
  if (config_dict.contains("bitrate"))
    config.bitrate = nb::cast<uint64_t>(config_dict["bitrate"]);
  if (config_dict.contains("bitrate_mode"))
    config.bitrate_mode = nb::cast<BitrateMode>(config_dict["bitrate_mode"]);

  // Opus 固有のオプション
  if (config_dict.contains("opus")) {
    nb::dict opus_dict = nb::cast<nb::dict>(config_dict["opus"]);
    OpusEncoderConfig opus_config;
    if (opus_dict.contains("format"))
      opus_config.format = nb::cast<std::string>(opus_dict["format"]);
    if (opus_dict.contains("signal"))
      opus_config.signal = nb::cast<std::string>(opus_dict["signal"]);
    if (opus_dict.contains("application"))
      opus_config.application = nb::cast<std::string>(opus_dict["application"]);
    if (opus_dict.contains("frame_duration"))
      opus_config.frame_duration =
          nb::cast<uint64_t>(opus_dict["frame_duration"]);
    if (opus_dict.contains("complexity"))
      opus_config.complexity = nb::cast<uint32_t>(opus_dict["complexity"]);
    if (opus_dict.contains("packetlossperc"))
      opus_config.packetlossperc =
          nb::cast<uint32_t>(opus_dict["packetlossperc"]);
    if (opus_dict.contains("useinbandfec"))
      opus_config.useinbandfec = nb::cast<bool>(opus_dict["useinbandfec"]);
    if (opus_dict.contains("usedtx"))
      opus_config.usedtx = nb::cast<bool>(opus_dict["usedtx"]);
    config.opus = opus_config;
  }

  // FLAC 固有のオプション
  if (config_dict.contains("flac")) {
    nb::dict flac_dict = nb::cast<nb::dict>(config_dict["flac"]);
    FlacEncoderConfig flac_config;
    if (flac_dict.contains("block_size"))
      flac_config.block_size = nb::cast<uint32_t>(flac_dict["block_size"]);
    if (flac_dict.contains("compress_level"))
      flac_config.compress_level =
          nb::cast<uint32_t>(flac_dict["compress_level"]);
    config.flac = flac_config;
  }

  // AudioEncoderConfig を保存
  config_ = config;

  // デフォルト値の設定
  if (!config_.bitrate.has_value()) {
    config_.bitrate = 128000;  // デフォルトビットレート
  }

  if (config_.codec == "opus") {
    init_opus_encoder();
  } else if (config_.codec == "flac") {
    init_flac_encoder();
#if defined(__APPLE__)
  } else if (is_aac_codec(config_.codec)) {
    init_aac_encoder();
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

void AudioEncoder::encode(const AudioData& data) {
  if (state_ != CodecState::CONFIGURED) {
    throw std::runtime_error("AudioEncoder is not configured");
  }

  // ワーカースレッドにタスクを追加
  EncodeTask task;
  task.data = std::make_shared<AudioData>(data);  // AudioDataのコピーを作成
  task.sequence_number = next_sequence_number_++;

  {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    encode_queue_.push(task);
    pending_tasks_++;
  }

  // ワーカースレッドに通知
  queue_cv_.notify_one();

  // デキューコールバックを呼び出す
  nb::object dequeue_cb;
  bool has_dequeue;
  {
    nb::ft_lock_guard guard(callback_mutex_);
    dequeue_cb = dequeue_callback_;
    has_dequeue = has_dequeue_callback_;
  }
  if (has_dequeue && !dequeue_cb.is_none()) {
    nb::gil_scoped_acquire gil;
    dequeue_cb();
  }
}

void AudioEncoder::handle_encoded_frame(const uint8_t* data,
                                        size_t size,
                                        int64_t timestamp) {
  bool has_output;
  {
    nb::ft_lock_guard guard(callback_mutex_);
    has_output = has_output_callback_;
  }
  if (has_output) {
    auto chunk = std::make_unique<EncodedAudioChunk>(
        std::vector<uint8_t>(data, data + size), EncodedAudioChunkType::KEY,
        timestamp, 0);
    // 一つの入力から複数のパケットが生成される場合に備えて、
    // 各パケットに一意なシーケンス番号を割り当てる
    uint64_t chunk_sequence = next_chunk_sequence_.fetch_add(1);
    handle_output(chunk_sequence, std::move(chunk));
  }

  nb::object dequeue_cb2;
  bool has_dequeue2;
  {
    nb::ft_lock_guard guard(callback_mutex_);
    dequeue_cb2 = dequeue_callback_;
    has_dequeue2 = has_dequeue_callback_;
  }
  if (has_dequeue2 && !dequeue_cb2.is_none()) {
    nb::gil_scoped_acquire gil;
    dequeue_cb2();
  }
}

void AudioEncoder::flush() {
  // 全てのペンディングタスクが完了するまで待機
  {
    std::unique_lock<std::mutex> lock(queue_mutex_);
    queue_cv_.wait(lock, [this]() {
      return encode_queue_.empty() && pending_tasks_ == 0;
    });
  }

  // FLAC エンコーダーは finish() で残りのデータをフラッシュする必要がある
  if (config_.codec == "flac" && flac_encoder_) {
    finalize_flac_encoder();
    // エンコーダーを再初期化して再利用可能にする
    FLAC__stream_encoder_delete(flac_encoder_);
    flac_encoder_ = nullptr;
    init_flac_encoder();
  }
#if defined(__APPLE__)
  // AAC エンコーダーは残りのデータをフラッシュする必要がある
  if (is_aac_codec(config_.codec) && aac_converter_) {
    finalize_aac_encoder();
    // エンコーダーを再初期化して再利用可能にする
    cleanup_aac_encoder();
    init_aac_encoder();
  }
#endif
  // Opus エンコーダーは明示的なフラッシュが不要
  // フレームを即座に処理する
}

void AudioEncoder::reset() {
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

void AudioEncoder::close() {
  if (state_ == CodecState::CLOSED) {
    return;
  }

  // ワーカースレッドを停止してからリソースを解放
  // stop_worker() は再入可能なので、デストラクタから呼ばれても安全
  if (worker_thread_.joinable()) {
    stop_worker();
  }

  if (opus_encoder_) {
    opus_encoder_destroy(opus_encoder_);
    opus_encoder_ = nullptr;
  }

  if (flac_encoder_) {
    finalize_flac_encoder();
    FLAC__stream_encoder_delete(flac_encoder_);
    flac_encoder_ = nullptr;
  }

#if defined(__APPLE__)
  if (aac_converter_) {
    finalize_aac_encoder();
    cleanup_aac_encoder();
  }
#endif

  state_ = CodecState::CLOSED;
}

AudioEncoderSupport AudioEncoder::is_config_supported(
    const AudioEncoderConfig& config) {
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

  return AudioEncoderSupport(supported, config);
}

// ワーカースレッドの開始
void AudioEncoder::start_worker() {
  should_stop_ = false;
  worker_thread_ = std::thread([this]() { worker_loop(); });
}

// ワーカースレッドの停止
void AudioEncoder::stop_worker() {
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
void AudioEncoder::worker_loop() {
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
    if (task.data) {
      process_encode_task(task);
    }

    // 処理待ちタスク数を減らす
    {
      std::lock_guard<std::mutex> lock(queue_mutex_);
      pending_tasks_--;
    }
    queue_cv_.notify_all();
  }
}

// エンコードタスクの処理
void AudioEncoder::process_encode_task(const EncodeTask& task) {
  // 現在のシーケンス番号を保存
  current_sequence_ = task.sequence_number;

  if (config_.codec == "opus") {
    encode_frame_opus(*task.data);
  } else if (config_.codec == "flac") {
    encode_frame_flac(*task.data);
#if defined(__APPLE__)
  } else if (is_aac_codec(config_.codec)) {
    encode_frame_aac(*task.data);
#endif
  }
}

// 出力チャンクの順序制御
void AudioEncoder::handle_output(uint64_t sequence,
                                 std::unique_ptr<EncodedAudioChunk> chunk) {
  std::vector<std::unique_ptr<EncodedAudioChunk>> chunks_to_output;

  {
    std::lock_guard<std::mutex> lock(output_mutex_);

    // チャンクをバッファに追加
    output_buffer_[sequence] = std::move(chunk);

    // 順序通りに出力できるチャンクを収集
    while (output_buffer_.find(next_output_sequence_) != output_buffer_.end()) {
      chunks_to_output.push_back(
          std::move(output_buffer_[next_output_sequence_]));
      output_buffer_.erase(next_output_sequence_);
      next_output_sequence_++;
    }
  }

  // コールバックを呼び出す（GIL を取得）
  nb::object output_cb;
  bool has_output;
  {
    nb::ft_lock_guard guard(callback_mutex_);
    output_cb = output_callback_;
    has_output = has_output_callback_;
  }
  if (has_output && !chunks_to_output.empty()) {
    nb::gil_scoped_acquire gil;
    for (auto& chunk : chunks_to_output) {
      if (!output_cb.is_none()) {
        output_cb(chunk.release());
      }
    }
  }
}

void init_audio_encoder(nb::module_& m) {
  nb::class_<AudioEncoder>(m, "AudioEncoder")
      .def(nb::init<nb::object, nb::object>(), "output"_a, "error"_a,
           nb::sig("def __init__(self, output: "
                   "typing.Callable[[EncodedAudioChunk], None], "
                   "error: typing.Callable[[str], None], /) -> None"))
      .def("configure", &AudioEncoder::configure, "config"_a,
           nb::sig("def configure(self, config: webcodecs.AudioEncoderConfig, "
                   "/) -> None"))
      .def("encode", &AudioEncoder::encode,
           nb::call_guard<nb::gil_scoped_release>(),
           nb::sig("def encode(self, data: AudioData, /) -> None"))
      .def("flush", &AudioEncoder::flush,
           nb::call_guard<nb::gil_scoped_release>(),
           nb::sig("def flush(self, /) -> None"))
      .def("reset", &AudioEncoder::reset, nb::sig("def reset(self, /) -> None"))
      .def("close", &AudioEncoder::close, nb::sig("def close(self, /) -> None"))
      .def_prop_ro("state", &AudioEncoder::state,
                   nb::sig("def state(self, /) -> CodecState"))
      .def_prop_ro("encode_queue_size", &AudioEncoder::encode_queue_size,
                   nb::sig("def encode_queue_size(self, /) -> int"))
      .def_static(
          "is_config_supported",
          [](nb::dict config_dict) {
            // dict から AudioEncoderConfig へ変換
            AudioEncoderConfig config;

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
            if (config_dict.contains("bitrate"))
              config.bitrate = nb::cast<uint64_t>(config_dict["bitrate"]);
            if (config_dict.contains("bitrate_mode"))
              config.bitrate_mode =
                  nb::cast<BitrateMode>(config_dict["bitrate_mode"]);

            return AudioEncoder::is_config_supported(config);
          },
          "config"_a,
          nb::sig("def is_config_supported(config: "
                  "webcodecs.AudioEncoderConfig, /) -> "
                  "webcodecs.AudioEncoderSupport"))
      .def("on_output", &AudioEncoder::on_output,
           nb::sig("def on_output(self, callback: "
                   "typing.Callable[[EncodedAudioChunk], None], /) -> None"))
      .def("on_error", &AudioEncoder::on_error,
           nb::sig("def on_error(self, callback: typing.Callable[[str], None], "
                   "/) -> None"))
      .def("on_dequeue", &AudioEncoder::on_dequeue,
           nb::sig("def on_dequeue(self, callback: typing.Callable[[], None], "
                   "/) -> None"));
}
