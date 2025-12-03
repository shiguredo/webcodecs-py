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

#include <aom/aom_codec.h>
#include <aom/aom_encoder.h>
#include <aom/aomcx.h>
#include "codec_parser.h"
#include "webcodecs_types.h"

#include "video_frame.h"

#if defined(__APPLE__)
// CFStringRef の前方宣言
typedef const struct __CFString* CFStringRef;
#endif

#if defined(NVIDIA_CUDA_TOOLKIT)
#include <cuda.h>
#include <nvEncodeAPI.h>
#endif

namespace nb = nanobind;

class EncodedVideoChunk;

enum class VideoEncoderCodec { AV1 };

class VideoEncoder {
 public:
  // AV1 エンコードオプション
  struct AV1EncodeOptions {
    std::optional<uint16_t> quantizer;  // 0-63 の範囲
  };

  // AVC エンコードオプション
  struct AVCEncodeOptions {
    std::optional<uint16_t> quantizer;  // 0-51 の範囲
  };

  // HEVC エンコードオプション
  struct HEVCEncodeOptions {
    std::optional<uint16_t> quantizer;  // 0-51 の範囲
  };

  // エンコードオプション
  struct EncodeOptions {
    bool keyframe = false;
    std::optional<AV1EncodeOptions> av1;
    std::optional<AVCEncodeOptions> avc;
    std::optional<HEVCEncodeOptions> hevc;
  };

  // エンコードタスクを表す構造体
  struct EncodeTask {
    std::shared_ptr<VideoFrame> frame;  // フレームの共有所有権
    bool keyframe;
    std::optional<uint16_t> av1_quantizer;   // AV1 の quantizer オプション
    std::optional<uint16_t> avc_quantizer;   // AVC の quantizer オプション
    std::optional<uint16_t> hevc_quantizer;  // HEVC の quantizer オプション
    uint64_t sequence_number;                // タスクの順序を保持
  };

  // コールバックを直接受け取るコンストラクタ
  VideoEncoder(nb::object output, nb::object error);
  ~VideoEncoder();

  // dict を受け取る configure
  void configure(nb::dict config);
  void encode(const VideoFrame& frame, bool keyframe = false);
  void encode(const VideoFrame& frame, const EncodeOptions& options);
  void flush();
  void reset();
  void close();

  CodecState state() const { return state_; }
  uint32_t encode_queue_size() const { return pending_tasks_.load(); }

  void on_output(nb::object callback) { output_callback_ = callback; }
  void on_error(nb::object callback) { error_callback_ = callback; }
  void on_dequeue(nb::object callback) { dequeue_callback_ = callback; }

  // Static method to check if configuration is supported
  static VideoEncoderSupport is_config_supported(
      const VideoEncoderConfig& config);

  // VideoToolbox コールバック用に public にする
  // metadata はオプショナルで、キーフレーム時に decoderConfig を含む
  void handle_output(
      uint64_t sequence,
      std::shared_ptr<EncodedVideoChunk> chunk,
      std::optional<EncodedVideoChunkMetadata> metadata = std::nullopt);

 private:
  aom_codec_ctx_t* aom_encoder_;
  aom_codec_enc_cfg_t aom_config_;
  const aom_codec_iface_t* aom_iface_;

  VideoEncoderConfig config_;     // 内部で保持する設定
  CodecParameters codec_params_;  // パースしたコーデックパラメータ
  CodecState state_;
  std::atomic<int64_t> frame_count_{0};

  nb::object output_callback_;
  nb::object error_callback_;
  nb::object dequeue_callback_;

  // 並列処理のためのメンバー
  std::queue<EncodeTask> encode_queue_;     // エンコード待ちタスクのキュー
  std::atomic<uint32_t> pending_tasks_{0};  // 処理待ちタスク数
  std::atomic<uint64_t> next_sequence_number_{0};  // タスクのシーケンス番号
  std::mutex queue_mutex_;                         // キューアクセスの同期
  std::condition_variable queue_cv_;               // キューの待機/通知
  // flush() が処理完了を待機できるように queue_cv_ を流用して通知する
  std::thread worker_thread_;             // ワーカースレッド
  std::atomic<bool> should_stop_{false};  // スレッド終了フラグ
  uint64_t current_sequence_{0};          // 現在処理中のシーケンス番号

  // 出力エントリ (chunk と metadata のペア)
  struct OutputEntry {
    std::shared_ptr<EncodedVideoChunk> chunk;
    std::optional<EncodedVideoChunkMetadata> metadata;
  };

  // 出力順序制御のためのメンバー
  std::map<uint64_t, OutputEntry> output_buffer_;  // 順序待ちバッファ
  uint64_t next_output_sequence_{0};  // 次に出力すべきシーケンス番号
  std::mutex output_mutex_;           // 出力バッファの同期

  void handle_encoded_frame(const uint8_t* data,
                            size_t size,
                            int64_t timestamp,
                            bool keyframe);

  void init_aom_encoder();
  void cleanup_aom_encoder();
  void encode_frame_aom(const VideoFrame& frame,
                        bool keyframe,
                        std::optional<uint16_t> quantizer = std::nullopt);

  // ハードウェアアクセラレーションバックエンド
  void init_videotoolbox_encoder();
  void encode_frame_videotoolbox(
      const VideoFrame& frame,
      bool keyframe,
      std::optional<uint16_t> quantizer = std::nullopt);
  void flush_videotoolbox_encoder();
  void cleanup_videotoolbox_encoder();

#if defined(__APPLE__)
  // codec_params_ から ProfileLevel を取得するヘルパーメソッド
  CFStringRef get_h264_profile_level();
  CFStringRef get_hevc_profile_level();
#endif

  // 並列処理のためのメソッド
  void worker_loop();  // ワーカースレッドのメインループ
  void process_encode_task(const EncodeTask& task);  // タスクの処理
  void start_worker();                               // ワーカースレッドの開始
  void stop_worker();                                // ワーカースレッドの停止

  // コーデック判定ヘルパーメソッド
  bool is_av1_codec() const;
  bool is_avc_codec() const;
  bool is_hevc_codec() const;
  bool uses_videotoolbox() const;
  bool uses_nvidia_video_codec() const;

  // プラットフォームのハードウェアアクセラレーション用の不透明ハンドル (Apple では VideoToolbox で使用)
  void* vt_session_ = nullptr;

  // libaom の初期化とエンコードを直列化するためのミューテックス
  std::mutex aom_mutex_;

#if defined(NVIDIA_CUDA_TOOLKIT)
  // NVIDIA Video Codec SDK (NVENC) 関連のメンバー
  void* nvenc_encoder_ = nullptr;
  void* nvenc_cuda_context_ = nullptr;
  NV_ENCODE_API_FUNCTION_LIST* nvenc_api_ = nullptr;
  void* nvenc_input_buffer_ = nullptr;
  void* nvenc_output_buffer_ = nullptr;

  // NVENC 関連のメソッド
  void init_nvenc_encoder();
  void encode_frame_nvenc(const VideoFrame& frame,
                          bool keyframe,
                          std::optional<uint16_t> quantizer = std::nullopt);
  void flush_nvenc_encoder();
  void cleanup_nvenc_encoder();
#endif
};

void init_video_encoder(nb::module_& m);
