#include "video_decoder.h"

#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include <CoreVideo/CoreVideo.h>
#include <VideoToolbox/VideoToolbox.h>
#include <nanobind/nanobind.h>
#include <cstring>
#include <memory>
#include <vector>

#include "encoded_video_chunk.h"
#include "video_frame.h"

namespace nb = nanobind;

namespace {
struct VTDecodeRef {
  VideoDecoder* self;
  uint64_t sequence;
  int64_t timestamp;  // usec
};

static void vt_decode_callback(void* decompressionOutputRefCon,
                               void* sourceFrameRefCon,
                               OSStatus status,
                               VTDecodeInfoFlags infoFlags,
                               CVImageBufferRef imageBuffer,
                               CMTime presentationTimeStamp,
                               CMTime presentationDuration) {
  (void)decompressionOutputRefCon;
  (void)presentationDuration;
  (void)presentationTimeStamp;

  if (!sourceFrameRefCon)
    return;

  // エラーチェック（GIL不要）
  if (status != noErr || !imageBuffer) {
    delete reinterpret_cast<VTDecodeRef*>(sourceFrameRefCon);
    return;
  }

  if (infoFlags & kVTDecodeInfo_FrameDropped) {
    delete reinterpret_cast<VTDecodeRef*>(sourceFrameRefCon);
    return;
  }

  // refからデータを取得
  std::unique_ptr<VTDecodeRef> ref(
      reinterpret_cast<VTDecodeRef*>(sourceFrameRefCon));
  VideoDecoder* self = ref->self;
  const uint64_t sequence = ref->sequence;
  const int64_t timestamp = ref->timestamp;

  // CVPixelBuffer から VideoFrame に変換
  CVPixelBufferRef pb = imageBuffer;
  CVPixelBufferLockBaseAddress(pb, kCVPixelBufferLock_ReadOnly);

  size_t width = CVPixelBufferGetWidth(pb);
  size_t height = CVPixelBufferGetHeight(pb);

  // NV12 フォーマットと仮定
  uint8_t* y_plane = (uint8_t*)CVPixelBufferGetBaseAddressOfPlane(pb, 0);
  size_t y_stride = CVPixelBufferGetBytesPerRowOfPlane(pb, 0);
  uint8_t* uv_plane = (uint8_t*)CVPixelBufferGetBaseAddressOfPlane(pb, 1);
  size_t uv_stride = CVPixelBufferGetBytesPerRowOfPlane(pb, 1);

  // VideoFrame を作成（GIL不要）
  auto frame = std::make_unique<VideoFrame>(width, height,
                                            VideoPixelFormat::NV12, timestamp);

  // Y プレーンをコピー
  const uint8_t* dst_y = frame->plane_ptr(0);
  uint8_t* dst_y_mut = const_cast<uint8_t*>(dst_y);
  for (size_t row = 0; row < height; ++row) {
    memcpy(dst_y_mut + row * width, y_plane + row * y_stride, width);
  }

  // UV プレーンをコピー
  const uint8_t* dst_uv = frame->plane_ptr(1);
  uint8_t* dst_uv_mut = const_cast<uint8_t*>(dst_uv);
  size_t chroma_height = (height + 1) / 2;
  size_t chroma_width = ((width + 1) / 2) * 2;  // 偶数幅
  for (size_t row = 0; row < chroma_height; ++row) {
    memcpy(dst_uv_mut + row * chroma_width, uv_plane + row * uv_stride,
           chroma_width);
  }

  CVPixelBufferUnlockBaseAddress(pb, kCVPixelBufferLock_ReadOnly);

  // handle_output を呼ぶ
  self->handle_output(sequence, std::move(frame));
}

// Annex B から AVCC/HVCC フォーマットに変換
static CMSampleBufferRef create_sample_buffer(
    const uint8_t* data,
    size_t size,
    CMVideoFormatDescriptionRef format_desc,
    int64_t timestamp,
    bool is_h264) {
  // Annex B のスタートコードを検索して NALU を抽出
  std::vector<uint8_t> avcc_data;
  std::vector<size_t> nalu_offsets;
  std::vector<size_t> nalu_sizes;

  size_t pos = 0;
  while (pos < size) {
    // スタートコード (0x00 0x00 0x00 0x01 または 0x00 0x00 0x01) を検索
    size_t start_code_len = 0;
    if (pos + 4 <= size && data[pos] == 0 && data[pos + 1] == 0 &&
        data[pos + 2] == 0 && data[pos + 3] == 1) {
      start_code_len = 4;
    } else if (pos + 3 <= size && data[pos] == 0 && data[pos + 1] == 0 &&
               data[pos + 2] == 1) {
      start_code_len = 3;
    }

    if (start_code_len > 0) {
      pos += start_code_len;
      size_t nalu_start = pos;

      // 次のスタートコードまたはデータの終端を検索
      size_t nalu_end = size;
      for (size_t i = pos; i < size - 2; ++i) {
        if (data[i] == 0 && data[i + 1] == 0 &&
            (data[i + 2] == 1 ||
             (i + 3 < size && data[i + 2] == 0 && data[i + 3] == 1))) {
          nalu_end = i;
          break;
        }
      }

      size_t nalu_size = nalu_end - nalu_start;
      if (nalu_size > 0) {
        // NALUタイプを確認
        uint8_t nalu_type;
        if (is_h264) {
          nalu_type = data[nalu_start] & 0x1F;
          // H.264: SPS(7), PPS(8)はスキップ
          if (nalu_type == 7 || nalu_type == 8) {
            pos = nalu_end;
            continue;
          }
        } else {
          nalu_type = (data[nalu_start] >> 1) & 0x3F;
          // H.265: VPS(32), SPS(33), PPS(34)はスキップ
          if (nalu_type == 32 || nalu_type == 33 || nalu_type == 34) {
            pos = nalu_end;
            continue;
          }
        }

        // 4バイトのサイズプレフィックスを追加 (ビッグエンディアン)
        uint32_t be_size = CFSwapInt32HostToBig(nalu_size);
        avcc_data.insert(avcc_data.end(), (uint8_t*)&be_size,
                         (uint8_t*)&be_size + 4);
        nalu_offsets.push_back(avcc_data.size());
        avcc_data.insert(avcc_data.end(), data + nalu_start, data + nalu_end);
        nalu_sizes.push_back(nalu_size);
      }

      pos = nalu_end;
    } else {
      pos++;
    }
  }

  if (avcc_data.empty()) {
    return nullptr;
  }

  // CMBlockBuffer を作成（データをコピー）
  CMBlockBufferRef block_buffer = nullptr;
  // データをコピーするために、allocatorをkCFAllocatorDefaultに変更
  void* buffer_data = malloc(avcc_data.size());
  memcpy(buffer_data, avcc_data.data(), avcc_data.size());

  OSStatus status = CMBlockBufferCreateWithMemoryBlock(
      kCFAllocatorDefault, buffer_data, avcc_data.size(), kCFAllocatorDefault,
      nullptr, 0, avcc_data.size(), 0, &block_buffer);

  if (status != noErr || !block_buffer) {
    return nullptr;
  }

  // CMSampleBuffer を作成
  CMSampleBufferRef sample_buffer = nullptr;
  CMTime pts = CMTimeMake(timestamp, 1000000);
  CMTime duration = kCMTimeInvalid;
  CMSampleTimingInfo timing = {duration, pts, pts};
  size_t sample_size = avcc_data.size();

  status =
      CMSampleBufferCreateReady(kCFAllocatorDefault, block_buffer, format_desc,
                                1, 1, &timing, 1, &sample_size, &sample_buffer);

  CFRelease(block_buffer);

  if (status != noErr || !sample_buffer) {
    return nullptr;
  }

  return sample_buffer;
}

// パラメーターセットを抽出してフォーマット記述子を作成
static CMVideoFormatDescriptionRef
create_format_description(const uint8_t* data, size_t size, bool is_h264) {
  std::vector<const uint8_t*> param_sets;
  std::vector<size_t> param_sizes;

  // Annex B から SPS/PPS (H.264) または VPS/SPS/PPS (H.265) を抽出
  size_t pos = 0;
  while (pos < size) {
    size_t start_code_len = 0;
    if (pos + 4 <= size && data[pos] == 0 && data[pos + 1] == 0 &&
        data[pos + 2] == 0 && data[pos + 3] == 1) {
      start_code_len = 4;
    } else if (pos + 3 <= size && data[pos] == 0 && data[pos + 1] == 0 &&
               data[pos + 2] == 1) {
      start_code_len = 3;
    }

    if (start_code_len > 0) {
      pos += start_code_len;
      if (pos >= size)
        break;

      // NALU タイプを確認
      uint8_t nalu_type;
      if (is_h264) {
        nalu_type = data[pos] & 0x1F;
        // H.264: SPS = 7, PPS = 8
        if (nalu_type == 7 || nalu_type == 8) {
          size_t nalu_start = pos;
          size_t nalu_end = size;

          // 次のスタートコードを検索
          for (size_t i = pos + 1; i < size - 2; ++i) {
            if (data[i] == 0 && data[i + 1] == 0 &&
                (data[i + 2] == 1 ||
                 (i + 3 < size && data[i + 2] == 0 && data[i + 3] == 1))) {
              nalu_end = i;
              break;
            }
          }

          param_sets.push_back(data + nalu_start);
          param_sizes.push_back(nalu_end - nalu_start);
          pos = nalu_end;
        } else {
          pos++;
        }
      } else {
        // H.265
        nalu_type = (data[pos] >> 1) & 0x3F;
        // H.265: VPS = 32, SPS = 33, PPS = 34
        if (nalu_type == 32 || nalu_type == 33 || nalu_type == 34) {
          size_t nalu_start = pos;
          size_t nalu_end = size;

          for (size_t i = pos + 1; i < size - 2; ++i) {
            if (data[i] == 0 && data[i + 1] == 0 &&
                (data[i + 2] == 1 ||
                 (i + 3 < size && data[i + 2] == 0 && data[i + 3] == 1))) {
              nalu_end = i;
              break;
            }
          }

          param_sets.push_back(data + nalu_start);
          param_sizes.push_back(nalu_end - nalu_start);
          pos = nalu_end;
        } else {
          pos++;
        }
      }
    } else {
      pos++;
    }
  }

  if (param_sets.empty()) {
    return nullptr;
  }

  CMVideoFormatDescriptionRef format_desc = nullptr;
  OSStatus status;

  if (is_h264) {
    status = CMVideoFormatDescriptionCreateFromH264ParameterSets(
        kCFAllocatorDefault, param_sets.size(), param_sets.data(),
        param_sizes.data(), 4, &format_desc);
  } else {
    status = CMVideoFormatDescriptionCreateFromHEVCParameterSets(
        kCFAllocatorDefault, param_sets.size(), param_sets.data(),
        param_sizes.data(), 4, nullptr, &format_desc);
  }

  if (status != noErr) {
    return nullptr;
  }

  return format_desc;
}
}  // namespace

