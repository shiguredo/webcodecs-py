#pragma once

#include <nanobind/nanobind.h>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include <ImageIO/ImageIO.h>
#endif

namespace nb = nanobind;

class VideoFrame;

// ImageTrack クラス
class ImageTrack {
 public:
  ImageTrack(bool animated,
             uint32_t frame_count,
             float repetition_count,
             bool selected = true);

  bool animated() const { return animated_; }
  uint32_t frame_count() const { return frame_count_; }
  float repetition_count() const { return repetition_count_; }
  bool selected() const { return selected_; }
  void set_selected(bool selected) { selected_ = selected; }

 private:
  bool animated_;
  uint32_t frame_count_;
  float repetition_count_;
  bool selected_;
};

// ImageTrackList クラス
class ImageTrackList {
 public:
  ImageTrackList();

  void add_track(std::shared_ptr<ImageTrack> track);
  std::shared_ptr<ImageTrack> get(uint32_t index) const;
  uint32_t length() const;
  int32_t selected_index() const;
  std::shared_ptr<ImageTrack> selected_track() const;
  bool is_ready() const { return ready_; }
  void set_ready(bool ready) { ready_ = ready; }

 private:
  std::vector<std::shared_ptr<ImageTrack>> tracks_;
  bool ready_ = false;
};

// ImageDecoder クラス
class ImageDecoder {
 public:
  // コンストラクタ
  ImageDecoder(nb::dict init);

  // デストラクタ
  ~ImageDecoder();

  // WebCodecs API メソッド
  nb::dict decode(nb::dict options = nb::dict());
  void reset();
  void close();

  // プロパティ
  std::string type() const { return type_; }
  bool complete() const { return complete_; }
  bool is_complete() const { return complete_; }
  std::shared_ptr<ImageTrackList> tracks() const { return tracks_; }
  bool is_closed() const { return closed_; }

  // 静的メソッド
  static bool is_type_supported(const std::string& type);

 private:
  std::string type_;
  std::vector<uint8_t> data_;
  std::string color_space_conversion_;
  std::optional<uint32_t> desired_width_;
  std::optional<uint32_t> desired_height_;
  std::optional<bool> prefer_animation_;

  bool complete_ = false;
  bool closed_ = false;
  std::shared_ptr<ImageTrackList> tracks_;

#if defined(__APPLE__)
  CGImageSourceRef image_source_ = nullptr;

  // Image I/O 固有のメソッド
  void init_image_io_decoder();
  void cleanup_image_io_decoder();
  std::unique_ptr<VideoFrame> decode_image_io(uint32_t frame_index);
#endif
};

void init_image_decoder(nb::module_& m);
