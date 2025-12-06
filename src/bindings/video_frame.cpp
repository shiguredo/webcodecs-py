#include "video_frame.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>

#include <libyuv.h>

using namespace nb::literals;

// 内部用コンストラクタ（clone, convert_format, デコーダーで使用）
VideoFrame::VideoFrame(uint32_t width,
                       uint32_t height,
                       VideoPixelFormat format,
                       int64_t timestamp)
    : width_(width),
      height_(height),
      format_(format),
      timestamp_(timestamp),
      duration_(0),
      closed_(false),
      coded_width_(width),
      coded_height_(height),
      display_width_(width),
      display_height_(height),
      rotation_(0),
      flip_(false) {
  // 新しいバッファを作成
  size_t frame_size = get_frame_size();
  data_.resize(frame_size, 0);
  calculate_plane_info();
}

// WebCodecs API 準拠コンストラクタ (dict を受け取る)
VideoFrame::VideoFrame(nb::ndarray<nb::numpy> data, nb::dict init_dict)
    : closed_(false) {
  // 必須パラメータのチェック
  if (!init_dict.contains("format")) {
    throw nb::value_error("format is required");
  }
  if (!init_dict.contains("coded_width")) {
    throw nb::value_error("coded_width is required");
  }
  if (!init_dict.contains("coded_height")) {
    throw nb::value_error("coded_height is required");
  }
  if (!init_dict.contains("timestamp")) {
    throw nb::value_error("timestamp is required");
  }

  // 必須フィールド
  auto format_value = init_dict["format"];
  if (nb::isinstance<nb::str>(format_value)) {
    format_ = string_to_format(nb::cast<std::string>(format_value));
  } else {
    format_ = nb::cast<VideoPixelFormat>(format_value);
  }
  coded_width_ = nb::cast<uint32_t>(init_dict["coded_width"]);
  coded_height_ = nb::cast<uint32_t>(init_dict["coded_height"]);
  timestamp_ = nb::cast<int64_t>(init_dict["timestamp"]);

  // オプションフィールド
  duration_ = nb::cast<uint64_t>(init_dict.get("duration", nb::int_(0)));

  if (init_dict.contains("layout")) {
    nb::list layout_list = nb::cast<nb::list>(init_dict["layout"]);
    std::vector<PlaneLayout> layouts;
    for (size_t i = 0; i < layout_list.size(); ++i) {
      layouts.push_back(nb::cast<PlaneLayout>(layout_list[i]));
    }
    layout_ = layouts;
  }

  if (init_dict.contains("visible_rect")) {
    nb::dict rect_dict = nb::cast<nb::dict>(init_dict["visible_rect"]);
    visible_rect_ = DOMRect(nb::cast<double>(rect_dict["x"]),
                            nb::cast<double>(rect_dict["y"]),
                            nb::cast<double>(rect_dict["width"]),
                            nb::cast<double>(rect_dict["height"]));
  }

  if (init_dict.contains("color_space")) {
    nb::dict cs_dict = nb::cast<nb::dict>(init_dict["color_space"]);
    VideoColorSpace cs;
    if (cs_dict.contains("primaries")) {
      cs.primaries = nb::cast<std::string>(cs_dict["primaries"]);
    }
    if (cs_dict.contains("transfer")) {
      cs.transfer = nb::cast<std::string>(cs_dict["transfer"]);
    }
    if (cs_dict.contains("matrix")) {
      cs.matrix = nb::cast<std::string>(cs_dict["matrix"]);
    }
    if (cs_dict.contains("full_range")) {
      cs.full_range = nb::cast<bool>(cs_dict["full_range"]);
    }
    color_space_ = cs;
  }

  rotation_ = nb::cast<uint32_t>(init_dict.get("rotation", nb::int_(0)));
  flip_ = nb::cast<bool>(init_dict.get("flip", nb::bool_(false)));

  if (init_dict.contains("metadata")) {
    metadata_ = nb::cast<nb::dict>(init_dict["metadata"]);
  }

  // display_width/height の処理
  uint32_t init_display_width = nb::cast<uint32_t>(
      init_dict.get("display_width", nb::int_(coded_width_)));
  uint32_t init_display_height = nb::cast<uint32_t>(
      init_dict.get("display_height", nb::int_(coded_height_)));

  // rotation が 90 度または 270 度の場合は display_width と display_height を入れ替える
  if (rotation_ == 90 || rotation_ == 270) {
    display_width_ = init_display_height;
    display_height_ = init_display_width;
  } else {
    display_width_ = init_display_width;
    display_height_ = init_display_height;
  }

  // visible_rect が指定されていない場合はコード化サイズ全体を使用
  if (!visible_rect_) {
    visible_rect_ = DOMRect(0, 0, coded_width_, coded_height_);
  }

  // 実際の表示サイズを決定
  width_ = static_cast<uint32_t>(visible_rect_->width);
  height_ = static_cast<uint32_t>(visible_rect_->height);

  // フレームサイズを計算 - coded_width/height を基準に計算
  size_t frame_size;
  if (layout_ && !layout_->empty()) {
    // カスタムレイアウトからサイズを計算
    frame_size = 0;
    for (const auto& plane : *layout_) {
      size_t plane_height = coded_height_;
      // I420 形式の場合、U/V プレーンの高さは半分
      if (format_ == VideoPixelFormat::I420 && layout_->size() == 3) {
        const size_t plane_index = &plane - layout_->data();
        if (plane_index > 0) {
          plane_height /= 2;
        }
      }
      size_t plane_size = plane.offset + plane.stride * plane_height;
      frame_size = std::max(frame_size, plane_size);
    }
  } else {
    // コード化されたサイズを基準にフレームサイズを計算
    switch (format_) {
      case VideoPixelFormat::I420:
      case VideoPixelFormat::NV12:
        frame_size = coded_width_ * coded_height_ * 3 / 2;
        break;
      case VideoPixelFormat::I422:
        frame_size = coded_width_ * coded_height_ * 2;
        break;
      case VideoPixelFormat::I444:
      case VideoPixelFormat::RGB:
      case VideoPixelFormat::BGR:
        frame_size = coded_width_ * coded_height_ * 3;
        break;
      case VideoPixelFormat::RGBA:
      case VideoPixelFormat::BGRA:
        frame_size = coded_width_ * coded_height_ * 4;
        break;
      default:
        throw std::runtime_error("Unsupported pixel format");
    }
  }

  // データサイズの検証
  size_t data_size = data.size() * data.itemsize();
  if (data_size != frame_size) {
    throw std::runtime_error("Data size mismatch with format and dimensions");
  }

  // データをコピー（部分的ゼロコピー実装のため）
  data_.resize(frame_size);
  std::memcpy(data_.data(), data.data(), frame_size);

  calculate_plane_info();
}

