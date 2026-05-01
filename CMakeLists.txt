cmake_minimum_required(VERSION 3.16)
project(vocal-stem-stt CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# whisper.cpp (static, no examples/tests)
# ---------------------------------------------------------------------------
set(BUILD_SHARED_LIBS        OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_EXAMPLES   OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_TESTS      OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_SERVER     OFF CACHE BOOL "" FORCE)
set(WHISPER_SDL2             OFF CACHE BOOL "" FORCE)

add_subdirectory(dependencies/whisper.cpp EXCLUDE_FROM_ALL)

# ---------------------------------------------------------------------------
# RTAudio (static)
# ---------------------------------------------------------------------------
set(RTAUDIO_BUILD_STATIC_LIBS ON  CACHE BOOL "" FORCE)
set(RTAUDIO_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING             OFF CACHE BOOL "" FORCE)

add_subdirectory(dependencies/rtaudio EXCLUDE_FROM_ALL)

# ---------------------------------------------------------------------------
# Our application
# ---------------------------------------------------------------------------
add_executable(vocal-stem-stt
    src/main.cpp
    src/audio.cpp
    src/osc.cpp
    src/transcriber.cpp
    src/tui.cpp
)

target_include_directories(vocal-stem-stt PRIVATE
    src/
    dependencies/whisper.cpp/include
    dependencies/rtaudio
)

target_link_libraries(vocal-stem-stt PRIVATE
    whisper
    rtaudio
)

# macOS system frameworks (CoreAudio, CoreFoundation, Metal)
if(APPLE)
    target_link_libraries(vocal-stem-stt PRIVATE
        "-framework CoreAudio"
        "-framework CoreFoundation"
        "-framework AudioToolbox"
    )
endif()

# Copy binary to project root for convenience
add_custom_command(TARGET vocal-stem-stt POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:vocal-stem-stt>
            ${CMAKE_SOURCE_DIR}/vocal-stem-stt
)