void VideoDecoder::init_videotoolbox_decoder() {
  if (vt_session_)
    return;

  bool is_h264 =
      (config_.codec.length() >= 5 && (config_.codec.substr(0, 5) == "avc1." ||
                                       config_.codec.substr(0, 5) == "avc3."));
  bool is_h265 =
      (config_.codec.length() >= 5 && (config_.codec.substr(0, 5) == "hvc1." ||
                                       config_.codec.substr(0, 5) == "hev1."));

  if (!is_h264 && !is_h265) {
    throw std::runtime_error("VideoToolbox supports only H.264/H.265");
  }

  // デコーダー仕様辞書を作成
  const void* spec_keys[] = {
      kVTVideoDecoderSpecification_EnableHardwareAcceleratedVideoDecoder};
  const void* spec_vals[] = {kCFBooleanTrue};
  CFDictionaryRef decoder_spec = CFDictionaryCreate(
      kCFAllocatorDefault, spec_keys, spec_vals, 1,
      &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);

  // 出力画像バッファ属性: NV12
  OSType pf = kCVPixelFormatType_420YpCbCr8BiPlanarFullRange;
  CFNumberRef pf_num =
      CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &pf);
  CFDictionaryRef empty_dict = CFDictionaryCreate(
      kCFAllocatorDefault, nullptr, nullptr, 0, &kCFTypeDictionaryKeyCallBacks,
      &kCFTypeDictionaryValueCallBacks);
  const void* keys[] = {kCVPixelBufferIOSurfacePropertiesKey,
                        kCVPixelBufferPixelFormatTypeKey};
  const void* vals[] = {empty_dict, pf_num};
  CFDictionaryRef dest_attr = CFDictionaryCreate(
      kCFAllocatorDefault, keys, vals, 2, &kCFTypeDictionaryKeyCallBacks,
      &kCFTypeDictionaryValueCallBacks);

  CFRelease(decoder_spec);
  CFRelease(dest_attr);
  CFRelease(pf_num);
  CFRelease(empty_dict);

  // セッションは最初のフレームで作成
  vt_session_ = nullptr;
}

