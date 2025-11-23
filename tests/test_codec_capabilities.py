"""コーデック機能情報取得のテスト"""

import platform

import pytest

from webcodecs import HardwareAccelerationEngine, get_video_codec_capabilities


def test_get_video_codec_capabilities_returns_dict():
    """get_video_codec_capabilities が dict を返すことを確認"""
    capabilities = get_video_codec_capabilities()
    assert isinstance(capabilities, dict)


def test_get_video_codec_capabilities_has_none_engine():
    """get_video_codec_capabilities が NONE エンジンを常に含むことを確認"""
    capabilities = get_video_codec_capabilities()
    assert HardwareAccelerationEngine.NONE in capabilities


def test_get_video_codec_capabilities_none_engine_structure():
    """NONE エンジンの構造を確認"""
    capabilities = get_video_codec_capabilities()
    none_info = capabilities[HardwareAccelerationEngine.NONE]

    # 必須フィールドの存在確認
    assert "available" in none_info
    assert "platform" in none_info
    assert "codecs" in none_info

    # 値の型確認
    assert isinstance(none_info["available"], bool)
    assert isinstance(none_info["platform"], str)
    assert isinstance(none_info["codecs"], dict)

    # NONE エンジンは常に利用可能
    assert none_info["available"] is True
    assert none_info["platform"] == "all"


def test_get_video_codec_capabilities_none_engine_has_av1():
    """NONE エンジンが AV1 をサポートしていることを確認"""
    capabilities = get_video_codec_capabilities()
    none_info = capabilities[HardwareAccelerationEngine.NONE]

    assert "av01" in none_info["codecs"]

    av1_info = none_info["codecs"]["av01"]
    assert "encoder" in av1_info
    assert "decoder" in av1_info
    assert isinstance(av1_info["encoder"], bool)
    assert isinstance(av1_info["decoder"], bool)

    # AV1 エンコーダー/デコーダーは両方サポート
    assert av1_info["encoder"] is True
    assert av1_info["decoder"] is True


@pytest.mark.skipif(platform.system() != "Darwin", reason="macOS でのみ実行")
def test_get_video_codec_capabilities_macos_has_videotoolbox():
    """macOS で APPLE_VIDEO_TOOLBOX が含まれることを確認"""
    capabilities = get_video_codec_capabilities()
    assert HardwareAccelerationEngine.APPLE_VIDEO_TOOLBOX in capabilities


@pytest.mark.skipif(platform.system() != "Darwin", reason="macOS でのみ実行")
def test_get_video_codec_capabilities_macos_videotoolbox_structure():
    """macOS での APPLE_VIDEO_TOOLBOX の構造を確認"""
    capabilities = get_video_codec_capabilities()
    vt_info = capabilities[HardwareAccelerationEngine.APPLE_VIDEO_TOOLBOX]

    # 必須フィールドの存在確認
    assert "available" in vt_info
    assert "platform" in vt_info
    assert "codecs" in vt_info

    # 値の型確認
    assert isinstance(vt_info["available"], bool)
    assert isinstance(vt_info["platform"], str)
    assert isinstance(vt_info["codecs"], dict)

    # macOS では利用可能
    assert vt_info["available"] is True
    assert vt_info["platform"] == "darwin"


@pytest.mark.skipif(platform.system() != "Darwin", reason="macOS でのみ実行")
def test_get_video_codec_capabilities_macos_videotoolbox_has_h264_h265():
    """macOS での APPLE_VIDEO_TOOLBOX が AVC1 と HVC1 をサポートしていることを確認"""
    capabilities = get_video_codec_capabilities()
    vt_info = capabilities[HardwareAccelerationEngine.APPLE_VIDEO_TOOLBOX]

    # AVC1 (H.264) サポート確認
    assert "avc1" in vt_info["codecs"]
    avc1_info = vt_info["codecs"]["avc1"]
    assert avc1_info["encoder"] is True
    assert avc1_info["decoder"] is True

    # HVC1 (H.265/HEVC) サポート確認
    assert "hvc1" in vt_info["codecs"]
    hvc1_info = vt_info["codecs"]["hvc1"]
    assert hvc1_info["encoder"] is True
    assert hvc1_info["decoder"] is True


@pytest.mark.skipif(platform.system() != "Darwin", reason="macOS でのみ実行")
def test_get_video_codec_capabilities_macos_no_other_engines():
    """macOS で NVIDIA、INTEL、AMD AMF が含まれないことを確認"""
    capabilities = get_video_codec_capabilities()
    assert HardwareAccelerationEngine.NVIDIA_VIDEO_CODEC not in capabilities
    assert HardwareAccelerationEngine.INTEL_VPL not in capabilities
    assert HardwareAccelerationEngine.AMD_AMF not in capabilities


@pytest.mark.skipif(platform.system() == "Darwin", reason="macOS 以外でのみ実行")
def test_get_video_codec_capabilities_non_macos_no_videotoolbox():
    """macOS 以外で APPLE_VIDEO_TOOLBOX が含まれないことを確認"""
    capabilities = get_video_codec_capabilities()
    assert HardwareAccelerationEngine.APPLE_VIDEO_TOOLBOX not in capabilities


@pytest.mark.skipif(platform.system() == "Darwin", reason="macOS 以外でのみ実行")
def test_get_video_codec_capabilities_non_macos_no_other_engines():
    """macOS 以外で NVIDIA、INTEL、AMD AMF が含まれないことを確認"""
    capabilities = get_video_codec_capabilities()
    assert HardwareAccelerationEngine.NVIDIA_VIDEO_CODEC not in capabilities
    assert HardwareAccelerationEngine.INTEL_VPL not in capabilities
    assert HardwareAccelerationEngine.AMD_AMF not in capabilities
