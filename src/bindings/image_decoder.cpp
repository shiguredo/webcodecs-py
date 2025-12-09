#include "image_decoder.h"

#include <nanobind/stl/optional.h>
#include <nanobind/stl/shared_ptr.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>
#include <stdexcept>

#include "video_frame.h"

#if defined(__APPLE__)
#include <CoreGraphics/CoreGraphics.h>
#endif

using namespace nb::literals;

// ImageTrack 実装

ImageTrack::ImageTrack(bool animated,
                       uint32_t frame_count,
                       float repetition_count,
                       bool selected)
    : animated_(animated),
      frame_count_(frame_count),
      repetition_count_(repetition_count),
      selected_(selected) {}

// ImageTrackList 実装

ImageTrackList::ImageTrackList() = default;

void ImageTrackList::add_track(std::shared_ptr<ImageTrack> track) {
  tracks_.push_back(track);
}

std::shared_ptr<ImageTrack> ImageTrackList::get(uint32_t index) const {
  if (index >= tracks_.size()) {
    return nullptr;
  }
  return tracks_[index];
}

uint32_t ImageTrackList::length() const {
  return static_cast<uint32_t>(tracks_.size());
}

int32_t ImageTrackList::selected_index() const {
  for (size_t i = 0; i < tracks_.size(); ++i) {
    if (tracks_[i]->selected()) {
      return static_cast<int32_t>(i);
    }
  }
  return -1;
}

std::shared_ptr<ImageTrack> ImageTrackList::selected_track() const {
  int32_t index = selected_index();
  if (index < 0) {
    return nullptr;
  }
  return tracks_[index];
}

// ImageDecoder 実装

ImageDecoder::ImageDecoder(nb::dict init) {
  // type (必須)
  if (!init.contains("type")) {
    throw std::invalid_argument("ImageDecoderInit requires 'type'");
  }
  type_ = nb::cast<std::string>(init["type"]);

  // data (必須)
  if (!init.contains("data")) {
    throw std::invalid_argument("ImageDecoderInit requires 'data'");
  }

  // data を bytes として取得
  nb::bytes data_bytes = nb::cast<nb::bytes>(init["data"]);
  const char* data_ptr = data_bytes.c_str();
  size_t data_size = data_bytes.size();
  data_.assign(reinterpret_cast<const uint8_t*>(data_ptr),
               reinterpret_cast<const uint8_t*>(data_ptr) + data_size);

  // color_space_conversion (オプション)
  if (init.contains("color_space_conversion")) {
    color_space_conversion_ =
        nb::cast<std::string>(init["color_space_conversion"]);
  } else {
    color_space_conversion_ = "default";
  }

  // desired_width (オプション)
  if (init.contains("desired_width") && !init["desired_width"].is_none()) {
    desired_width_ = nb::cast<uint32_t>(init["desired_width"]);
  }

  // desired_height (オプション)
  if (init.contains("desired_height") && !init["desired_height"].is_none()) {
    desired_height_ = nb::cast<uint32_t>(init["desired_height"]);
  }

  // prefer_animation (オプション)
  if (init.contains("prefer_animation") &&
      !init["prefer_animation"].is_none()) {
    prefer_animation_ = nb::cast<bool>(init["prefer_animation"]);
  }

  // トラックリストを初期化
  tracks_ = std::make_shared<ImageTrackList>();

#if defined(__APPLE__)
  init_image_io_decoder();
#else
  throw std::runtime_error("ImageDecoder is only supported on macOS");
#endif
}

ImageDecoder::~ImageDecoder() {
  if (!closed_) {
    close();
  }
}

nb::dict ImageDecoder::decode(nb::dict options) {
  if (closed_) {
    throw std::runtime_error("ImageDecoder is closed");
  }

  uint32_t frame_index = 0;
  if (options.contains("frame_index")) {
    frame_index = nb::cast<uint32_t>(options["frame_index"]);
  }

#if defined(__APPLE__)
  auto frame = decode_image_io(frame_index);

  nb::dict result;
  // unique_ptr から raw pointer を取り出して nanobind に所有権を移譲
  result["image"] = frame.release();
  result["complete"] = true;
  return result;
#else
  throw std::runtime_error("ImageDecoder is only supported on macOS");
#endif
}

