# ndarray から std::vector へのコピーを最適化する

## ステータス: 見送り

検討の結果、この最適化は見送ることにした。詳細は「検討結果」セクションを参照。

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

## 現在コピーしている理由

1. **Python オブジェクトのライフタイム問題**
   - ndarray は Python GC が管理
   - VideoFrame より先に ndarray が解放される可能性

2. **非同期エンコードの安全性**
   - `encode()` は即座に返り、バックグラウンドで処理
   - Python 側で ndarray を変更される可能性

3. **GIL 解放時の安全性**
   - エンコード中は GIL を解放している
   - Python オブジェクトへの参照を保持したままだと危険

## 技術的な調査結果

nanobind では `nb::object` として ndarray への参照を保持することで、Python GC による解放を防ぐことが可能。しかし、以下の問題が残る:

- ndarray の参照を保持しても、CVPixelBuffer へのコピーは必ず発生する
- つまり、コピー回数は 2回 → 1回 にしか減らせない
- native_buffer を使えば 0回のコピーで済む（既に実装済み）

## 検討結果

**この最適化は見送る。**

理由:

1. **効果が限定的**: コピー回数を 2回 → 1回 に減らしても、CVPixelBuffer へのコピーは必ず残る
2. **既存の解決策**: native_buffer を使えば 0回のコピーで済む。パフォーマンスが重要な場合は native_buffer を使うべき
3. **複雑化のリスク**: ndarray の参照保持を実装すると、ライフタイム管理が複雑になり、バグの原因になりやすい

## 推奨事項

パフォーマンスが重要なユースケースでは、ndarray ではなく native_buffer を使用すること。
native_buffer を使えば、エンコーダーが CVPixelBufferRef を直接使用でき、コピーが発生しない。
