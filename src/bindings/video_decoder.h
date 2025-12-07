#pragma once

#include <nanobind/nanobind.h>
#include <nanobind/stl/function.h>
#include <nanobind/stl/unique_ptr.h>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <thread>
#include <vector>
#include "codec_parser.h"
#include "encoded_video_chunk.h"
#include "video_frame.h"
#include "webcodecs_types.h"

#if defined(__APPLE__)
#include <vpx/vp8dx.h>
#include <vpx/vpx_codec.h>
#include <vpx/vpx_decoder.h>
#endif

namespace nb = nanobind;

enum class VideoCodec {
  AV1,
  H264,
  H265,
  VP8,
  VP9,
};

class VideoDecoder {
 public:
  using OutputCallback = std::function<void(std::unique_ptr<VideoFrame>)>;
  using ErrorCallback = std::function<void(const std::string&)>;

  // デコードタスクを表す構造体
  struct DecodeTask {
    std::optional<EncodedVideoChunk> chunk;
    uint64_t sequence_number;  // タスクの順序を保持
  };

  // コールバックを直接受け取るコンストラクタ
  VideoDecoder(nb::object output, nb::object error);
  ~VideoDecoder();

  // WebCodecs-like API
  void configure(nb::dict config);
  void decode(const EncodedVideoChunk& chunk);
  void flush();
  void reset();
  void close();

  // Properties
  CodecState state() const { return state_; }
  uint32_t decode_queue_size() const { return pending_tasks_.load(); }

  // Static method to check if configuration is supported
  static VideoDecoderSupport is_config_supported(
      const VideoDecoderConfig& config);

  // Callback setters
  void on_output(OutputCallback callback) { output_callback_ = callback; }
  void on_error(ErrorCallback callback) { error_callback_ = callback; }
  void on_dequeue(std::function<void()> callback) {
    dequeue_callback_ = callback;
  }

  // VideoToolbox コールバック用に public にする
  void handle_output(uint64_t sequence,
                     std::unique_ptr<VideoFrame> frame);  // 出力処理

 private:
  OutputCallback output_callback_;
  ErrorCallback error_callback_;
  std::function<void()> dequeue_callback_;
  CodecState state_;
  VideoDecoderConfig config_;     // 内部で保持する設定
  CodecParameters codec_params_;  // パースしたコーデックパラメータ

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
  std::map<uint64_t, std::unique_ptr<VideoFrame>>
      output_buffer_;                 // 順序待ちバッファ
  uint64_t next_output_sequence_{0};  // 次に出力すべきシーケンス番号
  std::mutex output_mutex_;           // 出力バッファの同期

  // コーデック固有のデコーダーコンテキスト
  void* decoder_context_;

  // プラットフォームのハードウェアアクセラレーション用の不透明ハンドル (Apple では VideoToolbox で使用)
  void* vt_session_ = nullptr;
  // キーフレームから作成した CMVideoFormatDescriptionRef をキャッシュ
  void* vt_format_desc_ = nullptr;

  // 内部メソッド
  void init_decoder();
  void cleanup_decoder();
  bool decode_internal(const EncodedVideoChunk& chunk);
  void init_dav1d_decoder();
  void cleanup_dav1d_decoder();
  bool decode_dav1d(const EncodedVideoChunk& chunk);
  void flush_dav1d();  // AV1 フラッシュ処理

  // ハードウェアアクセラレーションバックエンド
  void init_videotoolbox_decoder();
  void cleanup_videotoolbox_decoder();
  bool decode_videotoolbox(const EncodedVideoChunk& chunk);
  void flush_videotoolbox();

#if defined(__APPLE__)
  // libvpx デコーダー (macOS のみ)
  void init_vpx_decoder();
  void cleanup_vpx_decoder();
  bool decode_vpx(const EncodedVideoChunk& chunk);
  void flush_vpx();

  void* vpx_decoder_ = nullptr;
  std::mutex vpx_mutex_;
#endif

  // 並列処理のためのメソッド
  void worker_loop();  // ワーカースレッドのメインループ
  void process_decode_task(const DecodeTask& task);  // タスクの処理
  void start_worker();                               // ワーカースレッドの開始
  void stop_worker();                                // ワーカースレッドの停止

  // ユーティリティーメソッド
  static VideoCodec string_to_codec(const std::string& codec);
};
