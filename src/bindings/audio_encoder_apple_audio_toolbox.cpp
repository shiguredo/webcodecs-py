#include "audio_encoder.h"

#if defined(__APPLE__)
#include <AudioToolbox/AudioToolbox.h>
#include <cstring>
#include <stdexcept>
#include <vector>

#include "audio_data.h"
#include "encoded_audio_chunk.h"

namespace {
// AAC エンコーダーの定数
// AAC-LC は 1024 サンプル/フレーム
constexpr uint32_t AAC_SAMPLES_PER_FRAME = 1024;

// AudioConverter のコールバック
// 入力データを提供するコールバック
OSStatus aac_input_data_proc(
    AudioConverterRef converter,
    UInt32* number_data_packets,
    AudioBufferList* data,
    AudioStreamPacketDescription** data_packet_description,
    void* user_data) {
  (void)converter;
  (void)data_packet_description;

  auto* encoder = reinterpret_cast<AudioEncoder*>(user_data);
  return encoder->aac_provide_input_data(number_data_packets, data);
}
}  // namespace

OSStatus AudioEncoder::aac_provide_input_data(UInt32* number_data_packets,
                                              AudioBufferList* data) {
  if (aac_input_buffer_.empty()) {
    *number_data_packets = 0;
    return noErr;
  }

  // 要求されたパケット数を計算
  uint32_t available_frames = static_cast<uint32_t>(aac_input_buffer_.size()) /
                              config_.number_of_channels;
  uint32_t frames_to_provide = std::min(available_frames, *number_data_packets);

  if (frames_to_provide == 0) {
    *number_data_packets = 0;
    return noErr;
  }

  // データを提供
  size_t bytes_to_provide =
      frames_to_provide * config_.number_of_channels * sizeof(float);
  data->mBuffers[0].mData = aac_input_buffer_.data();
  data->mBuffers[0].mDataByteSize = static_cast<UInt32>(bytes_to_provide);
  data->mBuffers[0].mNumberChannels = config_.number_of_channels;

  *number_data_packets = frames_to_provide;

  // 提供したデータを削除
  size_t samples_to_remove = frames_to_provide * config_.number_of_channels;
  aac_input_buffer_.erase(
      aac_input_buffer_.begin(),
      aac_input_buffer_.begin() + static_cast<ptrdiff_t>(samples_to_remove));

  return noErr;
}

void AudioEncoder::init_aac_encoder() {
  // 入力フォーマットを設定 (PCM インターリーブ)
  AudioStreamBasicDescription input_format = {};
  input_format.mSampleRate = config_.sample_rate;
  input_format.mFormatID = kAudioFormatLinearPCM;
  input_format.mFormatFlags =
      kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
  input_format.mBytesPerPacket = sizeof(float) * config_.number_of_channels;
  input_format.mFramesPerPacket = 1;
  input_format.mBytesPerFrame = sizeof(float) * config_.number_of_channels;
  input_format.mChannelsPerFrame = config_.number_of_channels;
  input_format.mBitsPerChannel = 32;

  // 出力フォーマットを設定 (AAC)
  AudioStreamBasicDescription output_format = {};
  output_format.mSampleRate = config_.sample_rate;
  output_format.mFormatID = kAudioFormatMPEG4AAC;
  output_format.mFormatFlags = 0;
  output_format.mBytesPerPacket = 0;
  output_format.mFramesPerPacket = AAC_SAMPLES_PER_FRAME;
  output_format.mBytesPerFrame = 0;
  output_format.mChannelsPerFrame = config_.number_of_channels;
  output_format.mBitsPerChannel = 0;

  // AudioConverter を作成
  OSStatus status =
      AudioConverterNew(&input_format, &output_format, &aac_converter_);
  if (status != noErr) {
    throw std::runtime_error(
        "Failed to create AudioConverter for AAC encoding: " +
        std::to_string(status));
  }

  // ビットレートを設定
  UInt32 bitrate = static_cast<UInt32>(config_.bitrate.value_or(128000));
  status = AudioConverterSetProperty(
      aac_converter_, kAudioConverterEncodeBitRate, sizeof(bitrate), &bitrate);
  if (status != noErr) {
    // ビットレートの設定に失敗しても続行
  }

  aac_input_buffer_.clear();
  aac_current_timestamp_ = 0;
  aac_samples_encoded_ = 0;
}

