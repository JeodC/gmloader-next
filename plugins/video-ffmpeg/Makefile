ARCH      ?= aarch64-linux-gnu
CROSS     ?= ${ARCH}-
CC        := ${CROSS}gcc
CXX       := ${CROSS}g++
STRIP     := ${CROSS}strip
PKG_CONFIG?= pkg-config

ROOT      := $(realpath ../..)
BUILD_DIR := build/${ARCH}
OUTPUT    := build/libgmlvideo-ffmpeg.so

KIT_DIR   := ${ROOT}/3rdparty/SDL_kitchensink

KIT_CSRC  := \
    ${KIT_DIR}/src/kiterror.c \
    ${KIT_DIR}/src/kitlib.c \
    ${KIT_DIR}/src/kitplayer.c \
    ${KIT_DIR}/src/kitsource.c \
    ${KIT_DIR}/src/kitutils.c \
    ${KIT_DIR}/src/internal/audio/kitaudio.c \
    ${KIT_DIR}/src/internal/utils/kitbuffer.c \
    ${KIT_DIR}/src/internal/utils/kithelpers.c \
    ${KIT_DIR}/src/internal/utils/kitringbuffer.c \
    ${KIT_DIR}/src/internal/video/kitvideo.c \
    ${KIT_DIR}/src/internal/kitdecoder.c \
    ${KIT_DIR}/src/internal/kitlibstate.c \
    ${KIT_DIR}/src/internal/libass.c \
    ${KIT_DIR}/src/internal/subtitle/kitatlas.c \
    ${KIT_DIR}/src/internal/subtitle/kitsubtitle.c \
    ${KIT_DIR}/src/internal/subtitle/kitsubtitlepacket.c \
    ${KIT_DIR}/src/internal/subtitle/renderers/kitsubass.c \
    ${KIT_DIR}/src/internal/subtitle/renderers/kitsubimage.c \
    ${KIT_DIR}/src/internal/subtitle/renderers/kitsubrenderer.c

INCLUDES  := \
    -I${ROOT} \
    -I${ROOT}/loader \
    -I${ROOT}/gmloader \
    -I${KIT_DIR}/include

KIT_DEFS  := -DKIT_VERSION_MAJOR=1 -DKIT_VERSION_MINOR=0 -DKIT_VERSION_PATCH=12 \
             -DUSE_DYNAMIC_LIBASS -DDYNAMIC_LIBASS_NAME=\"libass.so\"

COMMON    := -O2 -fPIC -fvisibility=hidden -Wno-strict-aliasing ${INCLUDES} \
             -DVIDEO_SUPPORT=1 $(shell ${PKG_CONFIG} sdl2 --cflags)
CFLAGS    := ${COMMON} ${KIT_DEFS}
CXXFLAGS  := -std=gnu++2a -Werror=return-type ${COMMON}
# Plugin pulls in gmloader/video_ffmpeg.cpp which calls into the host
# (send_async_social etc); those symbols resolve via the host's RTLD_GLOBAL
# at runtime, so --no-undefined would falsely fail the link.
LDFLAGS   := -shared -fPIC -Wl,-soname,libgmlvideo-ffmpeg.so
# FFmpeg static-linked into this .so; --start-group lets the static archives
# resolve interdependencies in any order. System libs (m/pthread/z) are pulled
# in dynamically because FFmpeg's static .a archives reference them.
LIBS      := -Wl,--start-group -lavformat -lavcodec -lswresample -lswscale -lavutil -Wl,--end-group \
             $(shell ${PKG_CONFIG} sdl2 --libs) -lm -lpthread -lz -ldl

CXX_SRCS  := src/plugin_main.cpp ${ROOT}/gmloader/video_ffmpeg.cpp
KIT_OBJS  := $(patsubst ${KIT_DIR}/src/%.c,${BUILD_DIR}/kit/%.o,${KIT_CSRC})
CXX_OBJS  := $(patsubst %.cpp,${BUILD_DIR}/cxx/%.o,$(notdir ${CXX_SRCS}))

.PHONY: all clean strip
all: ${OUTPUT}

${OUTPUT}: ${KIT_OBJS} ${CXX_OBJS}
	@mkdir -p $(dir $@)
	${CXX} ${LDFLAGS} -o $@ ${CXX_OBJS} ${KIT_OBJS} ${LIBS}

${BUILD_DIR}/cxx/plugin_main.o: src/plugin_main.cpp
	@mkdir -p $(dir $@)
	${CXX} ${CXXFLAGS} -c $< -o $@

${BUILD_DIR}/cxx/video_ffmpeg.o: ${ROOT}/gmloader/video_ffmpeg.cpp
	@mkdir -p $(dir $@)
	${CXX} ${CXXFLAGS} -c $< -o $@

${BUILD_DIR}/kit/%.o: ${KIT_DIR}/src/%.c
	@mkdir -p $(dir $@)
	${CC} ${CFLAGS} -c $< -o $@

strip: ${OUTPUT}
	${STRIP} --strip-unneeded ${OUTPUT}

clean:
	rm -rf build
