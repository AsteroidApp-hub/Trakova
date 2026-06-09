cmake_minimum_required(VERSION 3.22)

set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version" FORCE)

project(Trakova VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── JUCE 8 ──
# 公開リポジトリには JUCE を含めない。ローカルに JUCE/ があればそれを使い (オフライン/高速)、
# 無ければ FetchContent で固定バージョンを取得する (CI / 新規 clone はこちら)。
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    message(STATUS "JUCE: ローカルの JUCE/ を使用")
    add_subdirectory(JUCE)
else()
    message(STATUS "JUCE: ローカルに無いため FetchContent で取得 (8.0.12)")
    include(FetchContent)
    FetchContent_Declare(
        JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.12          # 開発時 commit は 501c0767。厳密一致が必要ならこの SHA に置換し GIT_SHALLOW を外す
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_gui_app(Trakova
    PRODUCT_NAME                    "Trakova"
    COMPANY_NAME                    "Trakova"
    BUNDLE_ID                       "com.trakova.app"
    VERSION                         "0.1.0"
    ICON_BIG                        "${CMAKE_CURRENT_SOURCE_DIR}/Icon/icon_1024.png"
    ICON_SMALL                      "${CMAKE_CURRENT_SOURCE_DIR}/Icon/icon_256.png"
    MICROPHONE_PERMISSION_ENABLED   TRUE
    MICROPHONE_PERMISSION_TEXT      "Trakova は音声の録音にマイクへのアクセスを必要とします。 / Trakova needs microphone access for audio recording."
    PLIST_TO_MERGE                  "<plist version=\"1.0\"><dict><key>NSHumanReadableCopyright</key><string>Copyright (C) 2025-2026 Studio Asteroid</string></dict></plist>"
)

juce_generate_juce_header(Trakova)

target_sources(Trakova PRIVATE
    Source/Main.cpp
    Source/MainComponent.cpp
    Source/MainComponent_Commands.cpp
    Source/MainComponent_Plugins.cpp
    Source/MainComponent_Project.cpp
    Source/MainComponent_Dialogs.cpp
    Source/MainComponent_Preferences.cpp
    Source/MainComponent_DialogsMisc.cpp
    Source/Localisation.cpp
    Source/Audio/AudioEngine.cpp
    Source/Audio/AudioFileImporter.cpp
    Source/Audio/AudioDeviceSettings.cpp
    Source/Tracks/AudioClip.cpp
    Source/Tracks/Track.cpp
    Source/Tracks/TrackManager.cpp
    Source/Recording/RecordingManager.cpp
    Source/Project/ProjectManager.cpp
    Source/Project/BackupManager.cpp
    Source/Project/RecentProjects.cpp
    Source/Project/WindowState.cpp
    Source/Project/AppPreferences.cpp
    Source/Project/AdService.cpp
    Source/Project/UpdateChecker.cpp
    Source/Export/ExportEngine.cpp
    Source/Export/ExportDialog.cpp
    Source/Export/Mp3EncoderWriter.cpp
    Source/VST/PluginManager.cpp
    Source/VST/PluginChain.cpp
    Source/MIDI/MidiImporter.cpp
    Source/MIDI/MidiExporter.cpp
    Source/MIDI/MidiImportDialog.cpp
    Source/MIDI/InternalSynth.cpp
    $<$<PLATFORM_ID:Darwin>:Source/Mac/GlobalKeyMonitor.mm>
    $<$<NOT:$<PLATFORM_ID:Darwin>>:Source/Mac/GlobalKeyMonitor_stub.cpp>
    Source/UI/PluginManagerDialog.cpp
    Source/UI/StartupComponent.cpp
    Source/UI/AdPanel.cpp
    Source/UI/Toolbar.cpp
    Source/UI/TapTempoDialog.cpp
    Source/UI/TrackHeaderPanel.cpp
    Source/UI/TrackHeaderView.cpp
    Source/UI/TrackHeaderView_Interaction.cpp
    Source/UI/TimelineView.cpp
    Source/UI/TimelineView_Drawing.cpp
    Source/UI/TimelineView_Mouse.cpp
    Source/UI/TimelineView_MidiEdit.cpp
    Source/UI/TimelineView_Edit.cpp
    Source/UI/TimelineView_EditOps.cpp
    Source/UI/TimelineView_Markers.cpp
    Source/UI/MasterPanel.cpp
    Source/UI/PianoRollEditor.cpp
    Source/UI/StatusBar.cpp
)

target_include_directories(Trakova SYSTEM PRIVATE
    Source/ThirdParty/r8brain
)

target_compile_definitions(Trakova PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_MICROPHONE_PERMISSION_ENABLED=1
    JUCE_MODAL_LOOPS_PERMITTED=1
    # プラグインホスティング: VST3 のみ (AU は非対応)
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:Trakova,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:Trakova,JUCE_VERSION>"
)

# ── 起動画面の広告枠 (任意・既定 OFF) ──
# 公開ソースの既定はオフ (起動画面は従来どおり 2 列・通信なし)。公式配布ビルドだけが
# 配布ビルド時に以下を渡して 3 列の広告枠を有効化し、本番フィード URL を埋め込む:
#   cmake -DTRAKOVA_ADS_ENABLED=ON -DTRAKOVA_AD_FEED_URL="https://.../feed.json" ...
# 実効表示は (TRAKOVA_ADS_ENABLED && AppPreferences::showAds)。URL 未指定時はソースの既定値。
#
# 開発時に見た目を確認するには -DTRAKOVA_ADS_DEBUG=ON を渡す。広告枠を有効化し、サーバー無しで
# 組み込みのサンプル広告 (複数件・画像つき) を表示する (通信しない)。
option(TRAKOVA_ADS_ENABLED "Enable the startup-screen ad panel (official builds only)" OFF)
option(TRAKOVA_ADS_DEBUG   "Show built-in sample ads for development (implies ads enabled, no server)" OFF)
set(TRAKOVA_AD_FEED_URL "" CACHE STRING "Ad feed JSON URL embedded at build time (official builds set this)")
if(TRAKOVA_ADS_ENABLED OR TRAKOVA_ADS_DEBUG)
    target_compile_definitions(Trakova PRIVATE TRAKOVA_ADS_ENABLED=1)
endif()
if(TRAKOVA_ADS_DEBUG)
    target_compile_definitions(Trakova PRIVATE TRAKOVA_ADS_DEBUG=1)
    message(STATUS "Trakova: startup ad panel ENABLED (debug sample mode / no server)")
elseif(TRAKOVA_ADS_ENABLED)
    message(STATUS "Trakova: startup ad panel ENABLED")
else()
    message(STATUS "Trakova: startup ad panel disabled (default / public source)")
endif()
if(TRAKOVA_AD_FEED_URL)
    target_compile_definitions(Trakova PRIVATE TRAKOVA_AD_FEED_URL="${TRAKOVA_AD_FEED_URL}")
    message(STATUS "Trakova: ad feed URL = ${TRAKOVA_AD_FEED_URL}")
endif()

# MSVC は既定で Shift-JIS としてソースを読むため u8 リテラル等が壊れる。
# /utf-8 でソース・実行ファイル両方を UTF-8 解釈に統一する。
# /wd4819 はコードページ警告を抑制。NOMINMAX は Windows.h の min/max マクロ衝突回避。
if(MSVC)
    target_compile_options(Trakova PRIVATE /utf-8 /wd4819)
    target_compile_definitions(Trakova PRIVATE NOMINMAX=1 _USE_MATH_DEFINES=1)
endif()

# ── Windows: ASIO SDK が置かれていれば自動的に ASIO サポートを有効化 ──
# Steinberg ASIO SDK は再配布制限があるため Git には含めず、
# 各自で https://www.steinberg.net/developers/ から取得し
# Source/ThirdParty/asiosdk/ に配置する。
# 配置されていない環境では WASAPI / DirectSound のみが有効になる。
set(ASIO_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Source/ThirdParty/asiosdk")
if(WIN32)
    if(EXISTS "${ASIO_SDK_DIR}/common/iasiodrv.h")
        message(STATUS "ASIO SDK found at ${ASIO_SDK_DIR} — ASIO サポート有効")
        target_compile_definitions(Trakova PRIVATE JUCE_ASIO=1)
        target_include_directories(Trakova SYSTEM PRIVATE
            "${ASIO_SDK_DIR}/common"
            "${ASIO_SDK_DIR}/host"
            "${ASIO_SDK_DIR}/host/pc"
        )
    else()
        message(STATUS "ASIO SDK not found at ${ASIO_SDK_DIR} — ASIO 無効 (WASAPI/DirectSound のみ)")
    endif()
endif()

# ── 内蔵 LAME (libmp3lame) を静的ライブラリとしてビルド ──
file(GLOB LAME_LIBMP3LAME_SOURCES Source/ThirdParty/lame/libmp3lame/*.c)
file(GLOB LAME_LIBMP3LAME_VECTOR  Source/ThirdParty/lame/libmp3lame/vector/*.c)
file(GLOB LAME_MPGLIB_SOURCES     Source/ThirdParty/lame/mpglib/*.c)
add_library(lame_static STATIC
    ${LAME_LIBMP3LAME_SOURCES}
    ${LAME_LIBMP3LAME_VECTOR}
    ${LAME_MPGLIB_SOURCES}
)
target_include_directories(lame_static PRIVATE
    Source/ThirdParty/lame
    Source/ThirdParty/lame/libmp3lame
    Source/ThirdParty/lame/mpglib
)
target_include_directories(lame_static PUBLIC
    Source/ThirdParty/lame/include
)
target_compile_definitions(lame_static PRIVATE
    HAVE_CONFIG_H=1
    LAME_LIBRARY_BUILD=1
)
if(NOT MSVC)
    target_compile_options(lame_static PRIVATE
        -Wno-everything                # サードパーティの警告は抑制
    )
else()
    target_compile_options(lame_static PRIVATE
        /wd4244 /wd4267 /wd4305 /wd4554 /wd4018 /wd4101 /wd4146 /wd4133 /wd4996
        /wd4819   # コードページ警告 (C4819) 抑制
        /utf-8    # ソースを UTF-8 として解釈
    )
endif()

# ── Rubber Band Library (R3 高品質ピッチシフト) を静的ライブラリとしてビルド ──
file(GLOB RB_COMMON_SOURCES Source/ThirdParty/rubberband/src/common/*.cpp)
file(GLOB RB_FINER_SOURCES  Source/ThirdParty/rubberband/src/finer/*.cpp)
file(GLOB RB_FASTER_SOURCES Source/ThirdParty/rubberband/src/faster/*.cpp)
add_library(rubberband_static STATIC
    ${RB_COMMON_SOURCES}
    ${RB_FINER_SOURCES}
    ${RB_FASTER_SOURCES}
    Source/ThirdParty/rubberband/src/RubberBandStretcher.cpp
)
target_include_directories(rubberband_static PUBLIC
    Source/ThirdParty/rubberband
)
target_include_directories(rubberband_static PRIVATE
    Source/ThirdParty/rubberband/src
)
if(APPLE)
    target_compile_definitions(rubberband_static PRIVATE
        HAVE_VDSP=1          # Apple Accelerate vDSP を FFT バックエンドに
        LACK_SINCOS=1        # macOS には sincos() が無い
        USE_BQRESAMPLER=1    # 内蔵 BQResampler
        USE_PTHREADS=1       # POSIX threads
    )
elseif(WIN32)
    target_compile_definitions(rubberband_static PRIVATE
        USE_BUILTIN_FFT=1    # 内蔵 FFT (vDSP/FFTW/IPP に依存しない)
        USE_BQRESAMPLER=1    # 内蔵 BQResampler
        NOMINMAX=1           # Windows.h の min/max マクロ衝突回避
    )
else()
    target_compile_definitions(rubberband_static PRIVATE
        USE_BUILTIN_FFT=1
        USE_BQRESAMPLER=1
        USE_PTHREADS=1
    )
endif()
target_compile_features(rubberband_static PUBLIC cxx_std_17)
if(NOT MSVC)
    target_compile_options(rubberband_static PRIVATE -Wno-everything)
else()
    # MSVC: サードパーティ警告を抑制 + UTF-8 で .c/.cpp を解釈
    target_compile_options(rubberband_static PRIVATE /W0 /utf-8)
endif()

target_link_libraries(Trakova PRIVATE
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_core
    juce::juce_dsp
    juce::juce_data_structures
    juce::juce_events
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    # LTO は無効化 (juce_recommended_lto_flags を外した)。有効だとメモリの少ない環境で
    # LTO リンクがハング/極端に遅くなるため。性能差は配布用途では軽微。
    juce::juce_recommended_warning_flags
    lame_static
    rubberband_static
)

# ── 使い方ドキュメント (HTML) をアプリに同梱 ──
# ヘルプメニュー「使い方ドキュメント...」が既定ブラウザで開く単一 HTML ファイル。
# macOS: .app/Contents/Resources/help.html に配置 (Xcode の Copy Bundle Resources)。
# Windows: 実行ファイル隣へ POST_BUILD でコピー。
set(TRAKOVA_HELP_DOC "${CMAKE_CURRENT_SOURCE_DIR}/Docs/help.html")
target_sources(Trakova PRIVATE "${TRAKOVA_HELP_DOC}")
set_source_files_properties("${TRAKOVA_HELP_DOC}" PROPERTIES
    MACOSX_PACKAGE_LOCATION Resources)
if(WIN32)
    add_custom_command(TARGET Trakova POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${TRAKOVA_HELP_DOC}" "$<TARGET_FILE_DIR:Trakova>/help.html"
        VERBATIM)
endif()

# ── ユニットテスト (コンソールアプリ TrakovaTests) ──
# 書き出し経路 (ExportEngine + AudioEngine + シーン構築) をオーディオデバイス無しで検証する。
# juce::UnitTest / UnitTestRunner ベース。`-DTRAKOVA_BUILD_TESTS=OFF` で無効化可能。
option(TRAKOVA_BUILD_TESTS "Build TrakovaTests unit tests" ON)
if(TRAKOVA_BUILD_TESTS)
    juce_add_console_app(TrakovaTests PRODUCT_NAME "TrakovaTests")
    juce_generate_juce_header(TrakovaTests)

    target_sources(TrakovaTests PRIVATE
        Tests/ExportEngineTests.cpp
        Tests/MidiIoTests.cpp
        Tests/BackupManagerTests.cpp
        Tests/AudioClipTests.cpp
        Tests/RecordingTests.cpp
        Tests/AppSettingsTests.cpp
        Tests/MidiClipTests.cpp
        Tests/SilenceDetectorTests.cpp
        Tests/EditActionsTests.cpp
        Tests/PluginActionsTests.cpp
        Tests/LufsMeterTests.cpp
        Tests/BpmDetectorTests.cpp
        Tests/ProjectManagerTests.cpp
        Tests/InternalSynthTests.cpp
        Tests/MeterDrawTests.cpp
        Tests/TrackManagerTests.cpp
        Tests/LocalisationTests.cpp
        Tests/AudioFileImporterTests.cpp
        Tests/AppStorageTests.cpp
        Tests/AdServiceTests.cpp
        Tests/UpdateCheckerTests.cpp
        Source/Audio/AudioFileImporter.cpp
        Source/Project/AdService.cpp
        Source/Project/UpdateChecker.cpp
        Source/Project/RecentProjects.cpp
        Source/Project/WindowState.cpp
        Source/Project/BackupManager.cpp
        Source/Project/ProjectManager.cpp
        Source/VST/PluginManager.cpp
        Source/Export/ExportEngine.cpp
        Source/Export/Mp3EncoderWriter.cpp
        Source/Audio/AudioEngine.cpp
        Source/Audio/AudioDeviceSettings.cpp
        Source/Tracks/Track.cpp
        Source/Tracks/AudioClip.cpp
        Source/Tracks/TrackManager.cpp
        Source/VST/PluginChain.cpp
        Source/MIDI/InternalSynth.cpp
        Source/MIDI/MidiImporter.cpp
        Source/MIDI/MidiExporter.cpp
        Source/Localisation.cpp
    )

    target_include_directories(TrakovaTests SYSTEM PRIVATE
        Source/ThirdParty/r8brain
    )

    # 本体と同じプラグインホスト定義に揃える (共有する JUCE モジュールの構成不一致を防ぐ)。
    target_compile_definitions(TrakovaTests PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_MODAL_LOOPS_PERMITTED=1
        JUCE_PLUGINHOST_VST3=1
        JUCE_PLUGINHOST_AU=0
        JUCE_APPLICATION_NAME_STRING="TrakovaTests"
        JUCE_APPLICATION_VERSION_STRING="0.1.0"
        JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=0
    )

    if(MSVC)
        target_compile_options(TrakovaTests PRIVATE /utf-8 /wd4819)
        target_compile_definitions(TrakovaTests PRIVATE NOMINMAX=1 _USE_MATH_DEFINES=1)
    endif()

    target_link_libraries(TrakovaTests PRIVATE
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_core
        juce::juce_dsp
        juce::juce_data_structures
        juce::juce_events
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_gui_extra
        juce::juce_recommended_config_flags
        lame_static
    )
endif()
