# WebCodecs コーデック文字列仕様

webcodecs-py は WebCodecs API に準拠したコーデック文字列をサポートしています。このドキュメントでは、サポートされているコーデック文字列のフォーマットと、それらがエンコーダー/デコーダーの設定にどのように反映されるかを説明します。

## 概要

WebCodecs API では、コーデックを指定する際に標準化されたコーデック文字列を使用します。これらの文字列には、プロファイル、レベル、ビット深度などのパラメータが含まれており、webcodecs-py はこれらのパラメータを解析して、実際のエンコーダー/デコーダーの設定に反映します。

## サポートされているコーデック

### AV1 (av01.)

#### フォーマット

```text
av01.P.LLT.DD[.M.CCC.cp.tc.mc.F]
```

#### 必須パラメータ

- **P**: Profile
  - `0` = Main Profile
  - `1` = High Profile
  - `2` = Professional Profile
- **LL**: Level (10進数2桁)
  - 例: `04` = Level 3.0, `05` = Level 3.1
- **T**: Tier
  - `M` = Main Tier
  - `H` = High Tier
- **DD**: Bit Depth (ビット深度)
  - `08` = 8-bit
  - `10` = 10-bit
  - `12` = 12-bit

#### オプションパラメータ

- **M**: Monochrome (モノクロ)
  - `0` = Color (カラー)
  - `1` = Monochrome (モノクロ)
- **CCC**: Chroma Subsampling (クロマサブサンプリング)
  - 例: `112` = 4:2:0
- **cp**: Color Primaries (色域)
- **tc**: Transfer Characteristics (転送特性)
- **mc**: Matrix Coefficients (マトリックス係数)
- **F**: Full Range Flag (フルレンジフラグ)
  - `0` = Studio Swing
  - `1` = Full Range

#### 例

```python
# Main Profile, Level 3.0, Main Tier, 8-bit
encoder.configure({
    "codec": "av01.0.04M.08",
    "width": 1920,
    "height": 1080,
})

# Main Profile, Level 3.1, Main Tier, 8-bit, オプションパラメータ付き
encoder.configure({
    "codec": "av01.0.05M.08.0.112.09.16.09.0",
    "width": 1920,
    "height": 1080,
})
```

#### エンコーダーへの反映

AV1 エンコーダー (libaom) では、以下のパラメータが反映されます：

- **Profile**: `aom_config_.g_profile` に設定
- **Bit Depth**: `aom_config_.g_bit_depth` と `aom_config_.g_input_bit_depth` に設定

**注意**: 現在の実装では、Level と Tier は解析されますが、エンコーダーには明示的に設定されません。エンコーダーが自動的に適切な Level を選択します。

### AVC/H.264 (avc1., avc3.)

#### フォーマット

```
avc1.PPCCLL
avc3.PPCCLL
```

- **avc1**: パラメータセットを MP4 コンテナに格納
- **avc3**: パラメータセットをストリーム内に格納

#### パラメータ

- **PP**: Profile IDC (16進数2桁)
  - 例: `42` = Baseline Profile, `4D` = Main Profile, `64` = High Profile
- **CC**: Constraint Set Flags (16進数2桁)
- **LL**: Level IDC (16進数2桁)
  - 例: `1E` = Level 3.0, `1F` = Level 3.1

#### 例

```python
# Baseline Profile, Level 3.0
encoder.configure({
    "codec": "avc1.42E01E",
    "width": 1920,
    "height": 1080,
    "hardware_acceleration_engine": "apple_video_toolbox",  # macOS のみ
})

# Main Profile, Level 4.0
encoder.configure({
    "codec": "avc1.4D401F",
    "width": 1920,
    "height": 1080,
    "hardware_acceleration_engine": "apple_video_toolbox",  # macOS のみ
})
```

#### エンコーダーへの反映

現在、AVC/H.264 は macOS の VideoToolbox でのみサポートされています。コーデック文字列のパラメータは解析されますが、VideoToolbox の設定には直接反映されません。VideoToolbox が自動的に適切なプロファイルとレベルを選択します。

### HEVC/H.265 (hvc1., hev1.)

#### フォーマット

```
hvc1.X.X.X.X
hev1.X.X.X.X
```

- **hvc1**: パラメータセットを MP4 コンテナに格納
- **hev1**: パラメータセットをストリーム内に格納

#### パラメータ

ISO/IEC 14496-15 Section E.3 に準拠したパラメータ形式です。

#### 例

```python
# HEVC Main Profile
encoder.configure({
    "codec": "hvc1.1.6.L93.B0",
    "width": 1920,
    "height": 1080,
    "hardware_acceleration_engine": "apple_video_toolbox",  # macOS のみ
})
```

#### エンコーダーへの反映

現在、HEVC/H.265 は macOS の VideoToolbox でのみサポートされています。コーデック文字列のパラメータは解析されますが、VideoToolbox の設定には直接反映されません。VideoToolbox が自動的に適切なプロファイルとレベルを選択します。

## エラーハンドリング

無効なコーデック文字列を指定した場合、`configure()` メソッドは `ValueError` を発生させます：

```python
try:
    encoder.configure({
        "codec": "av01.9.04M.08",  # 無効な profile (9)
        "width": 1920,
        "height": 1080,
    })
except ValueError as e:
    print(f"Invalid codec string: {e}")
```

## 参考仕様

- [AV1 Codec ISO Media File Format Binding](https://aomediacodec.github.io/av1-isobmff/)
- [RFC 6381: The 'Codecs' and 'Profiles' Parameters for "Bucket" Media Types](https://datatracker.ietf.org/doc/html/rfc6381)
- [ISO/IEC 14496-15: Carriage of network abstraction layer (NAL) unit structured video in the ISO base media file format](https://www.iso.org/standard/74429.html)
- [WebCodecs AV1 Codec Registration](https://w3c.github.io/webcodecs/av1_codec_registration.html)
- [WebCodecs AVC Codec Registration](https://w3c.github.io/webcodecs/avc_codec_registration.html)
- [WebCodecs HEVC Codec Registration](https://w3c.github.io/webcodecs/hevc_codec_registration.html)