VideoFrame::~VideoFrame() {
  close();
}

// コピーコンストラクタ
VideoFrame::VideoFrame(const VideoFrame& other)
    : width_(other.width_),
      height_(other.height_),
      format_(other.format_),
      timestamp_(other.timestamp_),
      duration_(other.duration_),
      closed_(false),
      coded_width_(other.coded_width_),
      coded_height_(other.coded_height_),
      visible_rect_(other.visible_rect_),
      display_width_(other.display_width_),
      display_height_(other.display_height_),
      color_space_(other.color_space_),
      layout_(other.layout_),
      rotation_(other.rotation_),
      flip_(other.flip_),
      metadata_(other.metadata_),
      data_(other.data_),
      plane_offsets_(other.plane_offsets_),
      plane_sizes_(other.plane_sizes_) {
  if (other.closed_) {
    throw std::runtime_error("Cannot copy closed VideoFrame");
  }
}

// コピー代入演算子
VideoFrame& VideoFrame::operator=(const VideoFrame& other) {
  if (this == &other) {
    return *this;
  }

  if (other.closed_) {
    throw std::runtime_error("Cannot copy closed VideoFrame");
  }

  // 既存のリソースをクリーンアップ
  close();

  // メンバーをコピー
  width_ = other.width_;
  height_ = other.height_;
  format_ = other.format_;
  timestamp_ = other.timestamp_;
  duration_ = other.duration_;
  closed_ = false;
  coded_width_ = other.coded_width_;
  coded_height_ = other.coded_height_;
  visible_rect_ = other.visible_rect_;
  display_width_ = other.display_width_;
  display_height_ = other.display_height_;
  color_space_ = other.color_space_;
  layout_ = other.layout_;
  rotation_ = other.rotation_;
  flip_ = other.flip_;
  metadata_ = other.metadata_;
  data_ = other.data_;
  plane_offsets_ = other.plane_offsets_;
  plane_sizes_ = other.plane_sizes_;

  return *this;
}

void VideoFrame::close() {
  if (!closed_) {
    data_.clear();
    closed_ = true;
  }
}

size_t VideoFrame::allocation_size() const {
  // WebCodecs API 準拠: coded_width/height を基準にバッファサイズを計算
  switch (format_) {
    case VideoPixelFormat::I420:
    case VideoPixelFormat::NV12:
      return coded_width_ * coded_height_ * 3 / 2;
    case VideoPixelFormat::I422:
      return coded_width_ * coded_height_ * 2;
    case VideoPixelFormat::I444:
    case VideoPixelFormat::RGB:
    case VideoPixelFormat::BGR:
      return coded_width_ * coded_height_ * 3;
    case VideoPixelFormat::RGBA:
    case VideoPixelFormat::BGRA:
      return coded_width_ * coded_height_ * 4;
    default:
      throw std::runtime_error("Unsupported pixel format");
  }
}

VideoFrame::CopyToOptions VideoFrame::parse_copy_to_options(
    nb::dict options) const {
  CopyToOptions result;

  // rect オプション
  if (options.contains("rect") && !options["rect"].is_none()) {
    nb::dict rect_dict = nb::cast<nb::dict>(options["rect"]);
    result.rect = DOMRect(nb::cast<double>(rect_dict["x"]),
                          nb::cast<double>(rect_dict["y"]),
                          nb::cast<double>(rect_dict["width"]),
                          nb::cast<double>(rect_dict["height"]));
  }

  // layout オプション
  if (options.contains("layout") && !options["layout"].is_none()) {
    nb::list layout_list = nb::cast<nb::list>(options["layout"]);
    std::vector<PlaneLayout> layouts;
    for (size_t i = 0; i < layout_list.size(); ++i) {
      layouts.push_back(nb::cast<PlaneLayout>(layout_list[i]));
    }
    result.layout = layouts;
  }

  // format オプション
  if (options.contains("format") && !options["format"].is_none()) {
    auto format_value = options["format"];
    if (nb::isinstance<nb::str>(format_value)) {
      result.format = string_to_format(nb::cast<std::string>(format_value));
    } else {
      result.format = nb::cast<VideoPixelFormat>(format_value);
    }
  }

  return result;
}

