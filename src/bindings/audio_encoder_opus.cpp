#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <vector>

#include "audio_data.h"
#include "audio_encoder.h"

namespace {
// Opus エンコーダーの定数
constexpr int OPUS_MAX_PACKET_SIZE = 4000;

// 各サンプルレートでの 20ms フレームサイズ
constexpr int FRAME_SIZE_48KHZ = 960;  // 48000 * 0.02 = 960
constexpr int FRAME_SIZE_24KHZ = 480;  // 24000 * 0.02 = 480
constexpr int FRAME_SIZE_16KHZ = 320;  // 16000 * 0.02 = 320
constexpr int FRAME_SIZE_12KHZ = 240;  // 12000 * 0.02 = 240
constexpr int FRAME_SIZE_8KHZ = 160;   // 8000 * 0.02 = 160
}  // namespace

void AudioEncoder::init_opus_encoder() {
  // Opus エンコーダーを作成
  int error;

  // Opus がサポートするサンプルレート: 8000, 12000, 16000, 24000, 48000
  int sample_rate = config_.sample_rate;
  if (sample_rate != 8000 && sample_rate != 12000 && sample_rate != 16000 &&
      sample_rate != 24000 && sample_rate != 48000) {
    // WebCodecs 仕様に従い、サポートされていないサンプルレートの場合はエラー
    throw std::runtime_error(
        "NotSupportedError: Opus encoder only supports sample rates of 8000, "
        "12000, 16000, 24000, or 48000 Hz. Got " +
        std::to_string(sample_rate) + " Hz");
  }

  // OpusEncoderConfig からアプリケーションモードを取得
  int application = OPUS_APPLICATION_AUDIO;  // デフォルト
  if (config_.opus.has_value()) {
    if (config_.opus->application == "voip") {
      application = OPUS_APPLICATION_VOIP;
    } else if (config_.opus->application == "lowdelay") {
      application = OPUS_APPLICATION_RESTRICTED_LOWDELAY;
    }
    // "audio" はデフォルト
  }

  opus_encoder_ = opus_encoder_create(sample_rate, config_.number_of_channels,
                                      application, &error);
  if (error != OPUS_OK) {
    throw std::runtime_error("Failed to create Opus encoder: " +
                             std::string(opus_strerror(error)));
  }

  // ビットレートを設定
  opus_encoder_ctl(opus_encoder_,
                   OPUS_SET_BITRATE(config_.bitrate.value_or(64000)));

  // 複雑度を設定 (0-10、高い値は品質が良いが処理が遅い)
  // デフォルト: デスクトップは 9、モバイルは 5 (ここではデスクトップ想定で 9)
  int complexity = 9;
  if (config_.opus.has_value() && config_.opus->complexity.has_value()) {
    complexity = static_cast<int>(config_.opus->complexity.value());
  }
  opus_encoder_ctl(opus_encoder_, OPUS_SET_COMPLEXITY(complexity));

  // 信号タイプを設定
  if (config_.opus.has_value()) {
    int signal = OPUS_AUTO;
    if (config_.opus->signal == "music") {
      signal = OPUS_SIGNAL_MUSIC;
    } else if (config_.opus->signal == "voice") {
      signal = OPUS_SIGNAL_VOICE;
    }
    opus_encoder_ctl(opus_encoder_, OPUS_SET_SIGNAL(signal));
  }

  // パケットロス率を設定
  if (config_.opus.has_value()) {
    opus_encoder_ctl(opus_encoder_,
                     OPUS_SET_PACKET_LOSS_PERC(config_.opus->packetlossperc));
  }

  // インバンド FEC を設定
  if (config_.opus.has_value()) {
    opus_encoder_ctl(opus_encoder_,
                     OPUS_SET_INBAND_FEC(config_.opus->useinbandfec ? 1 : 0));
  }

  // DTX を設定
  if (config_.opus.has_value()) {
    opus_encoder_ctl(opus_encoder_, OPUS_SET_DTX(config_.opus->usedtx ? 1 : 0));
  }

  // 可変ビットレートを有効化
  opus_encoder_ctl(opus_encoder_, OPUS_SET_VBR(1));
}

void AudioEncoder::encode_frame_opus(const AudioData& data) {
  if (!opus_encoder_) {
    throw std::runtime_error("Opus encoder not initialized");
  }

  // Opus はインターリーブされた float サンプルを期待する
  // 必要に応じて最初に float 形式に変換
  auto float_data = data.convert_format(AudioSampleFormat::F32);
  uint32_t frame_count = float_data->number_of_frames();
  std::vector<float> pcm_copy(frame_count * config_.number_of_channels);
  std::memcpy(pcm_copy.data(), float_data->data_ptr(),
              pcm_copy.size() * sizeof(float));

  // 各サンプルレートに対応する 20ms フレームサイズを取得
  // 全てのサンプルレートで 20ms (0.02 秒) のフレーム期間を使用
  uint32_t frame_size = FRAME_SIZE_48KHZ;
  if (config_.sample_rate == 24000) {
    frame_size = FRAME_SIZE_24KHZ;
  } else if (config_.sample_rate == 16000) {
    frame_size = FRAME_SIZE_16KHZ;
  } else if (config_.sample_rate == 12000) {
    frame_size = FRAME_SIZE_12KHZ;
  } else if (config_.sample_rate == 8000) {
    frame_size = FRAME_SIZE_8KHZ;
  }

  // 出力バッファを準備
  std::vector<uint8_t> output(OPUS_MAX_PACKET_SIZE);

  // オーディオをチャンクごとに処理
  uint32_t samples_processed = 0;
  uint32_t total_samples = frame_count;

  while (samples_processed < total_samples) {
    uint32_t samples_to_process =
        std::min(frame_size, total_samples - samples_processed);

    // フルフレームに満たない場合は、ゼロでパディング
    std::vector<float> padded_frame;
    const float* frame_ptr =
        pcm_copy.data() + (samples_processed * config_.number_of_channels);

    if (samples_to_process < frame_size) {
      padded_frame.resize(frame_size * config_.number_of_channels, 0.0f);
      std::memcpy(
          padded_frame.data(), frame_ptr,
          samples_to_process * config_.number_of_channels * sizeof(float));
      frame_ptr = padded_frame.data();
    }

    // フレームをエンコード
    int encoded_bytes = opus_encode_float(opus_encoder_, frame_ptr, frame_size,
                                          output.data(), output.size());

    if (encoded_bytes < 0) {
      throw std::runtime_error("Opus encoding failed: " +
                               std::string(opus_strerror(encoded_bytes)));
    }

    // エンコードされたフレームを処理
    int64_t timestamp =
        data.timestamp() + (samples_processed * 1000000 /
                            config_.sample_rate);  // マイクロ秒に変換
    handle_encoded_frame(output.data(), encoded_bytes, timestamp);

    samples_processed += samples_to_process;
  }
}
