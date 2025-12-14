#include "video_encoder.h"

#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include <CoreVideo/CoreVideo.h>
#include <VideoToolbox/VideoToolbox.h>
#include <nanobind/nanobind.h>
#include <memory>
#include <vector>

#include "encoded_video_chunk.h"
#include "video_frame.h"  // VideoFrame の完全な定義が必要

namespace nb = nanobind;

namespace {
struct VTEncodeRef {
  VideoEncoder* self;
  uint64_t sequence;
  int64_t timestamp;  // usec
  bool
      use_annexb;  // true: Annex B フォーマット (start code), false: length-prefixed
  bool
      is_hevc;  // HEVC コーデックかどうか (AVC と HEVC で異なる処理が必要な場合)
};

// H.264 のプロファイルとレベルに対応する CFStringRef を返す
CFStringRef map_h264_profile_level(uint8_t profile_idc, uint8_t level_idc) {
  // プロファイルの判定
  enum Profile { BASELINE = 0x42, MAIN = 0x4D, HIGH = 0x64 };
  Profile profile = BASELINE;

  if (profile_idc == MAIN) {
    profile = MAIN;
  } else if (profile_idc == HIGH) {
    profile = HIGH;
  } else if (profile_idc == BASELINE) {
    profile = BASELINE;
  } else {
    // 未知のプロファイルは Main にフォールバック
    profile = MAIN;
  }

  // プロファイルとレベルの組み合わせで選択
  // level_idc の値（16進数）:
  // 0x1E=30 (Level 3.0), 0x1F=31 (3.1), 0x28=40 (4.0), 0x29=41 (4.1),
  // 0x32=50 (5.0), 0x33=51 (5.1), 0x34=52 (5.2)
  switch (profile) {
    case BASELINE:
      if (level_idc >= 0x29)
        return kVTProfileLevel_H264_Baseline_4_1;  // 4.1
      if (level_idc >= 0x28)
        return kVTProfileLevel_H264_Baseline_4_0;  // 4.0
      if (level_idc >= 0x1F)
        return kVTProfileLevel_H264_Baseline_3_1;  // 3.1
      return kVTProfileLevel_H264_Baseline_3_0;    // 3.0 以下

    case MAIN:
      if (level_idc >= 0x33)
        return kVTProfileLevel_H264_Main_5_1;  // 5.1
      if (level_idc >= 0x32)
        return kVTProfileLevel_H264_Main_5_0;  // 5.0
      if (level_idc >= 0x29)
        return kVTProfileLevel_H264_Main_4_1;  // 4.1
      if (level_idc >= 0x28)
        return kVTProfileLevel_H264_Main_4_0;  // 4.0
      if (level_idc >= 0x1F)
        return kVTProfileLevel_H264_Main_3_1;  // 3.1
      if (level_idc >= 0x1E)
        return kVTProfileLevel_H264_Main_3_0;  // 3.0
      return kVTProfileLevel_H264_Main_3_2;    // デフォルト 3.2

    case HIGH:
      if (level_idc >= 0x34)
        return kVTProfileLevel_H264_High_5_2;  // 5.2
      if (level_idc >= 0x33)
        return kVTProfileLevel_H264_High_5_1;  // 5.1
      if (level_idc >= 0x32)
        return kVTProfileLevel_H264_High_5_0;  // 5.0
      if (level_idc >= 0x29)
        return kVTProfileLevel_H264_High_4_1;  // 4.1
      if (level_idc >= 0x28)
        return kVTProfileLevel_H264_High_4_0;  // 4.0
      if (level_idc >= 0x1F)
        return kVTProfileLevel_H264_High_3_1;  // 3.1
      if (level_idc >= 0x1E)
        return kVTProfileLevel_H264_High_3_0;  // 3.0
      return kVTProfileLevel_H264_High_4_0;    // デフォルト 4.0

    default:
      return nullptr;
  }
}

// HEVC のプロファイルとレベルに対応する CFStringRef を返す
CFStringRef map_hevc_profile_level(uint8_t profile_idc, uint8_t level_idc) {
  // HEVC のプロファイル: 1 = Main, 2 = Main10
  if (profile_idc == 1) {
    // Main プロファイル
    return kVTProfileLevel_HEVC_Main_AutoLevel;
  } else if (profile_idc == 2) {
    // Main10 プロファイル
    return kVTProfileLevel_HEVC_Main10_AutoLevel;
  } else {
    // デフォルトは Main AutoLevel
    return kVTProfileLevel_HEVC_Main_AutoLevel;
  }
  // 注: VideoToolbox は AutoLevel を推奨しているため、
  // 明示的なレベル指定は行わない
}

// CMVideoFormatDescription から avcC/hvcC を抽出する
// これは MP4 の sample entry に直接使用できる形式
static std::vector<uint8_t> extract_description(
    CMVideoFormatDescriptionRef desc,
    bool is_h264) {
  std::vector<uint8_t> result;

  // SampleDescriptionExtensionAtoms から avcC/hvcC を取得
  CFDictionaryRef extensions = (CFDictionaryRef)CMFormatDescriptionGetExtension(
      desc, kCMFormatDescriptionExtension_SampleDescriptionExtensionAtoms);
  if (!extensions) {
    return result;
  }

  CFStringRef key = is_h264 ? CFSTR("avcC") : CFSTR("hvcC");
  CFDataRef data = (CFDataRef)CFDictionaryGetValue(extensions, key);
  if (!data) {
    return result;
  }

  CFIndex length = CFDataGetLength(data);
  result.resize(length);
  CFDataGetBytes(data, CFRangeMake(0, length), result.data());

  return result;
}

static void vt_output_callback(void* outputCallbackRefCon,
                               void* sourceFrameRefCon,
                               OSStatus status,
                               VTEncodeInfoFlags infoFlags,
                               CMSampleBufferRef sampleBuffer) {
  (void)outputCallbackRefCon;
  if (!sourceFrameRefCon)
    return;

  // handle_output が GIL を管理するため、ここでは取得しない

  std::unique_ptr<VTEncodeRef> ref(
      reinterpret_cast<VTEncodeRef*>(sourceFrameRefCon));
  VideoEncoder* self = ref->self;
  const uint64_t sequence = ref->sequence;
  const int64_t timestamp = ref->timestamp;
  const bool use_annexb = ref->use_annexb;
  const bool is_hevc_codec = ref->is_hevc;

  if (status != noErr || !sampleBuffer)
    return;
  if (infoFlags & kVTEncodeInfo_FrameDropped)
    return;

  // Keyframe detection
  bool key_frame = false;
  CFArrayRef attachments =
      CMSampleBufferGetSampleAttachmentsArray(sampleBuffer, 0);
  if (attachments && CFArrayGetCount(attachments) > 0) {
    CFDictionaryRef attachment =
        (CFDictionaryRef)CFArrayGetValueAtIndex(attachments, 0);
    key_frame =
        !CFDictionaryContainsKey(attachment, kCMSampleAttachmentKey_NotSync);
  }

  std::vector<uint8_t> out;
  const uint8_t start_code[4] = {0, 0, 0, 1};
  const size_t start_size = sizeof(start_code);

  // AVC/HEVC フォーマット (length-prefixed) の場合、パラメーターセットはビットストリームに含めない
  // （description メタデータとして提供される）
  // Annex B フォーマットの場合、パラメーターセットをビットストリームに含める
  bool include_parameter_sets = use_annexb;

  // キーフレーム時の metadata 用に description を取得
  std::optional<EncodedVideoChunkMetadata> metadata;
  CMVideoFormatDescriptionRef format_desc =
      CMSampleBufferGetFormatDescription(sampleBuffer);

  if (key_frame && include_parameter_sets && format_desc) {
    FourCharCode codec = CMFormatDescriptionGetMediaSubType(format_desc);
    bool is_h264 = codec == kCMVideoCodecType_H264;
    int nalu_len_size = 0;
    size_t ps_count = 0;
    OSStatus st =
        is_h264
            ? CMVideoFormatDescriptionGetH264ParameterSetAtIndex(
                  format_desc, 0, nullptr, nullptr, &ps_count, &nalu_len_size)
            : CMVideoFormatDescriptionGetHEVCParameterSetAtIndex(
                  format_desc, 0, nullptr, nullptr, &ps_count, &nalu_len_size);
    if (st == noErr) {
      for (size_t i = 0; i < ps_count; ++i) {
        const uint8_t* ps = nullptr;
        size_t ps_size = 0;
        st = is_h264 ? CMVideoFormatDescriptionGetH264ParameterSetAtIndex(
                           format_desc, i, &ps, &ps_size, nullptr, nullptr)
                     : CMVideoFormatDescriptionGetHEVCParameterSetAtIndex(
                           format_desc, i, &ps, &ps_size, nullptr, nullptr);
        if (st != noErr)
          break;
        out.insert(out.end(), start_code, start_code + start_size);
        out.insert(out.end(), ps, ps + ps_size);
      }
    }
  }

  // キーフレームかつ length-prefixed フォーマットの場合、decoderConfig を metadata に含める
  if (key_frame && !use_annexb && format_desc) {
    bool is_h264 = !is_hevc_codec;
    std::vector<uint8_t> description =
        extract_description(format_desc, is_h264);

    if (!description.empty()) {
      CMVideoDimensions dimensions =
          CMVideoFormatDescriptionGetDimensions(format_desc);

      EncodedVideoChunkMetadata meta;
      VideoDecoderConfig decoder_config;

      // コーデック文字列を設定
      decoder_config.codec = is_h264 ? "avc1" : "hvc1";
      decoder_config.coded_width = dimensions.width;
      decoder_config.coded_height = dimensions.height;
      decoder_config.description = std::move(description);

      meta.decoder_config = std::move(decoder_config);
      metadata = std::move(meta);
    }
  }

  CMBlockBufferRef block = CMSampleBufferGetDataBuffer(sampleBuffer);
  if (!block)
    return;
  size_t total = CMBlockBufferGetDataLength(block);
  size_t pos = 0;

  // Annex B 変換時の再アロケーションを防ぐため、事前に容量を確保
  // パラメーターセット + start code オーバーヘッド (1 NAL あたり 4 バイト) を考慮
  if (use_annexb) {
    // 1080p で約 10-20 NAL ユニット程度を想定
    size_t estimated_overhead = 20 * 4;
    out.reserve(out.size() + total + estimated_overhead);
  }

  if (use_annexb) {
    // Annex B フォーマット: start code (0x00000001) を使用
    while (pos + 4 <= total) {
      uint32_t be_len = 0;
      if (CMBlockBufferCopyDataBytes(block, pos, 4, &be_len) != noErr)
        return;
      pos += 4;
      uint32_t nalu_len = CFSwapInt32BigToHost(be_len);
      if (pos + nalu_len > total)
        return;
      std::vector<uint8_t> nalu(nalu_len);
      if (CMBlockBufferCopyDataBytes(block, pos, nalu_len, nalu.data()) !=
          noErr)
        return;
      pos += nalu_len;
      out.insert(out.end(), start_code, start_code + start_size);
      out.insert(out.end(), nalu.begin(), nalu.end());
    }
  } else {
    // AVC/HEVC フォーマット: length-prefixed (4バイトビッグエンディアン長)
    // VideoToolbox の出力はすでに length-prefixed なので、そのままコピー
    out.resize(total);
    if (CMBlockBufferCopyDataBytes(block, 0, total, out.data()) != noErr)
      return;
  }

  auto chunk = std::make_shared<EncodedVideoChunk>(
      out,
      key_frame ? EncodedVideoChunkType::KEY : EncodedVideoChunkType::DELTA,
      timestamp, 0);
  self->handle_output(sequence, chunk, metadata);
}
}  // namespace

