cmake_minimum_required(VERSION 4.1)
project(webcodecs_py LANGUAGES CXX C ASM)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# プラットフォームの検出
if(APPLE)
    # macOS デプロイメントターゲットが設定されていない場合は設定
    if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "14.0" CACHE STRING "Minimum macOS deployment version")
    endif()
    message(STATUS "macOS deployment target: ${CMAKE_OSX_DEPLOYMENT_TARGET}")
elseif(UNIX AND NOT APPLE)
    message(STATUS "Building for Linux")
elseif(WIN32)
    message(STATUS "Building for Windows")
else()
    message(FATAL_ERROR "Unsupported platform")
endif()

# NVIDIA Video Codec SDK の設定
# ビルド方法: USE_NVIDIA_CUDA_TOOLKIT=1 uv build --wheel
option(USE_NVIDIA_CUDA_TOOLKIT "Enable NVIDIA Video Codec SDK support" OFF)

# NVIDIA Video Codec SDK のパス設定
if(USE_NVIDIA_CUDA_TOOLKIT)
    # third_party/NvCodec を使用
    set(NVCODEC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/NvCodec")
    message(STATUS "NVIDIA Video Codec SDK path: ${NVCODEC_DIR}")

    # CUDA Toolkit を検索
    find_package(CUDAToolkit REQUIRED)
    message(STATUS "CUDA Toolkit found: ${CUDAToolkit_VERSION}")
    message(STATUS "CUDA include dir: ${CUDAToolkit_INCLUDE_DIRS}")

    # macOS では NVIDIA Video Codec SDK はサポートされていない
    if(APPLE)
        message(FATAL_ERROR "NVIDIA Video Codec SDK is not supported on macOS")
    endif()
endif()

# Intel VPL (Video Processing Library) の設定
# Linux では常に有効（ヘッダーは _deps からダウンロード、libvpl は dlopen で動的ロード）
if(NOT APPLE AND NOT WIN32)
    set(INTEL_VPL_ENABLED ON)
    message(STATUS "Intel VPL enabled (Linux)")
endif()

# レイアウト: ExternalProject の依存関係は _deps 配下で管理
# SKBUILD_PROJECT_DIR が利用可能な場合は使用（scikit-build-core）、そうでなければ CMAKE_CURRENT_SOURCE_DIR にフォールバック
if(DEFINED SKBUILD_PROJECT_DIR)
    set(PROJECT_ROOT "${SKBUILD_PROJECT_DIR}")
else()
    set(PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
endif()

set(DEPS_DIR "${PROJECT_ROOT}/_deps")
message(STATUS "Dependencies directory: ${DEPS_DIR}")

# Python と nanobind を検索
find_package(Python REQUIRED COMPONENTS Interpreter Development.Module)
find_package(nanobind CONFIG REQUIRED)

# 依存関係用の ExternalProject
include(ExternalProject)

# JSON から依存関係のバージョンを読み込み
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/deps.json DEPS_JSON)

# Opus の設定を解析
string(JSON OPUS_GIT_TAG GET ${DEPS_JSON} libopus tag)
string(JSON OPUS_GIT_REPOSITORY GET ${DEPS_JSON} libopus url)

# FLAC の設定を解析
string(JSON FLAC_GIT_TAG GET ${DEPS_JSON} libflac tag)
string(JSON FLAC_GIT_REPOSITORY GET ${DEPS_JSON} libflac url)

# AOM の設定を解析
string(JSON AOM_GIT_TAG GET ${DEPS_JSON} libaom tag)
string(JSON AOM_GIT_REPOSITORY GET ${DEPS_JSON} libaom url)

# dav1d の設定を解析
string(JSON DAV1D_GIT_TAG GET ${DEPS_JSON} libdav1d tag)
string(JSON DAV1D_GIT_REPOSITORY GET ${DEPS_JSON} libdav1d url)

# YUV の設定を解析
string(JSON YUV_GIT_REF GET ${DEPS_JSON} libyuv ref)
string(JSON YUV_GIT_REPOSITORY GET ${DEPS_JSON} libyuv url)

# VPL の設定を解析（Linux のみ）
if(NOT APPLE AND NOT WIN32)
    string(JSON VPL_GIT_TAG GET ${DEPS_JSON} libvpl tag)
    string(JSON VPL_GIT_REPOSITORY GET ${DEPS_JSON} libvpl url)
endif()

# 静的ライブラリのビルドオプションを設定
set(BUILD_SHARED_LIBS OFF CACHE BOOL "静的ライブラリをビルド" FORCE)

# ExternalProject ビルド用の並列ビルドジョブ数を決定
if(DEFINED ENV{CMAKE_BUILD_PARALLEL_LEVEL})
    set(PARALLEL_JOBS "$ENV{CMAKE_BUILD_PARALLEL_LEVEL}")
else()
    include(ProcessorCount)
    ProcessorCount(NCORES)
    if(NOT NCORES OR NCORES EQUAL 0)
        set(NCORES 2)
    endif()
    set(PARALLEL_JOBS "${NCORES}")
endif()

# ========== libopus ==========
message(STATUS "Setting up libopus...")
set(OPUS_SOURCE_DIR "${DEPS_DIR}/opus/${OPUS_GIT_TAG}/source")
set(OPUS_BUILD_DIR "${DEPS_DIR}/opus/${OPUS_GIT_TAG}/build")
set(OPUS_INSTALL_DIR "${DEPS_DIR}/opus/${OPUS_GIT_TAG}/install")

# Windows では .lib、それ以外では .a
if(WIN32)
    set(OPUS_LIB ${OPUS_INSTALL_DIR}/lib/opus.lib)
else()
    set(OPUS_LIB ${OPUS_INSTALL_DIR}/lib/libopus.a)
endif()

# ビルド済みかチェック
if(EXISTS ${OPUS_LIB})
    message(STATUS "libopus already built: ${OPUS_LIB}")
    # ダミーターゲットを作成
    add_custom_target(opus_build)
else()
    message(STATUS "Building libopus...")
    ExternalProject_Add(
        opus_build
        GIT_REPOSITORY ${OPUS_GIT_REPOSITORY}
        GIT_TAG ${OPUS_GIT_TAG}
        GIT_SHALLOW TRUE
        SOURCE_DIR ${OPUS_SOURCE_DIR}
        BINARY_DIR ${OPUS_BUILD_DIR}
        STAMP_DIR ${OPUS_BUILD_DIR}/stamp
        TMP_DIR ${OPUS_BUILD_DIR}/tmp
        UPDATE_COMMAND ""
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        CMAKE_ARGS
            -DCMAKE_INSTALL_PREFIX=${OPUS_INSTALL_DIR}
            -DCMAKE_BUILD_TYPE=Release
            -DBUILD_SHARED_LIBS=OFF
            -DCMAKE_POSITION_INDEPENDENT_CODE=ON
            -DOPUS_BUILD_SHARED_LIBRARY=OFF
            -DOPUS_BUILD_TESTING=OFF
            -DOPUS_BUILD_PROGRAMS=OFF
            -DOPUS_INSTALL_PKG_CONFIG_MODULE=OFF
            -DOPUS_INSTALL_CMAKE_CONFIG_MODULE=OFF
            -DOPUS_DISABLE_INTRINSICS=ON
        BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --config Release --parallel ${PARALLEL_JOBS}
        INSTALL_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --config Release --target install
        BUILD_BYPRODUCTS ${OPUS_LIB}
    )
endif()

# ========== libflac ==========
message(STATUS "Setting up libflac...")
set(FLAC_SOURCE_DIR "${DEPS_DIR}/flac/${FLAC_GIT_TAG}/source")
set(FLAC_BUILD_DIR "${DEPS_DIR}/flac/${FLAC_GIT_TAG}/build")
set(FLAC_INSTALL_DIR "${DEPS_DIR}/flac/${FLAC_GIT_TAG}/install")

# Windows では .lib、それ以外では .a
if(WIN32)
    set(FLAC_LIB ${FLAC_INSTALL_DIR}/lib/FLAC.lib)
else()
    set(FLAC_LIB ${FLAC_INSTALL_DIR}/lib/libFLAC.a)
endif()

# ビルド済みかチェック
if(EXISTS ${FLAC_LIB})
    message(STATUS "libflac already built: ${FLAC_LIB}")
    # ダミーターゲットを作成
    add_custom_target(flac_build)
else()
    message(STATUS "Building libflac...")
    ExternalProject_Add(
        flac_build
        GIT_REPOSITORY ${FLAC_GIT_REPOSITORY}
        GIT_TAG ${FLAC_GIT_TAG}
        GIT_SHALLOW TRUE
        SOURCE_DIR ${FLAC_SOURCE_DIR}
        BINARY_DIR ${FLAC_BUILD_DIR}
        STAMP_DIR ${FLAC_BUILD_DIR}/stamp
        TMP_DIR ${FLAC_BUILD_DIR}/tmp
        UPDATE_COMMAND ""
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        CMAKE_ARGS
            -DCMAKE_INSTALL_PREFIX=${FLAC_INSTALL_DIR}
            -DCMAKE_BUILD_TYPE=Release
            -DBUILD_SHARED_LIBS=OFF
            -DCMAKE_POSITION_INDEPENDENT_CODE=ON
            -DBUILD_CXXLIBS=OFF
            -DBUILD_PROGRAMS=OFF
            -DBUILD_EXAMPLES=OFF
            -DBUILD_TESTING=OFF
            -DBUILD_DOCS=OFF
            -DINSTALL_MANPAGES=OFF
            -DWITH_OGG=OFF
        BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --config Release --parallel ${PARALLEL_JOBS}
        INSTALL_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --config Release --target install
        BUILD_BYPRODUCTS ${FLAC_LIB}
    )
endif()

# ========== libaom ==========
message(STATUS "Setting up libaom...")
set(AOM_SOURCE_DIR "${DEPS_DIR}/aom/${AOM_GIT_TAG}/source")
set(AOM_BUILD_DIR "${DEPS_DIR}/aom/${AOM_GIT_TAG}/build")
set(AOM_INSTALL_DIR "${DEPS_DIR}/aom/${AOM_GIT_TAG}/install")

# Windows では .lib、それ以外では .a
if(WIN32)
    set(AOM_LIB ${AOM_INSTALL_DIR}/lib/aom.lib)
else()
    set(AOM_LIB ${AOM_INSTALL_DIR}/lib/libaom.a)
endif()

# ExternalProject を使用して libaom をダウンロードとビルド
# libaom がすでにビルドされているか確認
if(EXISTS ${AOM_LIB})
    message(STATUS "libaom already built: ${AOM_LIB}")
    # 依存関係の一貫性のためダミーターゲットを作成
    add_custom_target(libaom_build)
else()
    message(STATUS "Building libaom...")
    ExternalProject_Add(
        libaom_build
        GIT_REPOSITORY ${AOM_GIT_REPOSITORY}
        GIT_TAG ${AOM_GIT_TAG}
        GIT_SHALLOW TRUE
        SOURCE_DIR ${AOM_SOURCE_DIR}
        BINARY_DIR ${AOM_BUILD_DIR}
        STAMP_DIR ${AOM_BUILD_DIR}/stamp
        TMP_DIR ${AOM_BUILD_DIR}/tmp
        UPDATE_COMMAND ""
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        CMAKE_ARGS
            -DCMAKE_INSTALL_PREFIX=${AOM_INSTALL_DIR}
            -DCMAKE_BUILD_TYPE=Release
            -DCMAKE_OSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}
            -DBUILD_SHARED_LIBS=OFF
            -DENABLE_EXAMPLES=OFF
            -DENABLE_DOCS=OFF
            -DENABLE_TESTS=OFF
            -DENABLE_TOOLS=OFF
            -DCONFIG_UNIT_TESTS=0
            -DCONFIG_AV1_ENCODER=1
            -DCONFIG_AV1_DECODER=0
            -DAOM_TARGET_CPU=$<IF:$<OR:$<STREQUAL:${CMAKE_SYSTEM_PROCESSOR},arm64>,$<STREQUAL:${CMAKE_SYSTEM_PROCESSOR},aarch64>>,arm64,x86_64>
        BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --config Release --parallel ${PARALLEL_JOBS}
        INSTALL_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --config Release --target install
        BUILD_BYPRODUCTS ${AOM_LIB}
    )
