ARCH      ?= aarch64-linux-gnu
CROSS     ?= ${ARCH}-
CXX       := ${CROSS}g++
STRIP     := ${CROSS}strip
PKG_CONFIG?= pkg-config

ROOT      := $(realpath ../..)
BUILD_DIR := build/${ARCH}
OUTPUT    := build/libdualscreen.so

INCLUDES  := -I${ROOT}/gmloader -I${ROOT}/loader
CXXFLAGS  := -std=gnu++2a -Werror=return-type -O2 -fPIC -fvisibility=hidden ${INCLUDES} $(shell ${PKG_CONFIG} sdl2 --cflags)
LDFLAGS   := -shared -fPIC -Wl,-soname,libdualscreen.so
LIBS      := -lm

OBJS      := ${BUILD_DIR}/dualscreen.o

.PHONY: all clean strip
all: ${OUTPUT}

${OUTPUT}: ${OBJS}
	@mkdir -p $(dir $@)
	${CXX} ${LDFLAGS} -o $@ ${OBJS} ${LIBS}

${BUILD_DIR}/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	${CXX} ${CXXFLAGS} -c $< -o $@

strip: ${OUTPUT}
	${STRIP} --strip-unneeded ${OUTPUT}

clean:
	rm -rf build
