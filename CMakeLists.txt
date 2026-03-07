cmake_minimum_required(VERSION 3.16)
project(GodOfFrames LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(god_of_frames
    src/main.cpp
    src/system_monitor.cpp
    src/gemini_client.cpp
    src/remediator.cpp
    src/bottleneck_analyzer.cpp
    src/history_store.cpp
    src/dashboard_writer.cpp
    src/overlay_manager.cpp
    src/settings.cpp
    src/control_server.cpp
)

target_include_directories(god_of_frames PRIVATE src)

if (WIN32)
    target_compile_definitions(god_of_frames PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(god_of_frames PRIVATE psapi winhttp user32 gdi32 shell32 advapi32 ws2_32)
endif()

