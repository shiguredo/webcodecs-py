#pragma once

#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h>
#include <cstdint>
#include <memory>
#include <vector>

namespace nb = nanobind;

enum class AudioSampleFormat {
  U8,          // 符号なし 8 ビット
  S16,         // 符号付き 16 ビット
  S32,         // 符号付き 32 ビット
  F32,         // 32 ビット浮動小数点
  U8_PLANAR,   // 符号なし 8 ビットプレーナー
  S16_PLANAR,  // 符号付き 16 ビットプレーナー
  S32_PLANAR,  // 符号付き 32 ビットプレーナー
  F32_PLANAR,  // 32 ビット浮動小数点プレーナー
};

class AudioData {
 public:
  // WebCodecs API 準拠コンストラクタ (AudioDataInit dict を受け取る)
  explicit AudioData(nb::dict init);
  ~AudioData();

  // 内部使用のためのファクトリーメソッド (空のバッファを確保)
  static std::unique_ptr<AudioData> create_with_buffer(
      uint32_t number_of_channels,
      uint32_t sample_rate,
      uint32_t number_of_frames,
      AudioSampleFormat format,
      int64_t timestamp);

  // Properties
  uint32_t number_of_channels() const { return number_of_channels_; }
  uint32_t sample_rate() const { return sample_rate_; }
  uint32_t number_of_frames() const { return number_of_frames_; }
  AudioSampleFormat format() const { return format_; }
  int64_t timestamp() const { return timestamp_; }
  uint64_t duration() const { return duration_; }

  // Data access
  nb::ndarray<nb::numpy> get_channel_data(uint32_t channel) const;
  const uint8_t* data_ptr() const;
  uint8_t* mutable_data();

  // 内部用フォーマット変換（エンコーダ等から使用）
  std::unique_ptr<AudioData> convert_format(
      AudioSampleFormat target_format) const;

  // Numpy interop
  // copy_to(): destination に書き込む（WebCodecs API 準拠）
  // options: AudioDataCopyToOptions (plane_index, frame_offset, frame_count, format)
  void copy_to(nb::ndarray<nb::numpy> destination, nb::dict options);

  // WebCodecs-like methods
  // allocation_size(): options に基づいて必要なバッファサイズを返す
  // options: AudioDataCopyToOptions (plane_index, frame_offset, frame_count, format)
  size_t allocation_size(nb::dict options) const;
  void close();
  bool is_closed() const { return closed_; }
  std::unique_ptr<AudioData> clone() const;

  // AudioDataCopyToOptions のパース結果
  struct CopyToParams {
    uint32_t plane_index;
    uint32_t frame_offset;
    uint32_t frame_count;
    // format が指定された場合、変換先フォーマット
    // 指定されていない場合は has_format = false
    bool has_format;
    AudioSampleFormat target_format;
  };

 private:
  // 内部使用のためのプライベートコンストラクタ (clone, convert_to)
  AudioData(uint32_t number_of_channels,
            uint32_t sample_rate,
            uint32_t number_of_frames,
            AudioSampleFormat format,
            int64_t timestamp,
            uint64_t duration);

  // AudioDataCopyToOptions をパースするヘルパー
  CopyToParams parse_copy_to_options(nb::dict options) const;

  uint32_t number_of_channels_;
  uint32_t sample_rate_;
  uint32_t number_of_frames_;
  AudioSampleFormat format_;
  int64_t timestamp_;
  uint64_t duration_;
  bool closed_;

  std::vector<uint8_t> data_;

  size_t get_sample_size() const;
  size_t get_frame_size() const;
  bool is_planar() const;
};