void VideoEncoder::init_videotoolbox_encoder() {
  if (vt_session_)
    return;
  bool is_avc =
      (config_.codec.length() >= 5 && (config_.codec.substr(0, 5) == "avc1." ||
                                       config_.codec.substr(0, 5) == "avc3."));
  bool is_hevc =
      (config_.codec.length() >= 5 && (config_.codec.substr(0, 5) == "hvc1." ||
                                       config_.codec.substr(0, 5) == "hev1."));
  if (!is_avc && !is_hevc) {
    throw std::runtime_error("VideoToolbox supports only AVC/HEVC codecs");
  }

  // Source pixel buffer attributes: NV12
  OSType pf = kCVPixelFormatType_420YpCbCr8BiPlanarFullRange;
  CFNumberRef pf_num =
      CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &pf);
  CFDictionaryRef empty_dict = CFDictionaryCreate(
      kCFAllocatorDefault, nullptr, nullptr, 0, &kCFTypeDictionaryKeyCallBacks,
      &kCFTypeDictionaryValueCallBacks);
  const void* keys[] = {kCVPixelBufferIOSurfacePropertiesKey,
                        kCVPixelBufferPixelFormatTypeKey};
  const void* vals[] = {empty_dict, pf_num};
  CFDictionaryRef src_attr = CFDictionaryCreate(
      kCFAllocatorDefault, keys, vals, 2, &kCFTypeDictionaryKeyCallBacks,
      &kCFTypeDictionaryValueCallBacks);

  const void* spec_keys[] = {
      kVTVideoEncoderSpecification_EnableHardwareAcceleratedVideoEncoder};
  const void* spec_vals[] = {kCFBooleanTrue};
  CFDictionaryRef enc_spec = CFDictionaryCreate(
      kCFAllocatorDefault, spec_keys, spec_vals, 1,
      &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);

  CMVideoCodecType codec =
      is_avc ? kCMVideoCodecType_H264 : kCMVideoCodecType_HEVC;

  VTCompressionSessionRef session = nullptr;
  OSStatus err = VTCompressionSessionCreate(
      kCFAllocatorDefault, config_.width, config_.height, codec, enc_spec,
      src_attr, kCFAllocatorDefault, vt_output_callback, this, &session);

  CFRelease(enc_spec);
  CFRelease(src_attr);
  CFRelease(pf_num);
  CFRelease(empty_dict);

  if (err != noErr || !session) {
    throw std::runtime_error("Failed to create VTCompressionSession");
  }

  // Realtime, no B-frames, low latency settings
  VTSessionSetProperty(session, kVTCompressionPropertyKey_RealTime,
                       kCFBooleanTrue);
  VTSessionSetProperty(session, kVTCompressionPropertyKey_AllowFrameReordering,
                       kCFBooleanFalse);

  // 期待されるフレームレートを設定（低遅延のために重要）
  if (config_.framerate.has_value()) {
    float expected_fps = static_cast<float>(config_.framerate.value());
    CFNumberRef fps_num =
        CFNumberCreate(kCFAllocatorDefault, kCFNumberFloatType, &expected_fps);
    VTSessionSetProperty(session, kVTCompressionPropertyKey_ExpectedFrameRate,
                         fps_num);
    CFRelease(fps_num);
  }

  // codec_params_ からプロファイルとレベルを設定
  // 注: プロファイル/レベルの設定はオプションで、VideoToolbox は
  // 解像度とビットレートに基づいて自動的に適切な値を選択します。
  // ただし、ユーザーが指定したコーデック文字列を尊重するため、
  // 可能な限り設定を試みます。
  if (codec == kCMVideoCodecType_H264) {
    CFStringRef profile_level = get_h264_profile_level();
    if (profile_level) {
      OSStatus status = VTSessionSetProperty(
          session, kVTCompressionPropertyKey_ProfileLevel, profile_level);
      if (status != noErr) {
        // 設定に失敗した場合は、VideoToolbox に自動選択させる
        // （警告: コーデック文字列で指定されたプロファイル/レベルは使用されません）
      }
    }
    // profile_level が nullptr の場合は、VideoToolbox に自動選択させる
  } else {
    CFStringRef profile_level = get_hevc_profile_level();
    if (profile_level) {
      OSStatus status = VTSessionSetProperty(
          session, kVTCompressionPropertyKey_ProfileLevel, profile_level);
      if (status != noErr) {
        // 設定に失敗した場合は、VideoToolbox に自動選択させる
      }
    }
    // profile_level が nullptr の場合は、VideoToolbox に自動選択させる
  }

  // Bitrate (in bits/sec)
  int br = static_cast<int>(config_.bitrate.value_or(1000000));
  CFNumberRef br_num =
      CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &br);
  VTSessionSetProperty(session, kVTCompressionPropertyKey_AverageBitRate,
                       br_num);
  CFRelease(br_num);

  // セッションの設定を検証して、エンコード準備を行う
  err = VTCompressionSessionPrepareToEncodeFrames(session);
  if (err != noErr) {
    VTCompressionSessionInvalidate(session);
    CFRelease(session);
    throw std::runtime_error(
        "Failed to prepare VideoToolbox session (profile/level may be "
        "incompatible with resolution)");
  }

  vt_session_ = session;
}

