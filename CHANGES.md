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

## 2026.1.0

**リリース日**:: 2026-01-07

- [UPDATE] libuv を `022efdb0b771f7353741dbe360b8bef4e0a874eb` にアップデートする
  - @voluntas
- [UPDATE] libdav1d を v1.5.3 にアップデートする
  - @voluntas
- [UPDATE] libvpl を v2.16.0 にアップデートする
  - @voluntas
- [UPDATE] libopus を v1.6 にアップデートする
  - @voluntas
- [UPDATE] macOS で hardware_acceleration_engine 未指定時に H.264/HEVC で Apple Video Toolbox を自動選択するように修正する
  - WebCodecs API 準拠の自動選択動作を実装
  - HardwareAccelerationEngine.APPLE_VIDEO_TOOLBOX を明示的に指定しなくても H.264/HEVC では Video Toolbox が使用される
  - @voluntas
- [ADD] VideoFrame に native_buffer サポートを追加する (macOS)
  - コンストラクタで PyCapsule (CVPixelBufferRef) を直接受け取れるようになる
  - Apple Video Toolbox エンコーダーが直接利用可能
  - native_buffer のみの VideoFrame では plane()/planes()/copy_to()/clone() は RuntimeError
  - @voluntas
- [ADD] Python 3.13t / 3.14t の Free-Threading ビルドに対応する
  - VideoEncoder / VideoDecoder / AudioEncoder / AudioDecoder でスレッドセーフなコールバック管理を実装
  - nanobind の nb::ft_mutex を使用した排他制御
  - Windows 3.14t は nanobind ビルドの問題により非対応
  - @voluntas
- [ADD] Apple Video Toolbox で VP9/AV1 デコーダーをサポートする
  - VideoDecoderConfig で hardware_acceleration_engine に HardwareAccelerationEngine.APPLE_VIDEO_TOOLBOX を指定した場合のみ有効
  - デフォルトは libvpx (VP9) / dav1d (AV1) によるソフトウェアデコード
  - @voluntas
- [ADD] NVIDIA Video Codec SDK (H.264/HEVC) で description (avcC/hvcC) をサポートする
  - キーフレーム時に decoder_config.description を提供する
  - デコーダーの configure で description を指定可能
  - @voluntas
- [ADD] Intel VPL (H.264/HEVC) をサポートする
  - VideoEncoder / VideoDecoder で hardware_acceleration_engine に HardwareAccelerationEngine.INTEL_VPL を指定できる
  - H.264 / HEVC のハードウェアエンコード/デコードに対応
  - キーフレーム時に decoder_config.description (avcC/hvcC) を提供する
  - Ubuntu x86_64 のみ対応
- [ADD] ImageDecoder を追加する (macOS)
  - WebCodecs ImageDecoder API を実装
  - macOS の Image I/O フレームワークを使用
  - JPEG、PNG、GIF、WebP、BMP、TIFF、HEIC/HEIF に対応
  - @voluntas

### misc

- [CHANGE] GitHub Actions で auditwheel を uvx 経由で実行するように変更する
  - pyproject.toml から pypi 依存グループを削除
  - @voluntas
- [CHANGE] NVIDIA Video Codec SDK のビルドオプション名を変更する
  - `NVIDIA_CUDA_TOOLKIT` → `USE_NVIDIA_CUDA_TOOLKIT`
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
