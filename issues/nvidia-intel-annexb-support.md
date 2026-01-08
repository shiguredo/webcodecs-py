# NVIDIA Video Codec / Intel VPL で Annex B 出力をサポートする

## 概要

NVIDIA Video Codec SDK と Intel VPL エンコーダーで Annex B 形式の出力をサポートする。

## 背景

- 現在 Apple Video Toolbox のみが `avc: {"format": "annexb"}` / `hevc: {"format": "annexb"}` オプションをサポート
- NVIDIA / Intel は length-prefixed 形式 (avcC/hvcC スタイル) のみ出力
- ストリーミング用途 (RTP/RTSP/WebRTC) では Annex B 形式が標準

## 対応内容

- NVIDIA Video Codec SDK エンコーダーに `avc: {"format": "annexb"}` オプションを追加
- NVIDIA Video Codec SDK エンコーダーに `hevc: {"format": "annexb"}` オプションを追加
- Intel VPL エンコーダーに `avc: {"format": "annexb"}` オプションを追加
- Intel VPL エンコーダーに `hevc: {"format": "annexb"}` オプションを追加

## 実装方針

length-prefixed → Annex B 変換:
- 4 バイト長プレフィックスを `0x00000001` スタートコードに置換

## 関連

- WebCodecs API 仕様: `AvcEncoderConfig.format` / `HevcEncoderConfig.format`
