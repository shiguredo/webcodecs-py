#pragma once

#include <map>
#include <string>
#include <vector>

// ハードウェアアクセラレーションエンジン
enum class HWAccelerationEngine {
  NONE,
  APPLE_VIDEO_TOOLBOX,
  NVIDIA_VIDEO_CODEC,
  INTEL_VPL,
  AMD_AMF,
};

// コーデックサポート情報
struct CodecSupport {
  bool encoder;
  bool decoder;
};

// エンジンサポート情報
struct EngineSupport {
  bool available;
  std::string platform;
  std::map<std::string, CodecSupport> codecs;
};

// ビデオコーデックの機能情報を取得する
std::map<HWAccelerationEngine, EngineSupport>
get_video_codec_capabilities_impl();
