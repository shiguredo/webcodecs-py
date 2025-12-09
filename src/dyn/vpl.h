// Intel VPL (Video Processing Library) API の動的ロード

#ifndef WEBCODECS_PY_DYN_VPL_H_
#define WEBCODECS_PY_DYN_VPL_H_

#include <mfx.h>

#include "dyn.h"

namespace dyn {

// Linux のみサポート
static const char VPL_SO[] = "libvpl.so.2";

// Dispatcher 関数
DYN_REGISTER(VPL_SO, MFXLoad);
DYN_REGISTER(VPL_SO, MFXUnload);
DYN_REGISTER(VPL_SO, MFXCreateConfig);
DYN_REGISTER(VPL_SO, MFXSetConfigFilterProperty);
DYN_REGISTER(VPL_SO, MFXCreateSession);
DYN_REGISTER(VPL_SO, MFXDispReleaseImplDescription);
DYN_REGISTER(VPL_SO, MFXEnumImplementations);

// Core 関数
DYN_REGISTER(VPL_SO, MFXClose);
DYN_REGISTER(VPL_SO, MFXVideoCORE_SyncOperation);

// Encode 関数
DYN_REGISTER(VPL_SO, MFXVideoENCODE_Query);
DYN_REGISTER(VPL_SO, MFXVideoENCODE_QueryIOSurf);
DYN_REGISTER(VPL_SO, MFXVideoENCODE_Init);
DYN_REGISTER(VPL_SO, MFXVideoENCODE_EncodeFrameAsync);
DYN_REGISTER(VPL_SO, MFXVideoENCODE_Close);
DYN_REGISTER(VPL_SO, MFXVideoENCODE_GetVideoParam);

// Decode 関数
DYN_REGISTER(VPL_SO, MFXVideoDECODE_DecodeHeader);
DYN_REGISTER(VPL_SO, MFXVideoDECODE_Query);
DYN_REGISTER(VPL_SO, MFXVideoDECODE_QueryIOSurf);
DYN_REGISTER(VPL_SO, MFXVideoDECODE_Init);
DYN_REGISTER(VPL_SO, MFXVideoDECODE_DecodeFrameAsync);
DYN_REGISTER(VPL_SO, MFXVideoDECODE_Close);

// Memory 関数
DYN_REGISTER(VPL_SO, MFXMemory_GetSurfaceForEncode);
DYN_REGISTER(VPL_SO, MFXMemory_GetSurfaceForDecode);

}  // namespace dyn

#endif  // WEBCODECS_PY_DYN_VPL_H_
