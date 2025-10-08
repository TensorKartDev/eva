cmake_minimum_required(VERSION 3.16)
project(kws LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(kws
    src/main.cpp
    src/audio_capture.cpp
    src/transcriber.cpp
    src/utils.cpp
)

target_include_directories(kws PRIVATE include)

if(APPLE)
    find_library(AUDIOTOOLBOX_FRAMEWORK AudioToolbox REQUIRED)
    find_library(COREAUDIO_FRAMEWORK CoreAudio REQUIRED)
    find_library(COREFOUNDATION_FRAMEWORK CoreFoundation REQUIRED)
    target_link_libraries(kws PRIVATE
        ${AUDIOTOOLBOX_FRAMEWORK}
        ${COREAUDIO_FRAMEWORK}
        ${COREFOUNDATION_FRAMEWORK}
    )
elseif(UNIX)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(ALSA REQUIRED alsa)
    target_include_directories(kws PRIVATE ${ALSA_INCLUDE_DIRS})
    target_compile_options(kws PRIVATE ${ALSA_CFLAGS_OTHER})
    target_link_directories(kws PRIVATE ${ALSA_LIBRARY_DIRS})
    target_link_libraries(kws PRIVATE ${ALSA_LIBRARIES})
else()
    message(FATAL_ERROR "Unsupported platform for audio capture")
endif()

option(EVA_ENABLE_VOSK "Enable speech transcription with Vosk" ON)

if(EVA_ENABLE_VOSK)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(VOSK vosk)
    endif()

    if(NOT VOSK_FOUND)
        message(WARNING "Vosk not located; building without transcription support. Set EVA_ENABLE_VOSK=ON with Vosk installed to enable.")
    else()
        target_include_directories(kws PRIVATE ${VOSK_INCLUDE_DIRS})
        target_link_directories(kws PRIVATE ${VOSK_LIBRARY_DIRS})
        target_link_libraries(kws PRIVATE ${VOSK_LIBRARIES})
        target_compile_definitions(kws PRIVATE EVA_WITH_VOSK)
    endif()
endif()
