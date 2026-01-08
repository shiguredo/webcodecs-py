#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

// ビットストリームリーダー
// H.264/H.265 の SPS/PPS パースに必要な Exp-Golomb デコーダーを含む
class BitstreamReader {
 public:
  BitstreamReader(const uint8_t* data, size_t size)
      : data_(data), size_(size), byte_position_(0), bit_position_(0) {}

  BitstreamReader(const std::vector<uint8_t>& data)
      : BitstreamReader(data.data(), data.size()) {}

  // 指定ビット数を読み取る
  uint32_t read_bits(size_t n) {
    if (n > 32) {
      throw std::runtime_error("read_bits: n > 32");
    }

    uint32_t result = 0;
    for (size_t i = 0; i < n; ++i) {
      result = (result << 1) | read_bit();
    }
    return result;
  }

  // 1 ビットを読み取る
  uint32_t read_bit() {
    if (byte_position_ >= size_) {
      throw std::runtime_error("read_bit: データ不足");
    }

    uint32_t bit = (data_[byte_position_] >> (7 - bit_position_)) & 1;
    ++bit_position_;
    if (bit_position_ == 8) {
      bit_position_ = 0;
      ++byte_position_;
    }
    return bit;
  }

  // Exp-Golomb 符号なし整数を読み取る (ue(v))
  uint32_t read_ue() {
    // 先頭の 0 の数をカウント
    int leading_zeros = 0;
    while (read_bit() == 0) {
      ++leading_zeros;
      if (leading_zeros > 31) {
        throw std::runtime_error("read_ue: Exp-Golomb 符号が長すぎる");
      }
    }

    if (leading_zeros == 0) {
      return 0;
    }

    // 残りのビットを読み取る
    uint32_t value = read_bits(leading_zeros);
    return (1 << leading_zeros) - 1 + value;
  }

  // Exp-Golomb 符号付き整数を読み取る (se(v))
  int32_t read_se() {
    uint32_t ue_value = read_ue();
    // 偶数は負、奇数は正
    if (ue_value & 1) {
      return static_cast<int32_t>((ue_value + 1) >> 1);
    } else {
      return -static_cast<int32_t>(ue_value >> 1);
    }
  }

  // 指定ビット数をスキップする
  void skip_bits(size_t n) {
    for (size_t i = 0; i < n; ++i) {
      read_bit();
    }
  }

  // 残りビット数を取得
  size_t remaining_bits() const {
    return (size_ - byte_position_) * 8 - bit_position_;
  }

  // まだデータがあるか
  bool has_more_data() const { return remaining_bits() > 0; }

  // 現在のバイト位置を取得
  size_t byte_position() const { return byte_position_; }

  // 現在のビット位置を取得（バイト内）
  size_t bit_position() const { return bit_position_; }

  // バイト境界までスキップ
  void align_to_byte() {
    if (bit_position_ != 0) {
      bit_position_ = 0;
      ++byte_position_;
    }
  }

 private:
  const uint8_t* data_;
  size_t size_;
  size_t byte_position_;
  size_t bit_position_;
};

// RBSP (Raw Byte Sequence Payload) からエミュレーション防止バイトを除去
// H.264/H.265 では 0x00 0x00 0x03 のシーケンスで 0x03 を除去する
inline std::vector<uint8_t> remove_emulation_prevention_bytes(
    const uint8_t* data,
    size_t size) {
  std::vector<uint8_t> result;
  result.reserve(size);

  for (size_t i = 0; i < size; ++i) {
    // 0x00 0x00 0x03 のパターンを検出
    if (i + 2 < size && data[i] == 0x00 && data[i + 1] == 0x00 &&
        data[i + 2] == 0x03) {
      result.push_back(data[i]);
      result.push_back(data[i + 1]);
      i += 2;
      // 0x03 をスキップ
    } else {
      result.push_back(data[i]);
    }
  }

  return result;
}
