#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

// Annex B から NAL ユニットを抽出
// 戻り値: (オフセット, 長さ) のペアのベクタ
inline std::vector<std::pair<size_t, size_t>> find_annexb_nal_units(
    const uint8_t* data,
    size_t size) {
  std::vector<std::pair<size_t, size_t>> nal_units;

  // 最小スタートコード + 1 バイトデータが必要
  if (size < 4) {
    return nal_units;
  }

  size_t i = 0;
  while (i < size) {
    // スタートコードを探す
    size_t start = 0;
    bool found = false;

    while (i + 2 < size) {
      if (data[i] == 0x00 && data[i + 1] == 0x00) {
        if (data[i + 2] == 0x01) {
          start = i + 3;
          found = true;
          i += 3;
          break;
        } else if (i + 3 < size && data[i + 2] == 0x00 && data[i + 3] == 0x01) {
          start = i + 4;
          found = true;
          i += 4;
          break;
        }
      }
      ++i;
    }

    if (!found) {
      break;
    }

    // 次のスタートコードまたはデータ終端を探す
    size_t end = size;
    for (size_t j = start; j + 2 < size; ++j) {
      if (data[j] == 0x00 && data[j + 1] == 0x00 &&
          (data[j + 2] == 0x01 ||
           (j + 3 < size && data[j + 2] == 0x00 && data[j + 3] == 0x01))) {
        end = j;
        break;
      }
    }

    if (start < end) {
      nal_units.push_back({start, end - start});
    }

    i = end;
  }

  return nal_units;
}
