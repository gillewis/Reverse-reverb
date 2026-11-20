cmake_minimum_required(VERSION 3.22)

set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
set(CMAKE_XCODE_ATTRIBUTE_ONLY_ACTIVE_ARCH NO)
project(ReverseReverb VERSION 1.1.0)

# ---- Branding: change these before you sell (see SELLING.md) ----
set(RR_COMPANY_NAME   "Gil Lewis")
set(RR_BUNDLE_ID      "com.gillewis.reversereverb")
set(RR_COMPANY_URL    "https://gillewis.com")
set(RR_COMPANY_EMAIL  "gillewis@gmail.com")
# -----------------------------------------------------------------

option(RR_BUILD_TESTS "Build the internal test programs" ON)
option(RR_COPY_PLUGINS "Copy built plug-ins into ~/Library/Audio/Plug-Ins" ON)

add_subdirectory(JUCE)

juce_add_plugin(ReverseReverb
    COMPANY_NAME "${RR_COMPANY_NAME}"
    COMPANY_WEBSITE "${RR_COMPANY_URL}"
    COMPANY_EMAIL "${RR_COMPANY_EMAIL}"
    BUNDLE_ID "${RR_BUNDLE_ID}"
    VERSION ${PROJECT_VERSION}
    PLUGIN_MANUFACTURER_CODE Rvrb
    PLUGIN_CODE Rvr1
    FORMATS VST3 AU Standalone
    AU_MAIN_TYPE kAudioUnitType_Effect
    VST3_CATEGORIES Fx Reverb
    PRODUCT_NAME "Reverse Reverb"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD ${RR_COPY_PLUGINS}
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Reverse Reverb needs audio input access to run as a standalone app.")

target_sources(ReverseReverb PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Renderer.cpp)

target_compile_features(ReverseReverb PRIVATE cxx_std_17)

target_compile_definitions(ReverseReverb PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(ReverseReverb
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if(RR_BUILD_TESTS)
    juce_add_console_app(RenderTest PRODUCT_NAME "RenderTest")
    target_sources(RenderTest PRIVATE Test/main.cpp Source/Renderer.cpp)
    target_compile_definitions(RenderTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(RenderTest PRIVATE juce::juce_audio_formats juce::juce_dsp)

    juce_add_console_app(HostTest PRODUCT_NAME "HostTest")
    target_sources(HostTest PRIVATE Test/host.cpp)
    target_compile_definitions(HostTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_PLUGINHOST_VST3=1 JUCE_PLUGINHOST_AU=1)
    target_link_libraries(HostTest PRIVATE juce::juce_audio_processors juce::juce_gui_extra "-framework CoreAudioKit")

    juce_add_console_app(ClipTest PRODUCT_NAME "ClipTest")
    target_sources(ClipTest PRIVATE Test/clip.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp Source/Renderer.cpp)
    target_compile_definitions(ClipTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(ClipTest PRIVATE juce::juce_audio_utils juce::juce_dsp)

    juce_add_console_app(SnapshotTool PRODUCT_NAME "SnapshotTool")
    target_sources(SnapshotTool PRIVATE Test/snapshot.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp Source/Renderer.cpp)
    target_compile_definitions(SnapshotTool PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(SnapshotTool PRIVATE juce::juce_audio_utils juce::juce_dsp)
endif()
