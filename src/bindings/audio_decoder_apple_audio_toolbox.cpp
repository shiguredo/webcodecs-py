#include "audio_decoder.h"

#if defined(__APPLE__)
#include <AudioToolbox/AudioToolbox.h>
#include <cstring>
#include <stdexcept>
#include <vector>

#include "audio_data.h"
#include "encoded_audio_chunk.h"

namespace {
// AAC デコーダーの定数
// AAC-LC は 1024 サンプル/フレーム
constexpr uint32_t AAC_SAMPLES_PER_FRAME = 1024;

// AudioConverter のコールバック
// 入力データを提供するコールバック
OSStatus aac_decoder_input_data_proc(
    AudioConverterRef converter,
    UInt32* number_data_packets,
    AudioBufferList* data,
    AudioStreamPacketDescription** data_packet_description,
    void* user_data) {
  (void)converter;

  auto* decoder = reinterpret_cast<AudioDecoder*>(user_data);
  return decoder->aac_provide_input_data(number_data_packets, data,
                                         data_packet_description);
}
}  // namespace

OSStatus AudioDecoder::aac_provide_input_data(
    UInt32* number_data_packets,
    AudioBufferList* data,
    AudioStreamPacketDescription** data_packet_description) {
  if (aac_input_buffer_.empty()) {
    *number_data_packets = 0;
    return noErr;
  }

  // 1 パケットを提供
  *number_data_packets = 1;

  data->mBuffers[0].mData = aac_input_buffer_.data();
  data->mBuffers[0].mDataByteSize =
      static_cast<UInt32>(aac_input_buffer_.size());
  data->mBuffers[0].mNumberChannels = config_.number_of_channels;

  // パケット記述子を設定
  if (data_packet_description) {
    aac_packet_description_.mStartOffset = 0;
    aac_packet_description_.mVariableFramesInPacket = 0;
    aac_packet_description_.mDataByteSize =
        static_cast<UInt32>(aac_input_buffer_.size());
    *data_packet_description = &aac_packet_description_;
  }

  // データを消費したことを示す
  aac_input_buffer_.clear();

  return noErr;
}

void AudioDecoder::init_aac_decoder() {
  // 入力フォーマットを設定 (AAC)
  AudioStreamBasicDescription input_format = {};
  input_format.mSampleRate = config_.sample_rate;
  input_format.mFormatID = kAudioFormatMPEG4AAC;
  input_format.mFormatFlags = 0;
  input_format.mBytesPerPacket = 0;
  input_format.mFramesPerPacket = AAC_SAMPLES_PER_FRAME;
  input_format.mBytesPerFrame = 0;
  input_format.mChannelsPerFrame = config_.number_of_channels;
  input_format.mBitsPerChannel = 0;

  // 出力フォーマットを設定 (PCM float)
  AudioStreamBasicDescription output_format = {};
  output_format.mSampleRate = config_.sample_rate;
  output_format.mFormatID = kAudioFormatLinearPCM;
  output_format.mFormatFlags =
      kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
  output_format.mBytesPerPacket = sizeof(float) * config_.number_of_channels;
  output_format.mFramesPerPacket = 1;
  output_format.mBytesPerFrame = sizeof(float) * config_.number_of_channels;
  output_format.mChannelsPerFrame = config_.number_of_channels;
  output_format.mBitsPerChannel = 32;

  // AudioConverter を作成
  OSStatus status =
      AudioConverterNew(&input_format, &output_format, &aac_converter_);
  if (status != noErr) {
    throw std::runtime_error(
        "Failed to create AudioConverter for AAC decoding: " +
        std::to_string(status));
  }

  aac_input_buffer_.clear();
}

void AudioDecoder::decode_frame_aac(const EncodedAudioChunk& chunk) {
  if (!aac_converter_) {
    throw std::runtime_error("AAC decoder not initialized");
  }

  auto encoded_data = chunk.data_vector();

  // 入力バッファにデータを設定
  aac_input_buffer_ = encoded_data;

  // 出力バッファを準備
  // AAC-LC は 1024 サンプル/フレーム
  std::vector<float> output_buffer(AAC_SAMPLES_PER_FRAME *
                                   config_.number_of_channels);

  AudioBufferList output_buffer_list;
  output_buffer_list.mNumberBuffers = 1;
  output_buffer_list.mBuffers[0].mNumberChannels = config_.number_of_channels;
  output_buffer_list.mBuffers[0].mDataByteSize =
      static_cast<UInt32>(output_buffer.size() * sizeof(float));
  output_buffer_list.mBuffers[0].mData = output_buffer.data();

  UInt32 output_frames = AAC_SAMPLES_PER_FRAME;

  OSStatus status = AudioConverterFillComplexBuffer(
      aac_converter_, aac_decoder_input_data_proc, this, &output_frames,
      &output_buffer_list, nullptr);

  if (status != noErr && status != kAudioConverterErr_InvalidInputSize) {
    throw std::runtime_error("AAC decoding failed: " + std::to_string(status));
  }

  if (output_frames > 0) {
    // デコードされたフレームを処理
    auto audio_data = AudioData::create_with_buffer(
        config_.number_of_channels, config_.sample_rate, output_frames,
        AudioSampleFormat::F32, chunk.timestamp());

    float* dst = reinterpret_cast<float*>(audio_data->mutable_data());
    std::memcpy(dst, output_buffer.data(),
                static_cast<size_t>(output_frames) *
                    config_.number_of_channels * sizeof(float));

    handle_decoded_frame(std::move(audio_data));
  }
}

void AudioDecoder::cleanup_aac_decoder() {
  if (aac_converter_) {
    AudioConverterDispose(aac_converter_);
    aac_converter_ = nullptr;
  }
  aac_input_buffer_.clear();
}

#endif  // defined(__APPLE__)
