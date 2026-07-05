# MinGW build, used to compile/verify this without MSVC.
# On Windows, prefer CMakeLists.txt (MSVC + vcpkg SDL2), matching Pad-Within.
#
# NOTE: this Makefile links against whatever SDL2 dev libs are pointed to by
# SDL_ROOT below, purely to produce a working dinput8.dll for local testing.
# The actual release package ships the official SDL2.dll (2.32.8.0) instead -
# see README. Any recent SDL2 2.x build works at the ABI level; the specific
# dev libs used to link don't need to match the redistributed runtime DLL.
CXX      := i686-w64-mingw32-g++
SDL_ROOT ?= /opt/sdl2-mingw
CXXFLAGS := -m32 -O2 -std=c++17 -Wall -DDIRECTINPUT_VERSION=0x0800 \
            -I$(SDL_ROOT)/include/SDL2 -Iinclude
LDFLAGS  := -shared -static-libgcc -static-libstdc++ -Wl,--enable-stdcall-fixup
LIBS     := -L$(SDL_ROOT)/lib -lSDL2 -ldxguid -lole32 -loleaut32 \
            -limm32 -lversion -lsetupapi -lwinmm -lgdi32 -luser32 -ladvapi32 -lshell32

SRC := src/dllmain.cpp src/hook_di.cpp src/input_sdl.cpp src/config.cpp src/log.cpp
OUT := dist/dinput8.dll

all: $(OUT)

dist:
	mkdir -p dist

$(OUT): $(SRC) dinput8.def | dist
	$(CXX) $(CXXFLAGS) $(SRC) dinput8.def -o $(OUT) $(LDFLAGS) $(LIBS)
	strip --strip-unneeded $(OUT) 2>/dev/null || i686-w64-mingw32-strip --strip-unneeded $(OUT)

clean:
	rm -rf dist

.PHONY: all clean
