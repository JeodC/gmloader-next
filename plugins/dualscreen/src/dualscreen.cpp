#include <SDL2/SDL.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <nlohmann/json.hpp>
#include "platform.h"
#include "gml_plugin_api.h"

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
    X(void, GenTextures, (GLsizei, GLuint *)) \
    X(void, CopyTexImage2D, (GLenum, GLint, GLenum, GLint, GLint, GLsizei, GLsizei, GLint)) \
    X(void, CopyTexSubImage2D, (GLenum, GLint, GLint, GLint, GLint, GLint, GLsizei, GLsizei)) \
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
static int width, height, game_interval;
static std::string title = "Secondary screen";
static GLuint program, buffer;
static GLint region_uniform, brightness_uniform;

static struct layer {
    GLuint texture;
    int x, y, w, h; // second-screen pixels from the top left
    float u, v, uw, vh, brightness;
    int blend;
} layers[16];
static int layer_count;

static int swapped;
static GLuint swap_texture;
static int swap_w, swap_h;

// Movement since the last DualScreen_Touch call, in second-screen pixels.
static struct {
    double dx, dy;             // one-finger drag
    double zoom;               // pinch, in powers of two
    double x, y;               // latest position of the first finger
    double origin_x, origin_y; // where the first finger went down
    int tapped;                // the finger lifted without moving
} touch;
static struct { SDL_FingerID id; int active; double x, y; } contacts[2];
static double last_span;
static int moved;

static double (*GetReal)(RValue *val, int idx);
static void (*CreateString)(RValue *val, const char *str);

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
    if (program)
        return 1;

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

    GLuint v = pCreateShader(GL_VERTEX_SHADER), f = pCreateShader(GL_FRAGMENT_SHADER);
    pShaderSource(v, 1, &vs, NULL);
    pCompileShader(v);
    pShaderSource(f, 1, &fs, NULL);
    pCompileShader(f);

    GLint ok = 0;
    program = pCreateProgram();
    pAttachShader(program, v);
    pAttachShader(program, f);
    pBindAttribLocation(program, 0, "p");
    pBindAttribLocation(program, 1, "t");
    pLinkProgram(program);
    pGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        program = 0;
        return 0;
    }

    pGenBuffers(1, &buffer);
    pBindBuffer(GL_ARRAY_BUFFER, buffer);
    pBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);

    pUseProgram(program);
    region_uniform = pGetUniformLocation(program, "region");
    brightness_uniform = pGetUniformLocation(program, "brightness");
    pUniform1i(pGetUniformLocation(program, "image"), 0);
    return 1;
}

static void setup_context(void)
{
    pBindBuffer(GL_ARRAY_BUFFER, buffer);
    pEnableVertexAttribArray(0);
    pEnableVertexAttribArray(1);
    pVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 16, (void *)0);
    pVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 16, (void *)8);
    pUseProgram(program);
    pActiveTexture(GL_TEXTURE0);
    pBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    pClearColor(0, 0, 0, 1);
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

static void reset_touch(void)
{
    contacts[0].active = contacts[1].active = 0;
    touch = {};
}

