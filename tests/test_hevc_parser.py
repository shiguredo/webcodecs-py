"""H.265 (HEVC) ヘッダーパーサーのテスト

PBT でカバーできない具体的な値の検証テスト
"""

import pytest

from webcodecs import (
    parse_hevc_annexb,
    parse_hevc_sps,
    HEVCNalUnitType,
)


# H.265 VPS
HEVC_VPS = bytes(
    [
        0x40,
        0x01,
        0x0C,
        0x01,
        0xFF,
        0xFF,
        0x01,
        0x60,
        0x00,
        0x00,
        0x03,
        0x00,
        0x00,
        0x03,
        0x00,
        0x00,
        0x03,
        0x00,
        0x00,
        0x03,
        0x00,
        0x5D,
        0xAC,
        0x59,
    ]
)

# H.265 SPS
HEVC_SPS = bytes(
    [
        0x42,
        0x01,
        0x01,
        0x01,
        0x60,
        0x00,
        0x00,
        0x03,
        0x00,
        0x00,
        0x03,
        0x00,
        0x00,
        0x03,
        0x00,
        0x00,
        0x03,
        0x00,
        0x5D,
        0xA0,
        0x02,
        0x80,
        0x80,
        0x2D,
        0x16,
        0x59,
        0x59,
        0xA4,
        0x93,
        0x2B,
        0xC0,
        0x40,
        0x00,
        0x00,
        0x03,
        0x00,
        0x40,
        0x00,
        0x00,
        0x07,
        0x82,
    ]
)

# H.265 PPS
HEVC_PPS = bytes([0x44, 0x01, 0xC1, 0x72, 0xB4, 0x62, 0x40])

# Annex B フォーマットの H.265 ストリーム
HEVC_ANNEXB_STREAM = (
    bytes([0x00, 0x00, 0x00, 0x01])
    + HEVC_VPS
    + bytes([0x00, 0x00, 0x00, 0x01])
    + HEVC_SPS
    + bytes([0x00, 0x00, 0x00, 0x01])
    + HEVC_PPS
)


def test_parse_hevc_sps_extracts_correct_values():
    """HEVC SPS から正しい値が抽出されることを確認"""
    sps = parse_hevc_sps(HEVC_SPS)

    assert sps.bit_depth_luma == 8
    assert sps.bit_depth_chroma == 8


def test_parse_hevc_annexb_extracts_vps_sps_pps():
    """HEVC Annex B から VPS/SPS/PPS が抽出されることを確認"""
    info = parse_hevc_annexb(HEVC_ANNEXB_STREAM)

    assert info.vps is not None
    assert info.sps is not None
    assert info.pps is not None


def test_empty_data_raises_error():
    """空のデータでエラーが発生することを確認"""
    with pytest.raises(ValueError):
        parse_hevc_annexb(b"")


def test_hevc_nal_unit_type_enum_comparison():
    """HEVCNalUnitType が IntEnum 相当の動作をすることを確認"""
    # enum と int の比較
    assert HEVCNalUnitType.VPS == 32
    assert HEVCNalUnitType.SPS == 33
    assert HEVCNalUnitType.PPS == 34
    assert HEVCNalUnitType.IDR_W_RADL == 19
    assert HEVCNalUnitType.IDR_N_LP == 20

    # パース結果との比較
    info = parse_hevc_annexb(HEVC_ANNEXB_STREAM)
    assert len(info.nal_units) == 3

    # VPS NAL
    assert info.nal_units[0].nal_unit_type == HEVCNalUnitType.VPS
    assert info.nal_units[0].nal_unit_type == 32

    # SPS NAL
    assert info.nal_units[1].nal_unit_type == HEVCNalUnitType.SPS
    assert info.nal_units[1].nal_unit_type == 33

    # PPS NAL
    assert info.nal_units[2].nal_unit_type == HEVCNalUnitType.PPS
    assert info.nal_units[2].nal_unit_type == 34
