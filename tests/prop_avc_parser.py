"""Property-Based Testing による AVC (H.264) パーサーのテスト

ランダムなバイト列を入力してクラッシュやセグフォルトが発生しないことを確認
"""

from hypothesis import given, settings, HealthCheck
from hypothesis import strategies as st

from webcodecs import (
    parse_avc_annexb,
    parse_avc_description,
    parse_avc_sps,
    parse_avc_pps,
    AVCNalUnitType,
)


# 有効な AVC SPS データのベース（Baseline Profile）
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

# 有効な AVC PPS データ
AVC_PPS = bytes([0x68, 0xCE, 0x3C, 0x80])


@given(data=st.binary(min_size=0, max_size=1024))
@settings(
    max_examples=200,
    deadline=None,
    suppress_health_check=[HealthCheck.too_slow],
)
def prop_parse_avc_annexb_random_data_no_crash(data):
    """ランダムなデータで parse_avc_annexb がクラッシュしないことを確認"""
    try:
        parse_avc_annexb(data)
    except (ValueError, RuntimeError):
        pass


@given(data=st.binary(min_size=1, max_size=256))
@settings(
    max_examples=200,
    deadline=None,
    suppress_health_check=[HealthCheck.too_slow],
)
def prop_parse_avc_sps_random_data_no_crash(data):
    """ランダムなデータで parse_avc_sps がクラッシュしないことを確認"""
    try:
        parse_avc_sps(data)
    except (ValueError, RuntimeError):
        pass


@given(data=st.binary(min_size=1, max_size=256))
@settings(
    max_examples=200,
    deadline=None,
    suppress_health_check=[HealthCheck.too_slow],
)
def prop_parse_avc_pps_random_data_no_crash(data):
    """ランダムなデータで parse_avc_pps がクラッシュしないことを確認"""
    try:
        parse_avc_pps(data)
    except (ValueError, RuntimeError):
        pass


@st.composite
def annexb_avc_stream_strategy(draw):
    """有効な Annex B AVC ストリームを生成するストラテジ"""
    # NAL ユニット数
    num_nals = draw(st.integers(min_value=1, max_value=5))

    stream = b""
    for _ in range(num_nals):
        # スタートコードの選択（3 バイトまたは 4 バイト）
        use_long_start_code = draw(st.booleans())
        if use_long_start_code:
            stream += bytes([0x00, 0x00, 0x00, 0x01])
        else:
            stream += bytes([0x00, 0x00, 0x01])

        # NAL ユニットタイプの選択
        nal_type = draw(
            st.sampled_from(
                [
                    AVCNalUnitType.NON_IDR_SLICE,
                    AVCNalUnitType.IDR_SLICE,
                    AVCNalUnitType.SEI,
                    AVCNalUnitType.SPS,
                    AVCNalUnitType.PPS,
                    AVCNalUnitType.AUD,
                ]
            )
        )

        # NAL ヘッダーの生成
        nal_ref_idc = draw(st.integers(min_value=0, max_value=3))
        nal_header = (nal_ref_idc << 5) | nal_type

        # NAL ユニットに応じたデータを追加
        if nal_type == AVCNalUnitType.SPS:
            stream += AVC_SPS_BASELINE
        elif nal_type == AVCNalUnitType.PPS:
            stream += AVC_PPS
        else:
            # その他の NAL ユニット
            payload_size = draw(st.integers(min_value=1, max_value=32))
            payload = draw(st.binary(min_size=payload_size, max_size=payload_size))
            stream += bytes([nal_header]) + payload

    return stream


@given(stream=annexb_avc_stream_strategy())
@settings(
    max_examples=100,
    deadline=None,
    suppress_health_check=[HealthCheck.too_slow],
)
def prop_parse_avc_annexb_valid_stream(stream):
    """有効な Annex B ストリームを正しくパースできることを確認"""
    info = parse_avc_annexb(stream)

    # NAL ユニットが 1 つ以上ある
    assert len(info.nal_units) >= 1


