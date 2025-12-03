/*
 * This copyright notice applies to this header file only:
 *
 * Copyright (c) 2010-2023 NVIDIA Corporation
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

#pragma once

#include <vector>
#include "nvEncodeAPI.h"
#include <stdint.h>
#include <mutex>
#include <string>
#include <iostream>
#include <sstream>
#include <string.h>
#include <math.h>
#include "NvEncoder/NvEncoderCuda.h"
#include "../Utils/Metrics.h"

#define MAX_QP_H264 51
#define MAX_QP_HEVC 51
#define MAX_QP_AV1 255
#define MAX_CQ 51
#define MAX_NUM_ENC_STATE_BUF_AV1 32
#define MAX_NUM_ENC_STATE_BUF_HEVC 16
#define MAX_NUM_ENC_STATE_BUF_H264 16

struct NvEncIterationData
{
    uint32_t iterNum;
    uint32_t inputIdx;
    uint32_t outputIdx;
    uint32_t compIdx;
    uint32_t stateIdx;
    uint32_t dispIdx;
    uint32_t recIdx;
};

struct NvEncBlockStatsInfo
{
    NV_ENC_OUTPUT_STATS_LEVEL level;
    uint32_t sizePerLevel;
    uint32_t numElems;
    uint32_t totalSize;
    uint32_t elemDim;

    uint32_t getLevelSize() 
    {
        switch (level)
        {
        case NV_ENC_OUTPUT_STATS_ROW_LEVEL:
            return sizeof(NV_ENC_OUTPUT_STATS_ROW);
        case NV_ENC_OUTPUT_STATS_BLOCK_LEVEL:
            return sizeof(NV_ENC_OUTPUT_STATS_BLOCK);
        default:
            return 0;
        }
    }

    void calcNumOfElems(uint32_t w, uint32_t h)
    {
        uint32_t frameWidthInMb = (w + elemDim - 1) / elemDim;
        uint32_t frameHeightInMb = (h + elemDim - 1) / elemDim;
        switch (level)
        {
        case NV_ENC_OUTPUT_STATS_ROW_LEVEL:
            numElems = frameHeightInMb;
            break;
        case NV_ENC_OUTPUT_STATS_BLOCK_LEVEL:
            numElems = frameWidthInMb * frameHeightInMb;
            break;
        }
    }
    void calcSize(uint32_t w, uint32_t h, uint32_t b) { 
        elemDim = b;
        sizePerLevel = getLevelSize();
        calcNumOfElems(w, h); 
        totalSize = numElems * sizePerLevel;
    }
    uint32_t getTotalSize() {return totalSize;}
};

struct NvEncMultipleStates
{
    std::vector<NvEncIterationData> encIterationData;
    std::vector<bool> stateBufUsage;
    std::vector<float> qualMetric;
    std::vector<float> avgQPFrame;
    std::vector<uint64_t> totalBitsFrame;
    NvEncBlockStatsInfo statsInfo;
    std::vector<void*> statsData;

    std::vector<NvEncIterationData> findIterations(uint32_t dispIdx)
    {
        std::vector<NvEncIterationData> iterations;
        for (size_t i = 0; i < encIterationData.size(); ++i)
        {
            const NvEncIterationData& element = encIterationData[i];
            if (element.dispIdx == dispIdx)
            {
                iterations.push_back(element); 
            }
        }
        return iterations; 
    }
};

/**
* @brief Class for encode or ME only output in video memory feature for Cuda interfaces.
*/
class NvEncoderCudaIterative : public NvEncoderCuda
{
public:
    /**
    *  @brief  NvEncoderOutputInVidMem class constructor.
    */
    NvEncoderCudaIterative(CUcontext cuContext, uint32_t nWidth, uint32_t nHeight, NV_ENC_BUFFER_FORMAT eBufferFormat);

    /**
    *  @brief  NvEncoder class virtual destructor.
    */
    ~NvEncoderCudaIterative();

    /**
    *  @brief This function is used to initialize the encoder session.
    *  Application must call this function to initialize the encoder, before
    *  starting to encode or motion estimate any frames.
    */
    void CreateEncoder(const NV_ENC_INITIALIZE_PARAMS* pEncoderParams) override;

    /**
    *  @brief  This function is used to encode a frame.
    *  Applications must call EncodeFrame() function to encode the uncompressed
    *  data, which has been copied to an input buffer obtained from the
    *  GetNextInputFrame() function. 
    *  This function returns video memory buffer pointers containing compressed data
    *  in pOutputBuffer. If there is buffering enabled, this may return without 
    *  any data in pOutputBuffer.
    */
    void EncodeFrameConstantQuality(std::vector<std::vector<uint8_t>> &vPacket, std::vector<CUdeviceptr> vDeviceFrameBuffer, NV_ENC_RECONFIGURE_PARAMS* reconfigureParams, double minTargetQuality, double maxTargetQuality, uint32_t nQPDelta, uint32_t nFrame);