endif()

# ========== dav1d ==========
message(STATUS "Setting up dav1d...")
set(DAV1D_SOURCE_DIR "${DEPS_DIR}/dav1d/${DAV1D_GIT_TAG}/source")
set(DAV1D_BUILD_DIR "${DEPS_DIR}/dav1d/${DAV1D_GIT_TAG}/build")
set(DAV1D_INSTALL_DIR "${DEPS_DIR}/dav1d/${DAV1D_GIT_TAG}/install")

# Windows でも meson は libdav1d.a として生成する（MSVC でもリンク可能）
set(DAV1D_LIB ${DAV1D_INSTALL_DIR}/lib/libdav1d.a)

# dav1d がすでにビルドされているか確認
if(EXISTS ${DAV1D_LIB})
    message(STATUS "dav1d already built: ${DAV1D_LIB}")
    # 依存関係の一貫性のためダミーターゲットを作成
    add_custom_target(dav1d_build)
else()
    message(STATUS "Building dav1d...")
    ExternalProject_Add(
        dav1d_build
        GIT_REPOSITORY ${DAV1D_GIT_REPOSITORY}
        GIT_TAG ${DAV1D_GIT_TAG}
        GIT_SHALLOW TRUE
        SOURCE_DIR ${DAV1D_SOURCE_DIR}
        BINARY_DIR ${DAV1D_BUILD_DIR}
        STAMP_DIR ${DAV1D_BUILD_DIR}/stamp
        TMP_DIR ${DAV1D_BUILD_DIR}/tmp
        UPDATE_COMMAND ""
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        CONFIGURE_COMMAND meson setup <BINARY_DIR> <SOURCE_DIR>
            --prefix=${DAV1D_INSTALL_DIR}
            --libdir=lib
            --default-library=static
            --buildtype=release
            -Denable_tests=false
            -Denable_examples=false
        BUILD_COMMAND meson compile -C <BINARY_DIR> -j ${PARALLEL_JOBS}
        INSTALL_COMMAND meson install -C <BINARY_DIR>
        BUILD_BYPRODUCTS ${DAV1D_LIB}
    )
