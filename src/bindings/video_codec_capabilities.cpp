#include "video_codec_capabilities.h"

#include <nanobind/nanobind.h>
#include <nanobind/stl/map.h>
#include <nanobind/stl/string.h>

#if defined(__APPLE__)
#include <VideoToolbox/VideoToolbox.h>
#endif

namespace nb = nanobind;

std::map<HardwareAccelerationEngine, EngineSupport>
get_video_codec_capabilities_impl() {
  std::map<HardwareAccelerationEngine, EngineSupport> capabilities;

  // NONE エンジン（ソフトウェア実装）は常に利用可能
  EngineSupport none_support;
  none_support.available = true;
  none_support.platform = "all";
  none_support.codecs["av01"] = {true, true};  // AV1
  capabilities[HardwareAccelerationEngine::NONE] = none_support;

#if defined(__APPLE__)
  // Apple VideoToolbox の利用可能性を確認
  EngineSupport vt_support;
  vt_support.available = true;
  vt_support.platform = "darwin";

  // H.264 (AVC) のサポートを確認
  OSStatus status;
  VTCompressionSessionRef compressionSession = nullptr;

  // H.264 エンコーダーの確認
  CFMutableDictionaryRef encoderSpec =
      CFDictionaryCreateMutable(nullptr, 0, &kCFTypeDictionaryKeyCallBacks,
                                &kCFTypeDictionaryValueCallBacks);

  status = VTCompressionSessionCreate(
      nullptr, 1920, 1080, kCMVideoCodecType_H264, encoderSpec, nullptr,
      nullptr, nullptr, nullptr, &compressionSession);

  bool h264_encoder_available = (status == noErr);
  if (compressionSession) {
    VTCompressionSessionInvalidate(compressionSession);
    CFRelease(compressionSession);
    compressionSession = nullptr;
  }
  CFRelease(encoderSpec);

  // H.264 デコーダーの確認（デコーダーは基本的に利用可能）
  bool h264_decoder_available = true;

  if (h264_encoder_available || h264_decoder_available) {
    vt_support.codecs["avc1"] = {h264_encoder_available,
                                 h264_decoder_available};
  }

  // H.265 (HEVC) のサポートを確認
  encoderSpec =
      CFDictionaryCreateMutable(nullptr, 0, &kCFTypeDictionaryKeyCallBacks,
                                &kCFTypeDictionaryValueCallBacks);

  status = VTCompressionSessionCreate(
      nullptr, 1920, 1080, kCMVideoCodecType_HEVC, encoderSpec, nullptr,
      nullptr, nullptr, nullptr, &compressionSession);

  bool hevc_encoder_available = (status == noErr);
  if (compressionSession) {
    VTCompressionSessionInvalidate(compressionSession);
    CFRelease(compressionSession);
    compressionSession = nullptr;
  }
  CFRelease(encoderSpec);

  // H.265 デコーダーの確認（デコーダーは基本的に利用可能）
  bool hevc_decoder_available = true;

  if (hevc_encoder_available || hevc_decoder_available) {
    vt_support.codecs["hvc1"] = {hevc_encoder_available,
                                 hevc_decoder_available};
  }

  // VP9 デコーダーのサポートを追加
  // VP9 は VideoToolbox でデコードのみサポート
  VTRegisterSupplementalVideoDecoderIfAvailable(kCMVideoCodecType_VP9);
  vt_support.codecs["vp09"] = {false, true};

  // AV1 デコーダーのサポートを追加
  // AV1 は VideoToolbox でデコードのみサポート
  VTRegisterSupplementalVideoDecoderIfAvailable(kCMVideoCodecType_AV1);
  vt_support.codecs["av01"] = {false, true};

  // VideoToolbox がサポートするコーデックがある場合のみ追加
  if (!vt_support.codecs.empty()) {
    capabilities[HardwareAccelerationEngine::APPLE_VIDEO_TOOLBOX] = vt_support;
  }
#endif

  return capabilities;
}

void init_video_codec_capabilities(nb::module_& m) {
  // HardwareAccelerationEngine enum
  nb::enum_<HardwareAccelerationEngine>(m, "HardwareAccelerationEngine")
      .value("NONE", HardwareAccelerationEngine::NONE)
      .value("APPLE_VIDEO_TOOLBOX",
             HardwareAccelerationEngine::APPLE_VIDEO_TOOLBOX)
      .value("NVIDIA_VIDEO_CODEC",
             HardwareAccelerationEngine::NVIDIA_VIDEO_CODEC)
      .value("INTEL_VPL", HardwareAccelerationEngine::INTEL_VPL)
      .value("AMD_AMF", HardwareAccelerationEngine::AMD_AMF);

  // CodecSupport 構造体
  nb::class_<CodecSupport>(m, "_CodecSupport")
      .def_ro("encoder", &CodecSupport::encoder)
      .def_ro("decoder", &CodecSupport::decoder);

  // EngineSupport 構造体
  nb::class_<EngineSupport>(m, "_EngineSupport")
      .def_ro("available", &EngineSupport::available)
      .def_ro("platform", &EngineSupport::platform)
      .def_ro("codecs", &EngineSupport::codecs);

  // get_video_codec_capabilities 関数
  m.def("_get_video_codec_capabilities_impl",
        &get_video_codec_capabilities_impl,
        "実行環境で利用可能なビデオコーデックとその実装方法の詳細情報を返す");
}