void AudioEncoder::encode_frame_aac(const AudioData& data) {
  if (!aac_converter_) {
    throw std::runtime_error("AAC encoder not initialized");
  }

  // 入力を float に変換
  auto float_data = data.convert_format(AudioSampleFormat::F32);
  uint32_t frame_count = float_data->number_of_frames();

  // タイムスタンプを保存 (最初のフレームの場合)
  if (aac_input_buffer_.empty()) {
    aac_current_timestamp_ = data.timestamp();
  }

  // 入力バッファにデータを追加
  // AudioData から float データを取得
  const float* input_ptr =
      reinterpret_cast<const float*>(float_data->data_ptr());
  size_t total_samples = frame_count * config_.number_of_channels;
  aac_input_buffer_.insert(aac_input_buffer_.end(), input_ptr,
                           input_ptr + total_samples);

  // 十分なデータがある場合はエンコード
  while (aac_input_buffer_.size() >=
         AAC_SAMPLES_PER_FRAME * config_.number_of_channels) {
    encode_aac_frame_internal();
  }
}

void AudioEncoder::encode_aac_frame_internal() {
  // 出力バッファを準備
  // AAC の最大パケットサイズは約 768 バイト/チャンネル
  std::vector<uint8_t> output_buffer(2048);

  AudioBufferList output_buffer_list;
  output_buffer_list.mNumberBuffers = 1;
  output_buffer_list.mBuffers[0].mNumberChannels = config_.number_of_channels;
  output_buffer_list.mBuffers[0].mDataByteSize =
      static_cast<UInt32>(output_buffer.size());
  output_buffer_list.mBuffers[0].mData = output_buffer.data();

  // パケット記述子
  AudioStreamPacketDescription packet_description;

  UInt32 output_packets = 1;

  OSStatus status = AudioConverterFillComplexBuffer(
      aac_converter_, aac_input_data_proc, this, &output_packets,
      &output_buffer_list, &packet_description);

  if (status != noErr && status != kAudioConverterErr_InvalidInputSize) {
    throw std::runtime_error("AAC encoding failed: " + std::to_string(status));
  }

  if (output_packets > 0 && output_buffer_list.mBuffers[0].mDataByteSize > 0) {
    // エンコードされたフレームを処理
    // タイムスタンプを計算 (マイクロ秒)
    int64_t timestamp = aac_current_timestamp_ +
                        (aac_samples_encoded_ * 1000000 / config_.sample_rate);

    handle_encoded_frame(
        reinterpret_cast<uint8_t*>(output_buffer_list.mBuffers[0].mData),
        output_buffer_list.mBuffers[0].mDataByteSize, timestamp);

    aac_samples_encoded_ += AAC_SAMPLES_PER_FRAME;
  }
}

void AudioEncoder::finalize_aac_encoder() {
  if (!aac_converter_) {
    return;
  }

  // 残りのデータをパディングしてエンコード
  if (!aac_input_buffer_.empty()) {
    size_t samples_needed =
        AAC_SAMPLES_PER_FRAME * config_.number_of_channels -
        (aac_input_buffer_.size() %
         (AAC_SAMPLES_PER_FRAME * config_.number_of_channels));
    if (samples_needed < AAC_SAMPLES_PER_FRAME * config_.number_of_channels) {
      aac_input_buffer_.resize(aac_input_buffer_.size() + samples_needed, 0.0f);
    }
    while (aac_input_buffer_.size() >=
           AAC_SAMPLES_PER_FRAME * config_.number_of_channels) {
      encode_aac_frame_internal();
    }
  }
}

void AudioEncoder::cleanup_aac_encoder() {
  if (aac_converter_) {
    AudioConverterDispose(aac_converter_);
    aac_converter_ = nullptr;
  }
  aac_input_buffer_.clear();
}

#endif  // defined(__APPLE__)
