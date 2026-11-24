#include <SDL2/SDL.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "platform.h"
#include "gml_plugin_api.h"
#include "dualscreen.h"

#define EXPORT extern "C" __attribute__((visibility("default")))

#define GL_FUNCS(X) \
    X(void, GetIntegerv, (GLenum, GLint *)) \
    X(void, GetFramebufferAttachmentParameteriv, (GLenum, GLenum, GLenum, GLint *)) \
    X(GLuint, CreateShader, (GLenum)) \
    X(void, ShaderSource, (GLuint, GLsizei, const GLchar *const *, const GLint *)) \
    X(void, CompileShader, (GLuint)) \
    X(GLuint, CreateProgram, (void)) \
    X(void, AttachShader, (GLuint, GLuint)) \
    X(void, BindAttribLocation, (GLuint, GLuint, const GLchar *)) \
    X(void, LinkProgram, (GLuint)) \
    X(void, GetProgramiv, (GLuint, GLenum, GLint *)) \
    X(void, UseProgram, (GLuint)) \
    X(GLint, GetUniformLocation, (GLuint, const GLchar *)) \
    X(void, Uniform1i, (GLint, GLint)) \
    X(void, Uniform1f, (GLint, GLfloat)) \
    X(void, Uniform4f, (GLint, GLfloat, GLfloat, GLfloat, GLfloat)) \
    X(void, GenBuffers, (GLsizei, GLuint *)) \
    X(void, BindBuffer, (GLenum, GLuint)) \
    X(void, BufferData, (GLenum, GLsizeiptr, const void *, GLenum)) \
    X(void, EnableVertexAttribArray, (GLuint)) \
    X(void, VertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void *)) \
    X(void, ActiveTexture, (GLenum)) \
    X(void, BindTexture, (GLenum, GLuint)) \
    X(void, GetTexParameteriv, (GLenum, GLenum, GLint *)) \
    X(void, TexParameteri, (GLenum, GLenum, GLint)) \
    X(void, Viewport, (GLint, GLint, GLsizei, GLsizei)) \
    X(void, ClearColor, (GLfloat, GLfloat, GLfloat, GLfloat)) \
    X(void, Clear, (GLbitfield)) \
    X(void, Enable, (GLenum)) \
    X(void, Disable, (GLenum)) \
    X(void, BlendFunc, (GLenum, GLenum)) \
    X(void, DrawArrays, (GLenum, GLint, GLsizei)) \
    X(void, Finish, (void)) \
    X(void, Flush, (void))

#define DECLARE(ret, name, args) static ret (*p##name) args;
GL_FUNCS(DECLARE)

static EGLDisplay (*pGetCurrentDisplay)(void);
static PFNEGLCREATESYNCKHRPROC pCreateSync;
static PFNEGLDESTROYSYNCKHRPROC pDestroySync;
static PFNEGLWAITSYNCKHRPROC pWaitSync;

static SDL_Window *game_window, *window;
static SDL_GLContext game_context, context;
static int opened, width, height;
static GLint region_uniform, brightness_uniform;

static struct { SDL_FingerID id; int active; double x, y; } contacts[2];
static ds_touch_state touch;
static double last_span;
static int moved;

static int load_gl(void)
{
#define LOAD(ret, name, args) if (!(p##name = (decltype(p##name))SDL_GL_GetProcAddress("gl" #name))) return 0;
    GL_FUNCS(LOAD)
    pGetCurrentDisplay = (decltype(pGetCurrentDisplay))SDL_GL_GetProcAddress("eglGetCurrentDisplay");
    pCreateSync = (decltype(pCreateSync))SDL_GL_GetProcAddress("eglCreateSyncKHR");
    pDestroySync = (decltype(pDestroySync))SDL_GL_GetProcAddress("eglDestroySyncKHR");
    pWaitSync = (decltype(pWaitSync))SDL_GL_GetProcAddress("eglWaitSyncKHR");
    EGLDisplay display = pGetCurrentDisplay ? pGetCurrentDisplay() : EGL_NO_DISPLAY;
    auto query = (const char *(*)(EGLDisplay, EGLint))SDL_GL_GetProcAddress("eglQueryString");
    const char *ext = query && display != EGL_NO_DISPLAY ? query(display, EGL_EXTENSIONS) : NULL;
    if (!ext || !strstr(ext, "EGL_KHR_fence_sync") || !strstr(ext, "EGL_KHR_wait_sync") || !pDestroySync || !pWaitSync)
        pCreateSync = NULL;
    return 1;
}

