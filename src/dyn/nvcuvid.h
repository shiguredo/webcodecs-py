// NVIDIA Video Decoder (NVDEC) API の動的ロード

#ifndef WEBCODECS_PY_DYN_NVCUVID_H_
#define WEBCODECS_PY_DYN_NVCUVID_H_

#include <cuviddec.h>
#include <nvcuvid.h>

#include "dyn.h"

namespace dyn {

#if defined(_WIN32)
static const char NVCUVID_SO[] = "nvcuvid.dll";
#else
static const char NVCUVID_SO[] = "libnvcuvid.so.1";
#endif

// NVDEC 関数を動的にロード
DYN_REGISTER(NVCUVID_SO, cuvidCreateDecoder);
DYN_REGISTER(NVCUVID_SO, cuvidDestroyDecoder);
DYN_REGISTER(NVCUVID_SO, cuvidDecodePicture);
DYN_REGISTER(NVCUVID_SO, cuvidCreateVideoParser);
DYN_REGISTER(NVCUVID_SO, cuvidDestroyVideoParser);
DYN_REGISTER(NVCUVID_SO, cuvidParseVideoData);
DYN_REGISTER(NVCUVID_SO, cuvidMapVideoFrame);
DYN_REGISTER(NVCUVID_SO, cuvidUnmapVideoFrame);

}  // namespace dyn

#endif  // WEBCODECS_PY_DYN_NVCUVID_H_
