# Plain make build, so the project can be compiled without CMake.
#
#   make                      # host build: tests + preview + 2D SDL target
#   make CROSS=aarch64-linux-gnu-
#
# Targets:
#   luxor3_test       headless assertions (no SDL2 needed)
#   luxor3_preview    offline PNG renderer (no SDL2 needed)
#   luxor3            game binary, SDL_Renderer 2D backend
#   luxor3_gl         game binary, + GLES2 backend (needs -lGLESv2 + EGL)
#   device_probe      runtime probe for the target device

CROSS ?=
CXX   ?= $(CROSS)g++
CC    ?= $(CROSS)gcc
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2
CFLAGS   ?= -Wall -Wextra -O2
INCLUDES = -I. -I../third_party/khronos
LDLIBS  = -lm

SDL_CFLAGS := $(shell pkg-config --cflags sdl2 2>/dev/null || echo -I/usr/include/SDL2)
SDL_LIBS   := $(shell pkg-config --libs sdl2 2>/dev/null || echo -lSDL2)

CORE_SRC = core.cpp scene.cpp stb_impl.cpp game.cpp
CORE_OBJ = $(CORE_SRC:.cpp=.o)

.PHONY: all test preview clean

all: luxor3_test luxor3_preview

# ---- host-safe targets (no SDL2) -----------------------------------------
luxor3_test: test_core.o $(CORE_OBJ:game.o=) core.o scene.o stb_impl.o
	$(CXX) $(CXXFLAGS) -o $@ test_core.o core.o scene.o stb_impl.o $(LDLIBS)

luxor3_preview: tool_preview.o preview.o core.o scene.o stb_impl.o
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDLIBS)

# Read-only audit tools: they report what the parsers find in a data tree and
# never modify it. Useful locally against a real installation.
luxor3_config_audit: config_audit.o core.o scene.o stb_impl.o
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDLIBS)

luxor3_level_audit: level_audit.o core.o scene.o stb_impl.o
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDLIBS)

# ---- SDL2 targets ---------------------------------------------------------
luxor3: main.o game.o sdl2d.o $(CORE_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(SDL_LIBS) $(LDLIBS)

# The GL binary needs its own main object compiled with the GL backend enabled.
main_gl.o: main.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(SDL_CFLAGS) -DLUXOR_HAVE_GL=1 -c -o $@ $<

luxor3_gl: main_gl.o game.o sdl2d.o sdl_gl.o sprite_batch.o $(CORE_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(SDL_LIBS) -lGLESv2 $(LDLIBS)

# glGetString comes from libGLESv2; without it the link fails.
device_probe: device_probe.o
	$(CC) $(CFLAGS) -o $@ $^ $(SDL_LIBS) -lGLESv2

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(SDL_CFLAGS) -c -o $@ $<

%.o: %.c
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -c -o $@ $<

test: luxor3_test
	./luxor3_test "$(GAME_ROOT)"

preview: luxor3_preview
	./luxor3_preview "$(GAME_ROOT)" "$(GAME_ROOT)/data/levels/classic/A_1_1.lvl" preview.png

clean:
	rm -f *.o luxor3_test luxor3_preview luxor3_config_audit luxor3_level_audit luxor3 luxor3_gl device_probe preview.png