size_t VideoFrame::calculate_size_for_rect(const DOMRect& rect,
                                           VideoPixelFormat fmt) const {
  uint32_t w = static_cast<uint32_t>(rect.width);
  uint32_t h = static_cast<uint32_t>(rect.height);

  switch (fmt) {
    case VideoPixelFormat::I420:
    case VideoPixelFormat::NV12:
      return w * h * 3 / 2;
    case VideoPixelFormat::I422:
      return w * h * 2;
    case VideoPixelFormat::I444:
    case VideoPixelFormat::RGB:
    case VideoPixelFormat::BGR:
      return w * h * 3;
    case VideoPixelFormat::RGBA:
    case VideoPixelFormat::BGRA:
      return w * h * 4;
    default:
      throw std::runtime_error("Unsupported pixel format");
  }
}

size_t VideoFrame::allocation_size(nb::dict options) const {
  if (closed_) {
    throw std::runtime_error("VideoFrame is closed");
  }

  CopyToOptions opts = parse_copy_to_options(options);

  // 出力フォーマットを決定
  VideoPixelFormat target_format = opts.format.value_or(format_);

  // rect が指定されている場合はその領域のサイズを計算
  if (opts.rect) {
    return calculate_size_for_rect(*opts.rect, target_format);
  }

  // layout が指定されている場合はレイアウトに基づいてサイズを計算
  if (opts.layout && !opts.layout->empty()) {
    size_t total_size = 0;
    uint32_t w = coded_width_;
    uint32_t h = coded_height_;

    for (size_t i = 0; i < opts.layout->size(); ++i) {
      const auto& plane = (*opts.layout)[i];
      uint32_t plane_height = h;

      // I420/NV12 の場合、クロマプレーンは高さが半分
      if ((target_format == VideoPixelFormat::I420 ||
           target_format == VideoPixelFormat::NV12) &&
          i > 0) {
        plane_height /= 2;
      }

      size_t plane_end = plane.offset + plane.stride * plane_height;
      total_size = std::max(total_size, plane_end);
    }
    return total_size;
  }

  // デフォルト: フレーム全体のサイズ
  return calculate_size_for_rect(DOMRect(0, 0, coded_width_, coded_height_),
                                 target_format);
}

size_t VideoFrame::get_frame_size() const {
  // 内部使用: width_/height_ ベース（後方互換性のため残す）
  switch (format_) {
    case VideoPixelFormat::I420:
    case VideoPixelFormat::NV12:
      return width_ * height_ * 3 / 2;
    case VideoPixelFormat::I422:
      return width_ * height_ * 2;
    case VideoPixelFormat::I444:
    case VideoPixelFormat::RGB:
    case VideoPixelFormat::BGR:
      return width_ * height_ * 3;
    case VideoPixelFormat::RGBA:
    case VideoPixelFormat::BGRA:
      return width_ * height_ * 4;
    default:
      throw std::runtime_error("Unsupported pixel format");
  }
}

void VideoFrame::calculate_plane_info() {
  plane_offsets_.clear();
  plane_sizes_.clear();

  switch (format_) {
    case VideoPixelFormat::I420:
      plane_offsets_ = {0, width_ * height_, width_ * height_ * 5 / 4};
      plane_sizes_ = {width_ * height_, width_ * height_ / 4,
                      width_ * height_ / 4};
      break;
    case VideoPixelFormat::I422:
      plane_offsets_ = {0, width_ * height_, width_ * height_ * 3 / 2};
      plane_sizes_ = {width_ * height_, width_ * height_ / 2,
                      width_ * height_ / 2};
      break;
    case VideoPixelFormat::I444:
      plane_offsets_ = {0, width_ * height_, width_ * height_ * 2};
      plane_sizes_ = {width_ * height_, width_ * height_, width_ * height_};
      break;
    case VideoPixelFormat::NV12:
      plane_offsets_ = {0, width_ * height_};
      plane_sizes_ = {width_ * height_, width_ * height_ / 2};
      break;
    case VideoPixelFormat::RGB:
    case VideoPixelFormat::BGR:
      plane_offsets_ = {0};
      plane_sizes_ = {width_ * height_ * 3};
      break;
    case VideoPixelFormat::RGBA:
    case VideoPixelFormat::BGRA:
      plane_offsets_ = {0};
      plane_sizes_ = {width_ * height_ * 4};
      break;
  }
}

nb::ndarray<nb::numpy> VideoFrame::plane(int plane_index) const {
  if (closed_) {
    throw std::runtime_error("VideoFrame is closed");
  }

  if (plane_index < 0 || plane_index >= plane_offsets_.size()) {
    throw std::out_of_range("Invalid plane index");
  }

  uint32_t plane_height = height_;
  uint32_t plane_width = width_;

  // クロマプレーンの寸法を調整
  if (format_ == VideoPixelFormat::I420 || format_ == VideoPixelFormat::NV12) {
    if (plane_index > 0) {
      plane_height /= 2;
      plane_width /= 2;
      if (format_ == VideoPixelFormat::NV12) {
        plane_width *= 2;  // インターリーブされた UV
      }
    }
  } else if (format_ == VideoPixelFormat::I422 && plane_index > 0) {
    plane_width /= 2;
  }

  size_t shape[2] = {plane_height, plane_width};
  // 内部データへのビューを返す（ゼロコピー）
  return nb::ndarray<nb::numpy>(
      const_cast<uint8_t*>(data_.data()) + plane_offsets_[plane_index], 2,
      shape, nb::handle(), nullptr, nb::dtype<uint8_t>());
}

