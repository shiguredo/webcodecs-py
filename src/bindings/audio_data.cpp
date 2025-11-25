#include "audio_data.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>  // Windows ビルドで std::to_string に必要

// WebCodecs API 準拠コンストラクタ (AudioDataInit dict を受け取る)
AudioData::AudioData(nb::dict init) : duration_(0), closed_(false) {
  // 必須フィールドの検証
  if (!init.contains("format"))
    throw nb::value_error("format is required");
  if (!init.contains("sample_rate"))
    throw nb::value_error("sample_rate is required");
  if (!init.contains("number_of_frames"))
    throw nb::value_error("number_of_frames is required");
  if (!init.contains("number_of_channels"))
    throw nb::value_error("number_of_channels is required");
  if (!init.contains("timestamp"))
    throw nb::value_error("timestamp is required");
  if (!init.contains("data"))
    throw nb::value_error("data is required");

  // フィールドを取得
  format_ = nb::cast<AudioSampleFormat>(init["format"]);
  sample_rate_ = nb::cast<uint32_t>(init["sample_rate"]);
  number_of_frames_ = nb::cast<uint32_t>(init["number_of_frames"]);
  number_of_channels_ = nb::cast<uint32_t>(init["number_of_channels"]);
  timestamp_ = nb::cast<int64_t>(init["timestamp"]);
  nb::ndarray<nb::numpy> data = nb::cast<nb::ndarray<nb::numpy>>(init["data"]);

  // 配列の shape を検証
  auto shape = data.shape_ptr();
  auto ndim = data.ndim();

  if (ndim == 1) {
    // 1次元配列: モノラルのみサポート
    if (number_of_channels_ != 1) {
      throw std::runtime_error("1D array requires number_of_channels=1, got " +
                               std::to_string(number_of_channels_));
    }
    if (static_cast<uint32_t>(shape[0]) != number_of_frames_) {
      throw std::runtime_error("1D array shape[0]=" + std::to_string(shape[0]) +
                               " does not match number_of_frames=" +
                               std::to_string(number_of_frames_));
    }
  } else if (ndim == 2) {
    // 2次元配列: planar または interleaved
    uint32_t shape_channels, shape_frames;
    if (is_planar()) {
      shape_channels = shape[0];
      shape_frames = shape[1];
    } else {
      shape_frames = shape[0];
      shape_channels = shape[1];
    }

    if (shape_channels != number_of_channels_) {
      throw std::runtime_error(
          "Array channels=" + std::to_string(shape_channels) +
          " does not match number_of_channels=" +
          std::to_string(number_of_channels_));
    }
    if (shape_frames != number_of_frames_) {
      throw std::runtime_error("Array frames=" + std::to_string(shape_frames) +
                               " does not match number_of_frames=" +
                               std::to_string(number_of_frames_));
    }
  } else {
    throw std::runtime_error(
        "AudioData only supports 1D (mono) or 2D arrays. Got " +
        std::to_string(ndim) + "D array");
  }

  // データをコピー
  size_t total_size = number_of_frames_ * get_frame_size();
  data_.resize(total_size);
  std::memcpy(data_.data(), data.data(), total_size);

  // duration をマイクロ秒で計算
  if (sample_rate_ > 0) {
    duration_ = (static_cast<uint64_t>(number_of_frames_) * 1000000ULL) /
                static_cast<uint64_t>(sample_rate_);
  } else {
    duration_ = 0;
  }
}

// Private constructor for internal use
AudioData::AudioData(uint32_t number_of_channels,
                     uint32_t sample_rate,
                     uint32_t number_of_frames,
                     AudioSampleFormat format,
                     int64_t timestamp,
                     uint64_t duration)
    : number_of_channels_(number_of_channels),
      sample_rate_(sample_rate),
      number_of_frames_(number_of_frames),
      format_(format),
      timestamp_(timestamp),
      duration_(duration),
      closed_(false) {
  size_t total_size = number_of_frames_ * get_frame_size();
  data_.resize(total_size);
}

AudioData::~AudioData() {
  close();
}