static int build_program(void)
{
    static const char *vs =
        "attribute vec2 p; attribute vec2 t; varying vec2 uv;"
        "void main() { gl_Position = vec4(p, 0.0, 1.0); uv = t; }";
    static const char *fs =
        "precision highp float; varying vec2 uv;"
        "uniform sampler2D image; uniform vec4 region; uniform float brightness;"
        "void main() { vec4 c = texture2D(image, region.xy + uv * region.zw); gl_FragColor = vec4(c.rgb * brightness, c.a); }";
    static const float quad[] = {
        -1, -1, 0, 1,
         1, -1, 1, 1,
        -1,  1, 0, 0,
         1,  1, 1, 0,
    };

    GLuint v = pCreateShader(GL_VERTEX_SHADER), f = pCreateShader(GL_FRAGMENT_SHADER), buffer;
    pShaderSource(v, 1, &vs, NULL);
    pCompileShader(v);
    pShaderSource(f, 1, &fs, NULL);
    pCompileShader(f);

    GLint ok = 0;
    GLuint program = pCreateProgram();
    pAttachShader(program, v);
    pAttachShader(program, f);
    pBindAttribLocation(program, 0, "p");
    pBindAttribLocation(program, 1, "t");
    pLinkProgram(program);
    pGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok)
        return 0;

    pGenBuffers(1, &buffer);
    pBindBuffer(GL_ARRAY_BUFFER, buffer);
    pBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    pEnableVertexAttribArray(0);
    pEnableVertexAttribArray(1);
    pVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 16, (void *)0);
    pVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 16, (void *)8);

    pUseProgram(program);
    region_uniform = pGetUniformLocation(program, "region");
    brightness_uniform = pGetUniformLocation(program, "brightness");
    pUniform1i(pGetUniformLocation(program, "image"), 0);
    pActiveTexture(GL_TEXTURE0);
    pBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    pClearColor(0, 0, 0, 1);
    return 1;
}

static EGLSyncKHR fence(void)
{
    if (!pCreateSync) {
        pFinish();
        return EGL_NO_SYNC_KHR;
    }
    EGLSyncKHR sync = pCreateSync(pGetCurrentDisplay(), EGL_SYNC_FENCE_KHR, NULL);
    if (sync == EGL_NO_SYNC_KHR)
        pFinish();
    else
        pFlush();
    return sync;
}

static void wait_for(EGLSyncKHR sync)
{
    if (sync == EGL_NO_SYNC_KHR)
        return;
    pWaitSync(pGetCurrentDisplay(), sync, 0);
    pDestroySync(pGetCurrentDisplay(), sync);
}

static double span(void)
{
    return hypotf(contacts[1].x - contacts[0].x, contacts[1].y - contacts[0].y);
}

static void finger_down(SDL_FingerID id, double x, double y)
{
    for (int i = 0; i < 2; i++) {
        if (contacts[i].active)
            continue;
        contacts[i].id = id;
        contacts[i].active = 1;
        contacts[i].x = x;
        contacts[i].y = y;
        if (i == 0) {
            touch.origin_x = touch.x = x;
            touch.origin_y = touch.y = y;
            moved = 0;
        } else {
            last_span = span();
            moved = 1;
        }
        return;
    }
}

static void finger_move(int i, double x, double y)
{
    double dx = x - contacts[i].x, dy = y - contacts[i].y;
    contacts[i].x = x;
    contacts[i].y = y;
    if (contacts[0].active && contacts[1].active) {
        double now = span();
        if (now > 1 && last_span > 1)
            touch.zoom += log2f(now / last_span);
        last_span = now;
        moved = 1;
    } else if (i == 0) {
        touch.x = x;
        touch.y = y;
        if (hypotf(x - touch.origin_x, y - touch.origin_y) > 8)
            moved = 1;
        if (moved) {
            touch.dx += dx;
            touch.dy += dy;
        }
    }
}

static void finger_up(int i)
{
    if (i == 0 && !moved && !contacts[1].active)
        touch.tapped = 1;
    contacts[i].active = 0;
    if (i == 0 && contacts[1].active) {
        contacts[0] = contacts[1];
        contacts[1].active = 0;
        moved = 1;
    }
}

