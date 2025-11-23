#include "encoded_video_chunk.h"
#include <cstring>

EncodedVideoChunk::EncodedVideoChunk(const std::vector<uint8_t>& data,
                                     EncodedVideoChunkType type,
                                     int64_t timestamp,
                                     uint64_t duration)
    : data_(data), type_(type), timestamp_(timestamp), duration_(duration) {}

EncodedVideoChunk::EncodedVideoChunk(nb::bytes data,
                                     EncodedVideoChunkType type,
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
void EncodedVideoChunk::copy_to(nb::ndarray<nb::numpy> destination) const {
  // destination のサイズを検証
  size_t dest_size = destination.nbytes();
  if (dest_size < data_.size()) {
    throw std::runtime_error("destination buffer is too small");
  }

  // destination に内部データをコピー
  std::memcpy(destination.data(), data_.data(), data_.size());
}

void init_encoded_video_chunk(nb::module_& m) {
  nb::enum_<EncodedVideoChunkType>(m, "EncodedVideoChunkType")
      .value("KEY", EncodedVideoChunkType::KEY)
      .value("DELTA", EncodedVideoChunkType::DELTA);

  nb::class_<EncodedVideoChunk>(m, "EncodedVideoChunk")
      .def(
          "__init__",
          [](EncodedVideoChunk* self, nb::dict init) {
            // EncodedVideoChunkInit から必要なフィールドを取得
            if (!init.contains("type"))
              throw nb::value_error("type is required");
            if (!init.contains("timestamp"))
              throw nb::value_error("timestamp is required");
            if (!init.contains("data"))
              throw nb::value_error("data is required");

            EncodedVideoChunkType type =
                nb::cast<EncodedVideoChunkType>(init["type"]);
            int64_t timestamp = nb::cast<int64_t>(init["timestamp"]);
            uint64_t duration = 0;
            if (init.contains("duration")) {
              duration = nb::cast<uint64_t>(init["duration"]);
            }
            nb::bytes data = nb::cast<nb::bytes>(init["data"]);

            new (self) EncodedVideoChunk(data, type, timestamp, duration);
          },
          nb::arg("init"),
          nb::sig("def __init__(self, init: webcodecs.EncodedVideoChunkInit) "
                  "-> None"))
      .def_prop_ro("type", &EncodedVideoChunk::type,
                   nb::sig("def type(self, /) -> EncodedVideoChunkType"))
      .def_prop_ro("timestamp", &EncodedVideoChunk::timestamp,
                   nb::sig("def timestamp(self, /) -> int"))
      .def_prop_ro("duration", &EncodedVideoChunk::duration,
                   nb::sig("def duration(self, /) -> int"))
      .def_prop_ro("byte_length", &EncodedVideoChunk::byte_length,
                   nb::sig("def byte_length(self, /) -> int"))
      .def("copy_to", &EncodedVideoChunk::copy_to, nb::arg("destination"),
           nb::sig("def copy_to(self, destination: "
                   "numpy.typing.NDArray[numpy.uint8]) -> None"));
}