    NVENCSTATUS EncodeFrameExternalLookahead(uint32_t frameIdx);

    NVENCSTATUS EncodeFrameIteration(NV_ENC_PIC_PARAMS* picParams);

    /**
    *  @brief  This function to flush the encoder queue.
    *  The encoder might be queuing frames for B picture encoding or lookahead;
    *  the application must call EndEncode() to get all the queued encoded frames
    *  from the encoder. The application must call this function before destroying
    *  an encoder session. Video memory buffer pointer containing compressed data
    *  is returned in pOutputBuffer.
    */
    void EndEncode(std::vector<std::vector<uint8_t>> &vPacket, std::vector<CUdeviceptr> vDeviceFrameBuffer, NV_ENC_RECONFIGURE_PARAMS* reconfigureParams, double minTargetQuality, double maxTargetQuality, uint32_t nQPDelta, uint32_t nFrame);

    /**
    *  @brief  This function is used to destroy the encoder session.
    *  Application must call this function to destroy the encoder session and
    *  clean up any allocated resources. The application must call EndEncode()
    *  function to get any queued encoded frames before calling DestroyEncoder().
    */
    void DestroyEncoder();

    /**
    *  @brief This function is used to get the size of output buffer required to be 
    *  allocated in order to store the output.
    */
    uint32_t GetOutputBufferSize();

    /**
    *  @brief This function is used to map the input and output buffers to NvEncodeAPI.
    */
    void MapResources(uint32_t bfrIdx);
    void MapResourcesRecon(uint32_t bfrIdx);     
    void UnmapResources(uint32_t bfrIdx);
    void UnmapResourcesRecon(uint32_t bfrIdx);

    /**
    *  @brief This is a private function which is used to get video memory buffer pointer containing compressed data
    *         or motion estimation output from the encoder HW.
    *  This is called by EncodeFrame() function. If there is buffering enabled,
    *  this may return without any output data.
    */
    void GetEncodedPacket(uint32_t currIter, uint32_t &frameIdxDisplay, bool &overlayFrame, uint32_t &recIdxOut);

    uint32_t GetNewStateIdxBuf();
    uint32_t GetNumOfEncStates(){return m_nNumEncStates;};

    void InitStateBufferUsage(uint32_t nNumIterations);

    void InitVPackets(uint32_t nNumIters);

    void RestoreEncoderState(uint32_t bestIter, std::vector<NvEncIterationData> currItersData);

    void InitializeBitstreamBuffers(uint32_t nNumBitStreamBuffers);
    void DestroyBitstreamBuffers();
    void RegisterInputResources(std::vector<void*> inputframes, NV_ENC_INPUT_RESOURCE_TYPE eResourceType, int width, int height, int pitch, 
                                NV_ENC_BUFFER_FORMAT bufferFormat, bool bReferenceFrame, NV_ENC_BUFFER_USAGE bufferUsage);   
    void AllocateBuffers(int32_t numInputBuffers, NV_ENC_BUFFER_USAGE bufferUsage);
    NVENCSTATUS DoEncode(NV_ENC_INPUT_PTR inputBuffer, NV_ENC_OUTPUT_PTR outputBuffer, NV_ENC_PIC_PARAMS *pPicParams,  NV_ENC_INPUT_PTR reconBuffer, uint32_t compIdx);
    int GetFrameSize(uint32_t pitch) const;
    int GetFrameSize() const;
    
    size_t GetCUDAPitch(){return m_cudaPitch;};
    void updateQualParam(int32_t &currentQP, int32_t deltaQP, bool& reachedLimit, NV_ENC_RECONFIGURE_PARAMS* reconfigureParams);
    void collectFrameStats(void* stats);

private:
    NvEncMultipleStates m_EncMultipleStates;
    std::vector<NV_ENC_INPUT_PTR> m_vMappedReconBuffers;
    std::vector<NV_ENC_REGISTERED_PTR> m_vRegisteredResourcesReconFrames;
    std::vector<NvEncInputFrame> m_vReconFrames;

    uint32_t m_nExtraOutputBuffers = 0; // AV1 only - may need extra buffer
    uint32_t m_nNumEncStates = 0; // number of encoder states. This is higher than number of iterations when B frames are used
    uint32_t m_nNumIterations = 0; // max number of iterations per frame

    int32_t m_iToSendAllIterations = 0; // same as m_iToSend but includes all iterations
    int32_t m_iGotAllIterations = 0;    // same as m_iGot but includes all iterations
    std::vector<std::vector<std::vector<uint8_t>>> m_vPackets; // keep track packets per iteration
};