nb::ndarray<nb::numpy> VideoFrame::get_writable_plane(int plane_index) {
  if (closed_) {
    throw std::runtime_error("VideoFrame is closed");
  }

  if (plane_index < 0 || plane_index >= plane_offsets_.size()) {
    throw std::out_of_range("Invalid plane index");
  }

  uint32_t plane_height = height_;
  uint32_t plane_width = width_;

  // クロマプレーンの寸法を調整
  if (format_ == VideoPixelFormat::I420 || format_ == VideoPixelFormat::NV12) {
    if (plane_index > 0) {
      plane_height /= 2;
      plane_width /= 2;
      if (format_ == VideoPixelFormat::NV12) {
        plane_width *= 2;  // インターリーブされた UV
      }
    }
  } else if (format_ == VideoPixelFormat::I422 && plane_index > 0) {
    plane_width /= 2;
  }

  size_t shape[2] = {plane_height, plane_width};
  // 内部データへのビューを返す（部分的ゼロコピー）
  return nb::ndarray<nb::numpy>(
      const_cast<uint8_t*>(data_.data()) + plane_offsets_[plane_index], 2,
      shape, nb::handle(), nullptr, nb::dtype<uint8_t>());
}

nb::ndarray<nb::numpy> VideoFrame::get_plane_data(int plane_index) const {
  return plane(plane_index);
}

const uint8_t* VideoFrame::plane_ptr(int plane_index) const {
  if (closed_) {
    throw std::runtime_error("VideoFrame is closed");
  }
  if (plane_index < 0 ||
      plane_index >= static_cast<int>(plane_offsets_.size())) {
    throw std::out_of_range("Invalid plane index");
  }
  return data_.data() + plane_offsets_[plane_index];
}

uint8_t* VideoFrame::mutable_plane_ptr(int plane_index) {
  return const_cast<uint8_t*>(plane_ptr(plane_index));
}

uint8_t* VideoFrame::mutable_data() {
  if (closed_) {
    throw std::runtime_error("VideoFrame is closed");
  }
  return data_.data();
}

std::unique_ptr<VideoFrame> VideoFrame::convert_format(
    VideoPixelFormat target_format) const {
  if (closed_) {
    throw std::runtime_error("VideoFrame is closed");
  }

  auto result =
      std::make_unique<VideoFrame>(width_, height_, target_format, timestamp_);
  result->set_duration(duration_);

  // libyuv を使用して変換
  if (format_ == VideoPixelFormat::I420 &&
      target_format == VideoPixelFormat::RGBA) {
    libyuv::I420ToABGR(data_.data() + plane_offsets_[0], width_,
                       data_.data() + plane_offsets_[1], width_ / 2,
                       data_.data() + plane_offsets_[2], width_ / 2,
                       result->mutable_data(), width_ * 4, width_, height_);
  } else if (format_ == VideoPixelFormat::I420 &&
             target_format == VideoPixelFormat::RGB) {
    libyuv::I420ToRGB24(data_.data() + plane_offsets_[0], width_,
                        data_.data() + plane_offsets_[1], width_ / 2,
                        data_.data() + plane_offsets_[2], width_ / 2,
                        result->mutable_data(), width_ * 3, width_, height_);
  } else if (format_ == VideoPixelFormat::RGB &&
             target_format == VideoPixelFormat::I420) {
    libyuv::RGB24ToI420(
        data_.data(), width_ * 3,
        result->mutable_data() + result->plane_offsets_[0], width_,
        result->mutable_data() + result->plane_offsets_[1], width_ / 2,
        result->mutable_data() + result->plane_offsets_[2], width_ / 2, width_,
        height_);
  } else if (format_ == VideoPixelFormat::RGBA &&
             target_format == VideoPixelFormat::I420) {
    libyuv::ABGRToI420(
        data_.data(), width_ * 4,
        result->mutable_data() + result->plane_offsets_[0], width_,
        result->mutable_data() + result->plane_offsets_[1], width_ / 2,
        result->mutable_data() + result->plane_offsets_[2], width_ / 2, width_,
        height_);
  } else if (format_ == VideoPixelFormat::BGRA &&
             target_format == VideoPixelFormat::I420) {
    libyuv::ARGBToI420(
        data_.data(), width_ * 4,
        result->mutable_data() + result->plane_offsets_[0], width_,
        result->mutable_data() + result->plane_offsets_[1], width_ / 2,
        result->mutable_data() + result->plane_offsets_[2], width_ / 2, width_,
        height_);
  } else if (format_ == VideoPixelFormat::NV12 &&
             target_format == VideoPixelFormat::I420) {
    libyuv::NV12ToI420(
        data_.data() + plane_offsets_[0], width_,
        data_.data() + plane_offsets_[1], width_,
        result->mutable_data() + result->plane_offsets_[0], width_,
        result->mutable_data() + result->plane_offsets_[1], width_ / 2,
        result->mutable_data() + result->plane_offsets_[2], width_ / 2, width_,
        height_);
  } else if (format_ == VideoPixelFormat::I420 &&
             target_format == VideoPixelFormat::NV12) {
    libyuv::I420ToNV12(data_.data() + plane_offsets_[0], width_,
                       data_.data() + plane_offsets_[1], width_ / 2,
                       data_.data() + plane_offsets_[2], width_ / 2,
                       result->mutable_data() + result->plane_offsets_[0],
                       width_,
                       result->mutable_data() + result->plane_offsets_[1],
                       width_, width_, height_);
  } else {
    throw std::runtime_error("Unsupported conversion");
  }

  // metadata をコピー
  result->metadata_ = metadata_;

  return result;
}