// 内部使用のためのファクトリーメソッド (空のバッファを確保)
std::unique_ptr<AudioData> AudioData::create_with_buffer(
    uint32_t number_of_channels,
    uint32_t sample_rate,
    uint32_t number_of_frames,
    AudioSampleFormat format,
    int64_t timestamp) {
  // duration を計算
  uint64_t duration = 0;
  if (sample_rate > 0) {
    duration = (static_cast<uint64_t>(number_of_frames) * 1000000ULL) /
               static_cast<uint64_t>(sample_rate);
  }

  return std::unique_ptr<AudioData>(new AudioData(number_of_channels,
                                                  sample_rate, number_of_frames,
                                                  format, timestamp, duration));
}

void AudioData::close() {
  if (!closed_) {
    data_.clear();
    closed_ = true;
  }
}

// 指定されたフォーマットのサンプルサイズを返すスタティックヘルパー
static size_t get_sample_size_for_format(AudioSampleFormat format) {
  switch (format) {
    case AudioSampleFormat::U8:
    case AudioSampleFormat::U8_PLANAR:
      return sizeof(uint8_t);
    case AudioSampleFormat::S16:
    case AudioSampleFormat::S16_PLANAR:
      return sizeof(int16_t);
    case AudioSampleFormat::S32:
    case AudioSampleFormat::S32_PLANAR:
    case AudioSampleFormat::F32:
    case AudioSampleFormat::F32_PLANAR:
      return sizeof(int32_t);
    default:
      throw std::runtime_error("Unknown audio format");
  }
}

// 指定されたフォーマットがプレーナーかどうかを返すスタティックヘルパー
static bool is_planar_format(AudioSampleFormat format) {
  switch (format) {
    case AudioSampleFormat::U8_PLANAR:
    case AudioSampleFormat::S16_PLANAR:
    case AudioSampleFormat::S32_PLANAR:
    case AudioSampleFormat::F32_PLANAR:
      return true;
    default:
      return false;
  }
}

size_t AudioData::get_sample_size() const {
  return get_sample_size_for_format(format_);
}

size_t AudioData::get_frame_size() const {
  return number_of_channels_ * get_sample_size();
}

bool AudioData::is_planar() const {
  switch (format_) {
    case AudioSampleFormat::U8_PLANAR:
    case AudioSampleFormat::S16_PLANAR:
    case AudioSampleFormat::S32_PLANAR:
    case AudioSampleFormat::F32_PLANAR:
      return true;
    default:
      return false;
  }
}

const uint8_t* AudioData::data_ptr() const {
  if (closed_) {
    throw std::runtime_error("AudioData is closed");
  }
  return data_.data();
}

uint8_t* AudioData::mutable_data() {
  if (closed_) {
    throw std::runtime_error("AudioData is closed");
  }
  return data_.data();
}