@given(
    profile_idc=st.sampled_from([66, 77, 88, 100, 110, 122, 244]),
    level_idc=st.sampled_from([30, 31, 32, 40, 41, 42, 50, 51, 52]),
)
@settings(
    max_examples=50,
    deadline=None,
    suppress_health_check=[HealthCheck.too_slow],
)
def prop_parse_avc_sps_valid_profiles(profile_idc, level_idc):
    """様々なプロファイルの SPS をパースできることを確認"""
    # 最小限の有効な SPS を構築
    sps_data = bytearray(AVC_SPS_BASELINE)
    sps_data[1] = profile_idc
    sps_data[3] = level_idc

    try:
        sps = parse_avc_sps(bytes(sps_data))
        assert sps.profile_idc == profile_idc
        assert sps.level_idc == level_idc
    except (ValueError, RuntimeError):
        # プロファイルによっては追加データが必要な場合がある
        pass


@given(first_byte=st.integers(min_value=0, max_value=255))
@settings(max_examples=256, deadline=None)
def prop_avc_nal_header_all_values(first_byte):
    """すべての NAL ヘッダーバイト値でクラッシュしないことを確認"""
    # Annex B フォーマットで NAL ユニットを作成
    nal_data = bytes([0x00, 0x00, 0x00, 0x01, first_byte])

    try:
        info = parse_avc_annexb(nal_data)
        if len(info.nal_units) > 0:
            nal_unit = info.nal_units[0]
            # NAL ユニットタイプは下位 5 ビット
            expected_type = first_byte & 0x1F
            assert nal_unit.nal_unit_type == expected_type
            # nal_ref_idc は 5-6 ビット
            expected_ref_idc = (first_byte >> 5) & 0x03
            assert nal_unit.nal_ref_idc == expected_ref_idc
    except (ValueError, RuntimeError):
        pass


@st.composite
def avcc_box_strategy(draw):
    """有効な avcC box (AVCDecoderConfigurationRecord) を生成するストラテジ"""
    # configurationVersion = 1
    config_version = 1

    # profile_idc
    profile_idc = draw(st.sampled_from([66, 77, 88, 100, 110, 122, 244]))

    # profile_compatibility
    profile_compat = draw(st.integers(min_value=0, max_value=255))

    # level_idc
    level_idc = draw(st.sampled_from([30, 31, 32, 40, 41, 42, 50, 51, 52]))

    # lengthSizeMinusOne (下位 2 ビット)
    length_size_minus_one = draw(st.integers(min_value=0, max_value=3))
    byte4 = 0xFC | length_size_minus_one

    # numOfSequenceParameterSets (下位 5 ビット、1 に固定)
    byte5 = 0xE0 | 1

    # SPS 長さ（2 バイト）と SPS データ
    sps_length = len(AVC_SPS_BASELINE)
    sps_length_bytes = [(sps_length >> 8) & 0xFF, sps_length & 0xFF]

    # numOfPictureParameterSets = 1
    num_pps = 1

    # PPS 長さ（2 バイト）と PPS データ
    pps_length = len(AVC_PPS)
    pps_length_bytes = [(pps_length >> 8) & 0xFF, pps_length & 0xFF]

    box = bytes(
        [config_version, profile_idc, profile_compat, level_idc, byte4, byte5] + sps_length_bytes
    )
    box += AVC_SPS_BASELINE
    box += bytes([num_pps] + pps_length_bytes)
    box += AVC_PPS

    return box


@given(box=avcc_box_strategy())
@settings(
    max_examples=50,
    deadline=None,
    suppress_health_check=[HealthCheck.too_slow],
)
def prop_parse_avc_description_valid_box(box):
    """有効な avcC box を正しくパースできることを確認"""
    info = parse_avc_description(box)

    # SPS と PPS が抽出される
    assert info.sps is not None
    assert info.pps is not None

    # NAL ユニットが抽出される（SPS + PPS）
    assert len(info.nal_units) >= 2

    # length_size は 1-4 の範囲
    assert 1 <= info.length_size <= 4


@given(data=st.binary(min_size=7, max_size=256))
@settings(
    max_examples=200,
    deadline=None,
    suppress_health_check=[HealthCheck.too_slow],
)
def prop_parse_avc_description_random_no_crash(data):
    """ランダムな avcC 風データでクラッシュしないことを確認"""
    try:
        parse_avc_description(data)
    except (ValueError, RuntimeError):
        pass