std::unique_ptr<VideoFrame> VideoFrame::clone() const {
  if (closed_) {
    throw std::runtime_error("VideoFrame is closed");
  }

  auto cloned =
      std::make_unique<VideoFrame>(width_, height_, format_, timestamp_);
  cloned->set_duration(duration_);
  cloned->coded_width_ = coded_width_;
  cloned->coded_height_ = coded_height_;
  cloned->visible_rect_ = visible_rect_;
  cloned->display_width_ = display_width_;
  cloned->display_height_ = display_height_;
  cloned->color_space_ = color_space_;
  cloned->layout_ = layout_;
  cloned->rotation_ = rotation_;
  cloned->flip_ = flip_;
  cloned->metadata_ = metadata_;
  std::memcpy(cloned->mutable_data(), data_.data(), data_.size());
  return cloned;
}

std::unique_ptr<VideoFrame> VideoFrame::create_encoder_copy() const {
  if (closed_) {
    throw std::runtime_error("VideoFrame is closed");
  }

  // 新しい VideoFrame を作成（常にメモリを所有）
  auto copy =
      std::make_unique<VideoFrame>(width_, height_, format_, timestamp_);

  // 基本プロパティをコピー
  copy->duration_ = duration_;
  copy->coded_width_ = coded_width_;
  copy->coded_height_ = coded_height_;
  copy->visible_rect_ = visible_rect_;
  copy->display_width_ = display_width_;
  copy->display_height_ = display_height_;
  copy->color_space_ = color_space_;
  copy->layout_ = layout_;
  copy->rotation_ = rotation_;
  copy->flip_ = flip_;
  copy->metadata_ = metadata_;

  // データを深くコピー（エンコーダー安全性のため必須）
  std::memcpy(copy->mutable_data(), data_.data(), data_.size());

  return copy;
}

// copy_to(): WebCodecs API 準拠の実装
// destination に書き込み、PlaneLayout のリストを返す
std::vector<PlaneLayout> VideoFrame::copy_to(
    nb::ndarray<nb::numpy> destination) {
  if (closed_) {
    throw std::runtime_error("VideoFrame is closed");
  }

  // destination のサイズを検証
  if (destination.ndim() != 1) {
    throw std::runtime_error("destination must be a 1D array");
  }

  // 必要なサイズを計算
  size_t required_size = 0;
  for (size_t i = 0; i < plane_sizes_.size(); ++i) {
    required_size += plane_sizes_[i];
  }

  size_t dest_size = destination.shape(0) * destination.itemsize();
  if (dest_size < required_size) {
    throw std::runtime_error("destination buffer is too small");
  }

  // destination に各プレーンをコピー
  uint8_t* dest_ptr = static_cast<uint8_t*>(destination.data());

  // RGB/RGBA/BGR/BGRA の場合は単一プレーン
  if (format_ == VideoPixelFormat::RGBA || format_ == VideoPixelFormat::BGRA ||
      format_ == VideoPixelFormat::RGB || format_ == VideoPixelFormat::BGR) {
    std::memcpy(dest_ptr, data_.data(), plane_sizes_[0]);
    uint32_t bytes_per_pixel =
        (format_ == VideoPixelFormat::RGBA || format_ == VideoPixelFormat::BGRA)
            ? 4
            : 3;
    PlaneLayout layout{0, static_cast<uint32_t>(width_ * bytes_per_pixel)};
    return {layout};
  }

  // I420/I422/I444/NV12 のみサポート
  if (!(format_ == VideoPixelFormat::I420 ||
        format_ == VideoPixelFormat::I422 ||
        format_ == VideoPixelFormat::I444 ||
        format_ == VideoPixelFormat::NV12)) {
    throw std::runtime_error(
        "copy_to supports only I420/I422/I444/NV12/RGB/RGBA/BGR/BGRA formats");
  }
  size_t current_offset = 0;

  // NV12 の場合は 2 プレーン（Y と UV インターリーブ）
  if (format_ == VideoPixelFormat::NV12) {
    // Y プレーンをコピー
    std::memcpy(dest_ptr + current_offset, data_.data() + plane_offsets_[0],
                plane_sizes_[0]);
    PlaneLayout y_layout{static_cast<uint32_t>(current_offset),
                         static_cast<uint32_t>(width_)};
    current_offset += plane_sizes_[0];

    // UV プレーンをコピー（インターリーブ）
    std::memcpy(dest_ptr + current_offset, data_.data() + plane_offsets_[1],
                plane_sizes_[1]);
    PlaneLayout uv_layout{
        static_cast<uint32_t>(current_offset),
        static_cast<uint32_t>(width_)};  // UV は width と同じ stride

    return {y_layout, uv_layout};
  }

  // I420/I422/I444 の場合は 3 プレーン
  uint32_t y_width = width_;
  uint32_t u_width = width_;
  uint32_t v_width = width_;

  if (format_ == VideoPixelFormat::I420) {
    u_width /= 2;
    v_width /= 2;
  } else if (format_ == VideoPixelFormat::I422) {
    u_width /= 2;
    v_width /= 2;
  }

  // Y プレーンをコピー
  std::memcpy(dest_ptr + current_offset, data_.data() + plane_offsets_[0],
              plane_sizes_[0]);
  PlaneLayout y_layout{static_cast<uint32_t>(current_offset),
                       static_cast<uint32_t>(y_width)};
  current_offset += plane_sizes_[0];

  // U プレーンをコピー
  std::memcpy(dest_ptr + current_offset, data_.data() + plane_offsets_[1],
              plane_sizes_[1]);
  PlaneLayout u_layout{static_cast<uint32_t>(current_offset),
                       static_cast<uint32_t>(u_width)};
  current_offset += plane_sizes_[1];

  // V プレーンをコピー
  std::memcpy(dest_ptr + current_offset, data_.data() + plane_offsets_[2],
              plane_sizes_[2]);
  PlaneLayout v_layout{static_cast<uint32_t>(current_offset),
                       static_cast<uint32_t>(v_width)};

  // PlaneLayout のリストを返す
  return {y_layout, u_layout, v_layout};
}