nb::ndarray<nb::numpy> AudioData::get_channel_data(uint32_t channel) const {
  if (closed_) {
    throw std::runtime_error("AudioData is closed");
  }

  if (channel >= number_of_channels_) {
    throw std::out_of_range("Invalid channel index");
  }

  size_t sample_size = get_sample_size();
  size_t shape[1] = {number_of_frames_};

  if (is_planar()) {
    // AudioData の寿命を保持するための Python 参照を作成
    auto* self = const_cast<AudioData*>(this);
    nb::object py_self = nb::cast(self, nb::rv_policy::reference_internal);

    size_t offset = channel * number_of_frames_ * sample_size;
    switch (format_) {
      case AudioSampleFormat::U8_PLANAR:
        return nb::ndarray<nb::numpy>(
            const_cast<uint8_t*>(data_.data() + offset), 1, shape,
            nb::handle(py_self.ptr()), nullptr, nb::dtype<uint8_t>());
      case AudioSampleFormat::S16_PLANAR:
        return nb::ndarray<nb::numpy>(
            const_cast<int16_t*>(
                reinterpret_cast<const int16_t*>(data_.data() + offset)),
            1, shape, nb::handle(py_self.ptr()), nullptr, nb::dtype<int16_t>());
      case AudioSampleFormat::S32_PLANAR:
        return nb::ndarray<nb::numpy>(
            const_cast<int32_t*>(
                reinterpret_cast<const int32_t*>(data_.data() + offset)),
            1, shape, nb::handle(py_self.ptr()), nullptr, nb::dtype<int32_t>());
      case AudioSampleFormat::F32_PLANAR:
        return nb::ndarray<nb::numpy>(
            const_cast<float*>(
                reinterpret_cast<const float*>(data_.data() + offset)),
            1, shape, nb::handle(py_self.ptr()), nullptr, nb::dtype<float>());
      default:
        throw std::runtime_error("Unsupported format");
    }
  } else {
    // インターリーブデータの場合、チャンネルを抽出した新しい配列を作成
    // ローカル変数を即座に破棄しないように shared_ptr で管理
    auto channel_data =
        std::make_shared<std::vector<uint8_t>>(number_of_frames_ * sample_size);
    for (uint32_t i = 0; i < number_of_frames_; ++i) {
      std::memcpy(
          channel_data->data() + i * sample_size,
          data_.data() + (i * number_of_channels_ + channel) * sample_size,
          sample_size);
    }

    // shared_ptr を保持する capsule を作成
    nb::capsule owner(
        new std::shared_ptr<std::vector<uint8_t>>(channel_data),
        [](void* p) noexcept {
          delete static_cast<std::shared_ptr<std::vector<uint8_t>>*>(p);
        });

    switch (format_) {
      case AudioSampleFormat::U8:
        return nb::ndarray<nb::numpy>(channel_data->data(), 1, shape, owner,
                                      nullptr, nb::dtype<uint8_t>());
      case AudioSampleFormat::S16:
        return nb::ndarray<nb::numpy>(
            reinterpret_cast<int16_t*>(channel_data->data()), 1, shape, owner,
            nullptr, nb::dtype<int16_t>());
      case AudioSampleFormat::S32:
        return nb::ndarray<nb::numpy>(
            reinterpret_cast<int32_t*>(channel_data->data()), 1, shape, owner,
            nullptr, nb::dtype<int32_t>());
      case AudioSampleFormat::F32:
        return nb::ndarray<nb::numpy>(
            reinterpret_cast<float*>(channel_data->data()), 1, shape, owner,
            nullptr, nb::dtype<float>());
      default:
        throw std::runtime_error("Unsupported format");
    }
  }
}

// 内部用フォーマット変換メソッド
std::unique_ptr<AudioData> AudioData::convert_format(
    AudioSampleFormat target_format) const {
  if (closed_) {
    throw std::runtime_error("AudioData is closed");
  }

  auto result = std::unique_ptr<AudioData>(
      new AudioData(number_of_channels_, sample_rate_, number_of_frames_,
                    target_format, timestamp_, duration_));

  // フォーマットが同じ場合は、単にデータをコピー
  if (format_ == target_format) {
    result->data_ = data_;
    return result;
  }

  size_t total_samples = number_of_channels_ * number_of_frames_;

  // F32 -> S16 変換
  if (format_ == AudioSampleFormat::F32 &&
      target_format == AudioSampleFormat::S16) {
    const float* src = reinterpret_cast<const float*>(data_.data());
    int16_t* dst = reinterpret_cast<int16_t*>(result->data_.data());
    for (size_t i = 0; i < total_samples; ++i) {
      float sample = std::max(-1.0f, std::min(1.0f, src[i]));
      dst[i] = static_cast<int16_t>(sample * 32767.0f);
    }
  }
  // S16 -> F32 変換
  else if (format_ == AudioSampleFormat::S16 &&
           target_format == AudioSampleFormat::F32) {
    const int16_t* src = reinterpret_cast<const int16_t*>(data_.data());
    float* dst = reinterpret_cast<float*>(result->data_.data());
    for (size_t i = 0; i < total_samples; ++i) {
      dst[i] = static_cast<float>(src[i]) / 32767.0f;
    }
  } else {
    throw std::runtime_error("Unsupported format conversion");
  }

  return result;
}

