# ndarray から std::vector へのコピーを最適化する

## 現状

VideoFrame コンストラクタで ndarray を受け取る際、std::vector<uint8_t> にコピーしている。

```cpp
// video_frame.cpp:182-184
// データをコピー
data_.resize(frame_size);
std::memcpy(data_.data(), data.data(), frame_size);
```

## コピー回数の比較

| パターン | コピー回数 | 詳細 |
|----------|-----------|------|
| ndarray | 2回 | Python ndarray → C++ 内部バッファ → CVPixelBuffer |
| native_buffer | 0回 | CVPixelBufferRef への参照を保持、エンコーダーが直接使用 |

## 現在コピーしている理由（推測）

1. **Python オブジェクトのライフタイム問題**
   - ndarray は Python GC が管理
   - VideoFrame より先に ndarray が解放される可能性

2. **非同期エンコードの安全性**
   - `encode()` は即座に返り、バックグラウンドで処理
   - Python 側で ndarray を変更される可能性

3. **GIL 解放時の安全性**
   - エンコード中は GIL を解放している
   - Python オブジェクトへの参照を保持したままだと危険

## 検討事項

- native_buffer の場合は CVPixelBufferRef の参照カウントを増やして使用している
- ndarray も同様に参照を保持するアプローチが可能かもしれない
- nanobind で ndarray の参照を安全に保持する方法を調査する必要がある

## 期待される効果

ndarray の場合のコピー回数を 2回 → 1回 に削減できる可能性がある。
