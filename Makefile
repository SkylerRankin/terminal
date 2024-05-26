CC = clang
CFLAGS = -gdwarf-4 -Wall -O0 $$(pkg-config --cflags freetype2) -fstack-usage
LFLAGS = -lglfw -lGL $$(pkg-config --libs freetype2) -lm

SRCDIR = src
RESDIR = res
LIBDIR = lib
BUILDDIR = build

HEADERS =
OBJ_FILES = terminal.o glad.o
OBJS = $(patsubst %,$(BUILDDIR)/%,$(OBJ_FILES))

all: build_dir copy_shaders copy_fonts terminal

terminal: $(OBJS)
	$(CC) $^ -o $(BUILDDIR)/$@ $(LFLAGS)

$(BUILDDIR)/%.o: $(SRCDIR)/%.c $(HEADERS)
	$(CC) -c $(CFLAGS) -o $@ $<

$(BUILDDIR)/%.o: $(LIBDIR)/glad/%.c
	$(CC) -c $(CFLAGS) -o $@ $< -Ilib

build_dir:
	mkdir -p build/shaders && mkdir -p build/fonts

copy_shaders: $(SRCDIR)/shaders/*.glsl
	cp $(SRCDIR)/shaders/*.glsl $(BUILDDIR)/shaders

copy_fonts: $(RESDIR)/*.ttf
	cp $(RESDIR)/*.ttf $(BUILDDIR)/fonts

clean:
	rm -rf build