// copy_to(): WebCodecs API 準拠の実装 (options 付き)
std::vector<PlaneLayout> VideoFrame::copy_to(nb::ndarray<nb::numpy> destination,
                                             nb::dict options) {
  if (closed_) {
    throw std::runtime_error("VideoFrame is closed");
  }

  CopyToOptions opts = parse_copy_to_options(options);

  // 出力フォーマットを決定
  VideoPixelFormat target_format = opts.format.value_or(format_);

  // rect を決定（指定がなければフレーム全体）
  DOMRect rect = opts.rect.value_or(DOMRect(0, 0, coded_width_, coded_height_));

  // フォーマット変換が必要な場合
  if (target_format != format_) {
    // libyuv を使用した変換は convert_format() に委譲して、
    // 変換後のフレームから copy_to() を呼ぶ
    auto converted = convert_format(target_format);
    // 変換後は rect なしで copy_to を呼ぶ（rect は元フレームに適用済み）
    return converted->copy_to(destination);
  }

  // フォーマット変換なしの場合は I420/I422/I444/NV12 のみサポート
  if (!(format_ == VideoPixelFormat::I420 ||
        format_ == VideoPixelFormat::I422 ||
        format_ == VideoPixelFormat::I444 ||
        format_ == VideoPixelFormat::NV12)) {
    throw std::runtime_error(
        "copy_to without format conversion supports only I420/I422/I444/NV12 "
        "source formats");
  }

  // destination のサイズを検証
  if (destination.ndim() != 1) {
    throw std::runtime_error("destination must be a 1D array");
  }

  uint32_t rect_x = static_cast<uint32_t>(rect.x);
  uint32_t rect_y = static_cast<uint32_t>(rect.y);
  uint32_t rect_w = static_cast<uint32_t>(rect.width);
  uint32_t rect_h = static_cast<uint32_t>(rect.height);

  // 境界チェック
  if (rect_x + rect_w > coded_width_ || rect_y + rect_h > coded_height_) {
    throw std::runtime_error("rect exceeds frame boundaries");
  }

  uint8_t* dest_ptr = static_cast<uint8_t*>(destination.data());

  // NV12 の場合は 2 プレーン（Y と UV インターリーブ）
  if (format_ == VideoPixelFormat::NV12) {
    uint32_t y_width = rect_w;
    uint32_t y_height = rect_h;
    uint32_t uv_width = rect_w;  // UV インターリーブなので幅は同じ
    uint32_t uv_height = rect_h / 2;

    // レイアウトを決定
    std::vector<PlaneLayout> output_layout;
    if (opts.layout && opts.layout->size() >= 2) {
      output_layout = *opts.layout;
    } else {
      size_t y_size = y_width * y_height;
      output_layout = {PlaneLayout{0, y_width},
                       PlaneLayout{static_cast<uint32_t>(y_size), uv_width}};
    }

    // 必要なサイズを計算
    size_t required_size = 0;
    required_size = std::max(
        required_size, static_cast<size_t>(output_layout[0].offset +
                                           output_layout[0].stride * y_height));
    required_size =
        std::max(required_size,
                 static_cast<size_t>(output_layout[1].offset +
                                     output_layout[1].stride * uv_height));

    size_t dest_size = destination.shape(0) * destination.itemsize();
    if (dest_size < required_size) {
      throw std::runtime_error("destination buffer is too small");
    }

    // Y プレーンをコピー
    {
      uint32_t src_stride = width_;
      uint32_t dst_stride = output_layout[0].stride;
      const uint8_t* src =
          data_.data() + plane_offsets_[0] + rect_y * src_stride + rect_x;
      uint8_t* dst = dest_ptr + output_layout[0].offset;
      for (uint32_t row = 0; row < y_height; ++row) {
        std::memcpy(dst + row * dst_stride, src + row * src_stride, y_width);
      }
    }

    // UV プレーンをコピー（インターリーブ）
    {
      uint32_t src_stride = width_;
      uint32_t src_y_offset = rect_y / 2;
      uint32_t src_x_offset =
          rect_x;  // UV はインターリーブなので x オフセットはそのまま
      uint32_t dst_stride = output_layout[1].stride;
      const uint8_t* src = data_.data() + plane_offsets_[1] +
                           src_y_offset * src_stride + src_x_offset;
      uint8_t* dst = dest_ptr + output_layout[1].offset;
      for (uint32_t row = 0; row < uv_height; ++row) {
        std::memcpy(dst + row * dst_stride, src + row * src_stride, uv_width);
      }
    }

    return output_layout;
  }

  // I420/I422/I444 の場合は 3 プレーン
  uint32_t y_width = rect_w;
  uint32_t y_height = rect_h;
  uint32_t uv_width = rect_w;
  uint32_t uv_height = rect_h;

  if (format_ == VideoPixelFormat::I420) {
    uv_width /= 2;
    uv_height /= 2;
  } else if (format_ == VideoPixelFormat::I422) {
    uv_width /= 2;
  }

  // レイアウトを決定
  std::vector<PlaneLayout> output_layout;
  if (opts.layout && opts.layout->size() >= 3) {
    output_layout = *opts.layout;
  } else {
    // デフォルトレイアウト: 連続配置
    size_t y_size = y_width * y_height;
    size_t uv_size = uv_width * uv_height;
    output_layout = {
        PlaneLayout{0, y_width},
        PlaneLayout{static_cast<uint32_t>(y_size), uv_width},
        PlaneLayout{static_cast<uint32_t>(y_size + uv_size), uv_width}};
  }

  // 必要なサイズを計算
  size_t required_size = 0;
  for (size_t i = 0; i < output_layout.size(); ++i) {
    uint32_t plane_h = (i == 0) ? y_height : uv_height;
    size_t plane_end =
        output_layout[i].offset + output_layout[i].stride * plane_h;
    required_size = std::max(required_size, plane_end);
  }

  size_t dest_size = destination.shape(0) * destination.itemsize();
  if (dest_size < required_size) {
    throw std::runtime_error("destination buffer is too small");
  }

  // 各プレーンをコピー（rect を適用）
  // Y プレーン
  {
    uint32_t src_stride = width_;
    uint32_t dst_stride = output_layout[0].stride;
    const uint8_t* src =
        data_.data() + plane_offsets_[0] + rect_y * src_stride + rect_x;
    uint8_t* dst = dest_ptr + output_layout[0].offset;
    for (uint32_t row = 0; row < y_height; ++row) {
      std::memcpy(dst + row * dst_stride, src + row * src_stride, y_width);
    }
  }

  // U プレーン
  {
    uint32_t src_stride =
        (format_ == VideoPixelFormat::I444) ? width_ : width_ / 2;
    uint32_t src_y_offset =
        (format_ == VideoPixelFormat::I444) ? rect_y : rect_y / 2;
    uint32_t src_x_offset =
        (format_ == VideoPixelFormat::I444) ? rect_x : rect_x / 2;
    uint32_t dst_stride = output_layout[1].stride;
    const uint8_t* src = data_.data() + plane_offsets_[1] +
                         src_y_offset * src_stride + src_x_offset;
    uint8_t* dst = dest_ptr + output_layout[1].offset;
    for (uint32_t row = 0; row < uv_height; ++row) {
      std::memcpy(dst + row * dst_stride, src + row * src_stride, uv_width);
    }
  }

  // V プレーン
  {
    uint32_t src_stride =
        (format_ == VideoPixelFormat::I444) ? width_ : width_ / 2;
    uint32_t src_y_offset =
        (format_ == VideoPixelFormat::I444) ? rect_y : rect_y / 2;
    uint32_t src_x_offset =
        (format_ == VideoPixelFormat::I444) ? rect_x : rect_x / 2;
    uint32_t dst_stride = output_layout[2].stride;
    const uint8_t* src = data_.data() + plane_offsets_[2] +
                         src_y_offset * src_stride + src_x_offset;
    uint8_t* dst = dest_ptr + output_layout[2].offset;
    for (uint32_t row = 0; row < uv_height; ++row) {
      std::memcpy(dst + row * dst_stride, src + row * src_stride, uv_width);
    }
  }

  return output_layout;
}