static int SDLCALL on_event(void *data, SDL_Event *ev)
{
    if (ev->type != SDL_FINGERDOWN && ev->type != SDL_FINGERMOTION && ev->type != SDL_FINGERUP)
        return 1;
    SDL_Window *target = swapped ? game_window : window;
    if (ev->tfinger.windowID != SDL_GetWindowID(target))
        return 1;

    int w = width, h = height;
    if (swapped)
        SDL_GL_GetDrawableSize(target, &w, &h);
    double x = ev->tfinger.x * w, y = ev->tfinger.y * h;
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

static int second_display(void)
{
    SDL_Window *current = game_window ? game_window : SDL_GL_GetCurrentWindow();
    if (!current || SDL_GetNumVideoDisplays() < 2)
        return -1;
    return SDL_GetWindowDisplayIndex(current) == 0 ? 1 : 0;
}

static int open_window(void)
{
    if (window)
        return 1;

    int display = second_display();
    game_window = SDL_GL_GetCurrentWindow();
    game_context = SDL_GL_GetCurrentContext();
    if (!game_context || display < 0 || !load_gl()) {
        fprintf(stderr, "dualscreen: no second display\n");
        return 0;
    }

    SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1);
    window = SDL_CreateWindow(title.c_str(),
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

    setup_context();
    // SDL's Wayland swap interval is global, not per window
    game_interval = SDL_GL_GetSwapInterval();
    SDL_GL_SetSwapInterval(0);
    SDL_GL_GetDrawableSize(window, &width, &height);
    SDL_AddEventWatch(on_event, NULL);
    SDL_GL_MakeCurrent(game_window, game_context);
    fprintf(stderr, "dualscreen: opened display %d, %s\n", display, pCreateSync ? "fences" : "no fences");
    return 1;
}

static void close_window(void)
{
    if (!window)
        return;
    SDL_DelEventWatch(on_event, NULL);
    SDL_GL_MakeCurrent(game_window, game_context);
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    SDL_GL_SetSwapInterval(game_interval);
    context = NULL;
    window = NULL;
    layer_count = swapped = 0;
    reset_touch();
}

// Texture attached to the framebuffer the game is drawing to.
static GLuint bound_texture(void)
{
    GLint fbo = 0, type = 0, texture = 0;
    pGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);
    if (!fbo)
        return 0;
    pGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &type);
    if (type != GL_TEXTURE)
        return 0;
    pGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &texture);
    return texture;
}

static int begin(void)
{
    EGLSyncKHR sync = fence();
    if (SDL_GL_MakeCurrent(window, context) != 0)
        return 0;
    wait_for(sync);
    // The compositor can resize the window after it opens
    SDL_GL_GetDrawableSize(window, &width, &height);
    pClear(GL_COLOR_BUFFER_BIT);
    return 1;
}

static void draw(const layer &l)
{
    GLint min, mag;
    pBindTexture(GL_TEXTURE_2D, l.texture);
    pGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &min);
    pGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, &mag);
    pTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    pTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    if (l.blend)
        pEnable(GL_BLEND);
    else
        pDisable(GL_BLEND);
    pViewport(l.x, height - l.y - l.h, l.w, l.h);
    pUniform4f(region_uniform, l.u, l.v, l.uw, l.vh);
    pUniform1f(brightness_uniform, l.brightness);
    pDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    pTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, min);
    pTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, mag);
}

static void end(void)
{
    EGLSyncKHR sync = fence();
    SDL_GL_SwapWindow(window);
    SDL_GL_MakeCurrent(game_window, game_context);
    wait_for(sync);
}

static void copy_screen(int x, int y, int w, int h)
{
    GLint bound;
    pGetIntegerv(GL_TEXTURE_BINDING_2D, &bound);
    if (!swap_texture)
        pGenTextures(1, &swap_texture);
    pBindTexture(GL_TEXTURE_2D, swap_texture);
    if (w != swap_w || h != swap_h) {
        // GLES can't copy RGBA from a window without alpha
        pCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, x, y, w, h, 0);
        pTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        pTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        pTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        pTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        swap_w = w;
        swap_h = h;
    } else {
        pCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, x, y, w, h);
    }
    pBindTexture(GL_TEXTURE_2D, bound);
}

#define EXTENSION(name) static ABI_ATTR void name(RValue *ret, void *self, void *other, int argc, RValue *args)

static void set_real(RValue *ret, double value)
{
    ret->kind = VALUE_REAL;
    ret->rvalue.val = value;
}

static void screen_size(int *w, int *h)
{
    SDL_DisplayMode mode;
    if (window)
        SDL_GL_GetDrawableSize(window, &width, &height);
    *w = width;
    *h = height;
    if (!window && second_display() >= 0 && SDL_GetCurrentDisplayMode(second_display(), &mode) == 0) {
        *w = mode.w;
        *h = mode.h;
    }
}

EXTENSION(DualScreen_Available)
{
    set_real(ret, second_display() >= 0);
}

EXTENSION(DualScreen_Enable)
{
    if (GetReal(args, 0) == 1) {
        set_real(ret, open_window());
        return;
    }
    close_window();
    set_real(ret, 1);
}

EXTENSION(DualScreen_Ready)
{
    set_real(ret, window != NULL);
}

EXTENSION(DualScreen_Width)
{
    int w, h;
    screen_size(&w, &h);
    set_real(ret, w);
}

