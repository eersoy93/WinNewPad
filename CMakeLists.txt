cmake_minimum_required(VERSION 3.10)

project(WinNewPad VERSION 1.0.0 LANGUAGES C RC)

if(NOT WIN32)
    message(FATAL_ERROR "WinNewPad is a Windows application. Use a Windows compiler or a MinGW-w64 toolchain file.")
endif()

add_executable(WinNewPad WIN32
    src/main.c
    src/resource.h
    src/WinNewPad.rc
)

target_compile_definitions(WinNewPad PRIVATE UNICODE _UNICODE)
target_link_libraries(WinNewPad PRIVATE comctl32 comdlg32 shell32 gdi32 user32)

if(MSVC)
    target_compile_options(WinNewPad PRIVATE /W4)
    # The manifest is embedded by WinNewPad.rc
    target_link_options(WinNewPad PRIVATE /MANIFEST:NO)
else()
    target_compile_options(WinNewPad PRIVATE -Wall -Wextra -municode)
    target_link_options(WinNewPad PRIVATE -municode)
endif()
