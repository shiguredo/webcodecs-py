#pragma once

#include <nanobind/nanobind.h>
#include <atomic>
#include <condition_variable>
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

#if defined(USE_NVIDIA_CUDA_TOOLKIT)
#include <cuda.h>
#include <cuviddec.h>
#include <nvcuvid.h>
#endif

#if defined(__APPLE__) || defined(__linux__)
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
  void on_output(nb::object callback) {
    nb::ft_lock_guard guard(callback_mutex_);
    output_callback_ = callback;
    has_output_callback_ = !callback.is_none();
  }
  void on_error(nb::object callback) {
    nb::ft_lock_guard guard(callback_mutex_);
    error_callback_ = callback;
    has_error_callback_ = !callback.is_none();
  }
  void on_dequeue(nb::object callback) {
    nb::ft_lock_guard guard(callback_mutex_);
    dequeue_callback_ = callback;
    has_dequeue_callback_ = !callback.is_none();
  }

  // VideoToolbox コールバック用に public にする
  void handle_output(uint64_t sequence,
                     std::unique_ptr<VideoFrame> frame);  // 出力処理

 private:
  nb::object output_callback_;
  nb::object error_callback_;
  nb::object dequeue_callback_;
  nb::ft_mutex callback_mutex_;  // Free-Threading 用コールバック保護
  bool has_output_callback_{false};
  bool has_error_callback_{false};
  bool has_dequeue_callback_{false};
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

#if defined(__APPLE__) || defined(__linux__)
  // libvpx デコーダー
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

  // NVIDIA Video Codec SDK を使用するかどうかを判定
  bool uses_nvidia_video_codec() const;

  // Apple Video Toolbox を使用するかどうかを判定
  bool uses_apple_video_toolbox() const;

#if defined(USE_NVIDIA_CUDA_TOOLKIT)
  // NVIDIA Video Codec SDK (NVDEC) 関連のメンバー
  void* nvdec_decoder_ = nullptr;
  void* nvdec_cuda_context_ = nullptr;
  void* nvdec_video_parser_ = nullptr;
  void* nvdec_video_source_ = nullptr;

  // NVDEC デコード用のフレームキュー
  std::vector<void*> nvdec_frame_queue_;
  uint32_t nvdec_decode_surface_count_ = 0;

  // NVDEC 関連のメソッド
  void init_nvdec_decoder();
  bool decode_nvdec(const EncodedVideoChunk& chunk);
  void flush_nvdec();
  void cleanup_nvdec_decoder();

  // NVDEC コールバック用のメンバー関数
  static int handle_video_sequence(void* user_data, void* video_format);
  static int handle_decode_picture(void* user_data, void* pic_params);
  static int handle_display_picture(void* user_data, void* disp_info);
#endif

#if defined(__linux__)
  // Intel VPL 関連のメンバー
  void* vpl_loader_ = nullptr;
  void* vpl_session_ = nullptr;
  std::vector<uint8_t> vpl_bitstream_buffer_;
  std::vector<uint8_t> vpl_surface_buffer_;
  void* vpl_bitstream_ = nullptr;
  void* vpl_surface_pool_ = nullptr;
  bool vpl_initialized_ = false;

  // Intel VPL 関連のメソッド
  void init_intel_vpl_decoder();
  bool decode_intel_vpl(const EncodedVideoChunk& chunk);
  void flush_intel_vpl();
  void cleanup_intel_vpl_decoder();
#endif

  // Intel VPL を使用するかどうかを判定
  bool uses_intel_vpl() const;
};
