// スケーリングヘルパー関数の実装

#include "video_scaler.h"

#include <libyuv.h>
#include <stdexcept>

namespace video_scaler {

namespace {

// I420 スケーリング
int scale_i420(const uint8_t* src_y,
               int src_stride_y,
               const uint8_t* src_u,
               int src_stride_u,
               const uint8_t* src_v,
               int src_stride_v,
               int src_width,
               int src_height,
               uint8_t* dst_y,
               int dst_stride_y,
               uint8_t* dst_u,
               int dst_stride_u,
               uint8_t* dst_v,
               int dst_stride_v,
               int dst_width,
               int dst_height) {
  return libyuv::I420Scale(
      src_y, src_stride_y, src_u, src_stride_u, src_v, src_stride_v, src_width,
      src_height, dst_y, dst_stride_y, dst_u, dst_stride_u, dst_v, dst_stride_v,
      dst_width, dst_height, libyuv::kFilterBox);
}

// I422 スケーリング
int scale_i422(const uint8_t* src_y,
               int src_stride_y,
               const uint8_t* src_u,
               int src_stride_u,
               const uint8_t* src_v,
               int src_stride_v,
               int src_width,
               int src_height,
               uint8_t* dst_y,
               int dst_stride_y,
               uint8_t* dst_u,
               int dst_stride_u,
               uint8_t* dst_v,
               int dst_stride_v,
               int dst_width,
               int dst_height) {
  return libyuv::I422Scale(
      src_y, src_stride_y, src_u, src_stride_u, src_v, src_stride_v, src_width,
      src_height, dst_y, dst_stride_y, dst_u, dst_stride_u, dst_v, dst_stride_v,
      dst_width, dst_height, libyuv::kFilterBox);
}

// I444 スケーリング
int scale_i444(const uint8_t* src_y,
               int src_stride_y,
               const uint8_t* src_u,
               int src_stride_u,
               const uint8_t* src_v,
               int src_stride_v,
               int src_width,
               int src_height,
               uint8_t* dst_y,
               int dst_stride_y,
               uint8_t* dst_u,
               int dst_stride_u,
               uint8_t* dst_v,
               int dst_stride_v,
               int dst_width,
               int dst_height) {
  return libyuv::I444Scale(
      src_y, src_stride_y, src_u, src_stride_u, src_v, src_stride_v, src_width,
      src_height, dst_y, dst_stride_y, dst_u, dst_stride_u, dst_v, dst_stride_v,
      dst_width, dst_height, libyuv::kFilterBox);
}

// NV12 スケーリング
int scale_nv12(const uint8_t* src_y,
               int src_stride_y,
               const uint8_t* src_uv,
               int src_stride_uv,
               int src_width,
               int src_height,
               uint8_t* dst_y,
               int dst_stride_y,
               uint8_t* dst_uv,
               int dst_stride_uv,
               int dst_width,
               int dst_height) {
  return libyuv::NV12Scale(src_y, src_stride_y, src_uv, src_stride_uv,
                           src_width, src_height, dst_y, dst_stride_y, dst_uv,
                           dst_stride_uv, dst_width, dst_height,
                           libyuv::kFilterBox);
}

// ARGB スケーリング
int scale_argb(const uint8_t* src_argb,
               int src_stride_argb,
               int src_width,
               int src_height,
               uint8_t* dst_argb,
               int dst_stride_argb,
               int dst_width,
               int dst_height) {
  return libyuv::ARGBScale(src_argb, src_stride_argb, src_width, src_height,
                           dst_argb, dst_stride_argb, dst_width, dst_height,
                           libyuv::kFilterBox);
}

}  // namespace

I420ScaleResult scale_to_i420(const VideoFrame& frame,
                              uint32_t dst_width,
                              uint32_t dst_height) {
  I420ScaleResult result;
  result.width = dst_width;
  result.height = dst_height;

  bool needs_scaling =
      (frame.width() != dst_width || frame.height() != dst_height);

  size_t y_size = dst_width * dst_height;
  size_t uv_size = (dst_width / 2) * (dst_height / 2);

  // スケーリングが不要かつ I420 の場合は元フレームのポインタを返す
  if (!needs_scaling && frame.format() == VideoPixelFormat::I420) {
    result.y = frame.plane_ptr(0);
    result.u = frame.plane_ptr(1);
    result.v = frame.plane_ptr(2);
    result.stride_y = static_cast<int>(dst_width);
    result.stride_u = static_cast<int>(dst_width / 2);
    result.stride_v = static_cast<int>(dst_width / 2);
    return result;
  }

  // スケーリング用のバッファ
  std::vector<uint8_t> scaled_buffer;
  uint32_t current_width = frame.width();
  uint32_t current_height = frame.height();
  VideoPixelFormat current_format = frame.format();

  // 1. スケーリング (入力フォーマットのまま)
  if (needs_scaling) {
    int scale_result = 0;

    switch (frame.format()) {
      case VideoPixelFormat::I420: {
        size_t dst_y_size = dst_width * dst_height;
        size_t dst_uv_size = (dst_width / 2) * (dst_height / 2);
        scaled_buffer.resize(dst_y_size + dst_uv_size * 2);
        scale_result = scale_i420(
            frame.plane_ptr(0), static_cast<int>(frame.width()),
            frame.plane_ptr(1), static_cast<int>(frame.width() / 2),
            frame.plane_ptr(2), static_cast<int>(frame.width() / 2),
            static_cast<int>(frame.width()), static_cast<int>(frame.height()),
            scaled_buffer.data(), static_cast<int>(dst_width),
            scaled_buffer.data() + dst_y_size, static_cast<int>(dst_width / 2),
            scaled_buffer.data() + dst_y_size + dst_uv_size,
            static_cast<int>(dst_width / 2), static_cast<int>(dst_width),
            static_cast<int>(dst_height));
        break;
      }
      case VideoPixelFormat::I422: {
        size_t dst_y_size = dst_width * dst_height;
        size_t dst_uv_size = (dst_width / 2) * dst_height;
        scaled_buffer.resize(dst_y_size + dst_uv_size * 2);
        scale_result = scale_i422(
            frame.plane_ptr(0), static_cast<int>(frame.width()),
            frame.plane_ptr(1), static_cast<int>(frame.width() / 2),
            frame.plane_ptr(2), static_cast<int>(frame.width() / 2),
            static_cast<int>(frame.width()), static_cast<int>(frame.height()),
            scaled_buffer.data(), static_cast<int>(dst_width),
            scaled_buffer.data() + dst_y_size, static_cast<int>(dst_width / 2),
            scaled_buffer.data() + dst_y_size + dst_uv_size,
            static_cast<int>(dst_width / 2), static_cast<int>(dst_width),
            static_cast<int>(dst_height));
        break;
      }
      case VideoPixelFormat::I444: {
        size_t plane_size = dst_width * dst_height;
        scaled_buffer.resize(plane_size * 3);
        scale_result = scale_i444(
            frame.plane_ptr(0), static_cast<int>(frame.width()),
            frame.plane_ptr(1), static_cast<int>(frame.width()),
            frame.plane_ptr(2), static_cast<int>(frame.width()),
            static_cast<int>(frame.width()), static_cast<int>(frame.height()),
            scaled_buffer.data(), static_cast<int>(dst_width),
            scaled_buffer.data() + plane_size, static_cast<int>(dst_width),
            scaled_buffer.data() + plane_size * 2, static_cast<int>(dst_width),
            static_cast<int>(dst_width), static_cast<int>(dst_height));
        break;
      }
      case VideoPixelFormat::NV12: {
        size_t nv12_size = dst_width * dst_height * 3 / 2;
        scaled_buffer.resize(nv12_size);
        scale_result = scale_nv12(
            frame.plane_ptr(0), static_cast<int>(frame.width()),
            frame.plane_ptr(1), static_cast<int>(frame.width()),
            static_cast<int>(frame.width()), static_cast<int>(frame.height()),
            scaled_buffer.data(), static_cast<int>(dst_width),
            scaled_buffer.data() + dst_width * dst_height,
            static_cast<int>(dst_width), static_cast<int>(dst_width),
            static_cast<int>(dst_height));
        break;
      }
      case VideoPixelFormat::RGBA:
      case VideoPixelFormat::BGRA: {
        size_t argb_size = dst_width * dst_height * 4;
        scaled_buffer.resize(argb_size);
        scale_result = scale_argb(
            frame.plane_ptr(0), static_cast<int>(frame.width() * 4),
            static_cast<int>(frame.width()), static_cast<int>(frame.height()),
            scaled_buffer.data(), static_cast<int>(dst_width * 4),
            static_cast<int>(dst_width), static_cast<int>(dst_height));
        break;
      }
      case VideoPixelFormat::RGB:
      case VideoPixelFormat::BGR: {
        // RGB/BGR は直接スケーリングできないため、I420 に変換してからスケーリング
        size_t src_y_size = frame.width() * frame.height();
        size_t src_uv_size = (frame.width() / 2) * (frame.height() / 2);
        std::vector<uint8_t> src_i420(src_y_size + src_uv_size * 2);
        uint8_t* src_i420_y = src_i420.data();
        uint8_t* src_i420_u = src_i420_y + src_y_size;
        uint8_t* src_i420_v = src_i420_u + src_uv_size;

        if (frame.format() == VideoPixelFormat::RGB) {
          libyuv::RGB24ToI420(frame.plane_ptr(0),
                              static_cast<int>(frame.width() * 3), src_i420_y,
                              static_cast<int>(frame.width()), src_i420_u,
                              static_cast<int>(frame.width() / 2), src_i420_v,
                              static_cast<int>(frame.width() / 2),
                              static_cast<int>(frame.width()),
                              static_cast<int>(frame.height()));
        } else {
          libyuv::RAWToI420(frame.plane_ptr(0),
                            static_cast<int>(frame.width() * 3), src_i420_y,
                            static_cast<int>(frame.width()), src_i420_u,
                            static_cast<int>(frame.width() / 2), src_i420_v,
                            static_cast<int>(frame.width() / 2),
                            static_cast<int>(frame.width()),
                            static_cast<int>(frame.height()));
        }

        // I420 でスケーリング
        size_t dst_y_size = dst_width * dst_height;
        size_t dst_uv_size = (dst_width / 2) * (dst_height / 2);
        scaled_buffer.resize(dst_y_size + dst_uv_size * 2);

        scale_result = scale_i420(
            src_i420_y, static_cast<int>(frame.width()), src_i420_u,
            static_cast<int>(frame.width() / 2), src_i420_v,
            static_cast<int>(frame.width() / 2),
            static_cast<int>(frame.width()), static_cast<int>(frame.height()),
            scaled_buffer.data(), static_cast<int>(dst_width),
            scaled_buffer.data() + dst_y_size, static_cast<int>(dst_width / 2),
            scaled_buffer.data() + dst_y_size + dst_uv_size,
            static_cast<int>(dst_width / 2), static_cast<int>(dst_width),
            static_cast<int>(dst_height));

        // スケーリング後は I420
        current_format = VideoPixelFormat::I420;
        break;
      }
    }

    if (scale_result != 0) {
      throw std::runtime_error("libyuv scale failed");
    }

    current_width = dst_width;
    current_height = dst_height;
  }

  // 2. I420 に変換
  if (current_format == VideoPixelFormat::I420) {
    // スケーリング済みの I420 バッファをそのまま使用
    result.buffer = std::move(scaled_buffer);
    result.y = result.buffer.data();
    result.u = result.buffer.data() + y_size;
    result.v = result.buffer.data() + y_size + uv_size;
  } else {
    // I420 以外のフォーマットは変換が必要
    result.buffer.resize(y_size + uv_size * 2);
    uint8_t* dst_y = result.buffer.data();
    uint8_t* dst_u = dst_y + y_size;
    uint8_t* dst_v = dst_u + uv_size;

    const uint8_t* src_data =
        needs_scaling ? scaled_buffer.data() : frame.plane_ptr(0);

    switch (current_format) {
      case VideoPixelFormat::I422: {
        size_t src_uv_size = (current_width / 2) * current_height;
        const uint8_t* src_u_ptr =
            needs_scaling
                ? scaled_buffer.data() + current_width * current_height
                : frame.plane_ptr(1);
        const uint8_t* src_v_ptr =
            needs_scaling ? scaled_buffer.data() +
                                current_width * current_height + src_uv_size
                          : frame.plane_ptr(2);
        libyuv::I422ToI420(src_data, static_cast<int>(current_width), src_u_ptr,
                           static_cast<int>(current_width / 2), src_v_ptr,
                           static_cast<int>(current_width / 2), dst_y,
                           static_cast<int>(current_width), dst_u,
                           static_cast<int>(current_width / 2), dst_v,
                           static_cast<int>(current_width / 2),
                           static_cast<int>(current_width),
                           static_cast<int>(current_height));
        break;
      }
      case VideoPixelFormat::I444: {
        size_t plane_size = current_width * current_height;
        const uint8_t* src_u_ptr = needs_scaling
                                       ? scaled_buffer.data() + plane_size
                                       : frame.plane_ptr(1);
        const uint8_t* src_v_ptr = needs_scaling
                                       ? scaled_buffer.data() + plane_size * 2
                                       : frame.plane_ptr(2);
        libyuv::I444ToI420(src_data, static_cast<int>(current_width), src_u_ptr,
                           static_cast<int>(current_width), src_v_ptr,
                           static_cast<int>(current_width), dst_y,
                           static_cast<int>(current_width), dst_u,
                           static_cast<int>(current_width / 2), dst_v,
                           static_cast<int>(current_width / 2),
                           static_cast<int>(current_width),
                           static_cast<int>(current_height));
        break;
      }
      case VideoPixelFormat::NV12: {
        const uint8_t* src_uv =
            needs_scaling
                ? scaled_buffer.data() + current_width * current_height
                : frame.plane_ptr(1);
        libyuv::NV12ToI420(src_data, static_cast<int>(current_width), src_uv,
                           static_cast<int>(current_width), dst_y,
                           static_cast<int>(current_width), dst_u,
                           static_cast<int>(current_width / 2), dst_v,
                           static_cast<int>(current_width / 2),
                           static_cast<int>(current_width),
                           static_cast<int>(current_height));
        break;
      }
      case VideoPixelFormat::RGBA: {
        libyuv::ABGRToI420(src_data, static_cast<int>(current_width * 4), dst_y,
                           static_cast<int>(current_width), dst_u,
                           static_cast<int>(current_width / 2), dst_v,
                           static_cast<int>(current_width / 2),
                           static_cast<int>(current_width),
                           static_cast<int>(current_height));
        break;
      }
      case VideoPixelFormat::BGRA: {
        libyuv::ARGBToI420(src_data, static_cast<int>(current_width * 4), dst_y,
                           static_cast<int>(current_width), dst_u,
                           static_cast<int>(current_width / 2), dst_v,
                           static_cast<int>(current_width / 2),
                           static_cast<int>(current_width),
                           static_cast<int>(current_height));
        break;
      }
      case VideoPixelFormat::RGB: {
        libyuv::RGB24ToI420(src_data, static_cast<int>(current_width * 3),
                            dst_y, static_cast<int>(current_width), dst_u,
                            static_cast<int>(current_width / 2), dst_v,
                            static_cast<int>(current_width / 2),
                            static_cast<int>(current_width),
                            static_cast<int>(current_height));
        break;
      }
      case VideoPixelFormat::BGR: {
        libyuv::RAWToI420(src_data, static_cast<int>(current_width * 3), dst_y,
                          static_cast<int>(current_width), dst_u,
                          static_cast<int>(current_width / 2), dst_v,
                          static_cast<int>(current_width / 2),
                          static_cast<int>(current_width),
                          static_cast<int>(current_height));
        break;
      }
      default:
        throw std::runtime_error(
            "Unsupported pixel format for I420 conversion");
    }

    result.y = dst_y;
    result.u = dst_u;
    result.v = dst_v;
  }

  result.stride_y = static_cast<int>(dst_width);
  result.stride_u = static_cast<int>(dst_width / 2);
  result.stride_v = static_cast<int>(dst_width / 2);

  return result;
}

NV12ScaleResult scale_to_nv12(const VideoFrame& frame,
                              uint32_t dst_width,
                              uint32_t dst_height) {
  NV12ScaleResult result;
  result.width = dst_width;
  result.height = dst_height;

  bool needs_scaling =
      (frame.width() != dst_width || frame.height() != dst_height);

  size_t nv12_size = dst_width * dst_height * 3 / 2;

  // スケーリングが不要かつ NV12 の場合は元フレームのポインタを返す
  if (!needs_scaling && frame.format() == VideoPixelFormat::NV12) {
    result.y = frame.plane_ptr(0);
    result.uv = frame.plane_ptr(1);
    return result;
  }

  // スケーリング用のバッファ
  std::vector<uint8_t> scaled_buffer;
  uint32_t current_width = frame.width();
  uint32_t current_height = frame.height();
  VideoPixelFormat current_format = frame.format();

  // 1. スケーリング (入力フォーマットのまま)
  if (needs_scaling) {
    int scale_result = 0;

    switch (frame.format()) {
      case VideoPixelFormat::I420: {
        size_t dst_y_size = dst_width * dst_height;
        size_t dst_uv_size = (dst_width / 2) * (dst_height / 2);
        scaled_buffer.resize(dst_y_size + dst_uv_size * 2);
        scale_result = scale_i420(
            frame.plane_ptr(0), static_cast<int>(frame.width()),
            frame.plane_ptr(1), static_cast<int>(frame.width() / 2),
            frame.plane_ptr(2), static_cast<int>(frame.width() / 2),
            static_cast<int>(frame.width()), static_cast<int>(frame.height()),
            scaled_buffer.data(), static_cast<int>(dst_width),
            scaled_buffer.data() + dst_y_size, static_cast<int>(dst_width / 2),
            scaled_buffer.data() + dst_y_size + dst_uv_size,
            static_cast<int>(dst_width / 2), static_cast<int>(dst_width),
            static_cast<int>(dst_height));
        break;
      }
      case VideoPixelFormat::I422: {
        size_t dst_y_size = dst_width * dst_height;
        size_t dst_uv_size = (dst_width / 2) * dst_height;
        scaled_buffer.resize(dst_y_size + dst_uv_size * 2);
        scale_result = scale_i422(
            frame.plane_ptr(0), static_cast<int>(frame.width()),
            frame.plane_ptr(1), static_cast<int>(frame.width() / 2),
            frame.plane_ptr(2), static_cast<int>(frame.width() / 2),
            static_cast<int>(frame.width()), static_cast<int>(frame.height()),
            scaled_buffer.data(), static_cast<int>(dst_width),
            scaled_buffer.data() + dst_y_size, static_cast<int>(dst_width / 2),
            scaled_buffer.data() + dst_y_size + dst_uv_size,
            static_cast<int>(dst_width / 2), static_cast<int>(dst_width),
            static_cast<int>(dst_height));
        break;
      }
      case VideoPixelFormat::I444: {
        size_t plane_size = dst_width * dst_height;
        scaled_buffer.resize(plane_size * 3);
        scale_result = scale_i444(
            frame.plane_ptr(0), static_cast<int>(frame.width()),
            frame.plane_ptr(1), static_cast<int>(frame.width()),
            frame.plane_ptr(2), static_cast<int>(frame.width()),
            static_cast<int>(frame.width()), static_cast<int>(frame.height()),
            scaled_buffer.data(), static_cast<int>(dst_width),
            scaled_buffer.data() + plane_size, static_cast<int>(dst_width),
            scaled_buffer.data() + plane_size * 2, static_cast<int>(dst_width),
            static_cast<int>(dst_width), static_cast<int>(dst_height));
        break;
      }
      case VideoPixelFormat::NV12: {
        scaled_buffer.resize(nv12_size);
        scale_result = scale_nv12(
            frame.plane_ptr(0), static_cast<int>(frame.width()),
            frame.plane_ptr(1), static_cast<int>(frame.width()),
            static_cast<int>(frame.width()), static_cast<int>(frame.height()),
            scaled_buffer.data(), static_cast<int>(dst_width),
            scaled_buffer.data() + dst_width * dst_height,
            static_cast<int>(dst_width), static_cast<int>(dst_width),
            static_cast<int>(dst_height));
        break;
      }
      case VideoPixelFormat::RGBA:
      case VideoPixelFormat::BGRA: {
        size_t argb_size = dst_width * dst_height * 4;
        scaled_buffer.resize(argb_size);
        scale_result = scale_argb(
            frame.plane_ptr(0), static_cast<int>(frame.width() * 4),
            static_cast<int>(frame.width()), static_cast<int>(frame.height()),
            scaled_buffer.data(), static_cast<int>(dst_width * 4),
            static_cast<int>(dst_width), static_cast<int>(dst_height));
        break;
      }
      case VideoPixelFormat::RGB:
      case VideoPixelFormat::BGR: {
        // RGB/BGR は直接スケーリングできないため、NV12 に変換してからスケーリング
        size_t src_i420_size = frame.width() * frame.height() * 3 / 2;
        std::vector<uint8_t> src_i420(src_i420_size);
        uint8_t* src_i420_y = src_i420.data();
        uint8_t* src_i420_u = src_i420_y + frame.width() * frame.height();
        uint8_t* src_i420_v =
            src_i420_u + (frame.width() / 2) * (frame.height() / 2);

        if (frame.format() == VideoPixelFormat::RGB) {
          libyuv::RGB24ToI420(frame.plane_ptr(0),
                              static_cast<int>(frame.width() * 3), src_i420_y,
                              static_cast<int>(frame.width()), src_i420_u,
                              static_cast<int>(frame.width() / 2), src_i420_v,
                              static_cast<int>(frame.width() / 2),
                              static_cast<int>(frame.width()),
                              static_cast<int>(frame.height()));
        } else {
          libyuv::RAWToI420(frame.plane_ptr(0),
                            static_cast<int>(frame.width() * 3), src_i420_y,
                            static_cast<int>(frame.width()), src_i420_u,
                            static_cast<int>(frame.width() / 2), src_i420_v,
                            static_cast<int>(frame.width() / 2),
                            static_cast<int>(frame.width()),
                            static_cast<int>(frame.height()));
        }

        // I420 -> NV12
        size_t src_nv12_size = frame.width() * frame.height() * 3 / 2;
        std::vector<uint8_t> src_nv12(src_nv12_size);
        uint8_t* src_nv12_y = src_nv12.data();
        uint8_t* src_nv12_uv = src_nv12_y + frame.width() * frame.height();

        libyuv::I420ToNV12(src_i420_y, static_cast<int>(frame.width()),
                           src_i420_u, static_cast<int>(frame.width() / 2),
                           src_i420_v, static_cast<int>(frame.width() / 2),
                           src_nv12_y, static_cast<int>(frame.width()),
                           src_nv12_uv, static_cast<int>(frame.width()),
                           static_cast<int>(frame.width()),
                           static_cast<int>(frame.height()));

        // NV12 でスケーリング
        scaled_buffer.resize(nv12_size);
        scale_result = scale_nv12(
            src_nv12_y, static_cast<int>(frame.width()), src_nv12_uv,
            static_cast<int>(frame.width()), static_cast<int>(frame.width()),
            static_cast<int>(frame.height()), scaled_buffer.data(),
            static_cast<int>(dst_width),
            scaled_buffer.data() + dst_width * dst_height,
            static_cast<int>(dst_width), static_cast<int>(dst_width),
            static_cast<int>(dst_height));

        // スケーリング後は NV12
        current_format = VideoPixelFormat::NV12;
        break;
      }
    }

    if (scale_result != 0) {
      throw std::runtime_error("libyuv scale failed");
    }

    current_width = dst_width;
    current_height = dst_height;
  }

  // 2. NV12 に変換
  if (current_format == VideoPixelFormat::NV12) {
    // スケーリング済みの NV12 バッファをそのまま使用
    result.buffer = std::move(scaled_buffer);
    result.y = result.buffer.data();
    result.uv = result.buffer.data() + current_width * current_height;
  } else {
    // NV12 以外のフォーマットは変換が必要
    result.buffer.resize(nv12_size);
    uint8_t* dst_y = result.buffer.data();
    uint8_t* dst_uv = dst_y + current_width * current_height;

    const uint8_t* src_data =
        needs_scaling ? scaled_buffer.data() : frame.plane_ptr(0);

    switch (current_format) {
      case VideoPixelFormat::I420: {
        size_t y_size = current_width * current_height;
        size_t uv_size = (current_width / 2) * (current_height / 2);
        const uint8_t* src_u =
            needs_scaling ? scaled_buffer.data() + y_size : frame.plane_ptr(1);
        const uint8_t* src_v = needs_scaling
                                   ? scaled_buffer.data() + y_size + uv_size
                                   : frame.plane_ptr(2);
        libyuv::I420ToNV12(src_data, static_cast<int>(current_width), src_u,
                           static_cast<int>(current_width / 2), src_v,
                           static_cast<int>(current_width / 2), dst_y,
                           static_cast<int>(current_width), dst_uv,
                           static_cast<int>(current_width),
                           static_cast<int>(current_width),
                           static_cast<int>(current_height));
        break;
      }
      case VideoPixelFormat::I422: {
        size_t y_size = current_width * current_height;
        size_t uv_size = (current_width / 2) * current_height;
        const uint8_t* src_u =
            needs_scaling ? scaled_buffer.data() + y_size : frame.plane_ptr(1);
        const uint8_t* src_v = needs_scaling
                                   ? scaled_buffer.data() + y_size + uv_size
                                   : frame.plane_ptr(2);
        // I422 -> I420 -> NV12
        size_t i420_size = current_width * current_height * 3 / 2;
        std::vector<uint8_t> i420_tmp(i420_size);
        uint8_t* i420_y = i420_tmp.data();
        uint8_t* i420_u = i420_y + current_width * current_height;
        uint8_t* i420_v = i420_u + (current_width / 2) * (current_height / 2);
        libyuv::I422ToI420(src_data, static_cast<int>(current_width), src_u,
                           static_cast<int>(current_width / 2), src_v,
                           static_cast<int>(current_width / 2), i420_y,
                           static_cast<int>(current_width), i420_u,
                           static_cast<int>(current_width / 2), i420_v,
                           static_cast<int>(current_width / 2),
                           static_cast<int>(current_width),
                           static_cast<int>(current_height));
        libyuv::I420ToNV12(i420_y, static_cast<int>(current_width), i420_u,
                           static_cast<int>(current_width / 2), i420_v,
                           static_cast<int>(current_width / 2), dst_y,
                           static_cast<int>(current_width), dst_uv,
                           static_cast<int>(current_width),
                           static_cast<int>(current_width),
                           static_cast<int>(current_height));
        break;
      }
      case VideoPixelFormat::I444: {
        size_t plane_size = current_width * current_height;
        const uint8_t* src_u = needs_scaling ? scaled_buffer.data() + plane_size
                                             : frame.plane_ptr(1);
        const uint8_t* src_v = needs_scaling
                                   ? scaled_buffer.data() + plane_size * 2
                                   : frame.plane_ptr(2);
        // I444 -> I420 -> NV12
        size_t i420_size = current_width * current_height * 3 / 2;
        std::vector<uint8_t> i420_tmp(i420_size);
        uint8_t* i420_y = i420_tmp.data();
        uint8_t* i420_u = i420_y + current_width * current_height;
        uint8_t* i420_v = i420_u + (current_width / 2) * (current_height / 2);
        libyuv::I444ToI420(src_data, static_cast<int>(current_width), src_u,
                           static_cast<int>(current_width), src_v,
                           static_cast<int>(current_width), i420_y,
                           static_cast<int>(current_width), i420_u,
                           static_cast<int>(current_width / 2), i420_v,
                           static_cast<int>(current_width / 2),
                           static_cast<int>(current_width),
                           static_cast<int>(current_height));
        libyuv::I420ToNV12(i420_y, static_cast<int>(current_width), i420_u,
                           static_cast<int>(current_width / 2), i420_v,
                           static_cast<int>(current_width / 2), dst_y,
                           static_cast<int>(current_width), dst_uv,
                           static_cast<int>(current_width),
                           static_cast<int>(current_width),
                           static_cast<int>(current_height));
        break;
      }
      case VideoPixelFormat::RGBA: {
        // RGBA -> I420 -> NV12
        size_t i420_size = current_width * current_height * 3 / 2;
        std::vector<uint8_t> i420_tmp(i420_size);
        uint8_t* i420_y = i420_tmp.data();
        uint8_t* i420_u = i420_y + current_width * current_height;
        uint8_t* i420_v = i420_u + (current_width / 2) * (current_height / 2);
        libyuv::ABGRToI420(src_data, static_cast<int>(current_width * 4),
                           i420_y, static_cast<int>(current_width), i420_u,
                           static_cast<int>(current_width / 2), i420_v,
                           static_cast<int>(current_width / 2),
                           static_cast<int>(current_width),
                           static_cast<int>(current_height));
        libyuv::I420ToNV12(i420_y, static_cast<int>(current_width), i420_u,
                           static_cast<int>(current_width / 2), i420_v,
                           static_cast<int>(current_width / 2), dst_y,
                           static_cast<int>(current_width), dst_uv,
                           static_cast<int>(current_width),
                           static_cast<int>(current_width),
                           static_cast<int>(current_height));
        break;
      }
      case VideoPixelFormat::BGRA: {
        // BGRA -> I420 -> NV12
        size_t i420_size = current_width * current_height * 3 / 2;
        std::vector<uint8_t> i420_tmp(i420_size);
        uint8_t* i420_y = i420_tmp.data();
        uint8_t* i420_u = i420_y + current_width * current_height;
        uint8_t* i420_v = i420_u + (current_width / 2) * (current_height / 2);
        libyuv::ARGBToI420(src_data, static_cast<int>(current_width * 4),
                           i420_y, static_cast<int>(current_width), i420_u,
                           static_cast<int>(current_width / 2), i420_v,
                           static_cast<int>(current_width / 2),
                           static_cast<int>(current_width),
                           static_cast<int>(current_height));
        libyuv::I420ToNV12(i420_y, static_cast<int>(current_width), i420_u,
                           static_cast<int>(current_width / 2), i420_v,
                           static_cast<int>(current_width / 2), dst_y,
                           static_cast<int>(current_width), dst_uv,
                           static_cast<int>(current_width),
                           static_cast<int>(current_width),
                           static_cast<int>(current_height));
        break;
      }
      case VideoPixelFormat::RGB: {
        // RGB -> I420 -> NV12
        size_t i420_size = current_width * current_height * 3 / 2;
        std::vector<uint8_t> i420_tmp(i420_size);
        uint8_t* i420_y = i420_tmp.data();
        uint8_t* i420_u = i420_y + current_width * current_height;
        uint8_t* i420_v = i420_u + (current_width / 2) * (current_height / 2);
        libyuv::RGB24ToI420(src_data, static_cast<int>(current_width * 3),
                            i420_y, static_cast<int>(current_width), i420_u,
                            static_cast<int>(current_width / 2), i420_v,
                            static_cast<int>(current_width / 2),
                            static_cast<int>(current_width),
                            static_cast<int>(current_height));
        libyuv::I420ToNV12(i420_y, static_cast<int>(current_width), i420_u,
                           static_cast<int>(current_width / 2), i420_v,
                           static_cast<int>(current_width / 2), dst_y,
                           static_cast<int>(current_width), dst_uv,
                           static_cast<int>(current_width),
                           static_cast<int>(current_width),
                           static_cast<int>(current_height));
        break;
      }
      case VideoPixelFormat::BGR: {
        // BGR -> I420 -> NV12
        size_t i420_size = current_width * current_height * 3 / 2;
        std::vector<uint8_t> i420_tmp(i420_size);
        uint8_t* i420_y = i420_tmp.data();
        uint8_t* i420_u = i420_y + current_width * current_height;
        uint8_t* i420_v = i420_u + (current_width / 2) * (current_height / 2);
        libyuv::RAWToI420(src_data, static_cast<int>(current_width * 3), i420_y,
                          static_cast<int>(current_width), i420_u,
                          static_cast<int>(current_width / 2), i420_v,
                          static_cast<int>(current_width / 2),
                          static_cast<int>(current_width),
                          static_cast<int>(current_height));
        libyuv::I420ToNV12(i420_y, static_cast<int>(current_width), i420_u,
                           static_cast<int>(current_width / 2), i420_v,
                           static_cast<int>(current_width / 2), dst_y,
                           static_cast<int>(current_width), dst_uv,
                           static_cast<int>(current_width),
                           static_cast<int>(current_width),
                           static_cast<int>(current_height));
        break;
      }
      default:
        throw std::runtime_error(
            "Unsupported pixel format for NV12 conversion");
    }

    result.y = dst_y;
    result.uv = dst_uv;
  }

  return result;
}

}  // namespace video_scaler
