#pragma once

#include <nanobind/nanobind.h>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

#include <FLAC/stream_encoder.h>
#include <opus.h>
#include "webcodecs_types.h"

namespace nb = nanobind;

class AudioData;
class EncodedAudioChunk;

class AudioEncoder {
 public:
  // エンコードタスクを表す構造体
  struct EncodeTask {
    std::shared_ptr<AudioData> data;  // AudioDataの共有所有権
    uint64_t sequence_number;         // タスクの順序を保持
  };

  // コールバックを直接受け取るコンストラクタ
  AudioEncoder(nb::object output, nb::object error);
  ~AudioEncoder();

  // dict を受け取る configure
  void configure(nb::dict config);
  void encode(const AudioData& data);
  void flush();
  void reset();
  void close();

  CodecState state() const { return state_; }
  uint32_t encode_queue_size() const { return pending_tasks_.load(); }

  void on_output(nb::object callback) {
    output_callback_ = callback;
    has_output_callback_ = !callback.is_none();
  }
  void on_error(nb::object callback) {
    error_callback_ = callback;
    has_error_callback_ = !callback.is_none();
  }
  void on_dequeue(nb::object callback) {
    dequeue_callback_ = callback;
    has_dequeue_callback_ = !callback.is_none();
  }

  // Static method to check if configuration is supported
  static AudioEncoderSupport is_config_supported(
      const AudioEncoderConfig& config);

 private:
  OpusEncoder* opus_encoder_;
  FLAC__StreamEncoder* flac_encoder_;

  AudioEncoderConfig config_;  // 内部で保持する設定
  CodecState state_;
  int64_t frame_count_;

  nb::object output_callback_;
  nb::object error_callback_;
  nb::object dequeue_callback_;
  bool has_output_callback_{false};
  bool has_error_callback_{false};
  bool has_dequeue_callback_{false};

  // 並列処理のためのメンバー
  std::queue<EncodeTask> encode_queue_;     // エンコード待ちタスクのキュー
  std::atomic<uint32_t> pending_tasks_{0};  // 処理待ちタスク数
  std::atomic<uint64_t> next_sequence_number_{0};  // タスクのシーケンス番号
  std::mutex queue_mutex_;                         // キューアクセスの同期
  std::condition_variable queue_cv_;               // キューの待機/通知
  std::thread worker_thread_;                      // ワーカースレッド
  std::atomic<bool> should_stop_{false};           // スレッド終了フラグ
  uint64_t current_sequence_{0};                   // 現在処理中のシーケンス番号

  // 出力順序制御のためのメンバー
  std::map<uint64_t, std::unique_ptr<EncodedAudioChunk>>
      output_buffer_;  // 順序待ちバッファ
  std::atomic<uint64_t> next_chunk_sequence_{
      0};                             // 次に割り当てるチャンクのシーケンス番号
  uint64_t next_output_sequence_{0};  // 次に出力すべきシーケンス番号
  std::mutex output_mutex_;           // 出力バッファの同期

  void init_opus_encoder();
  void encode_frame_opus(const AudioData& data);

  void init_flac_encoder();
  void encode_frame_flac(const AudioData& data);
  void finalize_flac_encoder();
  // FLAC コールバック用の静的メソッド
  static FLAC__StreamEncoderWriteStatus flac_write_callback(
      const FLAC__StreamEncoder* encoder,
      const FLAC__byte buffer[],
      size_t bytes,
      uint32_t samples,
      uint32_t current_frame,
      void* client_data);

  // FLAC エンコード用バッファ
  std::vector<uint8_t> flac_output_buffer_;
  int64_t flac_current_timestamp_;

  void handle_encoded_frame(const uint8_t* data,
                            size_t size,
                            int64_t timestamp);

  // 並列処理のためのメソッド
  void worker_loop();  // ワーカースレッドのメインループ
  void process_encode_task(const EncodeTask& task);  // タスクの処理
  void handle_output(uint64_t sequence,
                     std::unique_ptr<EncodedAudioChunk> chunk);  // 出力処理
  void start_worker();  // ワーカースレッドの開始
  void stop_worker();   // ワーカースレッドの停止
};

void init_audio_encoder(nb::module_& m);
