# WebCodecs VideoFrame Metadata Registry

WebCodecs の VideoFrame に添付できるメタデータフィールドを定義するレジストリの仕様です。

## 概要

このレジストリは、`VideoFrame` オブジェクトに `VideoFrameMetadata` ディクショナリを介して添付できるメタデータフィールドを列挙することを目的としています。

## 登録エントリーの要件

レジストリへの登録エントリーは、1 つまたは複数のメタデータエントリーを記述するドキュメントであり、以下の要件があります：

1. 各メタデータエントリーは、個別の `VideoFrameMetadata` ディクショナリメンバーとして定義する必要がある
2. 各メタデータエントリーはシリアライズ可能である必要がある
3. 各メタデータエントリーは、発信元のワーキンググループでコンセンサスが得られた W3C 仕様によって定義されている必要がある
4. 各メタデータエントリーを定義する仕様は、明確に定義されたセマンティクスを提供する必要がある。特に、メディア処理パイプライン (エンコーダー、デコーダー、レンダラーなど) との相互作用が明確に定義されている必要がある
5. 候補登録エントリーは、レジストリに追加される前に、準拠性について議論および評価できるように、WebCodecs GitHub の Issue トラッカーに Issue を提出して発表する必要がある。Media Working Group が候補を受け入れることにコンセンサスを得た場合、候補を登録するためのプルリクエストをドラフトする必要がある (エディターまたは候補登録を要求する当事者のいずれかによって)。レジストリエディターがプルリクエストをレビューしてマージする
6. 既存のエントリーは、公開後に、候補エントリーと同じプロセスを通じて変更できる。変更には、公開仕様へのリンクの変更が含まれる場合がある
7. 既存のエントリーは非推奨にできる。これには Media Working Group のコンセンサスが必要であり、まだアクティブな場合は、登録エントリー仕様を作成したワーキンググループのコンセンサスが必要である

## VideoFrameMetadata のメンバー

以下は、現在レジストリに登録されているメタデータフィールドの一覧です。

| メンバー名 | 公開仕様 |
|-----------|---------|
| segments | [Human face segmentation](https://w3c.github.io/mediacapture-extensions/#human-face-segmentation) |
| captureTime | [Capture time](https://w3c.github.io/mediacapture-extensions/#dom-videoframemetadata-capturetime) |
| receiveTime | [Receive time](https://w3c.github.io/mediacapture-extensions/#dom-videoframemetadata-receivetime) |
| rtpTimestamp | [RTP timestamp](https://w3c.github.io/mediacapture-extensions/#dom-videoframemetadata-rtptimestamp) |
| backgroundBlur | [Background blur effect status](https://w3c.github.io/mediacapture-extensions/#background-blur-effect-status) |
| backgroundSegmentationMask | [Background segmentation mask](https://w3c.github.io/mediacapture-extensions/#background-segmentation-mask) |

### メタデータフィールドの説明

#### segments

人間の顔のセグメンテーション情報を提供します。MediaCapture Extensions で定義されています。

#### captureTime

ビデオフレームがキャプチャされた時刻を示します。この値は DOMHighResTimeStamp として表現されます。

#### receiveTime

ビデオフレームが受信された時刻を示します。この値は DOMHighResTimeStamp として表現されます。

#### rtpTimestamp

RTP (Real-time Transport Protocol) のタイムスタンプ値を示します。リアルタイム通信で使用されます。

#### backgroundBlur

背景ぼかし効果のステータスを示します。背景ぼかし処理が適用されているかどうかの情報を含みます。

#### backgroundSegmentationMask

背景セグメンテーションマスクを提供します。背景と前景を分離するためのマスク情報を含みます。

## プライバシーに関する考慮事項

プライバシーに関する考慮事項については、WebCodecs 仕様の [Privacy Considerations](https://w3c.github.io/webcodecs/#privacy-considerations) セクションを参照してください。

## セキュリティに関する考慮事項

セキュリティに関する考慮事項については、WebCodecs 仕様の [Security Considerations](https://w3c.github.io/webcodecs/#security-considerations) セクションを参照してください。

## 参考仕様

- [WebCodecs VideoFrame Metadata Registry](https://w3c.github.io/webcodecs/video_frame_metadata_registry.html)
- [W3C TR: WebCodecs VideoFrame Metadata Registry](https://www.w3.org/TR/webcodecs-video-frame-metadata-registry/)
- [MediaCapture Extensions](https://w3c.github.io/mediacapture-extensions/)