// AudioDataCopyToOptions から必要な情報を取得するヘルパー関数
AudioData::CopyToParams AudioData::parse_copy_to_options(
    nb::dict options) const {
  // plane_index は必須
  if (!options.contains("plane_index")) {
    throw nb::value_error("plane_index is required");
  }
  uint32_t plane_index = nb::cast<uint32_t>(options["plane_index"]);

  // プレーンインデックスの検証
  if (is_planar()) {
    // プレーナーフォーマット: plane_index はチャンネルインデックス
    if (plane_index >= number_of_channels_) {
      throw std::runtime_error(
          "plane_index out of range: " + std::to_string(plane_index) +
          " >= " + std::to_string(number_of_channels_));
    }
  } else {
    // インターリーブフォーマット: plane_index=0 のみ有効
    if (plane_index != 0) {
      throw std::runtime_error(
          "plane_index must be 0 for interleaved format, got " +
          std::to_string(plane_index));
    }
  }

  // frame_offset（デフォルト 0）
  uint32_t frame_offset = 0;
  if (options.contains("frame_offset")) {
    frame_offset = nb::cast<uint32_t>(options["frame_offset"]);
  }

  // frame_offset の検証
  if (frame_offset >= number_of_frames_) {
    throw std::runtime_error(
        "frame_offset out of range: " + std::to_string(frame_offset) +
        " >= " + std::to_string(number_of_frames_));
  }

  // frame_count（デフォルトは残りの全フレーム）
  uint32_t frame_count = number_of_frames_ - frame_offset;
  if (options.contains("frame_count")) {
    frame_count = nb::cast<uint32_t>(options["frame_count"]);
  }

  // frame_count の検証
  if (frame_offset + frame_count > number_of_frames_) {
    throw std::runtime_error(
        "frame_offset + frame_count exceeds number_of_frames: " +
        std::to_string(frame_offset) + " + " + std::to_string(frame_count) +
        " > " + std::to_string(number_of_frames_));
  }

  // format（オプション、指定された場合は変換する）
  bool has_format = false;
  AudioSampleFormat target_format = format_;
  if (options.contains("format")) {
    has_format = true;
    target_format = nb::cast<AudioSampleFormat>(options["format"]);
  }

  return {plane_index, frame_offset, frame_count, has_format, target_format};
}

size_t AudioData::allocation_size(nb::dict options) const {
  if (closed_) {
    throw std::runtime_error("AudioData is closed");
  }

  CopyToParams params = parse_copy_to_options(options);

  // format が指定された場合はそのフォーマットのサンプルサイズを使用
  AudioSampleFormat target_format =
      params.has_format ? params.target_format : format_;
  size_t target_sample_size = get_sample_size_for_format(target_format);
  bool target_is_planar = is_planar_format(target_format);

  if (target_is_planar) {
    // プレーナー: 1チャンネル分のフレーム
    return params.frame_count * target_sample_size;
  } else {
    // インターリーブ: 全チャンネル分のフレーム
    return params.frame_count * number_of_channels_ * target_sample_size;
  }
}

std::unique_ptr<AudioData> AudioData::clone() const {
  if (closed_) {
    throw std::runtime_error("AudioData is closed");
  }

  auto cloned = std::unique_ptr<AudioData>(
      new AudioData(number_of_channels_, sample_rate_, number_of_frames_,
                    format_, timestamp_, duration_));
  std::memcpy(cloned->data_.data(), data_.data(), data_.size());
  return cloned;
}

// サンプル変換ヘルパー関数
// S16 -> F32 変換
static void convert_s16_to_f32(const int16_t* src, float* dst, size_t count) {
  for (size_t i = 0; i < count; ++i) {
    dst[i] = static_cast<float>(src[i]) / 32767.0f;
  }
}

// F32 -> S16 変換
static void convert_f32_to_s16(const float* src, int16_t* dst, size_t count) {
  for (size_t i = 0; i < count; ++i) {
    float sample = std::max(-1.0f, std::min(1.0f, src[i]));
    dst[i] = static_cast<int16_t>(sample * 32767.0f);
  }
}