EXTENSION(DualScreen_Height)
{
    int w, h;
    screen_size(&w, &h);
    set_real(ret, h);
}

EXTENSION(DualScreen_Begin)
{
    layer_count = 0;
    set_real(ret, window != NULL);
}

EXTENSION(DualScreen_Draw)
{
    GLuint texture = window && layer_count < 16 ? bound_texture() : 0;
    if (texture) {
        layer &l = layers[layer_count++];
        l.texture = texture;
        l.x = lround(GetReal(args, 0));
        l.y = lround(GetReal(args, 1));
        l.w = lround(GetReal(args, 2));
        l.h = lround(GetReal(args, 3));
        l.u = GetReal(args, 4);
        l.v = GetReal(args, 5);
        l.uw = GetReal(args, 6);
        l.vh = GetReal(args, 7);
        l.brightness = fmin(1, fmax(0, GetReal(args, 8)));
        l.blend = GetReal(args, 9) != 0;
    }
    set_real(ret, texture != 0);
}

EXTENSION(DualScreen_End)
{
    set_real(ret, 0);
    if (!window || !layer_count || !begin())
        return;
    for (int i = 0; i < layer_count; i++)
        draw(layers[i]);
    layer_count = 0;
    end();
    set_real(ret, 1);
}

EXTENSION(DualScreen_Touch)
{
    char text[256] = "";
    if (touch.tapped || touch.dx != 0 || touch.dy != 0 || touch.zoom != 0) {
        snprintf(text, sizeof(text),
                 "{\"dx\":%.5f,\"dy\":%.5f,\"zoom\":%.5f,\"tap\":%s,\"x\":%.5f,\"y\":%.5f,\"originX\":%.5f,\"originY\":%.5f}",
                 touch.dx, touch.dy, touch.zoom, touch.tapped ? "true" : "false", touch.x, touch.y, touch.origin_x, touch.origin_y);
        touch.dx = touch.dy = touch.zoom = 0;
        touch.tapped = 0;
    }
    CreateString(ret, text);
}

EXTENSION(DualScreen_Swap)
{
    int swap = window && GetReal(args, 0) == 1;
    if (swap != swapped)
        reset_touch();
    swapped = swap;
    set_real(ret, swapped);
}

EXTENSION(DualScreen_SwapFrame)
{
    int x = GetReal(args, 0), y = GetReal(args, 1), w = GetReal(args, 2), h = GetReal(args, 3);
    set_real(ret, 0);
    if (!window || w < 1 || h < 1)
        return;
    copy_screen(x, y, w, h);
    if (!begin())
        return;
    float scale = fminf((float)width / w, (float)height / h);
    int fit_w = lroundf(w * scale), fit_h = lroundf(h * scale);
    draw({swap_texture, (width - fit_w) / 2, (height - fit_h) / 2, fit_w, fit_h, 0, 1, 1, -1, 1, 0});
    end();
    set_real(ret, 1);
}

extern "C" __attribute__((visibility("default")))
int gml_plugin_register(const gml_plugin_api_t *api, const char *config_json)
{
    try {
        auto cfg = nlohmann::json::parse(config_json ? config_json : "{}");
        if (cfg.contains("title") && cfg["title"].is_string())
            title = cfg["title"].get<std::string>();
    } catch (...) {}

    GetReal = api->YYGetReal;
    CreateString = api->YYCreateString;
    api->Extension_Add("DualScreen_Available", DualScreen_Available, 0);
    api->Extension_Add("DualScreen_Enable", DualScreen_Enable, 1);
    api->Extension_Add("DualScreen_Ready", DualScreen_Ready, 0);
    api->Extension_Add("DualScreen_Width", DualScreen_Width, 0);
    api->Extension_Add("DualScreen_Height", DualScreen_Height, 0);
    api->Extension_Add("DualScreen_Begin", DualScreen_Begin, 0);
    api->Extension_Add("DualScreen_Draw", DualScreen_Draw, 10);
    api->Extension_Add("DualScreen_End", DualScreen_End, 0);
    api->Extension_Add("DualScreen_Touch", DualScreen_Touch, 0);
    api->Extension_Add("DualScreen_Swap", DualScreen_Swap, 1);
    api->Extension_Add("DualScreen_SwapFrame", DualScreen_SwapFrame, 4);

    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    return 0;
}
