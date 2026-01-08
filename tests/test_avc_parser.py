"""H.264 (AVC) ヘッダーパーサーのテスト"""

import pytest

from webcodecs import (
    AVCNalUnitType,
    parse_avc_annexb,
    parse_avc_sps,
)


# =============================================================================
# テストデータ
# =============================================================================

# H.264 Baseline Profile Level 3.0 の SPS (320x240)
AVC_SPS_BASELINE = bytes(
    [
        0x67,
        0x42,
        0xC0,
        0x1E,
        0xDA,
        0x01,
        0x40,
        0x16,
        0xEC,
        0x04,
        0x40,
        0x00,
        0x00,
        0x03,
        0x00,
        0x40,
        0x00,
        0x00,
        0x0C,
        0x83,
        0xC5,
        0x8B,
        0x65,
        0x80,
    ]
)

# H.264 PPS
AVC_PPS = bytes([0x68, 0xCE, 0x3C, 0x80])

# Annex B フォーマットの H.264 ストリーム
AVC_ANNEXB_STREAM = (
    bytes([0x00, 0x00, 0x00, 0x01]) + AVC_SPS_BASELINE + bytes([0x00, 0x00, 0x00, 0x01]) + AVC_PPS
)


# =============================================================================
# 単体テスト
# =============================================================================


def test_parse_avc_sps_extracts_correct_values():
    """AVC SPS から正しい値が抽出されることを確認"""
    sps = parse_avc_sps(AVC_SPS_BASELINE)

    assert sps.profile_idc == 66
    assert sps.level_idc == 30
    assert sps.bit_depth_luma == 8
    assert sps.bit_depth_chroma == 8


def test_parse_avc_annexb_extracts_sps_and_pps():
    """AVC Annex B から SPS と PPS が抽出されることを確認"""
    info = parse_avc_annexb(AVC_ANNEXB_STREAM)

    assert info.sps is not None
    assert info.pps is not None
    assert info.sps.profile_idc == 66


def test_empty_data_raises_error():
    """空のデータでエラーが発生することを確認"""
    with pytest.raises(ValueError):
        parse_avc_annexb(b"")


def test_avc_nal_unit_type_enum_comparison():
    """AVCNalUnitType が IntEnum 相当の動作をすることを確認"""
    assert AVCNalUnitType.SPS == 7
    assert AVCNalUnitType.PPS == 8
    assert AVCNalUnitType.IDR_SLICE == 5
    assert AVCNalUnitType.NON_IDR_SLICE == 1

    info = parse_avc_annexb(AVC_ANNEXB_STREAM)
    assert len(info.nal_units) == 2
    assert info.nal_units[0].nal_unit_type == AVCNalUnitType.SPS
    assert info.nal_units[1].nal_unit_type == AVCNalUnitType.PPS
