cmake_minimum_required(VERSION 3.22)

# macOS: universal binaries (Intel and Apple Silicon); 10.15 is the first release
# whose C++ library provides std::filesystem. Both can be overridden on the command line.
if(APPLE)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum macOS version")
  set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64" CACHE STRING "macOS architectures")
endif()

project(ThisIsTheVoice VERSION 0.9.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# DPF's GUI code relies on GNU extensions (typeof).
set(CMAKE_CXX_EXTENSIONS ON)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(TITV_BUILD_PLUGIN "Build the plugin formats with DPF" ON)
option(TITV_BUILD_TESTS "Build the engine tests and benchmark" ON)

if(MSVC)
  # Sources and UI texts are UTF-8 (French strings, NanoVG expects UTF-8).
  add_compile_options(/utf-8)
  set(TITV_WARNINGS /W4)
else()
  set(TITV_WARNINGS -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion)
endif()

# Engine: DSP and signal chain, independent of plugin formats and UI.
add_library(titv_engine STATIC
  src/engine/Engine.cpp
  src/dsp/AutoLevel.cpp
  src/dsp/ColorEffects.cpp
  src/dsp/Echo.cpp
  src/dsp/Reverb.cpp
  src/dsp/DeEsser.cpp
  src/dsp/ToneEq.cpp
  src/dsp/VoiceCompressor.cpp)
target_include_directories(titv_engine PUBLIC src)
target_compile_options(titv_engine PRIVATE ${TITV_WARNINGS})

if(TITV_BUILD_PLUGIN)
  if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/DPF/CMakeLists.txt")
    message(FATAL_ERROR "DPF is missing: run `git submodule update --init --recursive`")
  endif()
  # Local changes to DPF, kept as patches so the submodule stays upstream DPF.
  # dpf-tail.patch: lets the plugin report its tail length to VST3 hosts.
  # dpf-vst3-parameter-sync.patch: applies VST3 controller-side parameter changes at
  #   once in single-component mode, so host and controller stay in sync.
  # dpf-state-chunked-read.patch: fixes the CLAP and VST3 state parsers when the host
  #   delivers the saved state in small chunks.
  foreach(patch dpf-tail.patch dpf-vst3-parameter-sync.patch dpf-state-chunked-read.patch)
    set(patch_file "${CMAKE_CURRENT_SOURCE_DIR}/patches/${patch}")
    execute_process(COMMAND git apply --reverse --check "${patch_file}"
                    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/external/DPF"
                    RESULT_VARIABLE already_applied OUTPUT_QUIET ERROR_QUIET)
    if(NOT already_applied EQUAL 0)
      execute_process(COMMAND git apply "${patch_file}"
                      WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/external/DPF"
                      RESULT_VARIABLE apply_result)
      if(NOT apply_result EQUAL 0)
        message(FATAL_ERROR "Could not apply patches/${patch} to external/DPF")
      endif()
      message(STATUS "Applied patches/${patch} to DPF")
    endif()
  endforeach()

  # MSVC ships no <GL/glext.h>. DPF would download it from the Khronos registry, which
  # has moved and refuses CI machines; the headers are kept in third_party/ instead
  # and copied where DPF looks for them.
  if(MSVC)
    file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/third_party/khronos/GL" "${CMAKE_CURRENT_SOURCE_DIR}/third_party/khronos/KHR"
         DESTINATION "${CMAKE_CURRENT_SOURCE_DIR}/external/DPF/khronos")
  endif()

  add_subdirectory(external/DPF)

  set(TITV_FORMATS jack lv2 vst3 clap)
  if(APPLE)
    list(APPEND TITV_FORMATS au) # Audio Unit, the only format Logic Pro loads
  endif()

  dpf_add_plugin(ThisIsTheVoice
    TARGETS ${TITV_FORMATS}
    UI_TYPE opengl
    FILES_DSP
      src/plugin/VoicePlugin.cpp
    FILES_UI
      src/ui/VoiceUI.cpp
      src/ui/Settings.cpp
      src/ui/UserPresets.cpp
      src/ui/PresetBrowser.cpp)

  target_include_directories(ThisIsTheVoice PUBLIC src src/plugin src/ui)
  # Version reported to hosts, from project(VERSION ...).
  target_compile_definitions(ThisIsTheVoice PUBLIC
    TITV_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    TITV_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    TITV_VERSION_PATCH=${PROJECT_VERSION_PATCH})
  target_link_libraries(ThisIsTheVoice PUBLIC titv_engine)
endif()

if(TITV_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
