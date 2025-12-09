#pragma once

#if defined(__linux__)

#include <mfx.h>
#include <cstdint>
#include <queue>
#include <stdexcept>
#include <string>

namespace intel_vpl {

// 定数定義
static constexpr mfxU32 VPL_SYNC_TIMEOUT_MS = 60000;
static constexpr mfxU32 VPL_FLUSH_SYNC_TIMEOUT_MS = 1000;
static constexpr size_t VPL_INITIAL_BITSTREAM_BUFFER_SIZE = 1024 * 1024;
static constexpr size_t VPL_MIN_BITSTREAM_BUFFER_SIZE = 512 * 1024;

// GOP 設定
static constexpr mfxU16 VPL_GOP_SIZE = 120;
static constexpr mfxU16 VPL_GOP_REF_DIST = 1;
static constexpr mfxU16 VPL_IDR_INTERVAL = 0;

// アライメント関数
template <typename T>
inline T align16(T value) {
  return (value + 15) & ~15;
}

template <typename T>
inline T align32(T value) {
  return (value + 31) & ~31;
}

// コーデック ID を取得するヘルパー
inline mfxU32 get_codec_id(const std::string& codec) {
  if (codec.length() >= 5 &&
      (codec.substr(0, 5) == "avc1." || codec.substr(0, 5) == "avc3.")) {
    return MFX_CODEC_AVC;
  } else if (codec.length() >= 5 &&
             (codec.substr(0, 5) == "hvc1." || codec.substr(0, 5) == "hev1.")) {
    return MFX_CODEC_HEVC;
  } else if (codec.length() >= 4 && codec.substr(0, 4) == "av01") {
    return MFX_CODEC_AV1;
  }
  throw std::runtime_error("Unsupported codec for Intel VPL: " + codec);
}

// エラーメッセージを生成するヘルパー
inline std::string make_error_message(const std::string& operation,
                                      mfxStatus status) {
  return operation + " failed (status: " + std::to_string(status) + ")";
}

// サーフェスプール管理クラス
class SurfacePool {
 public:
  SurfacePool() = default;
  ~SurfacePool() { clear(); }

  // サーフェスを初期化
  void init(mfxU16 count,
            const mfxFrameInfo& frame_info,
            std::vector<uint8_t>& buffer) {
    clear();

    mfxU16 width = align32(frame_info.Width);
    mfxU16 height = align32(frame_info.Height);
    // NV12: 12 bits per pixel
    size_t surface_size = width * height * 12 / 8;
    buffer.resize(count * surface_size);

    for (mfxU16 i = 0; i < count; i++) {
      mfxFrameSurface1* surface = new mfxFrameSurface1{};
      std::memset(surface, 0, sizeof(mfxFrameSurface1));
      surface->Info = frame_info;
      surface->Data.Y = buffer.data() + i * surface_size;
      surface->Data.U = buffer.data() + i * surface_size + width * height;
      surface->Data.V = buffer.data() + i * surface_size + width * height + 1;
      surface->Data.Pitch = width;
      surfaces_.push_back(surface);
      free_indices_.push(i);
    }
  }

  // 未使用のサーフェスを取得 (O(1))
  mfxFrameSurface1* acquire() {
    if (free_indices_.empty()) {
      return nullptr;
    }
    size_t index = free_indices_.front();
    free_indices_.pop();
    return surfaces_[index];
  }

  // サーフェスを解放 (O(1))
  void release(mfxFrameSurface1* surface) {
    for (size_t i = 0; i < surfaces_.size(); i++) {
      if (surfaces_[i] == surface) {
        free_indices_.push(i);
        break;
      }
    }
  }

  // 全サーフェスをクリア
  void clear() {
    for (mfxFrameSurface1* surface : surfaces_) {
      delete surface;
    }
    surfaces_.clear();
    while (!free_indices_.empty()) {
      free_indices_.pop();
    }
  }

  // 全サーフェスを取得 (flush 時用)
  const std::vector<mfxFrameSurface1*>& all() const { return surfaces_; }

 private:
  std::vector<mfxFrameSurface1*> surfaces_;
  std::queue<size_t> free_indices_;
};

}  // namespace intel_vpl

#endif  // defined(__linux__)
