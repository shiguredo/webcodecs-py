"""Property-Based Testing による HEVC (H.265) パーサーのテスト

ランダムなバイト列を入力してクラッシュやセグフォルトが発生しないことを確認
"""

from hypothesis import given, settings, HealthCheck, assume
from hypothesis import strategies as st

from webcodecs import (
    parse_hevc_annexb,
    parse_hevc_description,
    parse_hevc_vps,
    parse_hevc_sps,
    parse_hevc_pps,
    HEVCNalUnitType,
)


# 有効な HEVC VPS データ
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

# 有効な HEVC SPS データ
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

# 有効な HEVC PPS データ
HEVC_PPS = bytes([0x44, 0x01, 0xC1, 0x72, 0xB4, 0x62, 0x40])


@given(data=st.binary(min_size=0, max_size=1024))
@settings(
    max_examples=200,
    deadline=None,
    suppress_health_check=[HealthCheck.too_slow],
)
def prop_parse_hevc_annexb_random_data_no_crash(data):
    """ランダムなデータで parse_hevc_annexb がクラッシュしないことを確認"""
    try:
        parse_hevc_annexb(data)
    except (ValueError, RuntimeError):
        pass


@given(data=st.binary(min_size=1, max_size=256))
@settings(
    max_examples=200,
    deadline=None,
    suppress_health_check=[HealthCheck.too_slow],
)
def prop_parse_hevc_vps_random_data_no_crash(data):
    """ランダムなデータで parse_hevc_vps がクラッシュしないことを確認"""
    try:
        parse_hevc_vps(data)
    except (ValueError, RuntimeError):
        pass


@given(data=st.binary(min_size=1, max_size=256))
@settings(
    max_examples=200,
    deadline=None,
    suppress_health_check=[HealthCheck.too_slow],
)
def prop_parse_hevc_sps_random_data_no_crash(data):
    """ランダムなデータで parse_hevc_sps がクラッシュしないことを確認"""
    try:
        parse_hevc_sps(data)
    except (ValueError, RuntimeError):
        pass


@given(data=st.binary(min_size=1, max_size=256))
@settings(
    max_examples=200,
    deadline=None,
    suppress_health_check=[HealthCheck.too_slow],
)
def prop_parse_hevc_pps_random_data_no_crash(data):
    """ランダムなデータで parse_hevc_pps がクラッシュしないことを確認"""
    try:
        parse_hevc_pps(data)
    except (ValueError, RuntimeError):
        pass


@st.composite
def annexb_hevc_stream_strategy(draw):
    """有効な Annex B HEVC ストリームを生成するストラテジ"""
    num_nals = draw(st.integers(min_value=1, max_value=5))

    stream = b""
    for _ in range(num_nals):
        use_long_start_code = draw(st.booleans())
        if use_long_start_code:
            stream += bytes([0x00, 0x00, 0x00, 0x01])
        else:
            stream += bytes([0x00, 0x00, 0x01])

        # NAL ユニットタイプの選択
        nal_type = draw(
            st.sampled_from(
                [
                    HEVCNalUnitType.TRAIL_N,
                    HEVCNalUnitType.TRAIL_R,
                    HEVCNalUnitType.IDR_W_RADL,
                    HEVCNalUnitType.IDR_N_LP,
                    HEVCNalUnitType.CRA,
                    HEVCNalUnitType.VPS,
                    HEVCNalUnitType.SPS,
                    HEVCNalUnitType.PPS,
                ]
            )
        )

        if nal_type == HEVCNalUnitType.VPS:
            stream += HEVC_VPS
        elif nal_type == HEVCNalUnitType.SPS:
            stream += HEVC_SPS
        elif nal_type == HEVCNalUnitType.PPS:
            stream += HEVC_PPS
        else:
            # その他の NAL ユニット
            # HEVC NAL ヘッダー: 2 バイト
            nal_header_byte1 = (nal_type << 1) & 0x7E
            nal_header_byte2 = draw(st.integers(min_value=1, max_value=7))
            payload_size = draw(st.integers(min_value=0, max_value=32))
            payload = draw(st.binary(min_size=payload_size, max_size=payload_size))
            stream += bytes([nal_header_byte1, nal_header_byte2]) + payload

    return stream


@given(stream=annexb_hevc_stream_strategy())
@settings(
    max_examples=100,
    deadline=None,
    suppress_health_check=[HealthCheck.too_slow],
)
def prop_parse_hevc_annexb_valid_stream(stream):
    """有効な HEVC Annex B ストリームを正しくパースできることを確認"""
    info = parse_hevc_annexb(stream)

    # NAL ユニットが 1 つ以上ある
    assert len(info.nal_units) >= 1


