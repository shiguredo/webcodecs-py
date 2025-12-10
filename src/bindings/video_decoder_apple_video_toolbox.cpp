#include "video_decoder.h"

#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include <CoreMedia/CoreMedia.h>
#include <CoreVideo/CoreVideo.h>
#include <VideoToolbox/VideoToolbox.h>
#include <nanobind/nanobind.h>
#include <cstring>
#include <memory>
#include <vector>

#include "encoded_video_chunk.h"
#include "video_frame.h"

namespace nb = nanobind;

// VP9 コーデックタイプ
#ifndef kCMVideoCodecType_VP9
#define kCMVideoCodecType_VP9 'vp09'
#endif

// AV1 コーデックタイプ
#ifndef kCMVideoCodecType_AV1
#define kCMVideoCodecType_AV1 'av01'
#endif

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
  // ストライドが width と等しければ一括コピー
  if (y_stride == width) {
    memcpy(dst_y_mut, y_plane, width * height);
  } else {
    for (size_t row = 0; row < height; ++row) {
      memcpy(dst_y_mut + row * width, y_plane + row * y_stride, width);
    }
  }

  // UV プレーンをコピー
  const uint8_t* dst_uv = frame->plane_ptr(1);
  uint8_t* dst_uv_mut = const_cast<uint8_t*>(dst_uv);
  size_t chroma_height = (height + 1) / 2;
  size_t chroma_width = ((width + 1) / 2) * 2;  // 偶数幅
  // ストライドが chroma_width と等しければ一括コピー
  if (uv_stride == chroma_width) {
    memcpy(dst_uv_mut, uv_plane, chroma_width * chroma_height);
  } else {
    for (size_t row = 0; row < chroma_height; ++row) {
      memcpy(dst_uv_mut + row * chroma_width, uv_plane + row * uv_stride,
             chroma_width);
    }
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
  // 入力サイズと同程度を事前確保（再割り当てを減らす）
  avcc_data.reserve(size);
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
// VP9 フレームヘッダーからプロファイルとビット深度を取得
static bool parse_vp9_frame_header(const uint8_t* data,
                                   size_t size,
                                   uint8_t* profile,
                                   uint8_t* bit_depth,
                                   uint32_t* width,
                                   uint32_t* height) {
  if (size < 3) {
    return false;
  }

  // VP9 フレームマーカー (最初の2ビット = 0b10)
  uint8_t frame_marker = (data[0] >> 6) & 0x03;
  if (frame_marker != 0x02) {
    return false;
  }

  // プロファイル (ビット2-3)
  *profile = (data[0] >> 4) & 0x03;
  if (*profile == 3) {
    // Profile 3 の場合、追加ビットがある
    *profile += (data[0] >> 3) & 0x01;
  }

  // ビット深度
  if (*profile >= 2) {
    *bit_depth = ((data[0] >> 2) & 0x01) ? 12 : 10;
  } else {
    *bit_depth = 8;
  }

  // 解像度は設定から取得するため、ここでは簡易的に 0 を返す
  // 実際のパースは複雑なため、config から取得する
  *width = 0;
  *height = 0;

  return true;
}

// VP9 用のフォーマット記述子を作成
static CMVideoFormatDescriptionRef create_vp9_format_description(
    const uint8_t* data,
    size_t size,
    uint32_t width,
    uint32_t height) {
  // VP9 フレームヘッダーを解析
  uint8_t profile = 0;
  uint8_t bit_depth = 8;
  uint32_t parsed_width = 0;
  uint32_t parsed_height = 0;

  parse_vp9_frame_header(data, size, &profile, &bit_depth, &parsed_width,
                         &parsed_height);

  // vpcC ボックスを構築
  // ISO/IEC 14496-15 Section 7.6.6
  std::vector<uint8_t> vpcc_data;
  vpcc_data.push_back(1);  // version
  vpcc_data.push_back(0);  // flags (3 bytes)
  vpcc_data.push_back(0);
  vpcc_data.push_back(0);
  vpcc_data.push_back(profile);  // profile

  // コーデック文字列からレベルを取得 (vp09.PP.LL.DD の LL 部分)
  // デフォルトは 10 (Level 1.0)
  vpcc_data.push_back(10);  // level

  // bitDepth (4 bits) | chromaSubsampling (3 bits) | videoFullRangeFlag (1 bit)
  // chromaSubsampling: 0 = 4:2:0 vertical, 1 = 4:2:0 collocated
  // 通常の VP9 は 4:2:0 collocated (値 = 1) を使用
  uint8_t chroma_subsampling = 1;  // 4:2:0 collocated
  uint8_t video_full_range = 0;    // limited range
  vpcc_data.push_back((bit_depth << 4) | (chroma_subsampling << 1) |
                      video_full_range);

  // カラーメタデータ
  // 1 = BT.709
  vpcc_data.push_back(1);  // colourPrimaries
  vpcc_data.push_back(1);  // transferCharacteristics
  vpcc_data.push_back(1);  // matrixCoefficients

  // codecInitializationDataSize は VP9 では常に 0
  vpcc_data.push_back(0);  // codecInitializationDataSize (2 bytes, big endian)
  vpcc_data.push_back(0);

  // 拡張辞書を作成
  CFMutableDictionaryRef extensions = CFDictionaryCreateMutable(
      kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks,
      &kCFTypeDictionaryValueCallBacks);

  // vpcC アトムを追加
  CFDataRef vpcc_cf =
      CFDataCreate(kCFAllocatorDefault, vpcc_data.data(), vpcc_data.size());
  CFMutableDictionaryRef atoms = CFDictionaryCreateMutable(
      kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks,
      &kCFTypeDictionaryValueCallBacks);
  CFDictionarySetValue(atoms, CFSTR("vpcC"), vpcc_cf);
  CFDictionarySetValue(
      extensions, kCMFormatDescriptionExtension_SampleDescriptionExtensionAtoms,
      atoms);

  // CMVideoFormatDescription を作成
  CMVideoFormatDescriptionRef format_desc = nullptr;
  OSStatus status =
      CMVideoFormatDescriptionCreate(kCFAllocatorDefault, kCMVideoCodecType_VP9,
                                     width, height, extensions, &format_desc);

  CFRelease(vpcc_cf);
  CFRelease(atoms);
  CFRelease(extensions);

  return (status == noErr) ? format_desc : nullptr;
}

// VP9 用のサンプルバッファを作成
static CMSampleBufferRef create_vp9_sample_buffer(
    const uint8_t* data,
    size_t size,
    CMVideoFormatDescriptionRef format_desc,
    int64_t timestamp) {
  // CMBlockBuffer を作成（データをコピー）
  CMBlockBufferRef block_buffer = nullptr;
  void* buffer_data = malloc(size);
  memcpy(buffer_data, data, size);

  OSStatus status = CMBlockBufferCreateWithMemoryBlock(
      kCFAllocatorDefault, buffer_data, size, kCFAllocatorDefault, nullptr, 0,
      size, 0, &block_buffer);

  if (status != noErr || !block_buffer) {
    return nullptr;
  }

  // CMSampleBuffer を作成
  CMSampleBufferRef sample_buffer = nullptr;
  CMTime pts = CMTimeMake(timestamp, 1000000);
  CMTime duration = kCMTimeInvalid;
  CMSampleTimingInfo timing = {duration, pts, pts};
  size_t sample_size = size;

  status =
      CMSampleBufferCreateReady(kCFAllocatorDefault, block_buffer, format_desc,
                                1, 1, &timing, 1, &sample_size, &sample_buffer);

  CFRelease(block_buffer);

  if (status != noErr || !sample_buffer) {
    return nullptr;
  }

  return sample_buffer;
}

// AV1 OBU からシーケンスヘッダーを解析
static bool parse_av1_sequence_header_obu(const uint8_t* data,
                                          size_t size,
                                          uint8_t* profile,
                                          uint8_t* level,
                                          uint8_t* bit_depth,
                                          bool* mono_chrome,
                                          uint8_t* chroma_subsampling_x,
                                          uint8_t* chroma_subsampling_y) {
  if (size < 2) {
    return false;
  }

  // OBU ヘッダーを解析
  size_t pos = 0;
  while (pos < size) {
    if (pos >= size) {
      return false;
    }

    uint8_t obu_header = data[pos];
    uint8_t obu_type = (obu_header >> 3) & 0x0F;
    bool obu_extension_flag = (obu_header >> 2) & 0x01;
    bool obu_has_size_field = (obu_header >> 1) & 0x01;
    pos++;

    // 拡張ヘッダーをスキップ
    if (obu_extension_flag) {
      pos++;
    }

    // OBU サイズを読み取り
    size_t obu_size = 0;
    if (obu_has_size_field) {
      // leb128 デコード
      uint8_t byte;
      int shift = 0;
      do {
        if (pos >= size) {
          return false;
        }
        byte = data[pos++];
        obu_size |= (size_t)(byte & 0x7F) << shift;
        shift += 7;
      } while (byte & 0x80);
    } else {
      obu_size = size - pos;
    }

    // OBU_SEQUENCE_HEADER (タイプ 1) を探す
    if (obu_type == 1) {
      // シーケンスヘッダー OBU を解析
      if (pos + obu_size > size || obu_size < 3) {
        return false;
      }

      const uint8_t* seq_data = data + pos;

      // seq_profile (3 bits)
      *profile = (seq_data[0] >> 5) & 0x07;

      // still_picture (1 bit)
      // reduced_still_picture_header (1 bit)
      bool reduced_still_picture_header = (seq_data[0] >> 3) & 0x01;

      if (reduced_still_picture_header) {
        // seq_level_idx[0] (5 bits)
        *level = seq_data[0] & 0x1F;
      } else {
        // 複雑なケースは簡易的にデフォルト値を使用
        *level = 8;  // Level 4.0
      }

      // ビット深度とモノクローム情報（簡易的に）
      if (*profile == 2) {
        *bit_depth = 10;
      } else {
        *bit_depth = 8;
      }
      *mono_chrome = false;
      *chroma_subsampling_x = 1;
      *chroma_subsampling_y = 1;

      return true;
    }

    pos += obu_size;
  }

  return false;
}

// AV1 用のフォーマット記述子を作成
static CMVideoFormatDescriptionRef create_av1_format_description(
    const uint8_t* data,
    size_t size,
    uint32_t width,
    uint32_t height) {
  // AV1 シーケンスヘッダー OBU を解析
  uint8_t profile = 0;
  uint8_t level = 8;
  uint8_t bit_depth = 8;
  bool mono_chrome = false;
  uint8_t chroma_subsampling_x = 1;
  uint8_t chroma_subsampling_y = 1;

  parse_av1_sequence_header_obu(data, size, &profile, &level, &bit_depth,
                                &mono_chrome, &chroma_subsampling_x,
                                &chroma_subsampling_y);

  // av1C ボックスを構築
  // ISO/IEC 14496-15 Section 11.2.3.1
  std::vector<uint8_t> av1c_data;
  av1c_data.push_back(0x81);  // marker (1) | version (1)

  // seq_profile (3 bits) | seq_level_idx_0 (5 bits)
  av1c_data.push_back((profile << 5) | (level & 0x1F));

  // seq_tier_0 (1 bit) | high_bitdepth (1 bit) | twelve_bit (1 bit) |
  // monochrome (1 bit) | chroma_subsampling_x (1 bit) |
  // chroma_subsampling_y (1 bit) | chroma_sample_position (2 bits)
  uint8_t byte3 = 0;
  byte3 |= (bit_depth > 8 ? 1 : 0) << 6;    // high_bitdepth
  byte3 |= (bit_depth == 12 ? 1 : 0) << 5;  // twelve_bit
  byte3 |= (mono_chrome ? 1 : 0) << 4;      // monochrome
  byte3 |= (chroma_subsampling_x & 0x01) << 3;
  byte3 |= (chroma_subsampling_y & 0x01) << 2;
  byte3 |= 0;  // chroma_sample_position
  av1c_data.push_back(byte3);

  // initial_presentation_delay_present (1 bit) | reserved (3 bits) |
  // initial_presentation_delay_minus_one (4 bits) or reserved
  av1c_data.push_back(0);

  // configOBUs (シーケンスヘッダー OBU を含める)
  // OBU_SEQUENCE_HEADER を探してコピー
  size_t pos = 0;
  while (pos < size) {
    if (pos >= size)
      break;

    uint8_t obu_header = data[pos];
    uint8_t obu_type = (obu_header >> 3) & 0x0F;
    bool obu_extension_flag = (obu_header >> 2) & 0x01;
    bool obu_has_size_field = (obu_header >> 1) & 0x01;

    size_t obu_start = pos;
    pos++;

    if (obu_extension_flag) {
      pos++;
    }

    size_t obu_size = 0;
    if (obu_has_size_field) {
      uint8_t byte;
      int shift = 0;
      do {
        if (pos >= size)
          break;
        byte = data[pos++];
        obu_size |= (size_t)(byte & 0x7F) << shift;
        shift += 7;
      } while (byte & 0x80);
    } else {
      obu_size = size - pos;
    }

    if (obu_type == 1) {
      // シーケンスヘッダー OBU をコピー
      size_t total_obu_size = pos - obu_start + obu_size;
      for (size_t i = 0; i < total_obu_size && (obu_start + i) < size; ++i) {
        av1c_data.push_back(data[obu_start + i]);
      }
      break;
    }

    pos += obu_size;
  }

  // 拡張辞書を作成
  CFMutableDictionaryRef extensions = CFDictionaryCreateMutable(
      kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks,
      &kCFTypeDictionaryValueCallBacks);

  // av1C アトムを追加
  CFDataRef av1c_cf =
      CFDataCreate(kCFAllocatorDefault, av1c_data.data(), av1c_data.size());
  CFMutableDictionaryRef atoms = CFDictionaryCreateMutable(
      kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks,
      &kCFTypeDictionaryValueCallBacks);
  CFDictionarySetValue(atoms, CFSTR("av1C"), av1c_cf);
  CFDictionarySetValue(
      extensions, kCMFormatDescriptionExtension_SampleDescriptionExtensionAtoms,
      atoms);

  // CMVideoFormatDescription を作成
  CMVideoFormatDescriptionRef format_desc = nullptr;
  OSStatus status =
      CMVideoFormatDescriptionCreate(kCFAllocatorDefault, kCMVideoCodecType_AV1,
                                     width, height, extensions, &format_desc);

  CFRelease(av1c_cf);
  CFRelease(atoms);
  CFRelease(extensions);

  return (status == noErr) ? format_desc : nullptr;
}

// AV1 用のサンプルバッファを作成
static CMSampleBufferRef create_av1_sample_buffer(
    const uint8_t* data,
    size_t size,
    CMVideoFormatDescriptionRef format_desc,
    int64_t timestamp) {
  // CMBlockBuffer を作成（データをコピー）
  CMBlockBufferRef block_buffer = nullptr;
  void* buffer_data = malloc(size);
  memcpy(buffer_data, data, size);

  OSStatus status = CMBlockBufferCreateWithMemoryBlock(
      kCFAllocatorDefault, buffer_data, size, kCFAllocatorDefault, nullptr, 0,
      size, 0, &block_buffer);

  if (status != noErr || !block_buffer) {
    return nullptr;
  }

  // CMSampleBuffer を作成
  CMSampleBufferRef sample_buffer = nullptr;
  CMTime pts = CMTimeMake(timestamp, 1000000);
  CMTime duration = kCMTimeInvalid;
  CMSampleTimingInfo timing = {duration, pts, pts};
  size_t sample_size = size;

  status =
      CMSampleBufferCreateReady(kCFAllocatorDefault, block_buffer, format_desc,
                                1, 1, &timing, 1, &sample_size, &sample_buffer);

  CFRelease(block_buffer);

  if (status != noErr || !sample_buffer) {
    return nullptr;
  }

  return sample_buffer;
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
  bool is_vp9 =
      (config_.codec.length() >= 5 && config_.codec.substr(0, 5) == "vp09.");
  bool is_av1 =
      (config_.codec.length() >= 5 && config_.codec.substr(0, 5) == "av01.");

  if (!is_h264 && !is_h265 && !is_vp9 && !is_av1) {
    throw std::runtime_error("VideoToolbox supports only H.264/H.265/VP9/AV1");
  }

  // VP9/AV1 は macOS 11 以降で追加の登録が必要
  if (is_vp9) {
    VTRegisterSupplementalVideoDecoderIfAvailable(kCMVideoCodecType_VP9);
  }
  if (is_av1) {
    VTRegisterSupplementalVideoDecoderIfAvailable(kCMVideoCodecType_AV1);
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
  if (vt_format_desc_) {
    CFRelease((CFTypeRef)vt_format_desc_);
    vt_format_desc_ = nullptr;
  }
}

bool VideoDecoder::decode_videotoolbox(const EncodedVideoChunk& chunk) {
  bool is_h264 =
      (config_.codec.length() >= 5 && (config_.codec.substr(0, 5) == "avc1." ||
                                       config_.codec.substr(0, 5) == "avc3."));
  bool is_h265 =
      (config_.codec.length() >= 5 && (config_.codec.substr(0, 5) == "hvc1." ||
                                       config_.codec.substr(0, 5) == "hev1."));
  bool is_vp9 =
      (config_.codec.length() >= 5 && config_.codec.substr(0, 5) == "vp09.");
  bool is_av1 =
      (config_.codec.length() >= 5 && config_.codec.substr(0, 5) == "av01.");

  if (!is_h264 && !is_h265 && !is_vp9 && !is_av1) {
    if (error_callback_) {
      nb::gil_scoped_acquire gil;
      error_callback_("VideoToolbox supports only H.264/H.265/VP9/AV1");
    }
    return false;
  }

  const auto data = chunk.data_vector();

  // 解像度を取得
  uint32_t width = config_.coded_width.value_or(640);
  uint32_t height = config_.coded_height.value_or(480);

  // キーフレームの場合、フォーマット記述子を更新してキャッシュ
  if (chunk.type() == EncodedVideoChunkType::KEY) {
    CMVideoFormatDescriptionRef format_desc = nullptr;

    if (is_h264 || is_h265) {
      format_desc =
          create_format_description(data.data(), data.size(), is_h264);
    } else if (is_vp9) {
      format_desc = create_vp9_format_description(data.data(), data.size(),
                                                  width, height);
    } else if (is_av1) {
      format_desc = create_av1_format_description(data.data(), data.size(),
                                                  width, height);
    }

    if (!format_desc) {
      if (error_callback_) {
        nb::gil_scoped_acquire gil;
        error_callback_("Failed to create format description");
      }
      return false;
    }

    // 既存のキャッシュを解放
    if (vt_format_desc_) {
      CFRelease((CFTypeRef)vt_format_desc_);
    }
    // 新しいフォーマット記述子をキャッシュ（参照カウントを増やす）
    CFRetain(format_desc);
    vt_format_desc_ = (void*)format_desc;

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

  // キャッシュしたフォーマット記述子を使用（キーフレームとデルタフレームの両方で）
  // キャッシュがない場合はエラー（キーフレームが先に来ていない）
  if (!vt_format_desc_) {
    if (error_callback_) {
      nb::gil_scoped_acquire gil;
      error_callback_(
          "No cached format description (keyframe required before delta "
          "frames)");
    }
    return false;
  }

  CMVideoFormatDescriptionRef format_for_sample =
      (CMVideoFormatDescriptionRef)vt_format_desc_;

  // サンプルバッファを作成
  CMSampleBufferRef sample_buffer = nullptr;

  if (is_h264 || is_h265) {
    sample_buffer =
        create_sample_buffer(data.data(), data.size(), format_for_sample,
                             chunk.timestamp(), is_h264);
  } else if (is_vp9) {
    sample_buffer = create_vp9_sample_buffer(
        data.data(), data.size(), format_for_sample, chunk.timestamp());
  } else if (is_av1) {
    sample_buffer = create_av1_sample_buffer(
        data.data(), data.size(), format_for_sample, chunk.timestamp());
  }

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
