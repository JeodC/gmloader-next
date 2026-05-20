ARCH      ?= aarch64-linux-gnu
CROSS     ?= ${ARCH}-
CC        := ${CROSS}gcc
CXX       := ${CROSS}g++
STRIP     := ${CROSS}strip

ROOT      := $(realpath ../..)
FMOD_DIR  ?= ${ROOT}/3rdparty/fmod

ifeq (${ARCH},aarch64-linux-gnu)
    FMOD_ARCH := arm64
else ifeq (${ARCH},arm-linux-gnueabihf)
    FMOD_ARCH := armhf
else
    $(error Unsupported ARCH: ${ARCH})
endif

BUILD_DIR := build/${ARCH}
OUTPUT    := build/libfmod-gamemaker.so

INCLUDES  := \
    -I${ROOT}/gmloader \
    -I${ROOT}/loader \
    -I${ROOT}/3rdparty/FMOD_SDL \
    -I${FMOD_DIR}/api/core/inc \
    -I${FMOD_DIR}/api/studio/inc

COMMON    := -O2 -fPIC -fvisibility=hidden -Wno-strict-aliasing ${INCLUDES}
CFLAGS    := ${COMMON} $(shell pkg-config sdl2 --cflags)
CXXFLAGS  := -std=gnu++2a -Werror=return-type ${COMMON} $(shell pkg-config sdl2 --cflags)
LDFLAGS   := -shared -fPIC -Wl,-soname,libfmod-gamemaker.so -Wl,--no-undefined \
             -L${FMOD_DIR}/api/core/lib/${FMOD_ARCH} \
             -L${FMOD_DIR}/api/studio/lib/${FMOD_ARCH}
LIBS      := -lfmod -lfmodstudio -lpthread $(shell pkg-config sdl2 --libs)

CXX_SRCS  := src/plugin_main.cpp src/fmod-gamemaker.cpp
C_SRCS    := ${ROOT}/3rdparty/FMOD_SDL/FMOD_SDL.c
OBJS      := $(patsubst src/%.cpp,${BUILD_DIR}/%.o,${CXX_SRCS}) \
             ${BUILD_DIR}/FMOD_SDL.o

.PHONY: all clean strip
all: ${OUTPUT}

${OUTPUT}: ${OBJS}
	@mkdir -p $(dir $@)
	${CXX} ${LDFLAGS} -o $@ ${OBJS} ${LIBS}

${BUILD_DIR}/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	${CXX} ${CXXFLAGS} -c $< -o $@

${BUILD_DIR}/FMOD_SDL.o: ${C_SRCS}
	@mkdir -p $(dir $@)
	${CC} ${CFLAGS} -c $< -o $@

strip: ${OUTPUT}
	${STRIP} --strip-unneeded ${OUTPUT}

clean:
	rm -rf build
