// CUDA Driver API の動的ロード

#ifndef WEBCODECS_PY_DYN_CUDA_H_
#define WEBCODECS_PY_DYN_CUDA_H_

#include <cuda.h>

#include "dyn.h"

namespace dyn {

#if defined(_WIN32)
static const char CUDA_SO[] = "nvcuda.dll";
#else
static const char CUDA_SO[] = "libcuda.so.1";
#endif

// CUDA Driver API 関数を動的にロード
DYN_REGISTER(CUDA_SO, cuInit);
DYN_REGISTER(CUDA_SO, cuDeviceGet);
DYN_REGISTER(CUDA_SO, cuDeviceGetCount);
DYN_REGISTER(CUDA_SO, cuDeviceGetName);
DYN_REGISTER(CUDA_SO, cuCtxCreate);
DYN_REGISTER(CUDA_SO, cuCtxDestroy);
DYN_REGISTER(CUDA_SO, cuCtxPushCurrent);
DYN_REGISTER(CUDA_SO, cuCtxPopCurrent);
DYN_REGISTER(CUDA_SO, cuGetErrorName);
DYN_REGISTER(CUDA_SO, cuMemcpy2D);

}  // namespace dyn

#endif  // WEBCODECS_PY_DYN_CUDA_H_
