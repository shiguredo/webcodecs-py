// NVIDIA Video Encoder (NVENC) API の動的ロード

#ifndef WEBCODECS_PY_DYN_NVENC_H_
#define WEBCODECS_PY_DYN_NVENC_H_

#include <nvEncodeAPI.h>

#include "dyn.h"

namespace dyn {

#if defined(_WIN32)
static const char NVENC_SO[] = "nvEncodeAPI64.dll";
#else
static const char NVENC_SO[] = "libnvidia-encode.so.1";
#endif

// NVENC 関数を動的にロード
DYN_REGISTER(NVENC_SO, NvEncodeAPICreateInstance);

}  // namespace dyn

#endif  // WEBCODECS_PY_DYN_NVENC_H_
