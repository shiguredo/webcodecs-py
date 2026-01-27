// スケーリングヘルパー関数
// VideoFrame を指定サイズにスケーリングし、I420 または NV12 形式で返す

#pragma once

#include <cstdint>
#include <vector>

#include "video_frame.h"

namespace video_scaler {

// I420 形式のスケーリング結果
// AOM, VPX エンコーダー用
struct I420ScaleResult {
  // スケーリング用バッファ
  // スケーリング不要の場合は空
  std::vector<uint8_t> buffer;

  // 出力サイズ
  uint32_t width;
  uint32_t height;

  // Y, U, V プレーンへのポインタ
  // buffer が空の場合は元フレームのポインタ
  const uint8_t* y;
  const uint8_t* u;
  const uint8_t* v;

  // ストライド
  int stride_y;
  int stride_u;
  int stride_v;
};

// NV12 形式のスケーリング結果
// NVENC, Intel VPL エンコーダー用
struct NV12ScaleResult {
  // スケーリング用バッファ
  // スケーリング不要の場合は空
  std::vector<uint8_t> buffer;

  // 出力サイズ
  uint32_t width;
  uint32_t height;

  // Y, UV プレーンへのポインタ
  // buffer が空の場合は元フレームのポインタ
  const uint8_t* y;
  const uint8_t* uv;
};

// フレームを I420 形式にスケーリング/変換
// スケーリング不要かつ入力が I420 の場合は元フレームのポインタを返す
I420ScaleResult scale_to_i420(const VideoFrame& frame,
                              uint32_t dst_width,
                              uint32_t dst_height);

// フレームを NV12 形式にスケーリング/変換
// スケーリング不要かつ入力が NV12 の場合は元フレームのポインタを返す
NV12ScaleResult scale_to_nv12(const VideoFrame& frame,
                              uint32_t dst_width,
                              uint32_t dst_height);

}  // namespace video_scaler
