#include <cstring>
#include <stdexcept>
#include <vector>

#include "audio_data.h"
#include "audio_decoder.h"

void AudioDecoder::init_flac_decoder() {
  // FLAC デコーダーを作成
  flac_decoder_ = FLAC__stream_decoder_new();
  if (!flac_decoder_) {
    throw std::runtime_error("Failed to create FLAC decoder");
  }

  // ストリームモードでデコーダーを初期化
  FLAC__StreamDecoderInitStatus init_status = FLAC__stream_decoder_init_stream(
      flac_decoder_, flac_read_callback, nullptr,  // seek callback
      nullptr,                                     // tell callback
      nullptr,                                     // length callback
      nullptr,                                     // eof callback
      flac_write_callback, nullptr,                // metadata callback
      flac_error_callback, this);

  if (init_status != FLAC__STREAM_DECODER_INIT_STATUS_OK) {
    FLAC__stream_decoder_delete(flac_decoder_);
    flac_decoder_ = nullptr;
    throw std::runtime_error(
        "Failed to initialize FLAC decoder: " +
        std::string(FLAC__StreamDecoderInitStatusString[static_cast<int>(
            init_status)]));
  }

  // バッファを初期化
  flac_input_buffer_.clear();
  flac_input_position_ = 0;
  flac_current_timestamp_ = 0;
  flac_stream_started_ = false;
}

FLAC__StreamDecoderReadStatus AudioDecoder::flac_read_callback(
    const FLAC__StreamDecoder* decoder,
    FLAC__byte buffer[],
    size_t* bytes,
    void* client_data) {
  auto* self = static_cast<AudioDecoder*>(client_data);

  // 入力バッファから読み取る
  size_t available =
      self->flac_input_buffer_.size() - self->flac_input_position_;

  if (available == 0) {
    *bytes = 0;
    return FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM;
  }

  size_t to_read = std::min(*bytes, available);
  std::memcpy(buffer,
              self->flac_input_buffer_.data() + self->flac_input_position_,
              to_read);
  self->flac_input_position_ += to_read;
  *bytes = to_read;

  return FLAC__STREAM_DECODER_READ_STATUS_CONTINUE;
}

FLAC__StreamDecoderWriteStatus AudioDecoder::flac_write_callback(
    const FLAC__StreamDecoder* decoder,
    const FLAC__Frame* frame,
    const FLAC__int32* const buffer[],
    void* client_data) {
  auto* self = static_cast<AudioDecoder*>(client_data);

  uint32_t channels = frame->header.channels;
  uint32_t blocksize = frame->header.blocksize;
  uint32_t bits_per_sample = frame->header.bits_per_sample;

  // AudioData を作成
  // FLAC のデータは S16 または S32 としてデコードされる
  // ここでは F32 に変換して出力する
  auto audio_data = AudioData::create_with_buffer(
      channels, frame->header.sample_rate, blocksize, AudioSampleFormat::F32,
      self->flac_current_timestamp_);

  float* dst = reinterpret_cast<float*>(audio_data->mutable_data());

  // FLAC のプレーナー形式からインターリーブ形式に変換
  // また、整数から float に変換
  float scale = 1.0f;
  if (bits_per_sample == 16) {
    scale = 1.0f / 32768.0f;
  } else if (bits_per_sample == 24) {
    scale = 1.0f / 8388608.0f;
  } else if (bits_per_sample == 32) {
    scale = 1.0f / 2147483648.0f;
  } else if (bits_per_sample == 8) {
    scale = 1.0f / 128.0f;
  }

  for (uint32_t sample = 0; sample < blocksize; ++sample) {
    for (uint32_t channel = 0; channel < channels; ++channel) {
      dst[sample * channels + channel] =
          static_cast<float>(buffer[channel][sample]) * scale;
    }
  }

  // タイムスタンプを更新
  self->flac_current_timestamp_ +=
      static_cast<int64_t>(blocksize) * 1000000 / frame->header.sample_rate;

  // デコードされたフレームを保存
  self->flac_decoded_frames_.push_back(std::move(audio_data));

  return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}

void AudioDecoder::flac_error_callback(const FLAC__StreamDecoder* decoder,
                                       FLAC__StreamDecoderErrorStatus status,
                                       void* client_data) {
  // エラーを記録
  // 今のところは無視（後で適切なエラーハンドリングを追加可能）
}

void AudioDecoder::decode_frame_flac(const EncodedAudioChunk& chunk) {
  if (!flac_decoder_) {
    throw std::runtime_error("FLAC decoder not initialized");
  }

  // エンコードされたデータを入力バッファに追加（ストリーミング対応）
  auto encoded_data = chunk.data_vector();

  // 既存のバッファの未読部分を保持し、新しいデータを追加
  if (flac_input_position_ < flac_input_buffer_.size()) {
    // 未読データがある場合、それを先頭に移動
    std::vector<uint8_t> remaining(
        flac_input_buffer_.begin() + flac_input_position_,
        flac_input_buffer_.end());
    flac_input_buffer_ = std::move(remaining);
  } else {
    flac_input_buffer_.clear();
  }

  // 新しいデータを追加
  flac_input_buffer_.insert(flac_input_buffer_.end(), encoded_data.begin(),
                            encoded_data.end());
  flac_input_position_ = 0;

  // 最初のチャンクの場合のみタイムスタンプを設定
  if (!flac_stream_started_) {
    flac_current_timestamp_ = chunk.timestamp();
    flac_stream_started_ = true;
  }

  // デコード結果をクリア
  flac_decoded_frames_.clear();

  // フレームを処理（データがある限り続ける）
  while (true) {
    FLAC__StreamDecoderState state =
        FLAC__stream_decoder_get_state(flac_decoder_);

    // 終了条件のチェック
    if (state == FLAC__STREAM_DECODER_END_OF_STREAM ||
        state == FLAC__STREAM_DECODER_ABORTED) {
      break;
    }

    // 1 フレームを処理
    if (!FLAC__stream_decoder_process_single(flac_decoder_)) {
      break;
    }

    // 入力データを全て消費した場合は終了
    if (flac_input_position_ >= flac_input_buffer_.size()) {
      break;
    }
  }

  // デコードされたフレームを出力
  for (auto& audio_data : flac_decoded_frames_) {
    handle_decoded_frame(std::move(audio_data));
  }
  flac_decoded_frames_.clear();
}