endif()

# ========== libyuv ==========
message(STATUS "Setting up libyuv...")
set(YUV_SOURCE_DIR "${DEPS_DIR}/yuv/${YUV_GIT_REF}/source")
set(YUV_BUILD_DIR "${DEPS_DIR}/yuv/${YUV_GIT_REF}/build")
set(YUV_INSTALL_DIR "${DEPS_DIR}/yuv/${YUV_GIT_REF}/install")

# Windows では .lib、それ以外では .a
if(WIN32)
    set(YUV_LIB ${YUV_INSTALL_DIR}/lib/yuv.lib)
else()
    set(YUV_LIB ${YUV_INSTALL_DIR}/lib/libyuv.a)
endif()

if(EXISTS ${YUV_LIB})
    message(STATUS "libyuv already built: ${YUV_LIB}")
    # ダミーターゲットを作成して依存関係を満たす
    add_custom_target(libyuv_build)
else()
    message(STATUS "Building libyuv...")
    ExternalProject_Add(
        libyuv_build
        GIT_REPOSITORY ${YUV_GIT_REPOSITORY}
        GIT_TAG ${YUV_GIT_REF}
        GIT_SHALLOW FALSE
        SOURCE_DIR ${YUV_SOURCE_DIR}
        BINARY_DIR ${YUV_BUILD_DIR}
        STAMP_DIR ${YUV_BUILD_DIR}/stamp
        TMP_DIR ${YUV_BUILD_DIR}/tmp
        UPDATE_COMMAND ""
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        CMAKE_ARGS
            -DCMAKE_INSTALL_PREFIX=${YUV_INSTALL_DIR}
            -DCMAKE_BUILD_TYPE=Release
            -DBUILD_SHARED_LIBS=OFF
            -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --config Release --parallel ${PARALLEL_JOBS}
        INSTALL_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --config Release --target install
        BUILD_BYPRODUCTS ${YUV_LIB}
    )
