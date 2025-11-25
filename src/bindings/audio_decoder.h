#pragma once

#include <nanobind/nanobind.h>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <thread>
#include <vector>

#include <FLAC/stream_decoder.h>
#include <opus.h>
#include "webcodecs_types.h"

#if defined(__APPLE__)
#include <AudioToolbox/AudioToolbox.h>
#endif

namespace nb = nanobind;

class AudioData;
#include "encoded_audio_chunk.h"

class AudioDecoder {
 public:
  // デコードタスクを表す構造体
  struct DecodeTask {
    std::optional<EncodedAudioChunk> chunk;  // デコード対象のチャンク
    uint64_t sequence_number;                // タスクの順序を保持
  };

  // コールバックを直接受け取るコンストラクタ
  AudioDecoder(nb::object output, nb::object error);
  ~AudioDecoder();

  // dict を受け取る configure
  void configure(nb::dict config);
  void decode(const EncodedAudioChunk& chunk);
  void flush();
  void reset();
  void close();

  CodecState state() const { return state_; }
  uint32_t decode_queue_size() const { return pending_tasks_.load(); }

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
  static AudioDecoderSupport is_config_supported(
      const AudioDecoderConfig& config);

 public:
#if defined(__APPLE__)
  // AAC デコーダーのコールバック用
  OSStatus aac_provide_input_data(
      UInt32* number_data_packets,
      AudioBufferList* data,
      AudioStreamPacketDescription** data_packet_description);
#endif

 private:
  OpusDecoder* opus_decoder_;
  FLAC__StreamDecoder* flac_decoder_;

#if defined(__APPLE__)
  AudioConverterRef aac_converter_ = nullptr;
  std::vector<uint8_t> aac_input_buffer_;
  AudioStreamPacketDescription aac_packet_description_;
#endif

  AudioDecoderConfig config_;  // 内部で保持する設定
  CodecState state_;
  int64_t frame_count_;

  nb::object output_callback_;
  nb::object error_callback_;
  nb::object dequeue_callback_;
  bool has_output_callback_{false};
  bool has_error_callback_{false};
  bool has_dequeue_callback_{false};

  // 並列処理のためのメンバー
  std::queue<DecodeTask> decode_queue_;            // デコード待ちタスクのキュー
  std::atomic<uint32_t> pending_tasks_{0};         // 処理待ちタスク数
  std::atomic<uint64_t> next_sequence_number_{0};  // タスクのシーケンス番号
  std::mutex queue_mutex_;                         // キューアクセスの同期
  std::condition_variable queue_cv_;               // キューの待機/通知
  std::thread worker_thread_;                      // ワーカースレッド
  std::atomic<bool> should_stop_{false};           // スレッド終了フラグ
  uint64_t current_sequence_{0};                   // 現在処理中のシーケンス番号

  // 出力順序制御のためのメンバー
  std::map<uint64_t, std::unique_ptr<AudioData>>
      output_buffer_;                 // 順序待ちバッファ
  uint64_t next_output_sequence_{0};  // 次に出力すべきシーケンス番号
  std::mutex output_mutex_;           // 出力バッファの同期

  void init_opus_decoder();
  void decode_frame_opus(const EncodedAudioChunk& chunk);

  void init_flac_decoder();
  void decode_frame_flac(const EncodedAudioChunk& chunk);

#if defined(__APPLE__)
  void init_aac_decoder();
  void decode_frame_aac(const EncodedAudioChunk& chunk);
  void cleanup_aac_decoder();
#endif
  // FLAC コールバック用の静的メソッド
  static FLAC__StreamDecoderReadStatus flac_read_callback(
      const FLAC__StreamDecoder* decoder,
      FLAC__byte buffer[],
      size_t* bytes,
      void* client_data);
  static FLAC__StreamDecoderWriteStatus flac_write_callback(
      const FLAC__StreamDecoder* decoder,
      const FLAC__Frame* frame,
      const FLAC__int32* const buffer[],
      void* client_data);
  static void flac_error_callback(const FLAC__StreamDecoder* decoder,
                                  FLAC__StreamDecoderErrorStatus status,
                                  void* client_data);

  // FLAC デコード用バッファ
  std::vector<uint8_t> flac_input_buffer_;
  size_t flac_input_position_;
  int64_t flac_current_timestamp_;
  bool flac_stream_started_;  // ストリーミング開始フラグ
  std::vector<std::unique_ptr<AudioData>> flac_decoded_frames_;

  void handle_decoded_frame(std::unique_ptr<AudioData> data);

  // 並列処理のためのメソッド
  void worker_loop();  // ワーカースレッドのメインループ
  void process_decode_task(const DecodeTask& task);  // タスクの処理
  void handle_output(uint64_t sequence,
                     std::unique_ptr<AudioData> data);  // 出力処理
  void start_worker();  // ワーカースレッドの開始
  void stop_worker();   // ワーカースレッドの停止
};

void init_audio_decoder(nb::module_& m);
