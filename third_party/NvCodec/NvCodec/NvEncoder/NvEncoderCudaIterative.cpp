/*
 * This copyright notice applies to this header file only:
 *
 * Copyright (c) 2010-2024 NVIDIA Corporation
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the software, and to permit persons to whom the
 * software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include "NvEncoder/NvEncoderCudaIterative.h"

#ifndef _WIN32
#include <cstring>
static inline bool operator==(const GUID &guid1, const GUID &guid2) {
    return !memcmp(&guid1, &guid2, sizeof(GUID));
}

static inline bool operator!=(const GUID &guid1, const GUID &guid2) {
    return !(guid1 == guid2);
}
#endif

NvEncoderCudaIterative::NvEncoderCudaIterative(CUcontext cuContext, 
    uint32_t nWidth, uint32_t nHeight, NV_ENC_BUFFER_FORMAT eBufferFormat)
    : NvEncoderCuda(cuContext, nWidth, nHeight, eBufferFormat, 0, false, false)
{
}

NvEncoderCudaIterative::~NvEncoderCudaIterative()
{}

void NvEncoderCudaIterative::AllocateBuffers(int32_t numInputBuffers, NV_ENC_BUFFER_USAGE bufferUsage)
{
    if (!IsHWEncoderInitialized())
    {
        NVENC_THROW_ERROR("Encoder intialization failed", NV_ENC_ERR_ENCODER_NOT_INITIALIZED);
    }

    NV_ENC_BUFFER_FORMAT pixFmt = GetPixelFormat();

    CUDA_DRVAPI_CALL(cuCtxPushCurrent(m_cuContext));
    std::vector<void*> inputFrames;
    for (int i = 0; i < numInputBuffers; i++)
    {
        CUdeviceptr pDeviceFrame;
        uint32_t chromaHeight = GetNumChromaPlanes(GetPixelFormat()) * GetChromaHeight(GetPixelFormat(), GetMaxEncodeHeight());
        if (GetPixelFormat() == NV_ENC_BUFFER_FORMAT_YV12 || GetPixelFormat() == NV_ENC_BUFFER_FORMAT_IYUV)
            chromaHeight = GetChromaHeight(GetPixelFormat(), GetMaxEncodeHeight());
        CUDA_DRVAPI_CALL(cuMemAllocPitch((CUdeviceptr *)&pDeviceFrame,
            &m_cudaPitch,
            GetWidthInBytes(GetPixelFormat(), GetMaxEncodeWidth()),
            GetMaxEncodeHeight() + chromaHeight, 16));
        inputFrames.push_back((void*)pDeviceFrame);
    }
    CUDA_DRVAPI_CALL(cuCtxPopCurrent(NULL));

    if(bufferUsage == NV_ENC_OUTPUT_RECON)
    {
        if(pixFmt == NV_ENC_BUFFER_FORMAT_IYUV || 
        pixFmt == NV_ENC_BUFFER_FORMAT_YV12)
            pixFmt = NV_ENC_BUFFER_FORMAT_NV12; // internally gets converted to NV12
        else if(pixFmt == NV_ENC_BUFFER_FORMAT_ARGB ||
                pixFmt == NV_ENC_BUFFER_FORMAT_ARGB10 ||
                pixFmt == NV_ENC_BUFFER_FORMAT_AYUV ||
                pixFmt == NV_ENC_BUFFER_FORMAT_ABGR ||
                pixFmt == NV_ENC_BUFFER_FORMAT_ABGR10)
            NVENC_THROW_ERROR("Unsupported pixel format for Iterative NVENC", NV_ENC_ERR_UNSUPPORTED_PARAM);
    }

    RegisterInputResources(inputFrames,
        NV_ENC_INPUT_RESOURCE_TYPE_CUDADEVICEPTR,
        GetMaxEncodeWidth(),
        GetMaxEncodeHeight(),
        (int)m_cudaPitch,
        pixFmt,
        false,
        bufferUsage);
}

void NvEncoderCudaIterative::InitializeBitstreamBuffers(uint32_t nNumBitStreamBuffers)
{
    for (int i = 0; i < (nNumBitStreamBuffers); i++)
    {
        NV_ENC_CREATE_BITSTREAM_BUFFER createBitstreamBuffer = { NV_ENC_CREATE_BITSTREAM_BUFFER_VER };
        NVENC_API_CALL(m_nvenc.nvEncCreateBitstreamBuffer(m_hEncoder, &createBitstreamBuffer));
        m_vBitstreamOutputBuffer[i] = createBitstreamBuffer.bitstreamBuffer;
    }
}

void NvEncoderCudaIterative::DestroyBitstreamBuffers()
{
    for (uint32_t i = 0; i < m_vBitstreamOutputBuffer.size(); i++)
    {
        if (m_vBitstreamOutputBuffer[i])
        {
            m_nvenc.nvEncDestroyBitstreamBuffer(m_hEncoder, m_vBitstreamOutputBuffer[i]);
        }
    }

    m_vBitstreamOutputBuffer.clear();
}

void NvEncoderCudaIterative::RegisterInputResources(std::vector<void*> inputframes, NV_ENC_INPUT_RESOURCE_TYPE eResourceType,
                                         int width, int height, int pitch, NV_ENC_BUFFER_FORMAT bufferFormat, bool bReferenceFrame, NV_ENC_BUFFER_USAGE bufferUsage)
{
    for (uint32_t i = 0; i < inputframes.size(); ++i)
    {
        NV_ENC_REGISTERED_PTR registeredPtr = RegisterResource(inputframes[i], eResourceType, width, height, pitch, bufferFormat, bufferUsage);
        
        std::vector<uint32_t> _chromaOffsets;
        NvEncoder::GetChromaSubPlaneOffsets(bufferFormat, pitch, height, _chromaOffsets);
        NvEncInputFrame inputframe = {};
        inputframe.inputPtr = (void *)inputframes[i];
        inputframe.chromaOffsets[0] = 0;
        inputframe.chromaOffsets[1] = 0;
        for (uint32_t ch = 0; ch < _chromaOffsets.size(); ch++)
        {
            inputframe.chromaOffsets[ch] = _chromaOffsets[ch];
        }
        inputframe.numChromaPlanes = NvEncoder::GetNumChromaPlanes(bufferFormat);
        inputframe.pitch = pitch;
        inputframe.chromaPitch = NvEncoder::GetChromaPitch(bufferFormat, pitch);
        inputframe.bufferFormat = bufferFormat;
        inputframe.resourceType = eResourceType;

        if (bufferUsage == NV_ENC_OUTPUT_RECON)
        {
            m_vRegisteredResourcesReconFrames.push_back(registeredPtr);
            m_vReconFrames.push_back(inputframe);
        }
        else
        {
           if (bReferenceFrame)
            {
                m_vRegisteredResourcesForReference.push_back(registeredPtr);
                m_vReferenceFrames.push_back(inputframe);
            }
            else
            {
                m_vRegisteredResources.push_back(registeredPtr);
                m_vInputFrames.push_back(inputframe);
            }
        }
    }
}

void NvEncoderCudaIterative::CreateEncoder(const NV_ENC_INITIALIZE_PARAMS* pEncoderParams)
{
    if (!m_hEncoder)
    {
        NVENC_THROW_ERROR("Encoder Initialization failed", NV_ENC_ERR_NO_ENCODE_DEVICE);
    }

    if (!pEncoderParams)
    {
        NVENC_THROW_ERROR("Invalid NV_ENC_INITIALIZE_PARAMS ptr", NV_ENC_ERR_INVALID_PTR);
    }

    if (pEncoderParams->encodeWidth == 0 || pEncoderParams->encodeHeight == 0)
    {
        NVENC_THROW_ERROR("Invalid encoder width and height", NV_ENC_ERR_INVALID_PARAM);
    }

    if (pEncoderParams->encodeGUID != NV_ENC_CODEC_H264_GUID && pEncoderParams->encodeGUID != NV_ENC_CODEC_HEVC_GUID && pEncoderParams->encodeGUID != NV_ENC_CODEC_AV1_GUID)
    {
        NVENC_THROW_ERROR("Invalid codec guid", NV_ENC_ERR_INVALID_PARAM);
    }

    if (pEncoderParams->encodeGUID == NV_ENC_CODEC_H264_GUID)
    {
        if (m_eBufferFormat == NV_ENC_BUFFER_FORMAT_YUV420_10BIT || m_eBufferFormat == NV_ENC_BUFFER_FORMAT_YUV444_10BIT)
        {
            NVENC_THROW_ERROR("10-bit format isn't supported by H264 encoder", NV_ENC_ERR_INVALID_PARAM);
        }
    }

    if (pEncoderParams->encodeGUID == NV_ENC_CODEC_AV1_GUID)
    {
        if (m_eBufferFormat == NV_ENC_BUFFER_FORMAT_YUV444 || m_eBufferFormat == NV_ENC_BUFFER_FORMAT_YUV444_10BIT)
        {
            NVENC_THROW_ERROR("YUV444 format isn't supported by AV1 encoder", NV_ENC_ERR_INVALID_PARAM);
        }
    }

    // set other necessary params if not set yet
    if (pEncoderParams->encodeGUID == NV_ENC_CODEC_H264_GUID)
    {
        if ((m_eBufferFormat == NV_ENC_BUFFER_FORMAT_YUV444) &&
            (pEncoderParams->encodeConfig->encodeCodecConfig.h264Config.chromaFormatIDC != 3))
        {
            NVENC_THROW_ERROR("Invalid ChromaFormatIDC", NV_ENC_ERR_INVALID_PARAM);
        }
    }

    if (pEncoderParams->encodeGUID == NV_ENC_CODEC_HEVC_GUID)
    {
        bool yuv10BitFormat = (m_eBufferFormat == NV_ENC_BUFFER_FORMAT_YUV420_10BIT || m_eBufferFormat == NV_ENC_BUFFER_FORMAT_YUV444_10BIT) ? true : false;
        if (yuv10BitFormat && pEncoderParams->encodeConfig->encodeCodecConfig.hevcConfig.inputBitDepth != NV_ENC_BIT_DEPTH_10)
        {
            NVENC_THROW_ERROR("Invalid PixelBitdepth", NV_ENC_ERR_INVALID_PARAM);
        }

        if ((m_eBufferFormat == NV_ENC_BUFFER_FORMAT_YUV444 || m_eBufferFormat == NV_ENC_BUFFER_FORMAT_YUV444_10BIT) &&
            (pEncoderParams->encodeConfig->encodeCodecConfig.hevcConfig.chromaFormatIDC != 3))
        {
            NVENC_THROW_ERROR("Invalid ChromaFormatIDC", NV_ENC_ERR_INVALID_PARAM);
        }
    }

    if (pEncoderParams->encodeGUID == NV_ENC_CODEC_AV1_GUID)
    {
        bool yuv10BitFormat = (m_eBufferFormat == NV_ENC_BUFFER_FORMAT_YUV420_10BIT) ? true : false;
        if (yuv10BitFormat && pEncoderParams->encodeConfig->encodeCodecConfig.av1Config.inputBitDepth != NV_ENC_BIT_DEPTH_10)
        {
            NVENC_THROW_ERROR("Invalid PixelBitdepth", NV_ENC_ERR_INVALID_PARAM);
        }

        if (pEncoderParams->encodeConfig->encodeCodecConfig.av1Config.chromaFormatIDC != 1)
        {
            NVENC_THROW_ERROR("Invalid ChromaFormatIDC", NV_ENC_ERR_INVALID_PARAM);
        }

        if (m_bOutputInVideoMemory && pEncoderParams->encodeConfig->frameIntervalP > 1)
        {
            NVENC_THROW_ERROR("Alt Ref frames not supported for AV1 in case of OutputInVideoMemory", NV_ENC_ERR_INVALID_PARAM);
        }
    }

    memcpy(&m_initializeParams, pEncoderParams, sizeof(m_initializeParams));
    m_initializeParams.version = NV_ENC_INITIALIZE_PARAMS_VER;

    if (pEncoderParams->encodeConfig)
    {
        memcpy(&m_encodeConfig, pEncoderParams->encodeConfig, sizeof(m_encodeConfig));
        m_encodeConfig.version = NV_ENC_CONFIG_VER;
    }
    else
    {
        NV_ENC_PRESET_CONFIG presetConfig = { NV_ENC_PRESET_CONFIG_VER, 0, { NV_ENC_CONFIG_VER } };
        m_nvenc.nvEncGetEncodePresetConfigEx(m_hEncoder, pEncoderParams->encodeGUID, pEncoderParams->presetGUID, pEncoderParams->tuningInfo, &presetConfig);
        memcpy(&m_encodeConfig, &presetConfig.presetCfg, sizeof(NV_ENC_CONFIG));
        if (m_bOutputInVideoMemory && pEncoderParams->encodeGUID == NV_ENC_CODEC_AV1_GUID)
        {
            m_encodeConfig.frameIntervalP = 1;
        }
    }

    if (((uint32_t)m_encodeConfig.frameIntervalP) > m_encodeConfig.gopLength)
    {
        m_encodeConfig.frameIntervalP = m_encodeConfig.gopLength;
    }

    m_initializeParams.encodeConfig = &m_encodeConfig;

    m_nNumIterations = m_initializeParams.numStateBuffers;
    m_initializeParams.numStateBuffers = m_nNumIterations + m_encodeConfig.frameIntervalP; // if we have B frames we must add new state buffers to account for them
    m_nNumEncStates = m_initializeParams.numStateBuffers;
    InitVPackets(m_nNumIterations);
    InitStateBufferUsage(m_nNumEncStates);
    if(m_initializeParams.encodeConfig->rcParams.lookaheadDepth)
    {
        m_initializeParams.encodeConfig->rcParams.enableLookahead = false;
        m_initializeParams.encodeConfig->rcParams.enableExtLookahead = true;
    }

    // check recon buffers 
    if (!GetCapabilityValue(pEncoderParams->encodeGUID, NV_ENC_CAPS_OUTPUT_RECON_SURFACE))
        NVENC_THROW_ERROR("Recon API not supported", NV_ENC_ERR_INVALID_PARAM);

    // check output stats
    if (GetCapabilityValue(pEncoderParams->encodeGUID, NV_ENC_CAPS_OUTPUT_ROW_STATS))
        m_initializeParams.outputStatsLevel = NV_ENC_OUTPUT_STATS_ROW_LEVEL;
    else if(GetCapabilityValue(pEncoderParams->encodeGUID, NV_ENC_CAPS_OUTPUT_BLOCK_STATS))
        m_initializeParams.outputStatsLevel = NV_ENC_OUTPUT_STATS_BLOCK_LEVEL;
    else
        NVENC_THROW_ERROR("Stats API not supported", NV_ENC_ERR_INVALID_PARAM);

    m_nExtraOutputDelay = 0;
    m_initializeParams.enableEncodeAsync = false;

    std::cout << "Maximum selected iteration number: " << m_nNumIterations << std::endl;
    std::cout << "Number of B frames: " << m_encodeConfig.frameIntervalP - 1 << std::endl;
    std::cout << "Lookahead depth: " << m_initializeParams.encodeConfig->rcParams.lookaheadDepth << std::endl;
    std::cout << "NVENC state buffers: " << m_nNumEncStates << std::endl;
    if(pEncoderParams->encodeGUID == NV_ENC_CODEC_AV1_GUID && m_nNumEncStates > MAX_NUM_ENC_STATE_BUF_AV1)
        NVENC_THROW_ERROR("Maximum number of NVENC state buffers for AV1 is " + std::to_string(MAX_NUM_ENC_STATE_BUF_AV1), NV_ENC_ERR_INVALID_PARAM);
    if(pEncoderParams->encodeGUID == NV_ENC_CODEC_H264_GUID && m_nNumEncStates > MAX_NUM_ENC_STATE_BUF_H264)
        NVENC_THROW_ERROR("Maximum number of NVENC state buffers for H.264 is " + std::to_string(MAX_NUM_ENC_STATE_BUF_H264), NV_ENC_ERR_INVALID_PARAM);
    if(pEncoderParams->encodeGUID == NV_ENC_CODEC_HEVC_GUID && m_nNumEncStates > MAX_NUM_ENC_STATE_BUF_HEVC)
        NVENC_THROW_ERROR("Maximum number of NVENC state buffers for HEVC is " + std::to_string(MAX_NUM_ENC_STATE_BUF_HEVC), NV_ENC_ERR_INVALID_PARAM);

    NVENC_API_CALL(m_nvenc.nvEncInitializeEncoder(m_hEncoder, &m_initializeParams));

    m_bEncoderInitialized = true;
    m_nWidth = m_initializeParams.encodeWidth;
    m_nHeight = m_initializeParams.encodeHeight;
    m_nMaxEncodeWidth = m_initializeParams.maxEncodeWidth;
    m_nMaxEncodeHeight = m_initializeParams.maxEncodeHeight;

    m_nEncoderBuffer = m_encodeConfig.frameIntervalP + m_encodeConfig.rcParams.lookaheadDepth + m_nExtraOutputDelay;

    m_nOutputDelay = m_nEncoderBuffer - 1;

    if (pEncoderParams->encodeGUID == NV_ENC_CODEC_AV1_GUID) 
        m_nExtraOutputBuffers++;
    
    m_vpCompletionEvent.resize(m_nEncoderBuffer + m_nNumIterations + m_nExtraOutputBuffers, nullptr);
    std::cout << "Created Completion Events: " << m_nEncoderBuffer + m_nNumIterations + m_nExtraOutputBuffers << std::endl;

    m_vMappedInputBuffers.resize(m_nEncoderBuffer, nullptr);

    m_vBitstreamOutputBuffer.resize(m_nEncoderBuffer + m_nNumIterations + m_nExtraOutputBuffers, nullptr);
    InitializeBitstreamBuffers(m_nEncoderBuffer + m_nNumIterations + m_nExtraOutputBuffers);
    std::cout << "Created Bitstream Buffers: " << m_nEncoderBuffer + m_nNumIterations + m_nExtraOutputBuffers << std::endl;
   
    AllocateBuffers(m_nEncoderBuffer, NV_ENC_INPUT_IMAGE);
    std::cout << "Allocated Input Buffers: " << m_nEncoderBuffer << std::endl;

    m_vMappedReconBuffers.resize(m_nEncoderBuffer + m_nNumIterations, nullptr);
    AllocateBuffers(m_nEncoderBuffer + m_nNumIterations, NV_ENC_OUTPUT_RECON);
    std::cout << "Allocated Recon Buffers: " << m_nEncoderBuffer + m_nNumIterations << std::endl;

    m_EncMultipleStates.statsInfo.level = m_initializeParams.outputStatsLevel;
    uint32_t blockDim = 0; // block size per codec
    if(pEncoderParams->encodeGUID == NV_ENC_CODEC_H264_GUID)
        blockDim = 16;
    else if(pEncoderParams->encodeGUID == NV_ENC_CODEC_HEVC_GUID)
        blockDim = 32;
    else if(pEncoderParams->encodeGUID == NV_ENC_CODEC_AV1_GUID)
        blockDim = 64;
    else
        NVENC_THROW_ERROR("Invalid codec guid", NV_ENC_ERR_INVALID_PARAM);
    m_EncMultipleStates.statsInfo.calcSize(m_nWidth, m_nHeight, blockDim); 

    for (size_t i = 0; i < m_nNumIterations; i++)
    { 
        void *stats = (void *)malloc(m_EncMultipleStates.statsInfo.totalSize);
        m_EncMultipleStates.statsData.push_back((stats)); 
    }

}

NVENCSTATUS NvEncoderCudaIterative::EncodeFrameIteration(NV_ENC_PIC_PARAMS* pPicParams)
{
    NVENCSTATUS nvStatus = NV_ENC_SUCCESS;
    if (!IsHWEncoderInitialized())
    {
        NVENC_THROW_ERROR("Encoder device not found", NV_ENC_ERR_NO_ENCODE_DEVICE);
    }

    int inBufIdx = pPicParams->frameIdx % (m_nEncoderBuffer);
    int outBufIdx = m_iToSendAllIterations % (m_nEncoderBuffer + m_nNumIterations + m_nExtraOutputBuffers); 
    int recBufIdx = outBufIdx;
    if(m_initializeParams.encodeGUID == NV_ENC_CODEC_AV1_GUID)
        recBufIdx = pPicParams->stateBufferIdx;

    NvEncIterationData iterData;
    iterData.iterNum = m_iToSendAllIterations;
    iterData.compIdx = outBufIdx;
    iterData.dispIdx = pPicParams->frameIdx;
    iterData.inputIdx = inBufIdx;
    iterData.outputIdx = outBufIdx;
    iterData.recIdx = recBufIdx; 
    iterData.stateIdx = pPicParams->stateBufferIdx;
    m_EncMultipleStates.encIterationData.push_back(iterData);
    m_EncMultipleStates.stateBufUsage[pPicParams->stateBufferIdx] = true;

    MapResources(inBufIdx);    
    MapResourcesRecon(recBufIdx);
    nvStatus = DoEncode(m_vMappedInputBuffers[inBufIdx], m_vBitstreamOutputBuffer[outBufIdx], pPicParams, m_vMappedReconBuffers[recBufIdx], outBufIdx);

    m_iToSendAllIterations++;

    return nvStatus;
}

uint32_t NvEncoderCudaIterative::GetNewStateIdxBuf()
{
    for (size_t i = 0; i < m_nNumEncStates; i++)
    {
        if(!m_EncMultipleStates.stateBufUsage[i])
            return i;
    }
    return -1;
}

void NvEncoderCudaIterative::InitStateBufferUsage(uint32_t nNumStateIdxBuffers)
{
    for (size_t i = 0; i < nNumStateIdxBuffers; i++)
        m_EncMultipleStates.stateBufUsage.push_back(false);
}

void NvEncoderCudaIterative::InitVPackets(uint32_t nNumIters)
{
    for (size_t i = 0; i < nNumIters; i++)
        m_vPackets.push_back(std::vector<std::vector<uint8_t>>());
}


void NvEncoderCudaIterative::MapResources(uint32_t bfrIdx)
{
    NV_ENC_MAP_INPUT_RESOURCE mapInputResource = { NV_ENC_MAP_INPUT_RESOURCE_VER };

    mapInputResource.registeredResource = m_vRegisteredResources[bfrIdx];
    NVENC_API_CALL(m_nvenc.nvEncMapInputResource(m_hEncoder, &mapInputResource));
    m_vMappedInputBuffers[bfrIdx] = mapInputResource.mappedResource;
}

void NvEncoderCudaIterative::MapResourcesRecon(uint32_t bfrIdx)
{
    NV_ENC_MAP_INPUT_RESOURCE mapInputResource = { NV_ENC_MAP_INPUT_RESOURCE_VER };

    mapInputResource.registeredResource = m_vRegisteredResourcesReconFrames[bfrIdx];
    NVENC_API_CALL(m_nvenc.nvEncMapInputResource(m_hEncoder, &mapInputResource));
    m_vMappedReconBuffers[bfrIdx] = mapInputResource.mappedResource;

}

void NvEncoderCudaIterative::UnmapResources(uint32_t bfrIdx)
{
    if (m_vMappedInputBuffers[bfrIdx])
    {
        NVENC_API_CALL(m_nvenc.nvEncUnmapInputResource(m_hEncoder, m_vMappedInputBuffers[bfrIdx]));
        m_vMappedInputBuffers[bfrIdx] = nullptr;
    }
}


void NvEncoderCudaIterative::UnmapResourcesRecon(uint32_t bfrIdx)
{
    if (m_vMappedReconBuffers[bfrIdx])
    {
        NVENC_API_CALL(m_nvenc.nvEncUnmapInputResource(m_hEncoder, m_vMappedReconBuffers[bfrIdx]));
        m_vMappedReconBuffers[bfrIdx] = nullptr;
    }
}

void NvEncoderCudaIterative::collectFrameStats(void* stats)
{
    NV_ENC_OUTPUT_STATS_BLOCK* outData = reinterpret_cast<NV_ENC_OUTPUT_STATS_BLOCK*>(stats);
    float accumQP = 0.0;
    uint64_t accumBits = 0;
    for (size_t b = 0; b < m_EncMultipleStates.statsInfo.numElems; b++)
    {
        accumQP += static_cast<uint32_t>(outData[b].QP);
        accumBits += static_cast<uint32_t>(outData[b].bitcount);
    }
    m_EncMultipleStates.avgQPFrame.push_back(accumQP/m_EncMultipleStates.statsInfo.numElems);
    m_EncMultipleStates.totalBitsFrame.push_back(accumBits);
}

NVENCSTATUS NvEncoderCudaIterative::EncodeFrameExternalLookahead(uint32_t frameIdx)
{
    NVENCSTATUS nvStatus = NV_ENC_SUCCESS;
    int inBufIdx = frameIdx % (m_nEncoderBuffer);
    MapResources(inBufIdx);

    NV_ENC_LOOKAHEAD_PIC_PARAMS lookaheadPicPrarams;
    memset(&lookaheadPicPrarams, 0, sizeof(NV_ENC_LOOKAHEAD_PIC_PARAMS));
    lookaheadPicPrarams.version = NV_ENC_LOOKAHEAD_PIC_PARAMS_VER;
    lookaheadPicPrarams.inputBuffer = m_vMappedInputBuffers[inBufIdx];
    nvStatus = m_nvenc.nvEncLookaheadPicture(m_hEncoder, &lookaheadPicPrarams);

    return nvStatus;
}

void NvEncoderCudaIterative::updateQualParam(int32_t &currentQualParam, int32_t delta, bool& reachedLimit, NV_ENC_RECONFIGURE_PARAMS* reconfigureParams)
{
    int32_t maxQualParam = 0, minQualParam = 0;

    if(m_initializeParams.encodeConfig->rcParams.rateControlMode == NV_ENC_PARAMS_RC_CONSTQP) // contant QP mode
    {
        if(m_initializeParams.encodeGUID == NV_ENC_CODEC_H264_GUID || m_initializeParams.encodeGUID == NV_ENC_CODEC_HEVC_GUID)
            maxQualParam = MAX_QP_HEVC;
        else
            maxQualParam = MAX_QP_AV1;
    }
    else // VBR constant quality mode
        maxQualParam = MAX_CQ;

    int32_t newQualParam = currentQualParam + delta;
    if(newQualParam <= 0 || newQualParam > maxQualParam)
        reachedLimit = true;
    
    if(!reachedLimit)
    {
        currentQualParam += delta;
        if(reconfigureParams->reInitEncodeParams.encodeConfig->rcParams.rateControlMode == NV_ENC_PARAMS_RC_CONSTQP)
        {
            reconfigureParams->reInitEncodeParams.encodeConfig->rcParams.constQP = {uint32_t(currentQualParam), uint32_t(currentQualParam), uint32_t(currentQualParam)};
            std::cout << "New QP = " << currentQualParam << std::endl; 
        }
        else
        {
            reconfigureParams->reInitEncodeParams.encodeConfig->rcParams.targetQuality = uint8_t(currentQualParam);
            std::cout << "New CQ = " << currentQualParam << std::endl;
        }
        Reconfigure(reconfigureParams); 
    }

}

void NvEncoderCudaIterative::EncodeFrameConstantQuality(std::vector<std::vector<uint8_t>> &vPacket, std::vector<CUdeviceptr> vDeviceFrameBuffer, NV_ENC_RECONFIGURE_PARAMS* reconfigureParams, double minTargetQuality, double maxTargetQuality, uint32_t nDeltaQualParam, uint32_t nFrame)
{
    if (!IsHWEncoderInitialized())
    {
        NVENC_THROW_ERROR("Encoder device not found", NV_ENC_ERR_NO_ENCODE_DEVICE);
    }

    if(!nFrame)
        Reconfigure(reconfigureParams); // update initial QP/CQ only for first frame

    if(m_initializeParams.encodeConfig->rcParams.enableExtLookahead && EncodeFrameExternalLookahead(nFrame) != NV_ENC_SUCCESS)
        return;

	int32_t currQualParam = (reconfigureParams->reInitEncodeParams.encodeConfig->rcParams.rateControlMode == NV_ENC_PARAMS_RC_CONSTQP) ?
                            reconfigureParams->reInitEncodeParams.encodeConfig->rcParams.constQP.qpIntra : // get last QP
                            reconfigureParams->reInitEncodeParams.encodeConfig->rcParams.targetQuality;    // get last CQ
    int32_t bestQualParam = currQualParam;
    int32_t deltaQualParam = nDeltaQualParam;
    bool reachedQualParamLimit = false;
    float currQualMetric = 0.0;
    float bestQualMetric = 0.0;

    NVENCSTATUS nvStatus = NV_ENC_SUCCESS;
    std::vector<NvEncIterationData> currItersData;
    uint32_t frameIdxDisplay = 0;
    bool overlayFrame = false;
    uint32_t bestIter = 0, iter = 0;

    NV_ENC_PIC_PARAMS nvEncPicParams;
    memset(&nvEncPicParams, 0, sizeof(nvEncPicParams));
    nvEncPicParams.version = NV_ENC_PIC_PARAMS_VER;
    nvEncPicParams.encodePicFlags |= NV_ENC_PIC_FLAG_OUTPUT_RECON_FRAME;
    nvEncPicParams.encodePicFlags |= NV_ENC_PIC_FLAG_DISABLE_ENC_STATE_ADVANCE;
    nvEncPicParams.stateBufferIdx = GetNewStateIdxBuf();
    nvEncPicParams.frameIdx = m_iToSend; 
    nvStatus = EncodeFrameIteration(&nvEncPicParams); 
    m_iToSend++;
    
    if(nvStatus == NV_ENC_SUCCESS) 
    {
        while(m_iToSend > m_iGot) 
        {
            bestIter = 0;
            iter = 0;
            // check when iteration number reaches the maximum number of iterations
            while (iter < m_nNumIterations)
            {
                if(iter > 0)
                {
                    nvEncPicParams.stateBufferIdx = GetNewStateIdxBuf();
                    nvEncPicParams.frameIdx = frameIdxDisplay;
                    nvStatus = EncodeFrameIteration(&nvEncPicParams); 
                }
                uint32_t recIdx = 0;
                overlayFrame = true; // only for AV1
                GetEncodedPacket(iter, frameIdxDisplay, overlayFrame, recIdx);
                if(overlayFrame)
                    GetEncodedPacket(iter, frameIdxDisplay, overlayFrame, recIdx);
                calcPSNRY(
                    reinterpret_cast<uint8_t*>(vDeviceFrameBuffer[frameIdxDisplay % m_nEncoderBuffer]), 
                    reinterpret_cast<uint8_t*>((CUdeviceptr)m_vReconFrames[recIdx].inputPtr),  
                    m_nWidth, m_nHeight, m_cudaPitch, currQualMetric
                );
                std::cout << "frameIdxDisplay = " << frameIdxDisplay << 
                             " iter = " << iter << 
                             " avgQP = " << m_EncMultipleStates.avgQPFrame.back() << 
                             " totalBits = " << m_EncMultipleStates.totalBitsFrame.back() << 
                             " PSNR-Y = " << currQualMetric << std::endl;
                bestQualMetric = currQualMetric; 
                bestQualParam = currQualParam;
                bestIter = iter;
                // check when metric hits the target range 
                // for the last iteration QP/CQ is adjusted, however it only takes effect the next time EncodeFrameIteration is called
                if(!(currQualMetric > minTargetQuality && currQualMetric < maxTargetQuality))
                {
                    if(currQualMetric < minTargetQuality)
                        deltaQualParam = -nDeltaQualParam;
                    else
                        deltaQualParam = nDeltaQualParam;
                    updateQualParam(currQualParam, deltaQualParam, reachedQualParamLimit, reconfigureParams);
                    if(reachedQualParamLimit)
                        break;
                }
                // skip remaining iteration if the target range was reached
                else
                    break; 
                iter++;
            }
            currItersData = m_EncMultipleStates.findIterations(frameIdxDisplay);
            RestoreEncoderState(bestIter, currItersData);
            
            for (size_t i = 0; i < m_vPackets[bestIter].size(); i++)
                vPacket.push_back(m_vPackets[bestIter][i]);
            m_iGot++;
        }

    }
}

void NvEncoderCudaIterative::RestoreEncoderState(uint32_t bestIter, std::vector<NvEncIterationData> currItersData)
{
    uint32_t selectedStateIdx = currItersData[bestIter].stateIdx;

    NV_ENC_RESTORE_ENCODER_STATE_PARAMS restoreState;
    memset(&restoreState, 0, sizeof(NV_ENC_RESTORE_ENCODER_STATE_PARAMS));
    restoreState.version = NV_ENC_RESTORE_ENCODER_STATE_PARAMS_VER;
    restoreState.bufferIdx = selectedStateIdx;
    restoreState.state = NV_ENC_STATE_RESTORE_FULL;

    NVENCSTATUS nvStatus = m_nvenc.nvEncRestoreEncoderState(m_hEncoder, &restoreState);

    if (nvStatus == NV_ENC_ERR_NEED_MORE_OUTPUT) // AV1 only
    {
        int outBufIdx = m_iToSendAllIterations % (m_nEncoderBuffer + m_nNumIterations + m_nExtraOutputBuffers); 
        memset(&restoreState, 0, sizeof(NV_ENC_RESTORE_ENCODER_STATE_PARAMS));
        restoreState.version = NV_ENC_RESTORE_ENCODER_STATE_PARAMS_VER;
        restoreState.bufferIdx = selectedStateIdx;
        restoreState.state = NV_ENC_STATE_RESTORE_FULL;
        restoreState.outputBitstream = m_vBitstreamOutputBuffer[outBufIdx];
        restoreState.completionEvent = GetCompletionEvent(outBufIdx);
        nvStatus = m_nvenc.nvEncRestoreEncoderState(m_hEncoder, &restoreState);
        m_iToSendAllIterations++;
    }
    
    UnmapResources(m_iGot % m_nEncoderBuffer);

    for (size_t i = 0; i < currItersData.size(); i++)
    {
        m_EncMultipleStates.stateBufUsage[currItersData[i].stateIdx] = false;
        std::vector<uint32_t> idxToRemove;
        for (size_t j = 0; j < m_EncMultipleStates.encIterationData.size(); j++)
        {
            if(m_EncMultipleStates.encIterationData[j].iterNum == currItersData[i].iterNum)
            {
                m_EncMultipleStates.encIterationData.erase(m_EncMultipleStates.encIterationData.begin() + j);
                break;
            }
        }
    }
}

int NvEncoderCudaIterative::GetFrameSize(uint32_t pitch) const
{
    switch (GetPixelFormat())
    {
    case NV_ENC_BUFFER_FORMAT_YV12:
    case NV_ENC_BUFFER_FORMAT_IYUV:
    case NV_ENC_BUFFER_FORMAT_NV12:
        return pitch * (GetEncodeHeight() + (GetEncodeHeight() + 1) / 2);
    case NV_ENC_BUFFER_FORMAT_NV16:
        return pitch * GetEncodeHeight() * 2;
    case NV_ENC_BUFFER_FORMAT_YUV420_10BIT:
        return pitch * (GetEncodeHeight() + (GetEncodeHeight() + 1) / 2);
    case NV_ENC_BUFFER_FORMAT_P210:
        return pitch * GetEncodeHeight() * 2;
    case NV_ENC_BUFFER_FORMAT_YUV444:
        return pitch * GetEncodeHeight() * 3;
    case NV_ENC_BUFFER_FORMAT_YUV444_10BIT:
        return pitch * GetEncodeHeight() * 3;
    case NV_ENC_BUFFER_FORMAT_ARGB:
    case NV_ENC_BUFFER_FORMAT_ARGB10:
    case NV_ENC_BUFFER_FORMAT_AYUV:
    case NV_ENC_BUFFER_FORMAT_ABGR:
    case NV_ENC_BUFFER_FORMAT_ABGR10:
        return pitch * GetEncodeHeight();
    default:
        NVENC_THROW_ERROR("Invalid Buffer format", NV_ENC_ERR_INVALID_PARAM);
        return 0;
    }
}

int NvEncoderCudaIterative::GetFrameSize() const
{
    switch (GetPixelFormat())
    {
    case NV_ENC_BUFFER_FORMAT_YV12:
    case NV_ENC_BUFFER_FORMAT_IYUV:
    case NV_ENC_BUFFER_FORMAT_NV12:
        return GetEncodeWidth() * (GetEncodeHeight() + (GetEncodeHeight() + 1) / 2);
    case NV_ENC_BUFFER_FORMAT_NV16:
        return GetEncodeWidth() * GetEncodeHeight() * 2;
    case NV_ENC_BUFFER_FORMAT_YUV420_10BIT:
        return 2 * GetEncodeWidth() * (GetEncodeHeight() + (GetEncodeHeight() + 1) / 2);
    case NV_ENC_BUFFER_FORMAT_P210:
        return 2 * GetEncodeWidth() * GetEncodeHeight() * 2;
    case NV_ENC_BUFFER_FORMAT_YUV444:
        return GetEncodeWidth() * GetEncodeHeight() * 3;
    case NV_ENC_BUFFER_FORMAT_YUV444_10BIT:
        return 2 * GetEncodeWidth() * GetEncodeHeight() * 3;
    case NV_ENC_BUFFER_FORMAT_ARGB:
    case NV_ENC_BUFFER_FORMAT_ARGB10:
    case NV_ENC_BUFFER_FORMAT_AYUV:
    case NV_ENC_BUFFER_FORMAT_ABGR:
    case NV_ENC_BUFFER_FORMAT_ABGR10:
        return 4 * GetEncodeWidth() * GetEncodeHeight();
    default:
        NVENC_THROW_ERROR("Invalid Buffer format", NV_ENC_ERR_INVALID_PARAM);
        return 0;
    }
}

void NvEncoderCudaIterative::GetEncodedPacket(uint32_t currIter, uint32_t &frameIdxDisplay, bool &overlayFrame, uint32_t &recIdx)
{
    int outBfrIdx = m_iGotAllIterations % (m_nEncoderBuffer + m_nNumIterations + m_nExtraOutputBuffers);
    
    unsigned i = 0;
    m_vPackets[currIter].clear();

    WaitForCompletionEvent(outBfrIdx);
    NV_ENC_LOCK_BITSTREAM lockBitstreamData = { NV_ENC_LOCK_BITSTREAM_VER };
    lockBitstreamData.outputBitstream = m_vBitstreamOutputBuffer[outBfrIdx];
    lockBitstreamData.doNotWait = false;
    lockBitstreamData.outputStatsPtrSize = m_EncMultipleStates.statsInfo.totalSize;
    lockBitstreamData.outputStatsPtr = m_EncMultipleStates.statsData[currIter];

    NVENC_API_CALL(m_nvenc.nvEncLockBitstream(m_hEncoder, &lockBitstreamData));

    uint8_t *pData = (uint8_t *)lockBitstreamData.bitstreamBufferPtr;

    uint32_t frameWidth = GetEncodeWidth();
    uint32_t frameHeight = GetEncodeHeight();
    uint32_t framePitch = m_cudaPitch;
    uint32_t frameSizeWithPitch = GetFrameSize(m_cudaPitch);
    frameIdxDisplay = lockBitstreamData.frameIdxDisplay;
    std::vector<NvEncIterationData> iterInfo = m_EncMultipleStates.findIterations(frameIdxDisplay);
    if (m_initializeParams.encodeGUID == NV_ENC_CODEC_AV1_GUID)
    {
        if(!iterInfo.empty())
        {
            recIdx = iterInfo[currIter].recIdx;
            overlayFrame = false;
        }
        else
            overlayFrame = true;
    }
    else
    {
        recIdx = outBfrIdx;
        overlayFrame = false;
    }

    if (m_vPackets[currIter].size() < i + 1)
    {
        m_vPackets[currIter].push_back(std::vector<uint8_t>());
    }
    m_vPackets[currIter][i].clear();
    
    if ((m_initializeParams.encodeGUID == NV_ENC_CODEC_AV1_GUID) && (m_bUseIVFContainer))
    {
        if(lockBitstreamData.frameIdxDisplay)
        {
            m_bWriteIVFFileHeader = false;
        }
        if (m_bWriteIVFFileHeader)
        {
            m_IVFUtils.WriteFileHeader(m_vPackets[currIter][i], MAKE_FOURCC('A', 'V', '0', '1'), m_initializeParams.encodeWidth, m_initializeParams.encodeHeight, m_initializeParams.frameRateNum, m_initializeParams.frameRateDen, 0xFFFF);
        }

        m_IVFUtils.WriteFrameHeader(m_vPackets[currIter][i], lockBitstreamData.bitstreamSizeInBytes, lockBitstreamData.outputTimeStamp);
    }
    m_vPackets[currIter][i].insert(m_vPackets[currIter][i].end(), &pData[0], &pData[lockBitstreamData.bitstreamSizeInBytes]);

    if(!overlayFrame)
    {   
        UnmapResourcesRecon(recIdx);
        collectFrameStats(m_EncMultipleStates.statsData[currIter]); // collect/calculate stats using Stats APIs
    }

    NVENC_API_CALL(m_nvenc.nvEncUnlockBitstream(m_hEncoder, lockBitstreamData.outputBitstream));

    m_iGotAllIterations++;

}

NVENCSTATUS NvEncoderCudaIterative::DoEncode(NV_ENC_INPUT_PTR inputBuffer, NV_ENC_OUTPUT_PTR outputBuffer, NV_ENC_PIC_PARAMS *pPicParams,  NV_ENC_INPUT_PTR reconBuffer, uint32_t compIdx)
{
    NV_ENC_PIC_PARAMS picParams = {};
    if (pPicParams)
    {
        picParams = *pPicParams;
    }
    picParams.version = NV_ENC_PIC_PARAMS_VER;
    picParams.pictureStruct = NV_ENC_PIC_STRUCT_FRAME;
    picParams.inputBuffer = inputBuffer;
    picParams.bufferFmt = GetPixelFormat();
    picParams.inputWidth = GetEncodeWidth();
    picParams.inputHeight = GetEncodeHeight();
    picParams.outputBitstream = outputBuffer;
    picParams.outputReconBuffer = reconBuffer;
    picParams.completionEvent = GetCompletionEvent(compIdx);
    NVENCSTATUS nvStatus = m_nvenc.nvEncEncodePicture(m_hEncoder, &picParams);

    return nvStatus; 
}

void NvEncoderCudaIterative::EndEncode(std::vector<std::vector<uint8_t>> &vPacket, std::vector<CUdeviceptr> vDeviceFrameBuffer, NV_ENC_RECONFIGURE_PARAMS* reconfigureParams, double minTargetQuality, double maxTargetQuality, uint32_t nDeltaQualParam, uint32_t nFrame)
{
    if (!IsHWEncoderInitialized())
    {
        NVENC_THROW_ERROR("Encoder device not initialized", NV_ENC_ERR_ENCODER_NOT_INITIALIZED);
    }

    // if external lookahead was used there are pending frames to be encoded
    if(m_initializeParams.encodeConfig->rcParams.enableExtLookahead) 
    {
        uint32_t lookaheadDepth = m_initializeParams.encodeConfig->rcParams.lookaheadDepth;
        for (size_t i = 0; i < lookaheadDepth; i++) 
            EncodeFrameConstantQuality(vPacket, vDeviceFrameBuffer, reconfigureParams, minTargetQuality, maxTargetQuality, nDeltaQualParam, nFrame);
    }

    SendEOS();

    uint32_t recIdx = 0;
    // no reencoding is possible for these last frames but it is still possible to check the recon frame and calculate the quality
    if(m_iToSend > m_iGot) // we have the last pending frames 
    {
        uint32_t currPendingFrame = 0;
        uint32_t frameIdxDisplay = 0;
        float qualMetric = 0.0;
        while(m_iToSend > m_iGot) 
        { 
            bool overlayFrame = true;
            GetEncodedPacket(0, frameIdxDisplay, overlayFrame, recIdx);
            if(overlayFrame)
                GetEncodedPacket(0, frameIdxDisplay, overlayFrame, recIdx);
            calcPSNRY(
                    reinterpret_cast<uint8_t*>(vDeviceFrameBuffer[frameIdxDisplay % m_nEncoderBuffer]), 
                    reinterpret_cast<uint8_t*>((CUdeviceptr)m_vReconFrames[recIdx].inputPtr), 
                    m_nWidth, m_nHeight, m_cudaPitch, qualMetric
                );
            std::cout << "frameIdxDisplay = " << frameIdxDisplay << 
                         " iter = " << 0 << 
                         " avgQP = " << m_EncMultipleStates.avgQPFrame.back() << 
                         " totalBits = " << m_EncMultipleStates.totalBitsFrame.back() << 
                         " PSNR-Y = " << qualMetric << std::endl;
            for (size_t i = 0; i < m_vPackets[0].size(); i++)
                vPacket.push_back(m_vPackets[0][i]);
            m_iGot++;
        }
    }
}

void NvEncoderCudaIterative::DestroyEncoder()
{
    if (!m_hEncoder)
    {
        return;
    }

    for (size_t i = 0; i < m_nNumIterations; i++)
        free(m_EncMultipleStates.statsData[i]);

    NvEncoder::DestroyEncoder();
}
