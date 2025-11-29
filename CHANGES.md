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
