cmake_minimum_required(VERSION 3.20)
project(pad_of_time LANGUAGES CXX)

# -----------------------------------------------------------------------------
# IMPORTANT: this MUST build 32-bit (x86). PrinceOfPersia.exe (Sands of Time,
# Steam) is PE32 / Intel 80386. Configure with, e.g.:
#     cmake -B build -A Win32 -DSDL2_DIR=<path-to-SDL2-cmake>
#     cmake --build build --config Release
# -----------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "Pad-of-Time must be built as 32-bit (x86). Re-run cmake with -A Win32.")
endif()

find_package(SDL2 CONFIG REQUIRED)

add_library(dinput8 SHARED
    src/dllmain.cpp
    src/hook_di.cpp
    src/input_sdl.cpp
    src/config.cpp
    src/log.cpp
    dinput8.def
)

target_include_directories(dinput8 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# We resolve DirectInput8Create dynamically via GetProcAddress (see dllmain),
# so we must NOT link the SDK's dinput8.lib. We only need dxguid for the
# IID_/GUID_ symbols <dinput.h> references (IID_IDirectInput8, GUID_XAxis...).
target_link_libraries(dinput8 PRIVATE SDL2::SDL2 dxguid)

set_target_properties(dinput8 PROPERTIES OUTPUT_NAME "dinput8" PREFIX "")

if(TARGET SDL2::SDL2)
    add_custom_command(TARGET dinput8 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:SDL2::SDL2> $<TARGET_FILE_DIR:dinput8>)
endif()