void VideoDecoder::cleanup_videotoolbox_decoder() {
  if (vt_session_) {
    VTDecompressionSessionRef s = (VTDecompressionSessionRef)vt_session_;
    VTDecompressionSessionInvalidate(s);
    CFRelease(s);
    vt_session_ = nullptr;
  }
}

bool VideoDecoder::decode_videotoolbox(const EncodedVideoChunk& chunk) {
  bool is_h264 =
      (config_.codec.length() >= 5 && (config_.codec.substr(0, 5) == "avc1." ||
                                       config_.codec.substr(0, 5) == "avc3."));
  bool is_h265 =
      (config_.codec.length() >= 5 && (config_.codec.substr(0, 5) == "hvc1." ||
                                       config_.codec.substr(0, 5) == "hev1."));

  if (!is_h264 && !is_h265) {
    if (error_callback_) {
      nb::gil_scoped_acquire gil;
      error_callback_("VideoToolbox supports only H.264/H.265");
    }
    return false;
  }

  const auto data = chunk.data_vector();

  // キーフレームの場合、フォーマット記述子を更新
  CMVideoFormatDescriptionRef format_desc = nullptr;
  if (chunk.type() == EncodedVideoChunkType::KEY) {
    format_desc = create_format_description(data.data(), data.size(), is_h264);
    if (!format_desc) {
      if (error_callback_) {
        nb::gil_scoped_acquire gil;
        error_callback_("Failed to create format description");
      }
      return false;
    }

    // 既存のセッションをクリーンアップ
    if (vt_session_) {
      VTDecompressionSessionRef old_session =
          (VTDecompressionSessionRef)vt_session_;
      VTDecompressionSessionInvalidate(old_session);
      CFRelease(old_session);
      vt_session_ = nullptr;
    }

    // 新しいセッションを作成
    // デコーダー仕様辞書を作成
    const void* spec_keys[] = {
        kVTVideoDecoderSpecification_EnableHardwareAcceleratedVideoDecoder};
    const void* spec_vals[] = {kCFBooleanTrue};
    CFDictionaryRef decoder_spec = CFDictionaryCreate(
        kCFAllocatorDefault, spec_keys, spec_vals, 1,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);

    // 出力画像バッファ属性: NV12
    OSType pf = kCVPixelFormatType_420YpCbCr8BiPlanarFullRange;
    CFNumberRef pf_num =
        CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &pf);
    CFDictionaryRef empty_dict = CFDictionaryCreate(
        kCFAllocatorDefault, nullptr, nullptr, 0,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    const void* keys[] = {kCVPixelBufferIOSurfacePropertiesKey,
                          kCVPixelBufferPixelFormatTypeKey};
    const void* vals[] = {empty_dict, pf_num};
    CFDictionaryRef dest_attr = CFDictionaryCreate(
        kCFAllocatorDefault, keys, vals, 2, &kCFTypeDictionaryKeyCallBacks,
        &kCFTypeDictionaryValueCallBacks);

    VTDecompressionOutputCallbackRecord callback = {vt_decode_callback, this};

    VTDecompressionSessionRef session = nullptr;
    OSStatus status = VTDecompressionSessionCreate(
        kCFAllocatorDefault, format_desc, decoder_spec, dest_attr, &callback,
        &session);

    CFRelease(decoder_spec);
    CFRelease(dest_attr);
    CFRelease(pf_num);
    CFRelease(empty_dict);
    CFRelease(format_desc);

    if (status != noErr || !session) {
      if (error_callback_) {
        nb::gil_scoped_acquire gil;
        error_callback_("Failed to create VTDecompressionSession");
      }
      return false;
    }

    // リアルタイムデコード設定
    VTSessionSetProperty(session, kVTDecompressionPropertyKey_RealTime,
                         kCFBooleanTrue);

    vt_session_ = session;
  }

  if (!vt_session_) {
    if (error_callback_) {
      nb::gil_scoped_acquire gil;
      error_callback_("VTDecompressionSession not initialized");
    }
    return false;
  }

  VTDecompressionSessionRef session = (VTDecompressionSessionRef)vt_session_;

  // サンプルバッファを作成するためのフォーマット記述子を取得
  // セッションが既に存在する場合は、ダミーのフォーマット記述子を使用
  CMVideoFormatDescriptionRef format_for_sample = nullptr;

  if (chunk.type() == EncodedVideoChunkType::KEY) {
    // キーフレームの場合、フォーマット記述子を作成
    format_for_sample =
        create_format_description(data.data(), data.size(), is_h264);
    if (!format_for_sample) {
      if (error_callback_) {
        nb::gil_scoped_acquire gil;
        error_callback_("Failed to create format description for keyframe");
      }
      return false;
    }
  } else {
    // デルタフレームの場合、簡易的なフォーマット記述子を作成
    if (is_h264) {
      CMVideoFormatDescriptionCreate(
          kCFAllocatorDefault, kCMVideoCodecType_H264,
          config_.coded_width.value_or(0), config_.coded_height.value_or(0),
          nullptr, &format_for_sample);
    } else {
      CMVideoFormatDescriptionCreate(
          kCFAllocatorDefault, kCMVideoCodecType_HEVC,
          config_.coded_width.value_or(0), config_.coded_height.value_or(0),
          nullptr, &format_for_sample);
    }
    if (!format_for_sample) {
      if (error_callback_) {
        nb::gil_scoped_acquire gil;
        error_callback_("Failed to create format description for delta frame");
      }
      return false;
    }
  }

  // サンプルバッファを作成
  CMSampleBufferRef sample_buffer = create_sample_buffer(
      data.data(), data.size(), format_for_sample, chunk.timestamp(), is_h264);

  CFRelease(format_for_sample);

  if (!sample_buffer) {
    if (error_callback_) {
      nb::gil_scoped_acquire gil;
      error_callback_("Failed to create sample buffer");
    }
    return false;
  }

  // デコードリクエストを送信
  auto* ref = new VTDecodeRef{this, current_sequence_, chunk.timestamp()};
  VTDecodeInfoFlags info_flags = 0;

  // VTDecompressionSessionDecodeFrame を呼び出す
  OSStatus status = VTDecompressionSessionDecodeFrame(
      session, sample_buffer, kVTDecodeFrame_EnableAsynchronousDecompression,
      ref, &info_flags);

  CFRelease(sample_buffer);

  if (status != noErr) {
    delete ref;
    if (error_callback_) {
      nb::gil_scoped_acquire gil;
      error_callback_("VTDecompressionSessionDecodeFrame failed");
    }
    return false;
  }

  return true;
}

void VideoDecoder::flush_videotoolbox() {
  if (!vt_session_) {
    return;
  }

  VTDecompressionSessionRef session = (VTDecompressionSessionRef)vt_session_;

  // バインディング層で既に GIL を解放しているため、ここでは解放しない
  VTDecompressionSessionWaitForAsynchronousFrames(session);
}

#endif  // defined(__APPLE__)