static int SDLCALL on_event(void *data, SDL_Event *ev)
{
    if (ev->type != SDL_FINGERDOWN && ev->type != SDL_FINGERMOTION && ev->type != SDL_FINGERUP)
        return 1;
    if (ev->tfinger.windowID != SDL_GetWindowID(window))
        return 1;

    double x = ev->tfinger.x * width, y = ev->tfinger.y * height;
    if (ev->type == SDL_FINGERDOWN) {
        finger_down(ev->tfinger.fingerId, x, y);
        return 1;
    }
    for (int i = 0; i < 2; i++) {
        if (!contacts[i].active || contacts[i].id != ev->tfinger.fingerId)
            continue;
        if (ev->type == SDL_FINGERMOTION)
            finger_move(i, x, y);
        else
            finger_up(i);
        break;
    }
    return 1;
}

EXPORT int ds_open(void)
{
    if (opened)
        return window != NULL;
    opened = 1;

    game_window = SDL_GL_GetCurrentWindow();
    game_context = SDL_GL_GetCurrentContext();
    if (!game_context || SDL_GetNumVideoDisplays() < 2 || !load_gl()) {
        fprintf(stderr, "dualscreen: no second display\n");
        return 0;
    }

    int display = SDL_GetWindowDisplayIndex(game_window) == 0 ? 1 : 0;
    SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1);
    window = SDL_CreateWindow("Second screen",
                              SDL_WINDOWPOS_CENTERED_DISPLAY(display), SDL_WINDOWPOS_CENTERED_DISPLAY(display),
                              640, 480, SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN_DESKTOP);
    if (window)
        context = SDL_GL_CreateContext(window);
    SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 0);

    if (!context || SDL_GL_MakeCurrent(window, context) != 0 || !build_program()) {
        fprintf(stderr, "dualscreen: could not open the second screen: %s\n", SDL_GetError());
        SDL_GL_MakeCurrent(game_window, game_context);
        if (context)
            SDL_GL_DeleteContext(context);
        if (window)
            SDL_DestroyWindow(window);
        context = NULL;
        window = NULL;
        return 0;
    }

    SDL_GL_SetSwapInterval(0);
    SDL_GL_GetDrawableSize(window, &width, &height);
    SDL_AddEventWatch(on_event, NULL);
    SDL_GL_MakeCurrent(game_window, game_context);
    fprintf(stderr, "dualscreen: opened display %d, %s\n", display, pCreateSync ? "fences" : "no fences");
    return 1;
}

EXPORT int ds_ready(void)
{
    return window != NULL;
}

EXPORT void ds_size(int *w, int *h)
{
    *w = width;
    *h = height;
}

EXPORT GLuint ds_bound_texture(void)
{
    GLint fbo = 0, type = 0, texture = 0;
    if (!window)
        return 0;
    pGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);
    if (!fbo)
        return 0;
    pGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &type);
    if (type != GL_TEXTURE)
        return 0;
    pGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &texture);
    return texture;
}

EXPORT int ds_begin(void)
{
    if (!window)
        return 0;
    EGLSyncKHR sync = fence();
    if (SDL_GL_MakeCurrent(window, context) != 0)
        return 0;
    wait_for(sync);
    // The compositor can resize the window after it opens
    SDL_GL_GetDrawableSize(window, &width, &height);
    pClear(GL_COLOR_BUFFER_BIT);
    return 1;
}

EXPORT void ds_draw(GLuint texture, int x, int y, int w, int h, float u, float v, float uw, float vh, float brightness, int blend)
{
    GLint min, mag;
    pBindTexture(GL_TEXTURE_2D, texture);
    pGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &min);
    pGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, &mag);
    pTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    pTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    if (blend)
        pEnable(GL_BLEND);
    else
        pDisable(GL_BLEND);
    pViewport(x, y, w, h);
    pUniform4f(region_uniform, u, v, uw, vh);
    pUniform1f(brightness_uniform, brightness);
    pDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    pTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, min);
    pTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, mag);
}

EXPORT void ds_end(void)
{
    EGLSyncKHR sync = fence();
    SDL_GL_SwapWindow(window);
    SDL_GL_MakeCurrent(game_window, game_context);
    wait_for(sync);
}

EXPORT int ds_touch(ds_touch_state *out)
{
    if (!touch.tapped && touch.dx == 0 && touch.dy == 0 && touch.zoom == 0)
        return 0;
    *out = touch;
    touch.dx = touch.dy = touch.zoom = 0;
    touch.tapped = 0;
    return 1;
}

EXPORT int gml_plugin_register(const gml_plugin_api_t *api, const char *config_json)
{
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    return 0;
}