@given(
    first_byte=st.integers(min_value=0, max_value=255),
    second_byte=st.integers(min_value=1, max_value=255),
)
@settings(max_examples=500, deadline=None)
def prop_hevc_nal_header_all_values(first_byte, second_byte):
    """すべての HEVC NAL ヘッダーバイト値でクラッシュしないことを確認"""
    # forbidden_zero_bit が 0 であることを確認（そうでなければ無効）
    assume((first_byte & 0x80) == 0)

    nal_data = bytes([0x00, 0x00, 0x00, 0x01, first_byte, second_byte])

    try:
        info = parse_hevc_annexb(nal_data)
        if len(info.nal_units) > 0:
            nal_unit = info.nal_units[0]
            # NAL ユニットタイプは (first_byte >> 1) & 0x3F
            expected_type = (first_byte >> 1) & 0x3F
            assert nal_unit.nal_unit_type == expected_type
    except (ValueError, RuntimeError):
        pass


@st.composite
def hvcc_box_strategy(draw):
    """有効な hvcC box (HEVCDecoderConfigurationRecord) を生成するストラテジ"""
    # configurationVersion = 1
    config_version = 1

    # general_profile_space (2 bits) + general_tier_flag (1 bit) + general_profile_idc (5 bits)
    general_profile_idc = draw(st.sampled_from([1, 2, 3]))
    byte1 = general_profile_idc & 0x1F

    # general_profile_compatibility_flags (4 bytes)
    profile_compat = draw(st.binary(min_size=4, max_size=4))

    # general_constraint_indicator_flags (6 bytes)
    constraint_flags = draw(st.binary(min_size=6, max_size=6))

    # general_level_idc
    level_idc = draw(st.sampled_from([93, 120, 150, 180]))

    # reserved (4 bits) + min_spatial_segmentation_idc (12 bits)
    byte13_14 = bytes([0xF0, 0x00])

    # reserved (6 bits) + parallelismType (2 bits)
    byte15 = 0xFC

    # reserved (6 bits) + chromaFormat (2 bits)
    byte16 = 0xFC | 1

    # reserved (5 bits) + bitDepthLumaMinus8 (3 bits)
    byte17 = 0xF8

    # reserved (5 bits) + bitDepthChromaMinus8 (3 bits)
    byte18 = 0xF8

    # avgFrameRate (2 bytes)
    avg_frame_rate = bytes([0x00, 0x00])

    # constantFrameRate (2 bits) + numTemporalLayers (3 bits) + temporalIdNested (1 bit) + lengthSizeMinusOne (2 bits)
    byte21 = 0x0F

    # numOfArrays = 3 (VPS, SPS, PPS)
    num_arrays = 3

    header = bytes([config_version, byte1])
    header += profile_compat
    header += constraint_flags
    header += bytes([level_idc])
    header += byte13_14
    header += bytes([byte15, byte16, byte17, byte18])
    header += avg_frame_rate
    header += bytes([byte21, num_arrays])

    # VPS array
    vps_array = bytes([0x20, 0x00, 0x01])
    vps_length = len(HEVC_VPS)
    vps_array += bytes([(vps_length >> 8) & 0xFF, vps_length & 0xFF])
    vps_array += HEVC_VPS

    # SPS array
    sps_array = bytes([0x21, 0x00, 0x01])
    sps_length = len(HEVC_SPS)
    sps_array += bytes([(sps_length >> 8) & 0xFF, sps_length & 0xFF])
    sps_array += HEVC_SPS

    # PPS array
    pps_array = bytes([0x22, 0x00, 0x01])
    pps_length = len(HEVC_PPS)
    pps_array += bytes([(pps_length >> 8) & 0xFF, pps_length & 0xFF])
    pps_array += HEVC_PPS

    return header + vps_array + sps_array + pps_array


@given(box=hvcc_box_strategy())
@settings(
    max_examples=50,
    deadline=None,
    suppress_health_check=[HealthCheck.too_slow],
)
def prop_parse_hevc_description_valid_box(box):
    """有効な hvcC box を正しくパースできることを確認"""
    info = parse_hevc_description(box)

    # VPS, SPS, PPS が抽出される
    assert info.vps is not None
    assert info.sps is not None
    assert info.pps is not None

    # NAL ユニットが抽出される（VPS + SPS + PPS）
    assert len(info.nal_units) >= 3

    # length_size は 1-4 の範囲
    assert 1 <= info.length_size <= 4


@given(data=st.binary(min_size=23, max_size=256))
@settings(
    max_examples=200,
    deadline=None,
    suppress_health_check=[HealthCheck.too_slow],
)
def prop_parse_hevc_description_random_no_crash(data):
    """ランダムな hvcC 風データでクラッシュしないことを確認"""
    try:
        parse_hevc_description(data)
    except (ValueError, RuntimeError):
        pass
