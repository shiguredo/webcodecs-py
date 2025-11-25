#include <cstring>
#include <stdexcept>
#include <vector>

#include "audio_data.h"
#include "audio_decoder.h"

namespace {
// Opus デコーダーの定数
// Opus の最大フレームサイズ: 120ms @ 48kHz = 5760 サンプル
constexpr int OPUS_MAX_FRAME_SIZE = 5760;
}  // namespace

void AudioDecoder::init_opus_decoder() {
  // Opus デコーダーを作成
  int error;

  // Opus がサポートするサンプルレート: 8000, 12000, 16000, 24000, 48000
  int sample_rate = config_.sample_rate;
  if (sample_rate != 8000 && sample_rate != 12000 && sample_rate != 16000 &&
      sample_rate != 24000 && sample_rate != 48000) {
    // WebCodecs 仕様に従い、サポートされていないサンプルレートの場合はエラー
    throw std::runtime_error(
        "NotSupportedError: Opus decoder only supports sample rates of 8000, "
        "12000, 16000, 24000, or 48000 Hz. Got " +
        std::to_string(sample_rate) + " Hz");
  }

  opus_decoder_ =
      opus_decoder_create(sample_rate, config_.number_of_channels, &error);
  if (error != OPUS_OK) {
    throw std::runtime_error("Failed to create Opus decoder: " +
                             std::string(opus_strerror(error)));
  }
}

void AudioDecoder::decode_frame_opus(const EncodedAudioChunk& chunk) {
  if (!opus_decoder_) {
    throw std::runtime_error("Opus decoder not initialized");
  }

  auto encoded_data = chunk.data_vector();

  std::vector<float> output(OPUS_MAX_FRAME_SIZE * config_.number_of_channels);

  int decoded_samples =
      opus_decode_float(opus_decoder_, encoded_data.data(), encoded_data.size(),
                        output.data(), OPUS_MAX_FRAME_SIZE, 0);

  if (decoded_samples < 0) {
    throw std::runtime_error("Opus decoding failed: " +
                             std::string(opus_strerror(decoded_samples)));
  }

  auto audio_data = AudioData::create_with_buffer(
      config_.number_of_channels, config_.sample_rate,
      static_cast<uint32_t>(decoded_samples), AudioSampleFormat::F32,
      chunk.timestamp());

  float* dst = reinterpret_cast<float*>(audio_data->mutable_data());
  std::memcpy(dst, output.data(),
              static_cast<size_t>(decoded_samples) *
                  config_.number_of_channels * sizeof(float));

  handle_decoded_frame(std::move(audio_data));
}