nb::tuple VideoFrame::planes() {
  if (closed_) {
    throw std::runtime_error("VideoFrame is closed");
  }
  if (!(format_ == VideoPixelFormat::I420 ||
        format_ == VideoPixelFormat::I422 ||
        format_ == VideoPixelFormat::I444)) {
    throw std::runtime_error("planes supports only I420/I422/I444 formats");
  }

  // プレーンのサイズを計算
  uint32_t y_height = height_;
  uint32_t y_width = width_;
  uint32_t u_height = height_;
  uint32_t u_width = width_;
  uint32_t v_height = height_;
  uint32_t v_width = width_;

  if (format_ == VideoPixelFormat::I420) {
    u_height /= 2;
    u_width /= 2;
    v_height /= 2;
    v_width /= 2;
  } else if (format_ == VideoPixelFormat::I422) {
    u_width /= 2;
    v_width /= 2;
  }

  // 内部データへのビューを作成（部分的ゼロコピー）
  nb::handle owner = nb::handle();  // データは std::vector が所有

  size_t y_shape[2] = {y_height, y_width};
  auto y_plane = nb::ndarray<nb::numpy>(
      const_cast<uint8_t*>(data_.data()) + plane_offsets_[0], 2, y_shape, owner,
      nullptr, nb::dtype<uint8_t>());

  size_t u_shape[2] = {u_height, u_width};
  auto u_plane = nb::ndarray<nb::numpy>(
      const_cast<uint8_t*>(data_.data()) + plane_offsets_[1], 2, u_shape, owner,
      nullptr, nb::dtype<uint8_t>());

  size_t v_shape[2] = {v_height, v_width};
  auto v_plane = nb::ndarray<nb::numpy>(
      const_cast<uint8_t*>(data_.data()) + plane_offsets_[2], 2, v_shape, owner,
      nullptr, nb::dtype<uint8_t>());

  return nb::make_tuple(y_plane, u_plane, v_plane);
}

