#pragma once

#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>
#include "webcodecs_types.h"

namespace nb = nanobind;

enum class VideoPixelFormat {
  I420,  // YUV 4:2:0
  I422,  // YUV 4:2:2
  I444,  // YUV 4:4:4
  NV12,  // YUV 4:2:0 with interleaved UV
  RGBA,  // RGBA 8-bit per channel
  BGRA,  // BGRA 8-bit per channel
  RGB,   // RGB 8-bit per channel
  BGR,   // BGR 8-bit per channel
};

class VideoFrame {
 public:
  // WebCodecs API 準拠コンストラクタ (dict を受け取る)
  VideoFrame(nb::ndarray<nb::numpy> data, nb::dict init);

  // native_buffer (PyCapsule) を受け取るコンストラクタ
  VideoFrame(nb::capsule native_buffer, nb::dict init);

  // 内部用コンストラクタ（clone, convert_format, デコーダーで使用）
  // Python バインディングには公開しない
  VideoFrame(uint32_t width,
             uint32_t height,
             VideoPixelFormat format,
             int64_t timestamp = 0);

  // コピーコンストラクタとコピー代入演算子
  VideoFrame(const VideoFrame& other);
  VideoFrame& operator=(const VideoFrame& other);

  ~VideoFrame();

  // Properties
  uint32_t width() const { return width_; }
  uint32_t height() const { return height_; }
  VideoPixelFormat format() const { return format_; }
  int64_t timestamp() const { return timestamp_; }
  uint64_t duration() const { return duration_; }
  void set_duration(uint64_t duration) { duration_ = duration; }

  // WebCodecs API properties
  uint32_t coded_width() const { return coded_width_; }
  uint32_t coded_height() const { return coded_height_; }
  std::optional<DOMRect> visible_rect() const { return visible_rect_; }
  uint32_t display_width() const { return display_width_; }
  uint32_t display_height() const { return display_height_; }
  std::optional<VideoColorSpace> color_space() const { return color_space_; }
  std::optional<std::vector<PlaneLayout>> layout() const { return layout_; }
  uint32_t rotation() const { return rotation_; }
  bool flip() const { return flip_; }
  nb::dict metadata() const;

  // ネイティブバッファ (PyCapsule) のアクセサ
  nb::object native_buffer() const {
    if (native_buffer_.is_valid()) {
      return native_buffer_;
    }
    return nb::none();
  }
  void set_native_buffer(nb::object buffer) { native_buffer_ = buffer; }
  bool has_native_buffer() const {
    return native_buffer_.is_valid() && !native_buffer_.is_none();
  }
  void* native_buffer_ptr() const;

  // データの存在チェック (native_buffer のみの場合は false)
  bool has_data() const { return !data_.empty(); }

  // Data access
  nb::ndarray<nb::numpy> plane(int plane_index) const;
  nb::ndarray<nb::numpy> get_writable_plane(
      int plane_index);  // 書き込み可能なビュー（内部用）
  nb::ndarray<nb::numpy> get_plane_data(int plane_index) const;
  const uint8_t* plane_ptr(int plane_index) const;
  uint8_t* mutable_plane_ptr(int plane_index);
  uint8_t* mutable_data();

  // 内部用フォーマット変換（copy_to 等から使用）
  std::unique_ptr<VideoFrame> convert_format(
      VideoPixelFormat target_format) const;

  // VideoFrameCopyToOptions のパース結果
  struct CopyToOptions {
    std::optional<DOMRect> rect;                     // コピーする領域
    std::optional<std::vector<PlaneLayout>> layout;  // 出力先レイアウト
    std::optional<VideoPixelFormat> format;          // 出力フォーマット
  };

  // Numpy interop
  // allocation_size(): copy_to() に必要なバッファサイズを返す（WebCodecs API 準拠）
  size_t allocation_size() const;
  size_t allocation_size(nb::dict options) const;

  // copy_to(): destination に書き込み、PlaneLayout のリストを返す（WebCodecs API 準拠）
  std::vector<PlaneLayout> copy_to(nb::ndarray<nb::numpy> destination);
  std::vector<PlaneLayout> copy_to(nb::ndarray<nb::numpy> destination,
                                   nb::dict options);

  // planes(): ゼロコピービューを返す（独自拡張）
  nb::tuple planes();

  // WebCodecs-like methods
  void close();
  bool is_closed() const { return closed_; }
  std::unique_ptr<VideoFrame> clone() const;

  // エンコーダー専用のコピーメソッド（内部使用）
  std::unique_ptr<VideoFrame> create_encoder_copy() const;

 private:
  uint32_t width_;
  uint32_t height_;
  VideoPixelFormat format_;
  int64_t timestamp_;
  uint64_t duration_;
  bool closed_;

  // WebCodecs API プロパティ
  uint32_t coded_width_;
  uint32_t coded_height_;
  std::optional<DOMRect> visible_rect_;
  uint32_t display_width_;
  uint32_t display_height_;
  std::optional<VideoColorSpace> color_space_;
  std::optional<std::vector<PlaneLayout>> layout_;
  uint32_t rotation_;
  bool flip_;
  std::optional<nb::dict> metadata_;

  // ネイティブバッファ (PyCapsule)
  // macOS: CVPixelBufferRef を保持
  nb::object native_buffer_;

  // データストレージ（部分的ゼロコピーのため std::vector を使用）
  std::vector<uint8_t> data_;

  std::vector<size_t> plane_offsets_;
  std::vector<size_t> plane_sizes_;

  void calculate_plane_info();
  size_t get_frame_size() const;
  VideoPixelFormat string_to_format(const std::string& format_str) const;

  // init_dict をパースして共通プロパティを初期化するヘルパー
  void init_from_dict(nb::dict init_dict);

  // VideoFrameCopyToOptions をパースするヘルパー
  CopyToOptions parse_copy_to_options(nb::dict options) const;

  // rect を適用した領域のサイズを計算
  size_t calculate_size_for_rect(const DOMRect& rect,
                                 VideoPixelFormat fmt) const;
};
