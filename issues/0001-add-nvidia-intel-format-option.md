# NVIDIA Video Codec / Intel VPL で出力形式オプションをサポートする

## 概要

NVIDIA Video Codec SDK と Intel VPL エンコーダーで出力形式の切り替えオプションをサポートする。

## 現状

| エンコーダー | チャンク出力形式 | format オプション |
|-------------|-----------------|------------------|
| Apple Video Toolbox | デフォルト: length-prefixed | `annexb` / `avc` / `hevc` 対応 |
| NVIDIA NVENC | Annex B 固定 | 未対応 |
| Intel VPL | Annex B 固定 | 未対応 |

- NVIDIA / Intel は Annex B 形式のみ出力 (ストリーミング用途には OK)
- Apple Video Toolbox のみ `avc: {"format": "..."}` / `hevc: {"format": "..."}` オプションをサポート

## 対応内容

### NVIDIA Video Codec SDK

- `avc: {"format": "avc"}` オプションを追加 (length-prefixed 形式)
- `hevc: {"format": "hevc"}` オプションを追加 (length-prefixed 形式)
- デフォルトは現状維持 (Annex B)

### Intel VPL

- `avc: {"format": "avc"}` オプションを追加 (length-prefixed 形式)
- `hevc: {"format": "hevc"}` オプションを追加 (length-prefixed 形式)
- デフォルトは現状維持 (Annex B)

## 実装方針

Annex B → length-prefixed 変換:
- `0x00000001` / `0x000001` スタートコードを 4 バイト長プレフィックスに置換

## 用途

- length-prefixed 形式: MP4 コンテナ等
- Annex B 形式: ストリーミング (RTP/RTSP/WebRTC)

## 関連

- WebCodecs API 仕様: `AvcEncoderConfig.format` / `HevcEncoderConfig.format`