// copy_to(): WebCodecs API 準拠の実装
// destination に書き込む
void AudioData::copy_to(nb::ndarray<nb::numpy> destination, nb::dict options) {
  if (closed_) {
    throw std::runtime_error("AudioData is closed");
  }

  CopyToParams params = parse_copy_to_options(options);

  // destination のサイズを検証
  size_t required_size = allocation_size(options);
  size_t dest_size = destination.nbytes();
  if (dest_size < required_size) {
    throw std::runtime_error(
        "destination buffer is too small: " + std::to_string(dest_size) +
        " < " + std::to_string(required_size));
  }

  // 変換先フォーマット
  AudioSampleFormat target_format =
      params.has_format ? params.target_format : format_;
  size_t src_sample_size = get_sample_size();
  size_t frame_size = get_frame_size();

  // ソースデータのオフセット計算
  const uint8_t* src_data;
  size_t total_samples;

  if (is_planar()) {
    // プレーナーフォーマット: 指定されたプレーン（チャンネル）からコピー
    size_t plane_data_size = number_of_frames_ * src_sample_size;
    size_t plane_offset = params.plane_index * plane_data_size;
    size_t frame_byte_offset = params.frame_offset * src_sample_size;
    src_data = data_.data() + plane_offset + frame_byte_offset;
    total_samples = params.frame_count;
  } else {
    // インターリーブフォーマット: 全チャンネルをまとめてコピー
    size_t frame_byte_offset = params.frame_offset * frame_size;
    src_data = data_.data() + frame_byte_offset;
    total_samples = params.frame_count * number_of_channels_;
  }

  // フォーマット変換が必要かどうか
  if (format_ == target_format) {
    // 変換不要、そのままコピー
    size_t copy_size = is_planar() ? params.frame_count * src_sample_size
                                   : params.frame_count * frame_size;
    std::memcpy(destination.data(), src_data, copy_size);
  } else if (format_ == AudioSampleFormat::S16 &&
             target_format == AudioSampleFormat::F32) {
    // S16 -> F32 変換
    convert_s16_to_f32(reinterpret_cast<const int16_t*>(src_data),
                       reinterpret_cast<float*>(destination.data()),
                       total_samples);
  } else if (format_ == AudioSampleFormat::F32 &&
             target_format == AudioSampleFormat::S16) {
    // F32 -> S16 変換
    convert_f32_to_s16(reinterpret_cast<const float*>(src_data),
                       reinterpret_cast<int16_t*>(destination.data()),
                       total_samples);
  } else {
    throw std::runtime_error("Unsupported format conversion");
  }
}

void init_audio_data(nb::module_& m) {
  nb::enum_<AudioSampleFormat>(m, "AudioSampleFormat")
      .value("U8", AudioSampleFormat::U8)
      .value("S16", AudioSampleFormat::S16)
      .value("S32", AudioSampleFormat::S32)
      .value("F32", AudioSampleFormat::F32)
      .value("U8_PLANAR", AudioSampleFormat::U8_PLANAR)
      .value("S16_PLANAR", AudioSampleFormat::S16_PLANAR)
      .value("S32_PLANAR", AudioSampleFormat::S32_PLANAR)
      .value("F32_PLANAR", AudioSampleFormat::F32_PLANAR);

  nb::class_<AudioData>(m, "AudioData")
      .def(nb::init<nb::dict>(), nb::arg("init"),
           nb::sig("def __init__(self, init: webcodecs.AudioDataInit, /) "
                   "-> None"))
      .def_prop_ro("number_of_channels", &AudioData::number_of_channels,
                   nb::sig("def number_of_channels(self, /) -> int"))
      .def_prop_ro("sample_rate", &AudioData::sample_rate,
                   nb::sig("def sample_rate(self, /) -> int"))
      .def_prop_ro("number_of_frames", &AudioData::number_of_frames,
                   nb::sig("def number_of_frames(self, /) -> int"))
      .def_prop_ro("format", &AudioData::format,
                   nb::sig("def format(self, /) -> AudioSampleFormat"))
      .def_prop_ro("timestamp", &AudioData::timestamp,
                   nb::sig("def timestamp(self, /) -> int"))
      .def_prop_ro("duration", &AudioData::duration,
                   nb::sig("def duration(self, /) -> int"))
      .def("get_channel_data", &AudioData::get_channel_data, nb::arg("channel"),
           nb::sig("def get_channel_data(self, channel: int, /) -> "
                   "numpy.typing.NDArray"))
      .def("copy_to", &AudioData::copy_to, nb::arg("destination"),
           nb::arg("options"),
           nb::sig("def copy_to(self, destination: numpy.typing.NDArray, "
                   "options: webcodecs.AudioDataCopyToOptions) -> None"))
      .def("allocation_size", &AudioData::allocation_size, nb::arg("options"),
           nb::sig("def allocation_size(self, options: "
                   "webcodecs.AudioDataCopyToOptions, /) -> int"))
      .def("close", &AudioData::close, nb::sig("def close(self, /) -> None"))
      .def_prop_ro("is_closed", &AudioData::is_closed,
                   nb::sig("def is_closed(self, /) -> bool"))
      .def(
          "clone", [](const AudioData& self) { return self.clone().release(); },
          nb::rv_policy::take_ownership,
          nb::sig("def clone(self, /) -> AudioData"));
}