void ImageDecoder::reset() {
  if (closed_) {
    throw std::runtime_error("ImageDecoder is closed");
  }
  // リセット処理
  // Image I/O の場合は特に必要ない
}

void ImageDecoder::close() {
  if (closed_) {
    return;
  }

#if defined(__APPLE__)
  cleanup_image_io_decoder();
#endif

  closed_ = true;
}

bool ImageDecoder::is_type_supported(const std::string& type) {
#if defined(__APPLE__)
  // Image I/O がサポートする MIME タイプ
  if (type == "image/jpeg" || type == "image/jpg") {
    return true;
  }
  if (type == "image/png") {
    return true;
  }
  if (type == "image/gif") {
    return true;
  }
  if (type == "image/webp") {
    return true;
  }
  if (type == "image/bmp") {
    return true;
  }
  if (type == "image/tiff") {
    return true;
  }
  if (type == "image/heic" || type == "image/heif") {
    return true;
  }
  return false;
#else
  return false;
#endif
}

#if defined(__APPLE__)

void ImageDecoder::init_image_io_decoder() {
  // データから CFDataRef を作成
  CFDataRef cf_data =
      CFDataCreate(kCFAllocatorDefault, data_.data(), data_.size());

  if (!cf_data) {
    throw std::runtime_error("Failed to create CFData");
  }

  // CGImageSource を作成
  image_source_ = CGImageSourceCreateWithData(cf_data, nullptr);
  CFRelease(cf_data);

  if (!image_source_) {
    throw std::runtime_error(
        "Failed to create CGImageSource. Invalid image data.");
  }

  // フレーム数を取得
  size_t frame_count = CGImageSourceGetCount(image_source_);
  if (frame_count == 0) {
    CFRelease(image_source_);
    image_source_ = nullptr;
    throw std::runtime_error("Image contains no frames");
  }

  // アニメーション判定
  bool is_animated = frame_count > 1;
  float repetition_count = 0.0f;

  // GIF のループ回数を取得
  if (is_animated) {
    CFDictionaryRef properties =
        CGImageSourceCopyProperties(image_source_, nullptr);
    if (properties) {
      CFDictionaryRef gif_properties =
          static_cast<CFDictionaryRef>(CFDictionaryGetValue(
              properties, kCGImagePropertyGIFDictionary));
      if (gif_properties) {
        CFNumberRef loop_count = static_cast<CFNumberRef>(CFDictionaryGetValue(
            gif_properties, kCGImagePropertyGIFLoopCount));
        if (loop_count) {
          int count = 0;
          CFNumberGetValue(loop_count, kCFNumberIntType, &count);
          // 0 は無限ループを意味する
          repetition_count =
              (count == 0) ? std::numeric_limits<float>::infinity()
                           : static_cast<float>(count);
        }
      }
      CFRelease(properties);
    }
  }

  // トラックを追加
  auto track = std::make_shared<ImageTrack>(
      is_animated, static_cast<uint32_t>(frame_count), repetition_count);
  tracks_->add_track(track);
  tracks_->set_ready(true);

  complete_ = true;
}

void ImageDecoder::cleanup_image_io_decoder() {
  if (image_source_) {
    CFRelease(image_source_);
    image_source_ = nullptr;
  }
}

