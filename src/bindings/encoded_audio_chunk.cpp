#include "encoded_audio_chunk.h"
#include <cstring>

EncodedAudioChunk::EncodedAudioChunk(const std::vector<uint8_t>& data,
                                     EncodedAudioChunkType type,
                                     int64_t timestamp,
                                     uint64_t duration)
    : data_(data), type_(type), timestamp_(timestamp), duration_(duration) {}

EncodedAudioChunk::EncodedAudioChunk(nb::bytes data,
                                     EncodedAudioChunkType type,
                                     int64_t timestamp,
                                     uint64_t duration)
    : type_(type), timestamp_(timestamp), duration_(duration) {
  const char* ptr = data.c_str();
  size_t size = data.size();
  data_.resize(size);
  std::memcpy(data_.data(), ptr, size);
}

// copy_to(): WebCodecs API 準拠の実装
// destination に書き込む
void EncodedAudioChunk::copy_to(nb::ndarray<nb::numpy> destination) const {
  // destination のサイズを検証
  size_t dest_size = destination.nbytes();
  if (dest_size < data_.size()) {
    throw std::runtime_error("destination buffer is too small");
  }

  // destination に内部データをコピー
  std::memcpy(destination.data(), data_.data(), data_.size());
}

void init_encoded_audio_chunk(nb::module_& m) {
  nb::enum_<EncodedAudioChunkType>(m, "EncodedAudioChunkType")
      .value("KEY", EncodedAudioChunkType::KEY)
      .value("DELTA", EncodedAudioChunkType::DELTA);

  nb::class_<EncodedAudioChunk>(m, "EncodedAudioChunk")
      .def(nb::init<nb::bytes, EncodedAudioChunkType, int64_t, uint64_t>(),
           nb::arg("data"), nb::arg("type"), nb::arg("timestamp"),
           nb::arg("duration") = 0,
           nb::sig(
               "def __init__(self, data: bytes, type: EncodedAudioChunkType, "
               "timestamp: int, duration: int = 0) -> None"))
      .def_prop_ro("type", &EncodedAudioChunk::type,
                   nb::sig("def type(self, /) -> EncodedAudioChunkType"))
      .def_prop_ro("timestamp", &EncodedAudioChunk::timestamp,
                   nb::sig("def timestamp(self, /) -> int"))
      .def_prop_ro("duration", &EncodedAudioChunk::duration,
                   nb::sig("def duration(self, /) -> int"))
      .def_prop_ro("byte_length", &EncodedAudioChunk::byte_length,
                   nb::sig("def byte_length(self, /) -> int"))
      .def("copy_to", &EncodedAudioChunk::copy_to, nb::arg("destination"),
           nb::sig("def copy_to(self, destination: "
                   "numpy.typing.NDArray[numpy.uint8]) -> None"));
}