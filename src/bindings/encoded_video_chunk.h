#pragma once

#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>
#include <cstdint>
#include <string>
#include <vector>

namespace nb = nanobind;

enum class EncodedVideoChunkType {
  KEY,   // キーフレーム (I フレーム)
  DELTA  // デルタフレーム (P フレームまたは B フレーム)
};

class EncodedVideoChunk {
 public:
  EncodedVideoChunk(const std::vector<uint8_t>& data,
                    EncodedVideoChunkType type,
                    int64_t timestamp,
                    uint64_t duration = 0);

  EncodedVideoChunk(nb::bytes data,
                    EncodedVideoChunkType type,
                    int64_t timestamp,
                    uint64_t duration = 0);

  ~EncodedVideoChunk() = default;

  // Properties
  EncodedVideoChunkType type() const { return type_; }
  int64_t timestamp() const { return timestamp_; }
  uint64_t duration() const { return duration_; }
  size_t byte_length() const { return data_.size(); }

  // Data access
  void copy_to(nb::ndarray<nb::numpy> destination) const;
  // 内部使用: std::vector<uint8_t> を返す
  std::vector<uint8_t> data_vector() const { return data_; }

 private:
  std::vector<uint8_t> data_;
  EncodedVideoChunkType type_;
  int64_t timestamp_;
  uint64_t duration_;
};