VideoPixelFormat VideoFrame::string_to_format(
    const std::string& format_str) const {
  if (format_str == "I420")
    return VideoPixelFormat::I420;
  if (format_str == "I422")
    return VideoPixelFormat::I422;
  if (format_str == "I444")
    return VideoPixelFormat::I444;
  if (format_str == "NV12")
    return VideoPixelFormat::NV12;
  if (format_str == "RGBA")
    return VideoPixelFormat::RGBA;
  if (format_str == "BGRA")
    return VideoPixelFormat::BGRA;
  if (format_str == "RGB")
    return VideoPixelFormat::RGB;
  if (format_str == "BGR")
    return VideoPixelFormat::BGR;
  throw std::runtime_error("Unknown pixel format: " + format_str);
}

nb::dict VideoFrame::metadata() const {
  if (metadata_) {
    return *metadata_;
  } else {
    return nb::dict();
  }
}

void init_video_frame(nb::module_& m) {
  nb::enum_<VideoPixelFormat>(m, "VideoPixelFormat")
      .value("I420", VideoPixelFormat::I420)
      .value("I422", VideoPixelFormat::I422)
      .value("I444", VideoPixelFormat::I444)
      .value("NV12", VideoPixelFormat::NV12)
      .value("RGBA", VideoPixelFormat::RGBA)
      .value("BGRA", VideoPixelFormat::BGRA)
      .value("RGB", VideoPixelFormat::RGB)
      .value("BGR", VideoPixelFormat::BGR);

  nb::class_<VideoFrame>(m, "VideoFrame")
      .def(
          nb::init<nb::ndarray<nb::numpy>, nb::dict>(), "data"_a, "init"_a,
          nb::sig("def __init__(self, data: numpy.typing.NDArray[numpy.uint8], "
                  "init: VideoFrameBufferInit, /) -> None"))
      .def_prop_ro("format", &VideoFrame::format,
                   nb::sig("def format(self, /) -> VideoPixelFormat"))
      .def_prop_ro("timestamp", &VideoFrame::timestamp,
                   nb::sig("def timestamp(self, /) -> int"))
      .def_prop_rw(
          "duration", &VideoFrame::duration, &VideoFrame::set_duration,
          nb::for_getter(nb::sig("def duration(self, /) -> int")),
          nb::for_setter(nb::sig("def duration(self, value: int, /) -> None")))
      // WebCodecs API properties
      .def_prop_ro("coded_width", &VideoFrame::coded_width,
                   nb::sig("def coded_width(self, /) -> int"))
      .def_prop_ro("coded_height", &VideoFrame::coded_height,
                   nb::sig("def coded_height(self, /) -> int"))
      .def_prop_ro("visible_rect", &VideoFrame::visible_rect,
                   nb::sig("def visible_rect(self, /) -> DOMRect | None"))
      .def_prop_ro("display_width", &VideoFrame::display_width,
                   nb::sig("def display_width(self, /) -> int"))
      .def_prop_ro("display_height", &VideoFrame::display_height,
                   nb::sig("def display_height(self, /) -> int"))
      .def_prop_ro(
          "color_space", &VideoFrame::color_space,
          nb::sig("def color_space(self, /) -> VideoColorSpace | None"))
      .def_prop_ro("rotation", &VideoFrame::rotation,
                   nb::sig("def rotation(self, /) -> int"))
      .def_prop_ro("flip", &VideoFrame::flip,
                   nb::sig("def flip(self, /) -> bool"))
      .def("metadata", &VideoFrame::metadata,
           nb::sig("def metadata(self, /) -> dict"))
      .def("plane", &VideoFrame::plane, "plane_index"_a,
           nb::sig("def plane(self, plane_index: int, /) -> "
                   "numpy.typing.NDArray[numpy.uint8]"))
      .def(
          "allocation_size",
          [](const VideoFrame& self, std::optional<nb::dict> options) {
            if (options) {
              return self.allocation_size(*options);
            }
            return self.allocation_size();
          },
          "options"_a = nb::none(),
          nb::sig("def allocation_size(self, options: VideoFrameCopyToOptions "
                  "| None = None, /) -> int"))
      .def(
          "copy_to",
          [](VideoFrame& self, nb::ndarray<nb::numpy> destination,
             std::optional<nb::dict> options) {
            if (options) {
              return self.copy_to(destination, *options);
            }
            return self.copy_to(destination);
          },
          "destination"_a, "options"_a = nb::none(),
          nb::sig("def copy_to(self, destination: "
                  "numpy.typing.NDArray[numpy.uint8], "
                  "options: VideoFrameCopyToOptions | None = None, /) -> "
                  "list[PlaneLayout]"))
      .def(
          "planes", &VideoFrame::planes,
          nb::sig(
              "def planes(self, /) -> tuple[numpy.typing.NDArray[numpy.uint8], "
              "numpy.typing.NDArray[numpy.uint8], "
              "numpy.typing.NDArray[numpy.uint8]]"))
      .def("close", &VideoFrame::close, nb::sig("def close(self, /) -> None"))
      .def_prop_ro("is_closed", &VideoFrame::is_closed,
                   nb::sig("def is_closed(self, /) -> bool"))
      .def(
          "clone",
          [](const VideoFrame& self) { return self.clone().release(); },
          nb::rv_policy::take_ownership,
          nb::sig("def clone(self, /) -> VideoFrame"))
      // context manager 対応
      .def(
          "__enter__", [](VideoFrame& self) -> VideoFrame& { return self; },
          nb::rv_policy::reference)
      .def(
          "__exit__",
          [](VideoFrame& self, nb::object, nb::object, nb::object) {
            self.close();
          },
          "exc_type"_a.none(), "exc_val"_a.none(), "exc_tb"_a.none());
}
