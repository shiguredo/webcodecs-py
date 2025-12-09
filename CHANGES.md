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

- [CHANGE] NVIDIA Video Codec SDK のビルドオプション名を変更する
  - `NVIDIA_CUDA_TOOLKIT` → `USE_NVIDIA_CUDA_TOOLKIT`
  - @voluntas
- [ADD] Intel VPL (H.264/HEVC) をサポートする
  - VideoEncoder / VideoDecoder で hardware_acceleration_engine に HardwareAccelerationEngine.INTEL_VPL を指定できる
  - H.264 / HEVC のハードウェアエンコード/デコードに対応
  - キーフレーム時に decoder_config.description (avcC/hvcC) を提供する
  - Ubuntu のみ対応
  - @voluntas

## 2025.2.0

**リリース日**:: 2025-12-08

- [ADD] NVIDIA Video Codec SDK (NVENC/NVDEC) をサポートする
  - VideoEncoder / VideoDecoder で hardware_acceleration_engine に HardwareAccelerationEngine.NVIDIA_VIDEO_CODEC を指定できる
  - H.264 / HEVC / AV1 のハードウェアエンコード/デコードに対応
  - ビルド時に `NVIDIA_CUDA_TOOLKIT=true` の環境変数指定が必要
  - Ubuntu のみ対応
  - @voluntas
- [ADD] VideoDecoder に hardware_acceleration_engine オプションを追加する
  - HardwareAccelerationEngine ENUM でハードウェアアクセラレーションエンジンを指定可能
  - @voluntas
- [CHANGE] dict key の命名規則を camelCase から snake_case に統一する
  - `keyFrame` -> `key_frame`
  - `colorSpace` -> `color_space`
  - `codedWidth` -> `coded_width`
  - `codedHeight` -> `coded_height`
  - `decoderConfig` -> `decoder_config`
  - @voluntas
- [ADD] VP8/VP9 エンコーダー/デコーダーを追加する (macOS / Ubuntu)
  - libvpx 1.15.2 を使用
  - VP9 Profile 0/1/2/3 に対応 (10/12-bit 含む)
  - @voluntas
- [ADD] Python 3.12 に対応する
  - @voluntas
- [ADD] AudioData と VideoFrame でコンテキストマネージャー (with 文) をサポートする
  - `__enter__` / `__exit__` を実装し、with 文で自動的に close() が呼ばれるようにする
  - @voluntas
- [ADD] VideoEncoder の output callback で EncodedVideoChunkMetadata をサポートする
  - WebCodecs API 準拠で、キーフレーム時に decoder_config を含む metadata を提供する
  - @voluntas
- [ADD] VideoToolbox エンコーダーでキーフレーム時に decoder_config.description (avcC/hvcC) を提供する
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
- [UPDATE] examples/blend2d_to_mp4.py で metadata.decoder_config.description を使用して MP4 を生成するように修正する
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