void VideoEncoder::encode_frame_videotoolbox(
    const VideoFrame& frame,
    bool keyframe,
    std::optional<uint16_t> quantizer) {
  if (!vt_session_) {
    throw std::runtime_error("VideoToolbox encoder is not initialized");
  }
  auto session = (VTCompressionSessionRef)vt_session_;

  CVPixelBufferRef pb = nullptr;
  bool pb_from_native = false;

  // native_buffer (CVPixelBufferRef) がある場合は直接使用（ゼロコピー）
  if (frame.has_native_buffer()) {
    void* native_ptr = frame.native_buffer_ptr();
    if (native_ptr != nullptr) {
      pb = static_cast<CVPixelBufferRef>(native_ptr);
      CVPixelBufferRetain(pb);
      pb_from_native = true;
    }
  }

  // native_buffer がない場合は従来通りプールから作成してコピー
  if (!pb_from_native) {
    CVPixelBufferPoolRef pool = VTCompressionSessionGetPixelBufferPool(session);
    if (!pool) {
      throw std::runtime_error("Failed to get CVPixelBufferPool");
    }

    // Make sure we have NV12 source
    std::unique_ptr<VideoFrame> nv12;
    if (frame.format() != VideoPixelFormat::NV12) {
      nv12 = frame.convert_format(VideoPixelFormat::NV12);
    }
    const VideoFrame& src = nv12 ? *nv12 : frame;

    CVReturn r =
        CVPixelBufferPoolCreatePixelBuffer(kCFAllocatorDefault, pool, &pb);
    if (r != kCVReturnSuccess || !pb) {
      throw std::runtime_error("Failed to create CVPixelBuffer");
    }

    // Copy planes into CVPixelBuffer
    CVPixelBufferLockBaseAddress(pb, 0);
    uint8_t* dst_y = (uint8_t*)CVPixelBufferGetBaseAddressOfPlane(pb, 0);
    size_t dst_stride_y = CVPixelBufferGetBytesPerRowOfPlane(pb, 0);
    uint8_t* dst_uv = (uint8_t*)CVPixelBufferGetBaseAddressOfPlane(pb, 1);
    size_t dst_stride_uv = CVPixelBufferGetBytesPerRowOfPlane(pb, 1);

    const uint8_t* src_y = src.plane_ptr(0);
    const uint8_t* src_uv = src.plane_ptr(1);
    int width = static_cast<int>(src.width());
    int height = static_cast<int>(src.height());
    int chroma_height = (height + 1) / 2;
    // Y plane
    if (dst_stride_y == static_cast<size_t>(width)) {
      memcpy(dst_y, src_y, static_cast<size_t>(width * height));
    } else {
      for (int i = 0; i < height; ++i) {
        memcpy(dst_y + i * dst_stride_y, src_y + i * width, width);
      }
    }
    // UV plane (interleaved)
    int chroma_row_bytes = ((width + 1) / 2) * 2;
    if (dst_stride_uv == static_cast<size_t>(chroma_row_bytes)) {
      memcpy(dst_uv, src_uv,
             static_cast<size_t>(chroma_row_bytes * chroma_height));
    } else {
      for (int i = 0; i < chroma_height; ++i) {
        memcpy(dst_uv + i * dst_stride_uv, src_uv + i * chroma_row_bytes,
               chroma_row_bytes);
      }
    }
    CVPixelBufferUnlockBaseAddress(pb, 0);
  }

  // VideoToolbox はフレームごとの quantizer 指定をサポートしていないため、
  // avc.quantizer オプションは無視される
  (void)quantizer;

  // Per-frame options
  CFDictionaryRef frame_opts = nullptr;
  if (keyframe) {
    const void* fk_keys[] = {kVTEncodeFrameOptionKey_ForceKeyFrame};
    const void* fk_vals[] = {kCFBooleanTrue};
    frame_opts = CFDictionaryCreate(kCFAllocatorDefault, fk_keys, fk_vals, 1,
                                    &kCFTypeDictionaryKeyCallBacks,
                                    &kCFTypeDictionaryValueCallBacks);
  }

  // Pass sequence and timestamp through sourceFrameRefCon
  // avc_format: "annexb" -> start code, "avc" -> length-prefixed
  // hevc_format: "annexb" -> start code, "hevc" -> length-prefixed
  bool is_hevc = is_hevc_codec();
  bool is_avc = is_avc_codec();
  bool use_annexb = false;
  if (is_hevc) {
    use_annexb = (config_.hevc_format == "annexb");
  } else if (is_avc) {
    use_annexb = (config_.avc_format == "annexb");
  }
  auto* ref = new VTEncodeRef{this, current_sequence_, frame.timestamp(),
                              use_annexb, is_hevc};
  CMTime pts = CMTimeMake(frame.timestamp(), 1000000);

  // バインディング層で既に GIL を解放しているため、ここでは解放しない
  OSStatus err = VTCompressionSessionEncodeFrame(
      session, pb, pts, kCMTimeInvalid, frame_opts, ref, nullptr);

  if (frame_opts)
    CFRelease(frame_opts);
  CFRelease(pb);
  if (err != noErr) {
    delete ref;
    throw std::runtime_error("VTCompressionSessionEncodeFrame failed");
  }
}