endif()

# ========== libvpl (Linux のみ、ヘッダーのみ) ==========
if(INTEL_VPL_ENABLED)
    message(STATUS "Setting up libvpl...")
    set(VPL_SOURCE_DIR "${DEPS_DIR}/vpl/${VPL_GIT_TAG}/source")

    # ヘッダーディレクトリの存在チェック
    if(EXISTS "${VPL_SOURCE_DIR}/api/vpl/mfx.h")
        message(STATUS "libvpl already downloaded: ${VPL_SOURCE_DIR}")
        add_custom_target(libvpl_download)
    else()
        message(STATUS "Downloading libvpl...")
        ExternalProject_Add(
            libvpl_download
            GIT_REPOSITORY ${VPL_GIT_REPOSITORY}
            GIT_TAG ${VPL_GIT_TAG}
            GIT_SHALLOW TRUE
            SOURCE_DIR ${VPL_SOURCE_DIR}
            UPDATE_COMMAND ""
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
            CONFIGURE_COMMAND ""
            BUILD_COMMAND ""
            INSTALL_COMMAND ""
        )
    endif()

    # ヘッダーディレクトリを設定
    set(VPL_DIR "${VPL_SOURCE_DIR}/api/vpl")
    message(STATUS "Intel VPL header path: ${VPL_DIR}")
endif()

