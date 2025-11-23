#pragma once

#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>
#include <cstdint>
#include <string>
#include <vector>

namespace nb = nanobind;

enum class EncodedAudioChunkType {
  KEY,   // キーフレーム (独立)
  DELTA  // デルタフレーム (依存)
};

class EncodedAudioChunk {
 public:
  EncodedAudioChunk(const std::vector<uint8_t>& data,
                    EncodedAudioChunkType type,
                    int64_t timestamp,
                    uint64_t duration = 0);

  EncodedAudioChunk(nb::bytes data,
                    EncodedAudioChunkType type,
                    int64_t timestamp,
                    uint64_t duration = 0);

  ~EncodedAudioChunk() = default;

  // Properties
  EncodedAudioChunkType type() const { return type_; }
  int64_t timestamp() const { return timestamp_; }
  uint64_t duration() const { return duration_; }
  size_t byte_length() const { return data_.size(); }

  // Data access
  void copy_to(nb::ndarray<nb::numpy> destination) const;
  // 内部使用: std::vector<uint8_t> を返す
  std::vector<uint8_t> data_vector() const { return data_; }

 private:
  std::vector<uint8_t> data_;
  EncodedAudioChunkType type_;
  int64_t timestamp_;
  uint64_t duration_;
};