void VideoEncoder::flush_videotoolbox_encoder() {
  if (!vt_session_) {
    return;
  }

  VTCompressionSessionRef session = (VTCompressionSessionRef)vt_session_;

  // VideoToolbox のエンコード処理が完了するまで待機
  // CompleteFrames は全ての保留中のフレームを処理してコールバックを呼ぶ
  OSStatus err = VTCompressionSessionCompleteFrames(session, kCMTimeInvalid);
  if (err != noErr) {
    // エラーが発生してもクリーンアップは続行
  }
}

void VideoEncoder::cleanup_videotoolbox_encoder() {
  if (vt_session_) {
    VTCompressionSessionRef s = (VTCompressionSessionRef)vt_session_;
    VTCompressionSessionInvalidate(s);
    CFRelease(s);
    vt_session_ = nullptr;
  }
}

CFStringRef VideoEncoder::get_h264_profile_level() {
  // codec_params_ から AVC パラメータを取得
  if (auto* avc_params = std::get_if<AVCCodecParameters>(&codec_params_)) {
    return map_h264_profile_level(avc_params->profile_idc,
                                  avc_params->level_idc);
  }
  return nullptr;
}

CFStringRef VideoEncoder::get_hevc_profile_level() {
  // codec_params_ から HEVC パラメータを取得
  if (auto* hevc_params = std::get_if<HEVCCodecParameters>(&codec_params_)) {
    return map_hevc_profile_level(hevc_params->general_profile_idc,
                                  hevc_params->general_level_idc);
  }
  return nullptr;
}

#endif  // defined(__APPLE__)