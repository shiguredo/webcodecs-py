#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <vector>

#include "audio_data.h"
#include "audio_encoder.h"

void AudioEncoder::init_flac_encoder() {
  // FLAC エンコーダーを作成
  flac_encoder_ = FLAC__stream_encoder_new();
  if (!flac_encoder_) {
    throw std::runtime_error("Failed to create FLAC encoder");
  }

  // FLAC エンコーダーの設定
  // チャンネル数を設定
  if (!FLAC__stream_encoder_set_channels(flac_encoder_,
                                         config_.number_of_channels)) {
    FLAC__stream_encoder_delete(flac_encoder_);
    flac_encoder_ = nullptr;
    throw std::runtime_error("Failed to set FLAC channels");
  }

  // サンプルレートを設定
  if (!FLAC__stream_encoder_set_sample_rate(flac_encoder_,
                                            config_.sample_rate)) {
    FLAC__stream_encoder_delete(flac_encoder_);
    flac_encoder_ = nullptr;
    throw std::runtime_error("Failed to set FLAC sample rate");
  }

  // ビット深度を設定 (FLAC は 16bit を標準としている)
  // WebCodecs では S16 形式が一般的なので 16bit を使用
  if (!FLAC__stream_encoder_set_bits_per_sample(flac_encoder_, 16)) {
    FLAC__stream_encoder_delete(flac_encoder_);
    flac_encoder_ = nullptr;
    throw std::runtime_error("Failed to set FLAC bits per sample");
  }

  // FlacEncoderConfig からオプションを取得
  uint32_t compress_level = 5;  // デフォルト値
  uint32_t block_size = 0;      // 0 = 自動

  if (config_.flac.has_value()) {
    compress_level = config_.flac->compress_level;
    block_size = config_.flac->block_size;
  }

  // 圧縮レベルを設定 (0-8、高い値は圧縮率が高いが処理が遅い)
  if (!FLAC__stream_encoder_set_compression_level(flac_encoder_,
                                                  compress_level)) {
    FLAC__stream_encoder_delete(flac_encoder_);
    flac_encoder_ = nullptr;
    throw std::runtime_error("Failed to set FLAC compression level");
  }

  // ブロックサイズを設定 (0 以外の場合のみ)
  if (block_size > 0) {
    if (!FLAC__stream_encoder_set_blocksize(flac_encoder_, block_size)) {
      FLAC__stream_encoder_delete(flac_encoder_);
      flac_encoder_ = nullptr;
      throw std::runtime_error("Failed to set FLAC block size");
    }
  }

  // ストリームモードでエンコーダーを初期化
  FLAC__StreamEncoderInitStatus init_status =
      FLAC__stream_encoder_init_stream(flac_encoder_, flac_write_callback,
                                       nullptr,  // seek callback
                                       nullptr,  // tell callback
                                       nullptr,  // metadata callback
                                       this);

  if (init_status != FLAC__STREAM_ENCODER_INIT_STATUS_OK) {
    FLAC__stream_encoder_delete(flac_encoder_);
    flac_encoder_ = nullptr;
    throw std::runtime_error(
        "Failed to initialize FLAC encoder: " +
        std::string(FLAC__StreamEncoderInitStatusString[static_cast<int>(
            init_status)]));
  }

  // タイムスタンプを初期化
  flac_current_timestamp_ = 0;
}

FLAC__StreamEncoderWriteStatus AudioEncoder::flac_write_callback(
    const FLAC__StreamEncoder* encoder,
    const FLAC__byte buffer[],
    size_t bytes,
    uint32_t samples,
    uint32_t current_frame,
    void* client_data) {
  auto* self = static_cast<AudioEncoder*>(client_data);

  // エンコードされたデータをバッファに追加
  self->flac_output_buffer_.insert(self->flac_output_buffer_.end(), buffer,
                                   buffer + bytes);

  return FLAC__STREAM_ENCODER_WRITE_STATUS_OK;
}

void AudioEncoder::encode_frame_flac(const AudioData& data) {
  if (!flac_encoder_) {
    throw std::runtime_error("FLAC encoder not initialized");
  }

  // FLAC は整数サンプルを期待する (S16 形式)
  auto int16_data = data.convert_format(AudioSampleFormat::S16);
  uint32_t frame_count = int16_data->number_of_frames();
  uint32_t channels = config_.number_of_channels;

  // S16 データを取得
  const int16_t* pcm_data =
      reinterpret_cast<const int16_t*>(int16_data->data_ptr());

  // FLAC は FLAC__int32 の配列を期待する
  // インターリーブ形式から変換
  std::vector<FLAC__int32> samples(frame_count * channels);
  for (uint32_t i = 0; i < frame_count * channels; ++i) {
    samples[i] = static_cast<FLAC__int32>(pcm_data[i]);
  }

  // タイムスタンプを保存
  flac_current_timestamp_ = data.timestamp();

  // 出力バッファをクリア
  flac_output_buffer_.clear();

  // フレームをエンコード
  if (!FLAC__stream_encoder_process_interleaved(flac_encoder_, samples.data(),
                                                frame_count)) {
    throw std::runtime_error(
        "FLAC encoding failed: " +
        std::string(FLAC__StreamEncoderStateString[static_cast<int>(
            FLAC__stream_encoder_get_state(flac_encoder_))]));
  }

  // エンコードされたデータがあれば出力
  if (!flac_output_buffer_.empty()) {
    handle_encoded_frame(flac_output_buffer_.data(), flac_output_buffer_.size(),
                         flac_current_timestamp_);
  }
}

void AudioEncoder::finalize_flac_encoder() {
  if (flac_encoder_) {
    // 残りのデータをフラッシュ
    FLAC__stream_encoder_finish(flac_encoder_);

    // エンコードされたデータがあれば出力
    if (!flac_output_buffer_.empty()) {
      handle_encoded_frame(flac_output_buffer_.data(),
                           flac_output_buffer_.size(), flac_current_timestamp_);
      flac_output_buffer_.clear();
    }
  }
}