# ========== libvpx (macOS / Linux) ==========
if(APPLE OR UNIX)
    # VPX の設定を解析
    string(JSON VPX_GIT_TAG GET ${DEPS_JSON} libvpx tag)
    string(JSON VPX_GIT_REPOSITORY GET ${DEPS_JSON} libvpx url)

    message(STATUS "Setting up libvpx...")
    set(VPX_SOURCE_DIR "${DEPS_DIR}/vpx/${VPX_GIT_TAG}/source")
    set(VPX_BUILD_DIR "${DEPS_DIR}/vpx/${VPX_GIT_TAG}/build")
    set(VPX_INSTALL_DIR "${DEPS_DIR}/vpx/${VPX_GIT_TAG}/install")

    set(VPX_LIB ${VPX_INSTALL_DIR}/lib/libvpx.a)

    # アーキテクチャとプラットフォームに応じたターゲットを設定
    if(APPLE)
        # darwin24 = macOS 15 を使用
        if(CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64" OR CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
            set(VPX_TARGET "arm64-darwin24-gcc")
        else()
            set(VPX_TARGET "x86_64-darwin24-gcc")
        endif()
    else()
        # Linux
        if(CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64" OR CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
            set(VPX_TARGET "arm64-linux-gcc")
        else()
            set(VPX_TARGET "x86_64-linux-gcc")
        endif()
    endif()

    if(EXISTS ${VPX_LIB})
        message(STATUS "libvpx already built: ${VPX_LIB}")
        add_custom_target(libvpx_build)
    else()
        message(STATUS "Building libvpx...")
        ExternalProject_Add(
            libvpx_build
            GIT_REPOSITORY ${VPX_GIT_REPOSITORY}
            GIT_TAG ${VPX_GIT_TAG}
            GIT_SHALLOW TRUE
            SOURCE_DIR ${VPX_SOURCE_DIR}
            BINARY_DIR ${VPX_BUILD_DIR}
            STAMP_DIR ${VPX_BUILD_DIR}/stamp
            TMP_DIR ${VPX_BUILD_DIR}/tmp
            UPDATE_COMMAND ""
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
            CONFIGURE_COMMAND ${VPX_SOURCE_DIR}/configure
                --prefix=${VPX_INSTALL_DIR}
                --target=${VPX_TARGET}
                --enable-pic
                --enable-vp8
                --enable-vp9
                --enable-vp9-highbitdepth
                --enable-static
                --disable-shared
                --disable-examples
                --disable-tools
                --disable-docs
                --disable-unit-tests
            BUILD_COMMAND make -j ${PARALLEL_JOBS}
            INSTALL_COMMAND make install
            BUILD_BYPRODUCTS ${VPX_LIB}
        )
    endif()
endif()

# メインモジュールを作成
# 注意: video_encoder_*.cpp と video_decoder_*.cpp ファイルは
# video_encoder.cpp と video_decoder.cpp に #include されるため、
# 個別のコンパイル単位として追加しない
set(WEBCODECS_SOURCES
    src/bindings/webcodecs_ext.cpp
    src/bindings/webcodecs_types.cpp
    src/bindings/codec_parser.cpp
    src/bindings/video_frame.cpp
    src/bindings/audio_data.cpp
    src/bindings/video_decoder.cpp
    src/bindings/audio_decoder.cpp
    src/bindings/audio_decoder_opus.cpp
    src/bindings/audio_decoder_flac.cpp
    src/bindings/audio_decoder_apple_audio_toolbox.cpp
    src/bindings/video_encoder.cpp
    src/bindings/audio_encoder.cpp
    src/bindings/audio_encoder_opus.cpp
    src/bindings/audio_encoder_flac.cpp
    src/bindings/audio_encoder_apple_audio_toolbox.cpp
    src/bindings/encoded_video_chunk.cpp
    src/bindings/encoded_audio_chunk.cpp
    src/bindings/video_codec_capabilities.cpp
    src/bindings/image_decoder.cpp
)

nanobind_add_module(webcodecs_ext
    NB_DOMAIN webcodecs
    FREE_THREADED
    ${WEBCODECS_SOURCES}
)

# ビルド順序を保証するため依存関係を追加
add_dependencies(webcodecs_ext opus_build flac_build libaom_build libyuv_build dav1d_build)
if(APPLE OR UNIX)
    add_dependencies(webcodecs_ext libvpx_build)
endif()
if(INTEL_VPL_ENABLED)
    add_dependencies(webcodecs_ext libvpl_download)
endif()

# インクルードディレクトリ
target_include_directories(webcodecs_ext PRIVATE
    src/bindings
    ${OPUS_INSTALL_DIR}/include/opus
    ${FLAC_INSTALL_DIR}/include
    ${AOM_INSTALL_DIR}/include
    ${DAV1D_INSTALL_DIR}/include
    ${YUV_INSTALL_DIR}/include
)
if(APPLE OR UNIX)
    target_include_directories(webcodecs_ext PRIVATE
        ${VPX_INSTALL_DIR}/include
    )
endif()

# NVIDIA Video Codec SDK を使用する場合のインクルードディレクトリ
if(USE_NVIDIA_CUDA_TOOLKIT)
    target_include_directories(webcodecs_ext PRIVATE
        ${NVCODEC_DIR}/Interface
        ${NVCODEC_DIR}/Utils
        ${NVCODEC_DIR}/NvCodec/NvEncoder
        ${NVCODEC_DIR}/NvCodec/NvDecoder
        ${CUDAToolkit_INCLUDE_DIRS}
    )
    # NvCodec ソースファイルを追加
    target_sources(webcodecs_ext PRIVATE
        ${NVCODEC_DIR}/NvCodec/NvEncoder/NvEncoder.cpp
        ${NVCODEC_DIR}/NvCodec/NvEncoder/NvEncoderCuda.cpp
        ${NVCODEC_DIR}/NvCodec/NvDecoder/NvDecoder.cpp
    )
    # コンパイル定義を追加
    target_compile_definitions(webcodecs_ext PRIVATE USE_NVIDIA_CUDA_TOOLKIT=1)
endif()

# ライブラリをリンク
# Windows では絶対パスを明確に指定
if(WIN32)
    # Windows では文字列として扱って絶対パスを確保
    target_link_libraries(webcodecs_ext PRIVATE
        "${OPUS_LIB}"
        "${FLAC_LIB}"
        "${AOM_LIB}"
        "${DAV1D_LIB}"
        "${YUV_LIB}"
    )
    # FLAC を静的リンクする場合に必要
    target_compile_definitions(webcodecs_ext PRIVATE FLAC__NO_DLL)
else()
    target_link_libraries(webcodecs_ext PRIVATE
        ${OPUS_LIB}
        ${FLAC_LIB}
        ${AOM_LIB}
        ${DAV1D_LIB}
        ${YUV_LIB}
    )
endif()

# libvpx (macOS / Linux)
if(APPLE OR UNIX)
    target_link_libraries(webcodecs_ext PRIVATE
        ${VPX_LIB}
    )
endif()

# macOS フレームワーク
if(APPLE)
    target_link_libraries(webcodecs_ext PRIVATE
        "-framework CoreFoundation"
        "-framework CoreVideo"
        "-framework VideoToolbox"
        "-framework CoreMedia"
        "-framework AudioToolbox"
        "-framework CoreGraphics"
        "-framework ImageIO"
    )
endif()

# NVIDIA Video Codec SDK ライブラリ (Ubuntu のみ対応)
# CUDA ライブラリは dlopen で動的にロードするため、直接リンクしない
# これにより、GPU がない環境でもモジュールをインポートできる
if(USE_NVIDIA_CUDA_TOOLKIT)
    # dlopen/dlsym に必要
    target_link_libraries(webcodecs_ext PRIVATE dl)
endif()

# Intel VPL (Linux のみ)
# libvpl は dlopen で動的にロードするため、直接リンクしない
# これにより、Intel GPU がない環境でもモジュールをインポートできる
# C++ コードでは #if defined(__linux__) で判定する
if(INTEL_VPL_ENABLED)
    target_include_directories(webcodecs_ext PRIVATE ${VPL_DIR})
    # dlopen/dlsym に必要
    target_link_libraries(webcodecs_ext PRIVATE dl)
endif()

# .pyi スタブファイルを生成
nanobind_add_stub(
    webcodecs_ext_stub
    MODULE webcodecs_ext
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/webcodecs_ext.pyi
    PYTHON_PATH $<TARGET_FILE_DIR:webcodecs_ext>
    DEPENDS webcodecs_ext
)

# モジュールをインストール
install(TARGETS webcodecs_ext LIBRARY DESTINATION webcodecs)

# .pyi スタブファイルをインストール
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/webcodecs_ext.pyi DESTINATION webcodecs)

# py.typed マーカーファイルをインストール（型情報を含むパッケージであることを示す）
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/src/webcodecs/py.typed DESTINATION webcodecs)
