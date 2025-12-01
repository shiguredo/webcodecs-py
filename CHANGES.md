# 変更履歴

- CHANGE
  - 後方互換性のない変更
- UPDATE
  - 後方互換性がある変更
- ADD
  - 後方互換性がある追加
- FIX
  - バグ修正

## develop

- [ADD] AudioData と VideoFrame でコンテキストマネージャー (with 文) をサポートする
  - `__enter__` / `__exit__` を実装し、with 文で自動的に close() が呼ばれるようにする
  - @voluntas
- [ADD] VideoEncoder の output callback で EncodedVideoChunkMetadata をサポートする
  - WebCodecs API 準拠で、キーフレーム時に decoderConfig を含む metadata を提供する
  - @voluntas
- [ADD] VideoToolbox エンコーダーでキーフレーム時に decoderConfig.description (avcC/hvcC) を提供する
  - @voluntas
- [UPDATE] nanobind の dict.get() を使用してオプションフィールドの取得を簡潔化する
  - @voluntas

### misc

- [UPDATE] nanobind バインディングで `nb::arg()` を `_a` リテラル形式に統一する
  - @voluntas
- [ADD] pytest-benchmark を開発依存に追加する
  - @voluntas
- [ADD] tests/benchmarks/ にベンチマークテストを追加する
  - bench_ prefix を持つファイルのみがベンチマークとして実行される
  - @voluntas
- [UPDATE] examples/blend2d_to_mp4.py で metadata.decoderConfig.description を使用して MP4 を生成するように修正する
  - @voluntas
- [UPDATE] examples/blend2d_to_mp4.py で H.264 High Profile Level 5.1 に対応する
  - @voluntas

## 2025.1.2

**リリース日**:: 2025-11-29

- [FIX] VideoDecoderConfig と AudioDecoderConfig の description を string 型から bytes 型に修正する
  - WebCodecs API 仕様では AllowSharedBufferSource（バイナリデータ）として定義されている
  - @voluntas

## 2025.1.1

**リリース日**:: 2025-11-27

- [FIX] Apple Video Toolbox で連続フレームデコードが動かない問題を修正する
  - @voluntas

## 2025.1.0

**リリース日**:: 2025-11-25

**祝いリリース**