std::unique_ptr<VideoFrame> ImageDecoder::decode_image_io(
    uint32_t frame_index) {
  if (!image_source_) {
    throw std::runtime_error("ImageDecoder not initialized");
  }

  size_t frame_count = CGImageSourceGetCount(image_source_);
  if (frame_index >= frame_count) {
    throw std::runtime_error("Frame index out of range");
  }

  // フレームを取得
  CGImageRef cg_image =
      CGImageSourceCreateImageAtIndex(image_source_, frame_index, nullptr);
  if (!cg_image) {
    throw std::runtime_error("Failed to create CGImage");
  }

  // 画像サイズを取得
  size_t width = CGImageGetWidth(cg_image);
  size_t height = CGImageGetHeight(cg_image);

  // VideoFrame を作成（RGBA フォーマット）
  auto frame = std::make_unique<VideoFrame>(static_cast<uint32_t>(width),
                                            static_cast<uint32_t>(height),
                                            VideoPixelFormat::RGBA, 0);

  // RGBA バッファを取得
  uint8_t* rgba_data = frame->mutable_plane_ptr(0);
  size_t bytes_per_row = width * 4;

  // CGContext を作成して描画
  CGColorSpaceRef color_space = CGColorSpaceCreateDeviceRGB();
  CGContextRef context = CGBitmapContextCreate(
      rgba_data, width, height, 8, bytes_per_row, color_space,
      kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);

  if (!context) {
    CGColorSpaceRelease(color_space);
    CGImageRelease(cg_image);
    throw std::runtime_error("Failed to create CGContext");
  }

  CGContextDrawImage(context, CGRectMake(0, 0, width, height), cg_image);

  CGContextRelease(context);
  CGColorSpaceRelease(color_space);
  CGImageRelease(cg_image);

  return frame;
}

#endif  // defined(__APPLE__)

void init_image_decoder(nb::module_& m) {
  // ImageTrack クラス
  nb::class_<ImageTrack>(m, "ImageTrack")
      .def_prop_ro("animated", &ImageTrack::animated,
                   nb::sig("def animated(self, /) -> bool"))
      .def_prop_ro("frame_count", &ImageTrack::frame_count,
                   nb::sig("def frame_count(self, /) -> int"))
      .def_prop_ro("repetition_count", &ImageTrack::repetition_count,
                   nb::sig("def repetition_count(self, /) -> float"))
      .def_prop_rw("selected", &ImageTrack::selected, &ImageTrack::set_selected,
                   nb::sig("def selected(self, /) -> bool"));

  // ImageTrackList クラス
  nb::class_<ImageTrackList>(m, "ImageTrackList")
      .def("__getitem__", &ImageTrackList::get, "index"_a,
           nb::sig("def __getitem__(self, index: int, /) -> ImageTrack | None"))
      .def("__len__", &ImageTrackList::length,
           nb::sig("def __len__(self, /) -> int"))
      .def_prop_ro("length", &ImageTrackList::length,
                   nb::sig("def length(self, /) -> int"))
      .def_prop_ro("selected_index", &ImageTrackList::selected_index,
                   nb::sig("def selected_index(self, /) -> int"))
      .def_prop_ro("selected_track", &ImageTrackList::selected_track,
                   nb::sig("def selected_track(self, /) -> ImageTrack | None"))
      .def_prop_ro("is_ready", &ImageTrackList::is_ready,
                   nb::sig("def is_ready(self, /) -> bool"));

  // ImageDecoder クラス
  nb::class_<ImageDecoder>(m, "ImageDecoder")
      .def(nb::init<nb::dict>(), "init"_a,
           nb::sig("def __init__(self, init: dict, /) -> None"))
      .def("decode", &ImageDecoder::decode, "options"_a = nb::dict(),
           nb::sig("def decode(self, options: dict = {}, /) -> dict"))
      .def("reset", &ImageDecoder::reset, nb::sig("def reset(self, /) -> None"))
      .def("close", &ImageDecoder::close, nb::sig("def close(self, /) -> None"))
      .def_prop_ro("type", &ImageDecoder::type,
                   nb::sig("def type(self, /) -> str"))
      .def_prop_ro("complete", &ImageDecoder::complete,
                   nb::sig("def complete(self, /) -> bool"))
      .def_prop_ro("is_complete", &ImageDecoder::is_complete,
                   nb::sig("def is_complete(self, /) -> bool"))
      .def_prop_ro("tracks", &ImageDecoder::tracks,
                   nb::sig("def tracks(self, /) -> ImageTrackList"))
      .def_prop_ro("is_closed", &ImageDecoder::is_closed,
                   nb::sig("def is_closed(self, /) -> bool"))
      .def_static("is_type_supported", &ImageDecoder::is_type_supported,
                  "type"_a, nb::sig("def is_type_supported(type: str) -> bool"));
}
